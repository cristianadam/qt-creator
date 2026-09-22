// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppinsertvirtualmethods.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cpptoolsreuse.h"
#include "../functionutils.h"
#include "../insertionpointlocator.h"
#include "cppquickfixassistant.h"
#include "cppquickfixhelpers.h"

#include <coreplugin/icore.h>
#include <texteditor/fontsettings.h>

#include <cplusplus/CppRewriter.h>
#include <cplusplus/Overview.h>

#include <utils/algorithm.h>
#include <utils/changeset.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPointer>
#include <QQueue>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTextDocument>
#include <QToolButton>
#include <QTreeView>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#include <QTest>
#endif

using namespace CPlusPlus;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {

class InsertVirtualMethodsModel;
class VirtualMethodsSettings;

class InsertVirtualMethodsDialog : public QDialog
{
    Q_OBJECT
public:
    enum CustomItemRoles {
        Reimplemented = Qt::UserRole
    };

    enum ImplementationMode {
        ModeOnlyDeclarations = 0x00000001,
        ModeInsideClass = 0x00000002,
        ModeOutsideClass = 0x00000004,
        ModeImplementationFile = 0x00000008
    };

    InsertVirtualMethodsDialog(QWidget *parent = nullptr);
    ~InsertVirtualMethodsDialog() override;
    void initGui();
    void initData();
    virtual void saveSettings();
    const VirtualMethodsSettings *settings() const;

    void setHasImplementationFile(bool file);
    void setHasReimplementedFunctions(bool functions);

    virtual bool gather();

protected:
    void setInsertOverrideReplacement(bool insert);
    void setOverrideReplacement(const QString &replacements);

private:
    void setHideReimplementedFunctions(bool hide);
    void updateOverrideReplacementsComboBox();

private:
    QTreeView *m_view = nullptr;
    QLineEdit *m_filter = nullptr;
    QCheckBox *m_hideReimplementedFunctions = nullptr;
    QComboBox *m_insertMode = nullptr;
    QCheckBox *m_virtualKeyword = nullptr;
    QCheckBox *m_overrideReplacementCheckBox = nullptr;
    QComboBox *m_overrideReplacementComboBox = nullptr;
    QToolButton *m_clearUserAddedReplacementsButton = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QList<bool> m_expansionStateNormal;
    QList<bool> m_expansionStateReimp;
    QStringList m_availableOverrideReplacements;
    bool m_hasImplementationFile = false;
    bool m_hasReimplementedFunctions = false;

protected:
    VirtualMethodsSettings *m_settings;

    void saveExpansionState();
    void restoreExpansionState();

public:
    InsertVirtualMethodsModel *classFunctionModel;
    QSortFilterProxyModel *classFunctionFilterModel;
};

void registerInsertVirtualMethodsQuickfix()
{
    CppQuickFixFactory::registerFactory<InsertVirtualMethods>();
}

} // namespace CppEditor::Internal

Q_DECLARE_METATYPE(CppEditor::Internal::InsertVirtualMethodsDialog::ImplementationMode)

namespace {

class InsertVirtualMethodsItem
{
public:
    InsertVirtualMethodsItem(InsertVirtualMethodsItem *parent)
        : m_parent(parent)
    {}

    virtual ~InsertVirtualMethodsItem() = default;

    virtual QString description() const = 0;
    virtual Qt::ItemFlags flags() const = 0;
    virtual Qt::CheckState checkState() const = 0;

    InsertVirtualMethodsItem *parent() { return m_parent; }

    int row = -1;

private:
    InsertVirtualMethodsItem *m_parent = nullptr;
};

class FunctionItem;

// What a class above the one being added to offers: a member function
// somebody below may implement, as far as this fix is concerned. Whichever
// front end read the class writes the same facts down, and the rules about
// which of them to offer are applied on these rather than on symbols.
struct OfferedFunction
{
    // Where the declaration stands, written out: what tells one function
    // from another here. The class it is written in is part of it, since
    // two instantiations of one template declare their members in the same
    // place without being the same function.
    QString declaredAt;

    QString name;       // as it is written, with nothing in front of it
    QString signature;  // how it reads where it is declared
    QString returnType; // what it hands back, shown beside it

    // The declarations furthest up the hierarchy that said "virtual", each
    // with the class writing it: what a reader is offered is one entry per
    // those, and QObject's three are told from everything else by the name
    // of that class.
    struct FirstVirtual
    {
        QString declaredAt;
        QString className;
    };
    QList<FirstVirtual> firstVirtuals;

    bool isDestructor = false;
    bool isPureVirtual = false;
    bool isFinal = false;

    // The class being added to declares it already, so there is nothing to
    // write there.
    bool declaredInTargetClass = false;

    CppEditor::InsertionPointLocator::AccessSpec accessSpec
        = CppEditor::InsertionPointLocator::Invalid;

    // What writing it into the class being added to amounts to: the
    // declaration as it has to be written there -- every name in it as
    // little qualified as still finds it from there -- the same with the
    // class's own name in front, for a definition standing outside. Read
    // where the function is read, since that is the front end's part of
    // the work.
    QString declarationText;
    QString definitionText;

    // A class above the one declaring it said "virtual" first, so this
    // declaration is one more class's way of saying it rather than the
    // offer itself.
    bool isReimplementation() const
    {
        return !Utils::anyOf(firstVirtuals, [this](const FirstVirtual &first) {
            return first.declaredAt == declaredAt;
        });
    }
};

class ClassItem : public InsertVirtualMethodsItem
{
public:
    ClassItem(const QString &className);
    ~ClassItem() override;

    QString description() const override { return name; }
    Qt::ItemFlags flags() const override;
    Qt::CheckState checkState() const override;
    void removeFunction(int row);

    const QString name;
    QList<FunctionItem *> functions;
};

class FunctionItem : public InsertVirtualMethodsItem
{
public:
    FunctionItem(const OfferedFunction &offered, const QString &description, ClassItem *parent);
    QString description() const override;
    Qt::ItemFlags flags() const override;
    Qt::CheckState checkState() const override { return checked ? Qt::Checked : Qt::Unchecked; }

    OfferedFunction offered;

    bool reimplemented = false;
    bool alreadyFound = false;
    bool checked = false;
    FunctionItem *nextOverride = nullptr;

private:
    QString name;
};

ClassItem::ClassItem(const QString &className) :
    InsertVirtualMethodsItem(nullptr),
    name(className)
{
}

ClassItem::~ClassItem()
{
    qDeleteAll(functions);
    functions.clear();
}

Qt::ItemFlags ClassItem::flags() const
{
    for (FunctionItem *func : std::as_const(functions)) {
        if (!func->alreadyFound)
            return Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled;
    }

    return Qt::ItemIsSelectable;
}

Qt::CheckState ClassItem::checkState() const
{
    if (functions.isEmpty())
        return Qt::Unchecked;
    Qt::CheckState state = functions.first()->checkState();
    for (FunctionItem *function : std::as_const(functions)) {
        Qt::CheckState functionState = function->checkState();
        if (functionState != state)
            return Qt::PartiallyChecked;
    }
    return state;
}

void ClassItem::removeFunction(int row)
{
    QTC_ASSERT(row >= 0 && row < functions.count(), return);
    functions.removeAt(row);
    // Update row number for all the following functions
    for (int r = row, total = functions.count(); r < total; ++r)
        functions[r]->row = r;
}

FunctionItem::FunctionItem(const OfferedFunction &offered, const QString &description,
                           ClassItem *parent) :
    InsertVirtualMethodsItem(parent),
    offered(offered),
    nextOverride(this)
{
    name = description;
}

QString FunctionItem::description() const
{
    return name;
}

Qt::ItemFlags FunctionItem::flags() const
{
    Qt::ItemFlags res = Qt::NoItemFlags;
    if (!alreadyFound)
        res |= Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled;
    return res;
}

QStringList defaultOverrideReplacements()
{
    return {
        QLatin1String("override"),
        QLatin1String("Q_DECL_OVERRIDE")
    };
}

QStringList sortedAndTrimmedStringListWithoutEmptyElements(const QStringList &list)
{
    QStringList result;
    for (const QString &replacement : list) {
        const QString trimmedReplacement = replacement.trimmed();
        if (!trimmedReplacement.isEmpty())
            result << trimmedReplacement;
    }
    result.sort();
    return result;
}

} // namespace

namespace CppEditor::Internal {

const bool kInsertVirtualKeywordDefault = false;
const bool kHideReimplementedFunctionsDefault = false;
const bool kInsertOVerrideReplacementDefault = false;
const int kOverrideReplacementIndexDefault = 0;
const InsertVirtualMethodsDialog::ImplementationMode kImplementationModeDefault
    = InsertVirtualMethodsDialog::ModeOnlyDeclarations;

class VirtualMethodsSettings
{
public:
    void read()
    {
        QtcSettings *s = Core::ICore::settings();
        s->beginGroup(group());
        insertVirtualKeyword = s->value(insertVirtualKeywordKey(), kInsertVirtualKeywordDefault)
                                   .toBool();
        hideReimplementedFunctions
            = s->value(hideReimplementedFunctionsKey(), kHideReimplementedFunctionsDefault).toBool();
        insertOverrideReplacement
            = s->value(insertOverrideReplacementKey(), kInsertOVerrideReplacementDefault).toBool();
        overrideReplacementIndex
            = s->value(overrideReplacementIndexKey(), kOverrideReplacementIndexDefault).toInt();
        userAddedOverrideReplacements = s->value(userAddedOverrideReplacementsKey()).toStringList();
        implementationMode = static_cast<InsertVirtualMethodsDialog::ImplementationMode>(
            s->value(implementationModeKey(), int(kImplementationModeDefault)).toInt());
        s->endGroup();
    }

    void write() const
    {
        QtcSettings *s = Core::ICore::settings();
        s->beginGroup(group());
        s->setValueWithDefault(insertVirtualKeywordKey(),
                               insertVirtualKeyword,
                               kInsertVirtualKeywordDefault);
        s->setValueWithDefault(hideReimplementedFunctionsKey(),
                               hideReimplementedFunctions,
                               kHideReimplementedFunctionsDefault);
        s->setValueWithDefault(insertOverrideReplacementKey(),
                               insertOverrideReplacement,
                               kInsertOVerrideReplacementDefault);
        s->setValueWithDefault(overrideReplacementIndexKey(),
                               overrideReplacementIndex,
                               kOverrideReplacementIndexDefault);
        s->setValueWithDefault(userAddedOverrideReplacementsKey(), userAddedOverrideReplacements);
        s->setValueWithDefault(implementationModeKey(),
                               int(implementationMode),
                               int(kImplementationModeDefault));
        s->endGroup();
    }

    QString overrideReplacement; // internal
    QStringList userAddedOverrideReplacements;
    InsertVirtualMethodsDialog::ImplementationMode implementationMode = kImplementationModeDefault;
    int overrideReplacementIndex = kOverrideReplacementIndexDefault;
    bool insertVirtualKeyword = kInsertVirtualKeywordDefault;
    bool hideReimplementedFunctions = kHideReimplementedFunctionsDefault;
    bool insertOverrideReplacement = kInsertOVerrideReplacementDefault;

private:
    static Key group() { return "QuickFix/InsertVirtualMethods"; }
    static Key insertVirtualKeywordKey() { return "insertKeywordVirtual"; }
    static Key insertOverrideReplacementKey() { return "insertOverrideReplacement"; }
    static Key overrideReplacementIndexKey() { return "overrideReplacementIndex"; }
    static Key userAddedOverrideReplacementsKey() { return "userAddedOverrideReplacements"; }
    static Key implementationModeKey() { return "implementationMode"; }
    static Key hideReimplementedFunctionsKey() { return "hideReimplementedFunctions"; }
};

class InsertVirtualMethodsModel : public QAbstractItemModel
{
public:
    InsertVirtualMethodsModel(QObject *parent = nullptr) : QAbstractItemModel(parent)
    {
        const FontSettingsData fs = globalFontSettings().data();
        formatReimpFunc = fs.formatFor(C_DISABLED_CODE);
    }

    ~InsertVirtualMethodsModel() override
    {
        clear();
    }

    void clear()
    {
        beginResetModel();
        qDeleteAll(classes);
        classes.clear();
        endResetModel();
    }

    QModelIndex index(int row, int column, const QModelIndex &parent) const override
    {
        if (column != 0)
            return {};
        if (parent.isValid()) {
            auto classItem = static_cast<ClassItem *>(parent.internalPointer());
            return createIndex(row, column, classItem->functions.at(row));
        }
        return createIndex(row, column, classes.at(row));
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        if (!child.isValid())
            return {};
        InsertVirtualMethodsItem *parent = itemForIndex(child)->parent();
        return parent ? createIndex(parent->row, 0, parent) : QModelIndex();
    }

    int rowCount(const QModelIndex &parent) const override
    {
        if (!parent.isValid())
            return classes.count();
        InsertVirtualMethodsItem *item = itemForIndex(parent);
        if (item->parent()) // function -> no children
            return 0;
        return static_cast<ClassItem *>(item)->functions.count();
    }

    int columnCount(const QModelIndex &) const override
    {
        return 1;
    }

    void addClass(ClassItem *classItem)
    {
        int row = classes.count();
        classItem->row = row;
        beginInsertRows(QModelIndex(), row, row);
        classes.append(classItem);
        endInsertRows();
    }

    void removeFunction(FunctionItem *funcItem)
    {
        auto classItem = static_cast<ClassItem *>(funcItem->parent());
        beginRemoveRows(createIndex(classItem->row, 0, classItem), funcItem->row, funcItem->row);
        classItem->removeFunction(funcItem->row);
        endRemoveRows();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return QVariant();

        InsertVirtualMethodsItem *item = itemForIndex(index);
        switch (role) {
        case Qt::DisplayRole:
            return item->description();
        case Qt::CheckStateRole:
            return item->checkState();
        case Qt::ForegroundRole:
            if (item->parent() && static_cast<FunctionItem *>(item)->alreadyFound)
                return formatReimpFunc.foreground();
            break;
        case Qt::BackgroundRole:
            if (item->parent() && static_cast<FunctionItem *>(item)->alreadyFound) {
                const QColor background = formatReimpFunc.background();
                if (background.isValid())
                    return background;
            }
            break;
        case InsertVirtualMethodsDialog::Reimplemented:
            if (item->parent()) {
                auto function = static_cast<FunctionItem *>(item);
                return QVariant(function->alreadyFound);
            }

        }
        return QVariant();
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (!index.isValid())
            return false;

        InsertVirtualMethodsItem *item = itemForIndex(index);
        switch (role) {
        case Qt::CheckStateRole: {
            bool checked = value.toInt() == Qt::Checked;
            if (item->parent()) {
                auto funcItem = static_cast<FunctionItem *>(item);
                while (funcItem->checked != checked) {
                    funcItem->checked = checked;
                    const QModelIndex funcIndex = createIndex(funcItem->row, 0, funcItem);
                    emit dataChanged(funcIndex, funcIndex);
                    const QModelIndex parentIndex =
                            createIndex(funcItem->parent()->row, 0, funcItem->parent());
                    emit dataChanged(parentIndex, parentIndex);
                    funcItem = funcItem->nextOverride;
                }
            } else {
                auto classItem = static_cast<ClassItem *>(item);
                for (FunctionItem *funcItem : std::as_const(classItem->functions)) {
                    if (funcItem->alreadyFound || funcItem->checked == checked)
                        continue;
                    QModelIndex funcIndex = createIndex(funcItem->row, 0, funcItem);
                    setData(funcIndex, value, role);
                }
            }
            return true;
        }
        }
        return QAbstractItemModel::setData(index, value, role);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        return itemForIndex(index)->flags();
    }

    QList<ClassItem *> classes;

private:
    Format formatReimpFunc;

    InsertVirtualMethodsItem *itemForIndex(const QModelIndex &index) const
    {
        return static_cast<InsertVirtualMethodsItem *>(index.internalPointer());
    }
};

// The classes above \a clazz, furthest first -- which is the order their
// functions are offered in.
//
// The rest of this reading works in the file's own symbols, so a class
// the cxx-frontend model found is handed over as one of those; where any
// of them cannot be, the built-in walk answers instead. Half a hierarchy
// is worse than the other model's whole one.
static QList<const Class *> baseClassesOf(const CppQuickFixInterface &interface, Class *clazz)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (clazz && !clazz->filePath().isEmpty()) {
        if (const std::optional<QList<CxxFrontendDocument::BaseClass>> bases
            = cxxFrontendBasesOfTheClassAt(CppModelManager::workingCopy(),
                                           clazz->filePath(), clazz->line(), clazz->column())) {
            QList<const Class *> found;
            for (const CxxFrontendDocument::BaseClass &base : *bases) {
                // Out of the parse the rest of this reading works in: the
                // editor's own for the file being edited, the snapshot's
                // for a header it reads.
                const Utils::FilePath baseFile
                    = Utils::FilePath::fromUserInput(base.place.filePath);
                Class * const symbol
                    = baseFile == interface.filePath()
                          ? builtinClassWrittenAt(interface.currentFile()->cppDocument(),
                                                  base.place)
                          : builtinClassWrittenAt(interface.snapshot(), base.place);
                if (!symbol) {
                    found.clear();
                    break;
                }
                // Read nearest first and offered furthest first.
                if (!found.contains(symbol))
                    found.prepend(symbol);
            }
            if (!found.isEmpty())
                return found;
        }
    }
#endif

    QList<const Class *> baseClasses;
    QQueue<ClassOrNamespace *> baseClassQueue;
    QSet<ClassOrNamespace *> visitedBaseClasses;
    if (ClassOrNamespace *coN = interface.context().lookupType(clazz))
        baseClassQueue.enqueue(coN);
    while (!baseClassQueue.isEmpty()) {
        ClassOrNamespace *coN = baseClassQueue.dequeue();
        visitedBaseClasses.insert(coN);
        QList<ClassOrNamespace *> bases = coN->usings();
        while (!bases.isEmpty()) {
            const ClassOrNamespace *baseClass = bases.takeFirst();
            const QList<Symbol *> symbols = baseClass->symbols();

            // See QTCREATORBUG-32162.
            if (symbols.isEmpty() && baseClass->usings().size() == 1) {
                bases.prepend(baseClass->usings().first());
                continue;
            }

            for (Symbol *symbol : symbols) {
                Class *base = symbol->asClass();
                if (base
                        && (coN = interface.context().lookupType(symbol))
                        && !visitedBaseClasses.contains(coN)
                        && !baseClasses.contains(base)) {
                    baseClasses.prepend(base);
                    baseClassQueue.enqueue(coN);
                }
            }
        }
    }
    return baseClasses;
}

// What writing \a func into \a targetClass amounts to, as text. Every
// name in it is written with as little in front of it as still finds it
// from the class being added to -- a base and the class below it may be
// in different namespaces -- which takes qualifying the type in full
// where it was declared first, and then shortening it for where it is
// going.
static void writeInTheClass(const CppQuickFixInterface &interface,
                            const Class *targetClass, const Class *baseClass,
                            const Function *func, OfferedFunction &item,
                            int declarationGoesAt, int definitionGoesAt)
{
    Overview printer = CppCodeStyleSettings::currentProjectCodeStyleOverview();
    printer.showFunctionSignatures = true;
    printer.showReturnTypes = true;
    printer.showArgumentNames = true;
    printer.showTemplateParameters = true;

    const LookupContext targetContext(interface.currentFile()->cppDocument(),
                                      interface.snapshot());
    ClassOrNamespace *targetCoN = targetContext.lookupType(targetClass->enclosingScope());
    if (!targetCoN)
        targetCoN = targetContext.globalNamespace();
    Control * const control = interface.context().bindings()->control().get();

    Clone cloner(control);
    Function newFunc(&cloner, nullptr, const_cast<Function *>(func));
    newFunc.setEnclosingScope(const_cast<Class *>(targetClass));
    SubstitutionEnvironment envQualified;
    envQualified.setContext(interface.context());
    envQualified.switchScope(baseClass->enclosingScope());
    UseQualifiedNames useQualifiedNames;
    envQualified.enter(&useQualifiedNames);
    newFunc.setReturnType(rewriteType(newFunc.returnType(), &envQualified, control));
    const int argc = newFunc.argumentCount();
    for (int i = 0; i < argc; ++i) {
        Argument * const arg = newFunc.argumentAt(i)->asArgument();
        QTC_ASSERT(arg, continue);
        arg->setType(rewriteType(arg->type(), &envQualified, control));
    }

    SubstitutionEnvironment envMinimized;
    envMinimized.setContext(interface.context());
    envMinimized.switchScope(targetClass->enclosingScope());
    UseMinimalNames useMinimalNames(targetCoN);
    envMinimized.enter(&useMinimalNames);
    const FullySpecifiedType tn = rewriteType(newFunc.type(), &envMinimized, control);

    item.declarationText = printer.prettyType(tn, newFunc.unqualifiedName());
    item.definitionText = printer.prettyType(
        tn, printer.prettyName(targetClass->name()) + "::" + item.name);

#ifdef QTC_WITH_CXX_FRONTEND
    // The same two, as the cxx-frontend model writes them. It is asked of
    // the file being edited, which is the one that reads whatever header
    // declares the function.
    const auto onTheModel = [&](int goesAt, const QString &name) -> std::optional<QString> {
        int line = 0, column = 0;
        Utils::Text::convertPosition(interface.currentFile()->document(), goesAt, &line, &column);
        return cxxFrontendDeclarationHeadFor(interface.filePath(), func->filePath(),
                                             func->line(), func->column(), name,
                                             line, column + 1);
    };
    if (const std::optional<QString> declaration = onTheModel(declarationGoesAt, item.name))
        item.declarationText = *declaration;
    if (const std::optional<QString> definition
        = onTheModel(definitionGoesAt,
                     printer.prettyName(targetClass->name()) + "::" + item.name)) {
        item.definitionText = *definition;
    }
#else
    Q_UNUSED(declarationGoesAt)
    Q_UNUSED(definitionGoesAt)
#endif
}

// Which section of the class being added to a declaration of \a symbol goes
// in: what may name it, and what Qt makes of it.
static InsertionPointLocator::AccessSpec accessSpecOf(const Symbol *symbol)
{
    const Function *func = symbol->type()->asFunctionType();
    if (!func)
        return InsertionPointLocator::Invalid;
    if (func->isSignal())
        return InsertionPointLocator::Signals;

    InsertionPointLocator::AccessSpec spec = InsertionPointLocator::Invalid;
    if (symbol->isPrivate())
        spec = InsertionPointLocator::Private;
    else if (symbol->isProtected())
        spec = InsertionPointLocator::Protected;
    else if (symbol->isPublic())
        spec = InsertionPointLocator::Public;
    else
        return InsertionPointLocator::Invalid;

    if (func->isSlot()) {
        switch (spec) {
        case InsertionPointLocator::Private:
            return InsertionPointLocator::PrivateSlot;
        case InsertionPointLocator::Protected:
            return InsertionPointLocator::ProtectedSlot;
        case InsertionPointLocator::Public:
            return InsertionPointLocator::PublicSlot;
        default:
            return spec;
        }
    }
    return spec;
}

#ifdef QTC_WITH_CXX_FRONTEND
// The same, read off what the cxx-frontend model says of a member: who may
// name it, and what Qt makes of it.
static InsertionPointLocator::AccessSpec accessSpecOf(CxxFrontendDocument::Access access,
                                                      CxxFrontendDocument::QtMethod qtMethod)
{
    if (qtMethod == CxxFrontendDocument::QtMethod::Signal)
        return InsertionPointLocator::Signals;
    const bool isSlot = qtMethod == CxxFrontendDocument::QtMethod::Slot;
    switch (access) {
    case CxxFrontendDocument::Access::Private:
        return isSlot ? InsertionPointLocator::PrivateSlot : InsertionPointLocator::Private;
    case CxxFrontendDocument::Access::Protected:
        return isSlot ? InsertionPointLocator::ProtectedSlot : InsertionPointLocator::Protected;
    case CxxFrontendDocument::Access::Public:
        return isSlot ? InsertionPointLocator::PublicSlot : InsertionPointLocator::Public;
    }
    return InsertionPointLocator::Invalid;
}
#endif

// Whether \a targetClass declares \a func already, in which case there is
// nothing for this fix to write there.
static bool declaredAlready(const Class *targetClass, const Function *func)
{
    const Name * const funcName = func->name();
    const OperatorNameId * const opName = funcName->asOperatorNameId();
    Symbol *symbol = opName ? targetClass->find(opName->kind())
                            : targetClass->find(funcName->identifier());
    for (; symbol; symbol = symbol->next()) {
        if (!opName && (!symbol->name() || !funcName->identifier()->match(symbol->identifier())))
            continue;
        if (symbol->type().match(func->type()))
            return true;
    }
    return false;
}

// The virtual functions \a baseClass declares, in the order it writes them,
// as the built-in front end reads them.
static QList<OfferedFunction> builtinFunctionsOffered(const CppQuickFixInterface &interface,
                                                      const Class *targetClass,
                                                      const Class *baseClass,
                                                      int declarationGoesAt, int definitionGoesAt)
{
    Overview printer = CppCodeStyleSettings::currentProjectCodeStyleOverview();
    printer.showFunctionSignatures = true;

    // The class a function is written in belongs in the place, so that the
    // members of two instantiations of one template are told apart.
    const auto placeOf = [&printer](const Function *func) {
        const Class * const enclosing = func->enclosingClass();
        return (enclosing ? printer.prettyName(enclosing->name()) : QString())
               + '|' + func->filePath().toFSPathString()
               + ':' + QString::number(func->line()) + ':' + QString::number(func->column());
    };

    QList<OfferedFunction> offered;
    for (Scope::iterator it = baseClass->memberBegin(); it != baseClass->memberEnd(); ++it) {
        const Function * const func = (*it)->type()->asFunctionType();
        if (!func || !func->name())
            continue;

        QList<const Function *> firstVirtuals;
        if (!FunctionUtils::isVirtualFunction(func, interface.context(), &firstVirtuals))
            continue;

        OfferedFunction one;
        one.declaredAt = placeOf(func);
        one.name = printer.prettyName(func->name());
        one.signature = printer.prettyType(func->type(), func->name());
        one.returnType = printer.prettyType(func->returnType());
        for (const Function *first : std::as_const(firstVirtuals)) {
            const Class * const enclosing = first->enclosingClass();
            one.firstVirtuals.append(
                {placeOf(first),
                 enclosing ? printer.prettyName(enclosing->name()) : QString()});
        }
        one.isDestructor = func->name()->asDestructorNameId() != nullptr;
        one.isPureVirtual = func->isPureVirtual();
        one.isFinal = func->isFinal();
        one.declaredInTargetClass = declaredAlready(targetClass, func);
        one.accessSpec = accessSpecOf(*it);
        writeInTheClass(interface, targetClass, baseClass, func, one,
                        declarationGoesAt, definitionGoesAt);
        offered.append(one);
    }
    return offered;
}

#ifdef QTC_WITH_CXX_FRONTEND
// The same reading on the cxx-frontend model, and nothing where it cannot
// answer the whole of it. The classes above one are read together and their
// functions point at each other, so half of them read one way and half the
// other is no list at all -- one front end reads every base or none does.
static std::optional<QList<OfferedFunction>> modelFunctionsOffered(
    const CppQuickFixInterface &interface, const Class *targetClass, const Class *baseClass,
    int declarationGoesAt, int definitionGoesAt)
{
    if (targetClass->filePath().isEmpty() || baseClass->filePath().isEmpty())
        return std::nullopt;

    const Utils::FilePath inFile = interface.filePath();
    const Utils::FilePath baseFile = baseClass->filePath();
    const std::optional<QList<CxxFrontendDocument::MemberFunction>> members
        = cxxFrontendMemberFunctionsDeclaredAt(inFile, baseFile, baseClass->line(),
                                               baseClass->column());
    // A class this model cannot find is not one with nothing to offer, and
    // there is no telling the two apart from here -- so a class it says
    // nothing about is one the other front end reads. Nothing is what it
    // says of a class that declares no functions too, and then the other
    // one has nothing to offer either.
    if (!members || members->isEmpty())
        return std::nullopt;

    // The place a declaration stands, said the way the built-in reading says
    // it, since one hash holds the answers of whichever front end read them.
    const auto placeOf = [](const QString &className, const QString &filePath,
                            int line, int column) {
        return className + '|' + filePath + ':' + QString::number(line)
               + ':' + QString::number(column);
    };
    // The class a member is written in, as this model names it: what stands
    // in front of its own name in the path.
    const auto classOf = [](const CxxFrontendDocument::MemberFunction &member) {
        const QStringList path = member.name.split("::");
        return path.size() > 1 ? path.at(path.size() - 2) : QString();
    };

    int line = 0, column = 0;
    Utils::Text::convertPosition(interface.currentFile()->document(), declarationGoesAt,
                                 &line, &column);
    int definitionLine = 0, definitionColumn = 0;
    Utils::Text::convertPosition(interface.currentFile()->document(), definitionGoesAt,
                                 &definitionLine, &definitionColumn);
    const QString targetName
        = CppCodeStyleSettings::currentProjectCodeStyleOverview().prettyName(targetClass->name());
    const CxxFrontendDocument::Place targetPlace{
        targetClass->filePath() == inFile ? QString() : targetClass->filePath().toFSPathString(),
        targetClass->line(), targetClass->column()};

    QList<OfferedFunction> offered;
    for (const CxxFrontendDocument::MemberFunction &member : *members) {
        // Not virtual at all, which this model says of the declaration
        // itself: one that overrides a virtual function is virtual whether
        // it writes the word or not.
        if (!member.isVirtual)
            continue;

        const std::optional<CxxFrontendDocument::Virtuality> virtuality
            = cxxFrontendVirtualityAt(inFile, member.line, member.column, baseFile);
        if (!virtuality)
            return std::nullopt;
        if (!virtuality->isVirtual)
            continue;

        OfferedFunction one;
        one.declaredAt = placeOf(classOf(member), member.filePath, member.line, member.column);
        one.name = member.unqualifiedName;
        one.signature = member.signature;
        one.returnType = member.returnType;
        for (const CxxFrontendDocument::Virtuality::FirstVirtual &first :
             virtuality->firstVirtuals) {
            one.firstVirtuals.append({placeOf(first.className, first.place.filePath,
                                              first.place.line, first.place.column),
                                      first.className});
        }
        one.isDestructor = member.unqualifiedName.startsWith('~');
        one.isPureVirtual = member.isPureVirtual;
        one.isFinal = member.isFinal;

        // What the class below declares already: the same name, the same
        // parameters and the same constness, which is what overriding one
        // amounts to.
        const std::optional<QList<CxxFrontendDocument::Place>> overrides
            = cxxFrontendOverridesIn(CppModelManager::workingCopy(), inFile,
                                     targetPlace,
                                     {member.filePath, member.line, member.column});
        if (!overrides)
            return std::nullopt;
        one.declaredInTargetClass = !overrides->isEmpty();

        one.accessSpec = accessSpecOf(member.access, member.qtMethod);

        // What writing it into the class being added to amounts to. A
        // declaration this model will not write -- one under a template is
        // the case -- is one this fix cannot offer at all.
        const std::optional<QString> declaration
            = cxxFrontendDeclarationHeadFor(inFile, baseFile, member.line, member.column,
                                            one.name, line, column + 1);
        const std::optional<QString> definition
            = cxxFrontendDeclarationHeadFor(inFile, baseFile, member.line, member.column,
                                            targetName + "::" + one.name,
                                            definitionLine, definitionColumn + 1);
        if (!declaration || !definition)
            return std::nullopt;
        one.declarationText = *declaration;
        one.definitionText = *definition;

        offered.append(one);
    }
    return offered;
}
#endif

class InsertVirtualMethodsOp : public CppQuickFixOperation
{
public:
    InsertVirtualMethodsOp(const CppQuickFixInterface &interface,
                           InsertVirtualMethodsDialog *factory)
        : CppQuickFixOperation(interface, 0)
        , m_factory(factory)
    {
        setDescription(Tr::tr("Insert Virtual Functions of Base Classes"));

        const QList<AST *> &path = interface.path();
        const int pathSize = path.size();
        if (pathSize < 2)
            return;

        // Determine if cursor is on a class or a base class
        if (SimpleNameAST *nameAST = path.at(pathSize - 1)->asSimpleName()) {
            if (!interface.isCursorOn(nameAST))
                return;

            if (!(m_classAST = path.at(pathSize - 2)->asClassSpecifier())) { // normal class
                int index = pathSize - 2;
                const BaseSpecifierAST *baseAST = path.at(index)->asBaseSpecifier();// simple bclass
                if (!baseAST) {
                    if (index > 0 && path.at(index)->asQualifiedName()) // namespaced base class
                        baseAST = path.at(--index)->asBaseSpecifier();
                }
                --index;
                if (baseAST && index >= 0)
                    m_classAST = path.at(index)->asClassSpecifier();
            }
        }

        // Also offer the operation if we are on some "empty" part of the class declaration.
        if (!m_classAST)
            m_classAST = path.at(pathSize - 1)->asClassSpecifier();

        if (!m_classAST || !m_classAST->base_clause_list)
            return;

        // Determine insert positions
        const int endOfClassAST = interface.currentFile()->endOf(m_classAST);
        m_insertPosDecl = endOfClassAST - 1; // Skip last "}"
        m_insertPosOutside = endOfClassAST + 1; // Step over ";"

        // Determine base classes
        const QList<const Class *> baseClasses
            = baseClassesOf(interface, m_classAST->symbol);

        // Determine virtual functions
        m_factory->classFunctionModel->clear();
        Overview printer = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        printer.showFunctionSignatures = true;
        // What each of them offers, read by one front end for all of them:
        // the answers point at each other, so a list half read one way and
        // half the other is no list at all.
        QList<QList<OfferedFunction>> offeredByEach;
#ifdef QTC_WITH_CXX_FRONTEND
        for (const Class *clazz : std::as_const(baseClasses)) {
            const std::optional<QList<OfferedFunction>> offered
                = modelFunctionsOffered(interface, m_classAST->symbol, clazz,
                                        m_insertPosDecl, m_insertPosOutside);
            if (!offered) {
                offeredByEach.clear();
                break;
            }
            offeredByEach.append(*offered);
        }
#endif
        if (offeredByEach.isEmpty()) {
            for (const Class *clazz : std::as_const(baseClasses)) {
                offeredByEach.append(builtinFunctionsOffered(interface, m_classAST->symbol, clazz,
                                                             m_insertPosDecl, m_insertPosOutside));
            }
        }

        QHash<QString, FunctionItem *> virtualFunctions;
        for (int i = 0; i < baseClasses.size(); ++i) {
            const Class * const clazz = baseClasses.at(i);
            ClassItem *itemBase = new ClassItem(printer.prettyName(clazz->name()));
            for (const OfferedFunction &function : offeredByEach.at(i)) {
                // Filter virtual destructors
                if (function.isDestructor)
                    continue;

                if (function.isFinal) {
                    for (const OfferedFunction::FirstVirtual &firstVirtual :
                         function.firstVirtuals) {
                        if (FunctionItem *first = virtualFunctions.value(firstVirtual.declaredAt)) {
                            FunctionItem *next = nullptr;
                            for (FunctionItem *removed = first; next != first; removed = next) {
                                next = removed->nextOverride;
                                m_factory->classFunctionModel->removeFunction(removed);
                                delete removed;
                            };
                            virtualFunctions.remove(firstVirtual.declaredAt);
                        }
                    }
                    continue;
                }
                // Filter OQbject's
                //   - virtual const QMetaObject *metaObject() const;
                //   - virtual void *qt_metacast(const char *);
                //   - virtual int qt_metacall(QMetaObject::Call, int, void **);
                const bool isQObjectMagic
                    = magicQObjectFunctions().contains(function.name)
                      && Utils::anyOf(function.firstVirtuals,
                                      [](const OfferedFunction::FirstVirtual &firstVirtual) {
                                          return firstVirtual.className == "QObject";
                                      });
                if (isQObjectMagic)
                    continue;

                // Construct function item
                const bool isReimplemented = function.isReimplementation();
                const bool isPureVirtual = function.isPureVirtual;
                const bool funcExistsInClass = function.declaredInTargetClass;
                QString itemName = function.signature;
                if (isPureVirtual)
                    itemName += QLatin1String(" = 0");
                itemName += QLatin1String(" : ") + function.returnType;
                if (isReimplemented)
                    itemName += QLatin1String(" (redeclared)");
                auto funcItem = new FunctionItem(function, itemName, itemBase);
                if (isReimplemented) {
                    factory->setHasReimplementedFunctions(true);
                    funcItem->reimplemented = true;
                    funcItem->alreadyFound = funcExistsInClass;
                    for (const OfferedFunction::FirstVirtual &firstVirtual :
                         function.firstVirtuals) {
                        if (FunctionItem *first = virtualFunctions.value(firstVirtual.declaredAt)) {
                            if (!first->alreadyFound) {
                                while (first->checked != isPureVirtual) {
                                    first->checked = isPureVirtual;
                                    first = first->nextOverride;
                                }
                            }
                            funcItem->checked = first->checked;

                            FunctionItem *prev = funcItem;
                            for (FunctionItem *next = funcItem->nextOverride;
                                 next && next != funcItem; next = next->nextOverride) {
                                prev = next;
                            }
                            prev->nextOverride = first->nextOverride;
                            first->nextOverride = funcItem;
                        }
                    }
                } else {
                    if (!funcExistsInClass) {
                        funcItem->checked = isPureVirtual;
                    } else {
                        funcItem->alreadyFound = true;
                        funcItem->checked = true;
                        factory->setHasReimplementedFunctions(true);
                    }
                }

                funcItem->row = itemBase->functions.count();
                itemBase->functions.append(funcItem);

                virtualFunctions[function.declaredAt] = funcItem;

                // update internal counters
                if (!funcExistsInClass)
                    ++m_functionCount;
            }

            if (itemBase->functions.isEmpty())
                delete itemBase;
            else
                m_factory->classFunctionModel->addClass(itemBase);
        }
        if (m_factory->classFunctionModel->classes.isEmpty() || m_functionCount == 0)
            return;

        bool isHeaderFile = false;
        m_cppFilePath = correspondingHeaderOrSource(interface.filePath(), &isHeaderFile);
        m_factory->setHasImplementationFile(isHeaderFile && !m_cppFilePath.isEmpty());

        m_valid = true;
    }

    bool isValid() const
    {
        return m_valid;
    }

    void perform() override
    {
        if (!m_factory->gather())
            return;

        m_factory->saveSettings();

        // Insert declarations (and definition if Inside-/OutsideClass)
        Overview printer = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        printer.showFunctionSignatures = true;
        printer.showReturnTypes = true;
        printer.showArgumentNames = true;
        printer.showTemplateParameters = true;
        Utils::ChangeSet headerChangeSet;
        const CppRefactoringChanges refactoring(snapshot());
        const CppRefactoringFilePtr headerFile = currentFile();
        const LookupContext targetContext(headerFile->cppDocument(), snapshot());

        const Class *targetClass = m_classAST->symbol;
        QStringList insertedFunctions;
        for (ClassItem *classItem : std::as_const(m_factory->classFunctionModel->classes)) {
            if (classItem->checkState() == Qt::Unchecked)
                continue;

            // Insert Declarations (+ definitions)
            QString lastAccessSpecString;
            bool first = true;
            for (FunctionItem *funcItem : std::as_const(classItem->functions)) {
                if (funcItem->reimplemented || funcItem->alreadyFound || !funcItem->checked)
                    continue;

                // One function, however many of the bases declare it.
                if (insertedFunctions.contains(funcItem->offered.declarationText))
                    continue;
                insertedFunctions.append(funcItem->offered.declarationText);

                if (first) {
                    // Add comment
                    const QString comment = QLatin1String("\n// ") + classItem->name +
                            QLatin1String(" interface\n");
                    headerChangeSet.insert(m_insertPosDecl, comment);
                    first = false;
                }

                QString declaration = funcItem->offered.declarationText;

                if (m_factory->settings()->insertVirtualKeyword)
                    declaration = QLatin1String("virtual ") + declaration;
                if (m_factory->settings()->insertOverrideReplacement) {
                    const QString overrideReplacement = m_factory->settings()->overrideReplacement;
                    if (!overrideReplacement.isEmpty())
                        declaration += QLatin1Char(' ') + overrideReplacement;
                }
                if (m_factory->settings()->implementationMode
                        & InsertVirtualMethodsDialog::ModeInsideClass) {
                    declaration += QLatin1String("\n{\n}\n");
                } else {
                    declaration += QLatin1String(";\n");
                }

                const QString accessSpecString =
                        InsertionPointLocator::accessSpecToString(funcItem->offered.accessSpec);
                if (accessSpecString != lastAccessSpecString) {
                    declaration = accessSpecString + QLatin1String(":\n") + declaration;
                    if (!lastAccessSpecString.isEmpty()) // separate if not direct after the comment
                        declaration = QLatin1String("\n") + declaration;
                    lastAccessSpecString = accessSpecString;
                }
                headerChangeSet.insert(m_insertPosDecl, declaration);

                // Insert definition outside class
                if (m_factory->settings()->implementationMode
                        & InsertVirtualMethodsDialog::ModeOutsideClass) {
                    const QString defText = funcItem->offered.definitionText + QLatin1String("\n{\n}");
                    headerChangeSet.insert(m_insertPosOutside,  QLatin1String("\n\n") + defText);
                }
            }
        }

        // Write header file
        if (!headerChangeSet.isEmpty()) {
            headerFile->setOpenEditor(true, m_insertPosDecl);
            headerFile->apply(headerChangeSet);
        }

        // Insert in implementation file
        if (m_factory->settings()->implementationMode
                & InsertVirtualMethodsDialog::ModeImplementationFile) {
            const Symbol *symbol = headerFile->cppDocument()->lastVisibleSymbolAt(
                        targetClass->line(), targetClass->column());
            if (!symbol)
                return;
            const Class *clazz = symbol->asClass();
            if (!clazz)
                return;

            CppRefactoringFilePtr implementationFile = refactoring.cppFile(m_cppFilePath);
            Utils::ChangeSet implementationChangeSet;
            const int insertPos = qMax(0, implementationFile->document()->characterCount() - 1);

            // make target lookup context
            Document::Ptr implementationDoc = implementationFile->cppDocument();
            int line, column;
            implementationDoc->translationUnit()->getPosition(insertPos, &line, &column);
            Scope *targetScope = implementationDoc->scopeAt(line, column);
            const LookupContext targetContext(implementationDoc, snapshot());
            ClassOrNamespace *targetCoN = targetContext.lookupType(targetScope);
            if (!targetCoN)
                targetCoN = targetContext.globalNamespace();

            // Loop through inserted declarations
            for (int i = targetClass->memberCount(); i < clazz->memberCount(); ++i) {
                Declaration *decl = clazz->memberAt(i)->asDeclaration();
                if (!decl)
                    continue;

                // setup rewriting to get minimally qualified names
                SubstitutionEnvironment env;
                env.setContext(context());
                env.switchScope(decl->enclosingScope());
                UseMinimalNames q(targetCoN);
                env.enter(&q);
                Control *control = context().bindings()->control().get();

                // rewrite the function type and name + create definition
                const FullySpecifiedType type = rewriteType(decl->type(), &env, control);
                const QString name = printer.prettyName(
                            LookupContext::minimalName(decl, targetCoN, control));
                const QString defText = printer.prettyType(type, name) + QLatin1String("\n{\n}");

                implementationChangeSet.insert(insertPos,  QLatin1String("\n\n") + defText);
            }

            implementationFile->apply(implementationChangeSet);
        }
    }

private:
    InsertVirtualMethodsDialog *m_factory = nullptr;
    const ClassSpecifierAST *m_classAST = nullptr;
    bool m_valid = false;
    FilePath m_cppFilePath;
    int m_insertPosDecl = 0;
    int m_insertPosOutside = 0;
    unsigned m_functionCount = 0;
};

class InsertVirtualMethodsFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    InsertVirtualMethodsFilterModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {}

    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

        // Handle base class
        if (!sourceParent.isValid()) {
            // check if any child is valid
            if (!sourceModel()->hasChildren(index))
                return false;
            if (!m_hideReimplemented)
                return true;

            for (int i = 0; i < sourceModel()->rowCount(index); ++i) {
                const QModelIndex child = sourceModel()->index(i, 0, index);
                if (!child.data(InsertVirtualMethodsDialog::Reimplemented).toBool())
                    return true;
            }
            return false;
        }

        if (!QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent))
            return false;
        if (m_hideReimplemented)
            return !index.data(InsertVirtualMethodsDialog::Reimplemented).toBool();
        return true;
    }

    bool hideReimplemented() const
    {
        return m_hideReimplemented;
    }

    void setHideReimplementedFunctions(bool show)
    {
        m_hideReimplemented = show;
        invalidateFilter();
    }

private:
    bool m_hideReimplemented = false;
};

InsertVirtualMethodsDialog::InsertVirtualMethodsDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new VirtualMethodsSettings)
    , classFunctionModel(new InsertVirtualMethodsModel(this))
    , classFunctionFilterModel(new InsertVirtualMethodsFilterModel(this))
{
    classFunctionFilterModel->setSourceModel(classFunctionModel);
    classFunctionFilterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
}

InsertVirtualMethodsDialog::~InsertVirtualMethodsDialog()
{
    delete m_settings;
}

void InsertVirtualMethodsDialog::initGui()
{
    if (m_view)
        return;

    setWindowTitle(Tr::tr("Insert Virtual Functions"));

    // View
    m_filter = new QLineEdit(this);
    m_filter->setClearButtonEnabled(true);
    m_filter->setPlaceholderText(Tr::tr("Filter"));
    m_view = new QTreeView(this);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setHeaderHidden(true);
    m_hideReimplementedFunctions =
            new QCheckBox(Tr::tr("&Hide reimplemented functions"), this);

    // Insertion options
    m_insertMode = new QComboBox(this);
    m_insertMode->addItem(Tr::tr("Insert only declarations"), ModeOnlyDeclarations);
    m_insertMode->addItem(Tr::tr("Insert definitions inside class"), ModeInsideClass);
    m_insertMode->addItem(Tr::tr("Insert definitions outside class"), ModeOutsideClass);
    m_insertMode->addItem(Tr::tr("Insert definitions in implementation file"), ModeImplementationFile);
    m_virtualKeyword = new QCheckBox(Tr::tr("Add \"&virtual\" to function declaration"), this);
    m_overrideReplacementCheckBox = new QCheckBox(
                Tr::tr("Add \"override\" equivalent to function declaration:"), this);
    m_overrideReplacementComboBox = new QComboBox(this);
    QSizePolicy sizePolicy = m_overrideReplacementComboBox->sizePolicy();
    sizePolicy.setHorizontalPolicy(QSizePolicy::Expanding);
    m_overrideReplacementComboBox->setSizePolicy(sizePolicy);
    m_overrideReplacementComboBox->setEditable(true);
    connect(m_overrideReplacementCheckBox, &QCheckBox::clicked,
            m_overrideReplacementComboBox, &QComboBox::setEnabled);

    auto clearUserAddedReplacements = new QAction(this);
    clearUserAddedReplacements->setIcon(Utils::Icons::CLEAN_TOOLBAR.icon());
    clearUserAddedReplacements->setText(Tr::tr("Clear Added \"override\" Equivalents"));
    connect(clearUserAddedReplacements, &QAction::triggered, this, [this] {
       m_availableOverrideReplacements = defaultOverrideReplacements();
       updateOverrideReplacementsComboBox();
       m_clearUserAddedReplacementsButton->setEnabled(false);
    });
    m_clearUserAddedReplacementsButton = new QToolButton(this);
    m_clearUserAddedReplacementsButton->setDefaultAction(clearUserAddedReplacements);

    // Bottom button box
    m_buttons = new QDialogButtonBox(this);
    m_buttons->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    using namespace Layouting;
    Column {
        Group {
            title(Tr::tr("&Functions to insert:")),
            Column {
                m_filter,
                m_view,
                m_hideReimplementedFunctions,
            },
        },
        Group {
            title(Tr::tr("&Insertion options:")),
            Column {
                m_insertMode,
                m_virtualKeyword,
                Row {
                    m_overrideReplacementCheckBox,
                    m_overrideReplacementComboBox,
                    m_clearUserAddedReplacementsButton,
                    spacing(0),
                },
            },
        },
        m_buttons,
    }.attachTo(this);

    connect(m_hideReimplementedFunctions, &QAbstractButton::toggled,
            this, &InsertVirtualMethodsDialog::setHideReimplementedFunctions);
    connect(m_filter, &QLineEdit::textChanged,
            classFunctionFilterModel, &QSortFilterProxyModel::setFilterWildcard);
}

void InsertVirtualMethodsDialog::initData()
{
    m_settings->read();
    m_filter->clear();
    m_hideReimplementedFunctions->setChecked(m_settings->hideReimplementedFunctions);
    const QStringList alwaysPresentReplacements = defaultOverrideReplacements();
    m_availableOverrideReplacements = alwaysPresentReplacements;
    m_availableOverrideReplacements += m_settings->userAddedOverrideReplacements;

    m_view->setModel(classFunctionFilterModel);
    m_expansionStateNormal.clear();
    m_expansionStateReimp.clear();
    m_hideReimplementedFunctions->setEnabled(m_hasReimplementedFunctions);
    m_virtualKeyword->setChecked(m_settings->insertVirtualKeyword);
    m_insertMode->setCurrentIndex(m_insertMode->findData(m_settings->implementationMode));

    m_overrideReplacementCheckBox->setChecked(m_settings->insertOverrideReplacement);
    updateOverrideReplacementsComboBox();
    const bool canClear = m_availableOverrideReplacements.size() > alwaysPresentReplacements.size();
    m_clearUserAddedReplacementsButton->setEnabled(canClear);
    int overrideReplacementIndex = m_settings->overrideReplacementIndex;
    if (overrideReplacementIndex >= m_overrideReplacementComboBox->count())
        overrideReplacementIndex = 0;
    m_overrideReplacementComboBox->setCurrentIndex(overrideReplacementIndex);

    setHideReimplementedFunctions(m_hideReimplementedFunctions->isChecked());

    if (m_hasImplementationFile) {
        if (m_insertMode->count() == 3) {
            m_insertMode->addItem(Tr::tr("Insert definitions in implementation file"),
                                  ModeImplementationFile);
        }
    } else {
        if (m_insertMode->count() == 4)
            m_insertMode->removeItem(3);
    }
}

void InsertVirtualMethodsDialog::saveSettings()
{
    m_settings->insertVirtualKeyword = m_virtualKeyword->isChecked();
    m_settings->implementationMode = static_cast<InsertVirtualMethodsDialog::ImplementationMode>(
                m_insertMode->itemData(m_insertMode->currentIndex()).toInt());
    m_settings->hideReimplementedFunctions = m_hideReimplementedFunctions->isChecked();
    m_settings->insertOverrideReplacement = m_overrideReplacementCheckBox->isChecked();
    m_settings->overrideReplacementIndex = m_overrideReplacementComboBox->currentIndex();
    if (m_overrideReplacementComboBox && m_overrideReplacementComboBox->isEnabled())
        m_settings->overrideReplacement = m_overrideReplacementComboBox->currentText().trimmed();
    QSet<QString> addedReplacements = Utils::toSet(m_availableOverrideReplacements);
    addedReplacements.insert(m_settings->overrideReplacement);
    addedReplacements.subtract(Utils::toSet(defaultOverrideReplacements()));
    m_settings->userAddedOverrideReplacements =
            sortedAndTrimmedStringListWithoutEmptyElements(Utils::toList(addedReplacements));
    m_settings->write();
}

const VirtualMethodsSettings *InsertVirtualMethodsDialog::settings() const
{
    return m_settings;
}

bool InsertVirtualMethodsDialog::gather()
{
    initGui();
    initData();
    m_filter->setFocus();

    // Expand the dialog a little bit
    adjustSize();
    resize(size() * 1.5);

    QPointer<InsertVirtualMethodsDialog> that(this);
    const int ret = exec();
    if (!that)
        return false;

    return (ret == QDialog::Accepted);
}

void InsertVirtualMethodsDialog::setHasImplementationFile(bool file)
{
    m_hasImplementationFile = file;
}

void InsertVirtualMethodsDialog::setHasReimplementedFunctions(bool functions)
{
    m_hasReimplementedFunctions = functions;
}

void InsertVirtualMethodsDialog::setHideReimplementedFunctions(bool hide)
{
    auto model = qobject_cast<InsertVirtualMethodsFilterModel *>(classFunctionFilterModel);

    if (m_expansionStateNormal.isEmpty() && m_expansionStateReimp.isEmpty()) {
        model->setHideReimplementedFunctions(hide);
        m_view->expandAll();
        saveExpansionState();
        return;
    }

    if (model->hideReimplemented() == hide)
        return;

    saveExpansionState();
    model->setHideReimplementedFunctions(hide);
    restoreExpansionState();
}

void InsertVirtualMethodsDialog::updateOverrideReplacementsComboBox()
{
    m_overrideReplacementComboBox->clear();
    for (const QString &replacement : std::as_const(m_availableOverrideReplacements))
        m_overrideReplacementComboBox->addItem(replacement);
}

void InsertVirtualMethodsDialog::saveExpansionState()
{
    auto model = qobject_cast<InsertVirtualMethodsFilterModel *>(classFunctionFilterModel);

    QList<bool> &state = model->hideReimplemented() ? m_expansionStateReimp
                                                    : m_expansionStateNormal;
    state.clear();
    for (int i = 0; i < model->rowCount(); ++i)
        state << m_view->isExpanded(model->index(i, 0));
}

void InsertVirtualMethodsDialog::restoreExpansionState()
{
    auto model = qobject_cast<InsertVirtualMethodsFilterModel *>(classFunctionFilterModel);

    const QList<bool> &state = model->hideReimplemented() ? m_expansionStateReimp
                                                          : m_expansionStateNormal;
    const int stateCount = state.count();
    for (int i = 0; i < model->rowCount(); ++i) {
        if (i < stateCount && !state.at(i)) {
            m_view->collapse(model->index(i, 0));
            continue;
        }
        m_view->expand(model->index(i, 0));
    }
}

InsertVirtualMethods::InsertVirtualMethods(InsertVirtualMethodsDialog *dialog)
    : m_dialog(dialog)
{
    if (!dialog)
        m_dialog = new InsertVirtualMethodsDialog;
}

InsertVirtualMethods::~InsertVirtualMethods()
{
    m_dialog->deleteLater();
}

void InsertVirtualMethods::doMatch(const CppQuickFixInterface &interface,
                                   QuickFixOperations &result)
{
    QSharedPointer<InsertVirtualMethodsOp> op(new InsertVirtualMethodsOp(interface, m_dialog));
    if (op->isValid())
        result.append(op);
}

#ifdef WITH_TESTS

namespace Tests {

typedef QByteArray _;

/// Stub dialog of InsertVirtualMethodsDialog that does not pop up anything.
class InsertVirtualMethodsDialogTest : public InsertVirtualMethodsDialog
{
public:
    InsertVirtualMethodsDialogTest(ImplementationMode mode,
                                   bool insertVirtualKeyword,
                                   bool insertOverrideKeyword,
                                   QWidget *parent = 0)
        : InsertVirtualMethodsDialog(parent)
    {
        m_settings->implementationMode = mode;
        m_settings->insertVirtualKeyword = insertVirtualKeyword;
        m_settings->insertOverrideReplacement = insertOverrideKeyword;
        m_settings->overrideReplacement = QLatin1String("override");
    }

    bool gather() override { return true; }
    void saveSettings() override { }
};

class InsertVirtualMethodsTest : public QObject
{
    Q_OBJECT

private slots:
    void test_data();
    void test();
    void testImplementationFile();
    void testBaseClassInNamespace();
    void testImplementationFileNestedType();
};

void InsertVirtualMethodsTest::test_data()
{
    QTest::addColumn<InsertVirtualMethodsDialog::ImplementationMode>("implementationMode");
    QTest::addColumn<bool>("insertVirtualKeyword");
    QTest::addColumn<bool>("insertOverrideKeyword");
    QTest::addColumn<QByteArray>("original");
    QTest::addColumn<QByteArray>("expected");

    // Check: Insert only declarations
    QTest::newRow("onlyDecl")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "};\n"
    );

    // Check: A destructor is not one of the functions to implement. Every
    // class below a base with a virtual one has it already, and writing a
    // body for it is not what asking for the interface means.
    QTest::newRow("skipDestructor")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual ~BaseA() = 0;\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual ~BaseA() = 0;\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "};\n"
    );

    // Check: The three virtuals moc writes into every QObject are not
    // offered. They are declared by the Q_OBJECT macro and reimplemented
    // by it, so nobody writes one by hand.
    QTest::newRow("skipMagicQObjectFunctions")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class QMetaObject { public: enum Call { InvokeMetaMethod }; };\n"
        "class QObject {\n"
        "public:\n"
        "    virtual const QMetaObject *metaObject() const = 0;\n"
        "    virtual void *qt_metacast(const char *) = 0;\n"
        "    virtual int qt_metacall(QMetaObject::Call, int, void **) = 0;\n"
        "    virtual bool event() = 0;\n"
        "};\n\n"
        "class Derived : public QObje@ct {\n"
        "};\n"
        ) << _(
        "class QMetaObject { public: enum Call { InvokeMetaMethod }; };\n"
        "class QObject {\n"
        "public:\n"
        "    virtual const QMetaObject *metaObject() const = 0;\n"
        "    virtual void *qt_metacast(const char *) = 0;\n"
        "    virtual int qt_metacall(QMetaObject::Call, int, void **) = 0;\n"
        "    virtual bool event() = 0;\n"
        "};\n\n"
        "class Derived : public QObject {\n"
        "\n"
        "    // QObject interface\n"
        "public:\n"
        "    virtual bool event();\n"
        "};\n"
    );

    // Check: Insert only declarations without virtual keyword but with override
    QTest::newRow("onlyDeclWithoutVirtual")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << false << true << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    int virtualFuncA() override;\n"
        "};\n"
    );

    // Check: Are access specifiers considered
    QTest::newRow("Access")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "protected:\n"
        "    virtual int b() = 0;\n"
        "private:\n"
        "    virtual int c() = 0;\n"
        "public slots:\n"
        "    virtual int d() = 0;\n"
        "protected slots:\n"
        "    virtual int e() = 0;\n"
        "private slots:\n"
        "    virtual int f() = 0;\n"
        "signals:\n"
        "    virtual int g() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "protected:\n"
        "    virtual int b() = 0;\n"
        "private:\n"
        "    virtual int c() = 0;\n"
        "public slots:\n"
        "    virtual int d() = 0;\n"
        "protected slots:\n"
        "    virtual int e() = 0;\n"
        "private slots:\n"
        "    virtual int f() = 0;\n"
        "signals:\n"
        "    virtual int g() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n\n"
        "protected:\n"
        "    virtual int b();\n\n"
        "private:\n"
        "    virtual int c();\n\n"
        "public slots:\n"
        "    virtual int d();\n\n"
        "protected slots:\n"
        "    virtual int e();\n\n"
        "private slots:\n"
        "    virtual int f();\n\n"
        "signals:\n"
        "    virtual int g();\n"
        "};\n"
    );

    // Check: Is a base class of a base class considered.
    QTest::newRow("Superclass")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "\n"
        "    // BaseB interface\n"
        "public:\n"
        "    virtual int b();\n"
        "};\n"
    );


    // Check: Do not insert reimplemented functions twice.
    QTest::newRow("SuperclassOverride")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "};\n"
    );

    // Check: Insert only declarations for pure virtual function
    QTest::newRow("PureVirtualOnlyDecl")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "};\n"
    );

    // Check: Insert pure virtual functions inside class
    QTest::newRow("PureVirtualInside")
        << InsertVirtualMethodsDialog::ModeInsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA()\n"
        "    {\n"
        "    }\n"
        "};\n"
    );

    // Check: Overloads
    QTest::newRow("Overloads")
        << InsertVirtualMethodsDialog::ModeInsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virt(int i) = 0;\n"
        "    virtual int virt(double d) = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virt(int i) = 0;\n"
        "    virtual int virt(double d) = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virt(int i)\n"
        "    {\n"
        "    }\n"
        "    virtual int virt(double d)\n"
        "    {\n"
        "    }\n"
        "};\n"
    );

    // Check: Insert inside class
    QTest::newRow("inside")
        << InsertVirtualMethodsDialog::ModeInsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA()\n"
        "    {\n"
        "    }\n"
        "};\n"
    );

    // Check: Insert outside class
    QTest::newRow("outside")
        << InsertVirtualMethodsDialog::ModeOutsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "};\n\n"
        "int Derived::virtualFuncA()\n"
        "{\n"
        "}\n"
    );

    // Check: No trigger: all implemented
    QTest::newRow("notrigger_allImplemented")
        << InsertVirtualMethodsDialog::ModeOutsideClass << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "    virtual operator==(const BaseA &);\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "    virtual operator==(const BaseA &);\n"
        "};\n"
        ) << _();

    // Check: One pure, one not
    QTest::newRow("Some_Pure")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "    virtual int virtualFuncB();\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int virtualFuncA() = 0;\n"
        "    virtual int virtualFuncB();\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int virtualFuncA();\n"
        "};\n"
    );

    // Check: Pure function in derived class
    QTest::newRow("Pure_in_Derived")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "};\n"
    );

    // Check: One pure function in base class, one in derived
    QTest::newRow("Pure_in_Base_And_Derived")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "    virtual int b();\n"
        "};\n"
    );

    // Check: One pure function in base class, two in derived
    QTest::newRow("Pure_in_Base_And_Derived_2")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "    virtual int c() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b();\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int b() = 0;\n"
        "    virtual int c() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a();\n"
        "    virtual int b();\n"
        "\n"
        "    // BaseB interface\n"
        "public:\n"
        "    virtual int c();\n"
        "};\n"
    );

    // Check: Remove final function
    QTest::newRow("final_function_removed")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() final = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() final = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseB {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int b();\n"
        "};\n"
    );

    // Check: Remove multiple final functions
    QTest::newRow("multiple_final_functions_removed")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << true << false << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int c() = 0;\n"
        "};\n\n"
        "class BaseC : public BaseB {\n"
        "public:\n"
        "    virtual int a() final = 0;\n"
        "    virtual int d() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseC {\n"
        "};\n"
        ) << _(
        "class BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int b() = 0;\n"
        "};\n\n"
        "class BaseB : public BaseA {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual int c() = 0;\n"
        "};\n\n"
        "class BaseC : public BaseB {\n"
        "public:\n"
        "    virtual int a() final = 0;\n"
        "    virtual int d() = 0;\n"
        "};\n\n"
        "class Der@ived : public BaseC {\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int b();\n"
        "\n"
        "    // BaseB interface\n"
        "public:\n"
        "    virtual int c();\n"
        "\n"
        "    // BaseC interface\n"
        "public:\n"
        "    virtual int d();\n"
        "};\n"
    );

    // Check: Insert multiply-inherited virtual function only once.
    QTest::newRow("multiple_inheritance_insert")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << false << true << _(
        "struct Base1 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Base2 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct @Derived : Base1, Base2 {\n"
        "};\n") << _(
        "struct Base1 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Base2 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Derived : Base1, Base2 {\n\n"
        "    // Base2 interface\n"
        "public:\n"
        "    void virt() override;\n"
        "};\n");

    // Check: Do not insert multiply-inherited virtual function that has been re-implemented
    //        along the way.
    QTest::newRow("multiple_inheritance_no_insert")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << false << true << _(
        "struct Base1 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Base2 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Derived1 : Base1, Base2 {\n"
        "    void virt() override;\n"
        "};\n\n"
        "struct @Derived2 : Derived1\n"
        "};\n") << _(
        "struct Base1 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Base2 {\n"
        "    virtual void virt() = 0;\n"
        "};\n\n"
        "struct Derived1 : Base1, Base2 {\n"
        "    void virt() override;\n"
        "};\n\n"
        "struct Derived2 : Derived1\n"
        "};\n");

    QTest::newRow("base class via using")
        << InsertVirtualMethodsDialog::ModeOnlyDeclarations << false << true
        << _(R"cpp(
                  namespace A { struct Base { virtual void foo() = 0; }; } // namespace A
                  using A::Base;
                  struct @Derived : Base {};)cpp")
        << _(R"cpp(
                  namespace A { struct Base { virtual void foo() = 0; }; } // namespace A
                  using A::Base;
                  struct @Derived : Base {
                      // Base interface
                  public:
                      void foo() override;
                  };)cpp");
}

void InsertVirtualMethodsTest::test()
{
    QFETCH(InsertVirtualMethodsDialog::ImplementationMode, implementationMode);
    QFETCH(bool, insertVirtualKeyword);
    QFETCH(bool, insertOverrideKeyword);
    QFETCH(QByteArray, original);
    QFETCH(QByteArray, expected);

    InsertVirtualMethods factory(
                new Tests::InsertVirtualMethodsDialogTest(implementationMode,
                                                          insertVirtualKeyword,
                                                          insertOverrideKeyword));
    Tests::QuickFixOperationTest(Tests::singleDocument(original, expected), &factory);
}

/// Check: Insert in implementation file
void InsertVirtualMethodsTest::testImplementationFile()
{
    QList<Tests::TestDocumentPtr> testFiles;
    QByteArray original;
    QByteArray expected;

    // Header File
    original =
        "class BaseA {\n"
        "public:\n"
        "    virtual int a(const std::vector<int> &v) = 0;\n"
        "};\n\n"
        "class Derived : public Bas@eA {\n"
        "public:\n"
        "    Derived();\n"
        "};\n";
    expected =
        "class BaseA {\n"
        "public:\n"
        "    virtual int a(const std::vector<int> &v) = 0;\n"
        "};\n\n"
        "class Derived : public BaseA {\n"
        "public:\n"
        "    Derived();\n"
        "\n"
        "    // BaseA interface\n"
        "public:\n"
        "    virtual int a(const std::vector<int> &v);\n"
        "};\n";
    testFiles << Tests::CppTestDocument::create("file.h", original, expected);

    // Source File
    original = "#include \"file.h\"\n";
    expected =
        "#include \"file.h\"\n"
        "\n\n"
        "int Derived::a(const std::vector<int> &v)\n"
        "{\n}";
    testFiles << Tests::CppTestDocument::create("file.cpp", original, expected);

    InsertVirtualMethods factory(new Tests::InsertVirtualMethodsDialogTest(
                                     InsertVirtualMethodsDialog::ModeImplementationFile,
                                     true,
                                     false));
    Tests::QuickFixOperationTest(testFiles, &factory);
}

/// Check: Qualified names.
// A type a base declares inside itself is named by the class below it
// without saying where it comes from -- it inherits the name -- and has to
// be said in full where the definition goes, which is outside both.
void InsertVirtualMethodsTest::testImplementationFileNestedType()
{
    QList<Tests::TestDocumentPtr> testFiles;
    QByteArray original;
    QByteArray expected;

    // Header File
    original =
        "class Base {\n"
        "public:\n"
        "    class Inner {};\n"
        "    virtual Inner f() = 0;\n"
        "};\n"
        "class Deri@ved : public Base {\n"
        "};\n";
    expected =
        "class Base {\n"
        "public:\n"
        "    class Inner {};\n"
        "    virtual Inner f() = 0;\n"
        "};\n"
        "class Derived : public Base {\n"
        "\n"
        "    // Base interface\n"
        "public:\n"
        "    virtual Inner f();\n"
        "};\n";
    testFiles << Tests::CppTestDocument::create("file.h", original, expected);

    // Source File
    original = "#include \"file.h\"\n";
    expected =
        "#include \"file.h\"\n"
        "\n\n"
        "Base::Inner Derived::f()\n"
        "{\n}";
    testFiles << Tests::CppTestDocument::create("file.cpp", original, expected);

    InsertVirtualMethods factory(new Tests::InsertVirtualMethodsDialogTest(
                                     InsertVirtualMethodsDialog::ModeImplementationFile,
                                     true,
                                     false));
    Tests::QuickFixOperationTest(testFiles, &factory);
}

void InsertVirtualMethodsTest::testBaseClassInNamespace()
{
    QList<Tests::TestDocumentPtr> testFiles;
    QByteArray original;
    QByteArray expected;

    // Header File
    original =
        "namespace BaseNS {enum BaseEnum {EnumA = 1};}\n"
        "namespace BaseNS {\n"
        "class Base {\n"
        "public:\n"
        "    virtual BaseEnum a(BaseEnum e) = 0;\n"
        "};\n"
        "}\n"
        "class Deri@ved : public BaseNS::Base {\n"
        "public:\n"
        "    Derived();\n"
        "};\n";
    expected =
        "namespace BaseNS {enum BaseEnum {EnumA = 1};}\n"
        "namespace BaseNS {\n"
        "class Base {\n"
        "public:\n"
        "    virtual BaseEnum a(BaseEnum e) = 0;\n"
        "};\n"
        "}\n"
        "class Deri@ved : public BaseNS::Base {\n"
        "public:\n"
        "    Derived();\n"
        "\n"
        "    // Base interface\n"
        "public:\n"
        "    virtual BaseNS::BaseEnum a(BaseNS::BaseEnum e);\n"
        "};\n";
    testFiles << Tests::CppTestDocument::create("file.h", original, expected);

    // Source File
    original = "#include \"file.h\"\n";
    expected =
        "#include \"file.h\"\n"
        "\n\n"
        "BaseNS::BaseEnum Derived::a(BaseNS::BaseEnum e)\n"
        "{\n}";
    testFiles << Tests::CppTestDocument::create("file.cpp", original, expected);

    InsertVirtualMethods factory(new Tests::InsertVirtualMethodsDialogTest(
                                     InsertVirtualMethodsDialog::ModeImplementationFile,
                                     true,
                                     false));
    Tests::QuickFixOperationTest(testFiles, &factory);
}

} // namespace Tests

InsertVirtualMethods *InsertVirtualMethods::createTestFactory()
{
    return new InsertVirtualMethods(new Tests::InsertVirtualMethodsDialogTest(
                                        InsertVirtualMethodsDialog::ModeOutsideClass, true, false));
}

QObject *InsertVirtualMethods::createTest()
{
    return new Tests::InsertVirtualMethodsTest;
}

#endif // WITH_TESTS

} // namespace CppEditor::Internal

#include "cppinsertvirtualmethods.moc"
