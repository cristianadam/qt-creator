// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodegenerationquickfixes.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "../insertionpointlocator.h"
#include "cppquickfix.h"
#include "cppquickfixhelpers.h"
#include "cppquickfixsettings.h"

#include <cplusplus/Overview.h>
#include <cplusplus/CppRewriter.h>
#include <projectexplorer/projecttree.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cppmodelmanager.h"
#include "../cxxfrontendmodel.h"
#endif
#include <utils/basetreeview.h>
#include <utils/treemodel.h>

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMimeData>
#include <QPushButton>
#include <QProxyStyle>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTreeView>
#include <QLabel>

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#include <QAbstractItemModelTester>
#include <QTest>
#endif

using namespace CPlusPlus;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

enum GenerateFlag {
    GenerateGetter = 1 << 0,
    GenerateSetter = 1 << 1,
    GenerateSignal = 1 << 2,
    GenerateMemberVariable = 1 << 3,
    GenerateReset = 1 << 4,
    GenerateProperty = 1 << 5,
    GenerateConstantProperty = 1 << 6,
    HaveExistingQProperty = 1 << 7,
    GenerateBindable = 1 << 8,
    Invalid = -1,
};

static QStringList toStringList(const QList<Symbol *> names)
{
    QStringList list;
    list.reserve(names.size());
    for (const auto symbol : names) {
        const Identifier *const id = symbol->identifier();
        list << QString::fromUtf8(id->chars(), id->size());
    }
    return list;
}

static QList<Symbol *> getMemberFunctions(const Class *clazz)
{
    QList<Symbol *> memberFunctions;
    for (auto it = clazz->memberBegin(); it != clazz->memberEnd(); ++it) {
        Symbol *const s = *it;
        if (!s->identifier() || !s->type())
            continue;
        if ((s->asDeclaration() && s->type()->asFunctionType()) || s->asFunction())
            memberFunctions << s;
    }
    return memberFunctions;
}

static QString symbolAtDifferentLocation(
    const CppQuickFixInterface &interface,
    Symbol *symbol,
    const CppRefactoringFilePtr &targetFile,
    InsertionLocation targetLocation)
{
    QTC_ASSERT(symbol, return QString());
    Scope *scopeAtInsertPos = targetFile->cppDocument()->scopeAt(targetLocation.line(),
                                                                 targetLocation.column());

    LookupContext cppContext(targetFile->cppDocument(), interface.snapshot());
    ClassOrNamespace *cppCoN = cppContext.lookupType(scopeAtInsertPos);
    if (!cppCoN)
        cppCoN = cppContext.globalNamespace();
    SubstitutionEnvironment env;
    env.setContext(interface.context());
    env.switchScope(symbol->enclosingScope());
    UseMinimalNames q(cppCoN);
    env.enter(&q);
    Control *control = interface.context().bindings()->control().get();
    Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();
    return oo.prettyName(LookupContext::minimalName(symbol, cppCoN, control));
}

static FullySpecifiedType typeAtDifferentLocation(
    const CppQuickFixInterface &interface,
    FullySpecifiedType type,
    Scope *originalScope,
    const CppRefactoringFilePtr &targetFile,
    InsertionLocation targetLocation,
    const QStringList &newNamespaceNamesAtLoc = {})
{
    Scope *scopeAtInsertPos = targetFile->cppDocument()->scopeAt(targetLocation.line(),
                                                                 targetLocation.column());
    for (const QString &nsName : newNamespaceNamesAtLoc) {
        const QByteArray utf8Name = nsName.toUtf8();
        Control *control = targetFile->cppDocument()->control();
        const Name *name = control->identifier(utf8Name.data(), utf8Name.size());
        Namespace *ns = control->newNamespace(0, name);
        ns->setEnclosingScope(scopeAtInsertPos);
        scopeAtInsertPos = ns;
    }
    LookupContext cppContext(targetFile->cppDocument(), interface.snapshot());
    ClassOrNamespace *cppCoN = cppContext.lookupType(scopeAtInsertPos);
    if (!cppCoN)
        cppCoN = cppContext.globalNamespace();
    SubstitutionEnvironment env;
    env.setContext(interface.context());
    env.switchScope(originalScope);
    UseMinimalNames q(cppCoN);
    env.enter(&q);
    Control *control = interface.context().bindings()->control().get();
    return rewriteType(type, &env, control);
}

static std::optional<FullySpecifiedType> getFirstTemplateParameter(const Name *name)
{
    if (const QualifiedNameId *qualifiedName = name->asQualifiedNameId())
        return getFirstTemplateParameter(qualifiedName->name());

    if (const TemplateNameId *templateName = name->asTemplateNameId()) {
        if (templateName->templateArgumentCount() > 0)
            return templateName->templateArgumentAt(0).type();
    }
    return {};
}

static std::optional<FullySpecifiedType> getFirstTemplateParameter(Type *type)
{
    if (NamedType *namedType = type->asNamedType())
        return getFirstTemplateParameter(namedType->name());

    return {};
}

static std::optional<FullySpecifiedType> getFirstTemplateParameter(FullySpecifiedType type)
{
    return getFirstTemplateParameter(type.type());
}

// How this generation prints what it writes: the project's own style,
// and template parameters spelled out, a type being written down and not
// summarised.
static Overview generationOverview()
{
    Overview overview = CppCodeStyleSettings::currentProjectCodeStyleOverview();
    overview.showTemplateParameters = true;
    return overview;
}

class GeneratedClass;

// What a type or a class was read with, kept for the writing that comes
// later: what reads a member runs in the quick fix's match, and what
// writes it runs when the fix is chosen.
using ReadWith = std::shared_ptr<const CppQuickFixInterface>;

// What a front end says about a type it read. The generation holds a
// GeneratedType, which is a value; this is what fills one in, and there
// is one of these for each front end that reads a member.
class TypeReading
{
public:
    using Ptr = std::shared_ptr<const TypeReading>;
    virtual ~TypeReading() = default;

    virtual bool isPointer() const = 0;
    virtual bool isConst() const = 0;
    virtual bool isStatic() const = 0;
    virtual bool isValueType(bool *saidByName) const = 0;

    virtual Ptr asDeclared() const = 0;
    virtual Ptr withoutConst() const = 0;
    virtual Ptr asValue() const = 0;
    virtual Ptr constReference() const = 0;
    virtual Ptr withConstOnReference() const = 0;
    virtual Ptr firstTemplateArgument() const = 0;
    virtual Ptr writtenAt(const CppRefactoringFilePtr &file,
                          const InsertionLocation &location,
                          const QStringList &namespacesOpenedThere) const = 0;

    virtual QString asDeclarationOf(const QString &name) const = 0;
    virtual QString asTextWithoutTemplateParameters() const = 0;
};

// A type the getter and setter generation writes down, and the only
// thing it knows about a front end's idea of one. A type is not a string
// here because it is written differently depending on where it goes --
// as the class writes it inside the class, and with as little in front
// of it as still finds it from anywhere else -- so it is asked rather
// than printed.
class GeneratedType
{
public:
    GeneratedType() = default;
    explicit GeneratedType(TypeReading::Ptr reading)
        : m_reading(std::move(reading))
    {}

    // Read by the built-in front end, from a type of its own.
    static GeneratedType readBuiltin(const FullySpecifiedType &type, Scope *scope,
                                     const ReadWith &readWith);
    // The same, as the reading itself: what a reading on another front
    // end carries for the questions it cannot answer.
    static TypeReading::Ptr builtinReading(const FullySpecifiedType &type, Scope *scope,
                                           const ReadWith &readWith);
    // A type nobody read, spelled out by a setting: it is written as it
    // stands, wherever it is written.
    static GeneratedType fromText(const QString &text, const ReadWith &readWith);

    bool isValid() const { return m_reading != nullptr; }
    bool isPointer() const { return isValid() && m_reading->isPointer(); }

    // What a member is declared with, the type having been read off one
    // that may have said const or static.
    GeneratedType asDeclared() const { return made(&TypeReading::asDeclared); }
    // Without the const a member may have been declared with, which a
    // parameter taking its value has no use for.
    GeneratedType withoutConst() const { return made(&TypeReading::withoutConst); }
    // The value behind it: what a Q_PROPERTY says, a getter handing back
    // a const reference notwithstanding.
    GeneratedType asValue() const { return made(&TypeReading::asValue); }
    // const T &, and the const that a T & handed over needs.
    GeneratedType constReference() const { return made(&TypeReading::constReference); }
    GeneratedType withConstOnReference() const
    {
        return made(&TypeReading::withConstOnReference);
    }
    // QList<int> -> int. Invalid where the type names no template.
    GeneratedType firstTemplateArgument() const
    {
        return made(&TypeReading::firstTemplateArgument);
    }
    // The same type where a definition is being written, named with as
    // little in front of it as still finds it from there.
    GeneratedType writtenAt(const CppRefactoringFilePtr &file,
                            const InsertionLocation &location,
                            const QStringList &namespacesOpenedThere = {}) const
    {
        if (!isValid())
            return {};
        return GeneratedType(m_reading->writtenAt(file, location, namespacesOpenedThere));
    }

    // Written as a declaration of \a name, or alone where that is empty.
    QString asDeclarationOf(const QString &name) const
    {
        return isValid() ? m_reading->asDeclarationOf(name) : QString();
    }
    QString asText() const { return asDeclarationOf({}); }
    // How the settings name a type they have a template for.
    QString asTextWithoutTemplateParameters() const
    {
        return isValid() ? m_reading->asTextWithoutTemplateParameters() : QString();
    }

    // Whether it is handed over by value: a pointer, an enumeration, a
    // number or a reference is, and so is whatever the settings name --
    // which is what \a saidByName reports.
    bool isValueType(bool *saidByName = nullptr) const
    {
        return isValid() && m_reading->isValueType(saidByName);
    }

    bool isConst() const { return isValid() && m_reading->isConst(); }
    bool isStatic() const { return isValid() && m_reading->isStatic(); }

private:
    GeneratedType made(TypeReading::Ptr (TypeReading::*step)() const) const
    {
        return isValid() ? GeneratedType((m_reading.get()->*step)()) : GeneratedType();
    }

    TypeReading::Ptr m_reading;
};

// What a front end says about a class it read, and the class the
// generation writes into. Its name is asked rather than kept for the
// same reason a type is: a class in a namespace is not named from the
// source file by the name it was declared under.
class ClassReading
{
public:
    using Ptr = std::shared_ptr<const ClassReading>;
    virtual ~ClassReading() = default;

    virtual QString name() const = 0;
    virtual bool isAnonymous() const = 0;
    virtual bool isQObject() const = 0;
    virtual QString writtenAt(const CppRefactoringFilePtr &file,
                              const InsertionLocation &location) const = 0;
    virtual InsertionLocation placeItIsWrittenAt(const Utils::FilePath &filePath) const = 0;
    virtual InsertionLocation placeForDeclaration(const InsertionPointLocator &locator,
                                                  const Utils::FilePath &filePath,
                                                  InsertionPointLocator::AccessSpec spec) const = 0;
    virtual DeclarationToDefine toDefine(const CppRefactoringChanges &changes) const = 0;
    virtual ReadWith readWith() const = 0;
};

class GeneratedClass
{
public:
    GeneratedClass() = default;
    explicit GeneratedClass(ClassReading::Ptr reading)
        : m_reading(std::move(reading))
    {}

    // Read by the built-in front end, from a class of its own.
    static GeneratedClass readBuiltin(Class *clazz, const ReadWith &readWith);

    bool isValid() const { return m_reading != nullptr; }

    // As it was written. A class written with no name of its own has
    // none, and nothing of it can be declared anywhere but inside it,
    // since nothing outside could say which class it is.
    QString name() const { return isValid() ? m_reading->name() : QString(); }
    bool isAnonymous() const { return !isValid() || m_reading->isAnonymous(); }
    // Whether QObject is above it, which is what lets a setter be a slot
    // and a property be notified of.
    bool isQObject() const { return isValid() && m_reading->isQObject(); }
    // Its name where a definition of one of its members is going, which
    // for a class in a namespace is more than the name.
    QString writtenAt(const CppRefactoringFilePtr &file,
                      const InsertionLocation &location) const
    {
        return isValid() ? m_reading->writtenAt(file, location) : QString();
    }
    // Where its own name is written, which is where a declaration
    // standing outside it stands: a type written there is written as
    // whatever encloses the class writes it.
    InsertionLocation placeItIsWrittenAt(const Utils::FilePath &filePath) const
    {
        return isValid() ? m_reading->placeItIsWrittenAt(filePath) : InsertionLocation();
    }
    // Where a declaration of the kind \a spec goes in it.
    InsertionLocation placeForDeclaration(const InsertionPointLocator &locator,
                                          const Utils::FilePath &filePath,
                                          InsertionPointLocator::AccessSpec spec) const
    {
        return isValid() ? m_reading->placeForDeclaration(locator, filePath, spec)
                         : InsertionLocation();
    }

    // The class itself as the thing being defined, which is what a
    // constructor has to go by: it is not declared anywhere yet, so its
    // class stands in for it.
    DeclarationToDefine toDefine(const CppRefactoringChanges &changes) const
    {
        return isValid() ? m_reading->toDefine(changes) : DeclarationToDefine();
    }

    // What read it, for reading a type of its members with the same.
    ReadWith readWith() const { return isValid() ? m_reading->readWith() : ReadWith(); }
    // And the reading itself, for a reading on another front end to carry
    // for what it cannot say.
    const ClassReading::Ptr &reading() const { return m_reading; }

private:
    ClassReading::Ptr m_reading;
};

#ifdef QTC_WITH_CXX_FRONTEND
// What the cxx-frontend model says about a class it read. Only the four
// questions the two front ends answer differently are asked of it; where
// a class is written and where a declaration goes in it are places, which
// they agree on, and the built-in reading it carries says those.
class CxxClassReading : public ClassReading
{
public:
    CxxClassReading(const Utils::FilePath &filePath, int line, int column,
                    ClassReading::Ptr builtin, const ReadWith &readWith)
        : m_filePath(filePath)
        , m_line(line)
        , m_column(column)
        , m_builtin(std::move(builtin))
        , m_readWith(readWith)
    {}

    QString name() const override
    {
        const std::optional<CxxFrontendDocument::Declaration> declared
            = cxxFrontendDeclarationIn(CppModelManager::workingCopy(),
                                       m_filePath, m_line, m_column);
        if (!declared || declared->name.isEmpty())
            return m_builtin->name();
        // What it is called, which is the last of what it is called after
        // the scopes that hold it.
        return declared->name.split("::").last();
    }

    bool isAnonymous() const override { return m_builtin->isAnonymous(); }

    bool isQObject() const override
    {
        const std::optional<QList<CxxFrontendDocument::BaseClass>> bases
            = cxxFrontendBasesOfTheClassAt(CppModelManager::workingCopy(),
                                           m_filePath, m_line, m_column);
        if (!bases)
            return m_builtin->isQObject();
        // Standing below a class called QObject is what the built-in
        // reading works out by looking for the connect() it would have
        // inherited.
        for (const CxxFrontendDocument::BaseClass &base : *bases) {
            if (base.qualifiedName == "QObject" || base.qualifiedName.endsWith("::QObject"))
                return true;
        }
        return false;
    }

    QString writtenAt(const CppRefactoringFilePtr &file,
                      const InsertionLocation &location) const override
    {
        // A class is a type, and its name where a definition of one of its
        // members goes is the same question asked of any other type.
        CxxFrontendTypeRequest request;
        request.filePath = m_filePath;
        request.line = m_line;
        request.column = m_column;
        request.writtenIn = file->filePath();
        request.writtenAtLine = location.line();
        request.writtenAtColumn = location.column();
        const std::optional<QString> written = cxxFrontendTypeWritten(
            CppModelManager::workingCopy(), request, {});
        return written && !written->isEmpty() ? *written
                                              : m_builtin->writtenAt(file, location);
    }

    InsertionLocation placeItIsWrittenAt(const Utils::FilePath &filePath) const override
    {
        return m_builtin->placeItIsWrittenAt(filePath);
    }

    InsertionLocation placeForDeclaration(
        const InsertionPointLocator &locator,
        const Utils::FilePath &filePath,
        InsertionPointLocator::AccessSpec spec) const override
    {
        return m_builtin->placeForDeclaration(locator, filePath, spec);
    }

    DeclarationToDefine toDefine(const CppRefactoringChanges &changes) const override
    {
        return m_builtin->toDefine(changes);
    }

    ReadWith readWith() const override { return m_readWith; }

private:
    Utils::FilePath m_filePath;
    int m_line = 0;
    int m_column = 0;
    ClassReading::Ptr m_builtin;
    ReadWith m_readWith;
};
#endif

#ifdef QTC_WITH_CXX_FRONTEND
// What the cxx-frontend model says about a type it read. It holds the
// place the thing is declared at and what is to be made of its type
// rather than the type itself: the answer may have to be written into
// another file, which is read together with this one and is a reading of
// its own.
//
// It carries the built-in reading of the same type for the two things it
// cannot say. Staticness is not a question about a type at all -- a type
// does not carry it, the declaration does -- and a type written where
// namespaces are about to be created has to be written as if they were
// already there, which is a scope that exists nowhere.
class CxxTypeReading : public TypeReading
{
public:
    CxxTypeReading(const CxxFrontendTypeRequest &request, TypeReading::Ptr builtin,
                   const ReadWith &readWith)
        : m_request(request)
        , m_builtin(std::move(builtin))
        , m_readWith(readWith)
    {}

    bool isPointer() const override { return facts() && facts()->isPointer; }
    bool isConst() const override { return facts() && facts()->isConst; }
    bool isStatic() const override { return m_builtin->isStatic(); }

    bool isValueType(bool *saidByName) const override
    {
        if (saidByName)
            *saidByName = false;
        const std::optional<CxxFrontendTypeFacts> what = facts();
        if (!what)
            return m_builtin->isValueType(saidByName);

        // What is handed over by value. No search through the names in
        // front of it: this model resolved them while it read the file,
        // so what is left is the type itself.
        if (what->isPointer || what->isReference || what->isEnumeration || what->isNumber)
            return true;
        if (what->declaredName.isEmpty())
            return false;
        const CppQuickFixSettings * const settings = cppQuickFixSettingsForProject(
            ProjectTree::currentProject());
        if (!settings->isValueType(what->declaredName))
            return false;
        if (saidByName)
            *saidByName = true;
        return true;
    }

    Ptr asDeclared() const override { return with(CxxFrontendTypeStep::WithoutConst,
                                                  m_builtin->asDeclared()); }
    Ptr withoutConst() const override { return with(CxxFrontendTypeStep::WithoutConst,
                                                    m_builtin->withoutConst()); }
    Ptr asValue() const override { return with(CxxFrontendTypeStep::Value,
                                               m_builtin->asValue()); }
    Ptr constReference() const override { return with(CxxFrontendTypeStep::ConstReference,
                                                      m_builtin->constReference()); }
    Ptr withConstOnReference() const override
    {
        return with(CxxFrontendTypeStep::ConstOnReference, m_builtin->withConstOnReference());
    }
    Ptr firstTemplateArgument() const override
    {
        return with(CxxFrontendTypeStep::FirstTemplateArgument,
                    m_builtin->firstTemplateArgument());
    }

    Ptr writtenAt(const CppRefactoringFilePtr &file, const InsertionLocation &location,
                  const QStringList &namespacesOpenedThere) const override
    {
        const Ptr builtin = m_builtin->writtenAt(file, location, namespacesOpenedThere);
        // A type written where namespaces are about to be created is
        // written as if they stood there already, and they stand nowhere
        // yet: nothing to read it in.
        if (!namespacesOpenedThere.isEmpty())
            return builtin;
        CxxFrontendTypeRequest request = m_request;
        request.writtenIn = file->filePath();
        request.writtenAtLine = location.line();
        request.writtenAtColumn = location.column();
        return std::make_shared<CxxTypeReading>(request, builtin, m_readWith);
    }

    QString asDeclarationOf(const QString &name) const override
    {
        const std::optional<QString> written = cxxFrontendTypeWritten(
            CppModelManager::workingCopy(), m_request, name);
        return written ? *written : m_builtin->asDeclarationOf(name);
    }

    QString asTextWithoutTemplateParameters() const override
    {
        const std::optional<QString> written = cxxFrontendTypeWithoutTemplateParameters(
            CppModelManager::workingCopy(), m_request);
        return written ? *written : m_builtin->asTextWithoutTemplateParameters();
    }

private:
    Ptr with(CxxFrontendTypeStep step, Ptr builtin) const
    {
        if (!builtin)
            return {};
        CxxFrontendTypeRequest request = m_request;
        request.steps.append(step);
        return std::make_shared<CxxTypeReading>(request, builtin, m_readWith);
    }

    // Read once: reading a file is the whole cost of an answer here.
    const std::optional<CxxFrontendTypeFacts> &facts() const
    {
        if (!m_facts) {
            m_facts.emplace(cxxFrontendTypeFacts(CppModelManager::workingCopy(), m_request));
        }
        return *m_facts;
    }

    CxxFrontendTypeRequest m_request;
    TypeReading::Ptr m_builtin;
    ReadWith m_readWith;
    mutable std::optional<std::optional<CxxFrontendTypeFacts>> m_facts;
};
#endif

// The type of the member declared at \a line and \a column of the file
// being edited, read by whichever front end is running. The model reads
// it where it has the file and can see a type there; the built-in one
// answers otherwise, and answers the two questions the model leaves to
// it in any case.
GeneratedType readTypeOfTheMember(const CppQuickFixInterface &interface,
                                  const ReadWith &readWith,
                                  const FullySpecifiedType &type, Scope *scope,
                                  int line, int column)
{
    const TypeReading::Ptr builtin = GeneratedType::builtinReading(type, scope, readWith);
#ifdef QTC_WITH_CXX_FRONTEND
    if (builtin && line > 0 && column > 0) {
        CxxFrontendTypeRequest request;
        request.filePath = interface.filePath();
        request.line = line;
        request.column = column;
        if (cxxFrontendTypeFacts(CppModelManager::workingCopy(), request)) {
            return GeneratedType(
                std::make_shared<CxxTypeReading>(request, builtin, readWith));
        }
    }
#endif
    return GeneratedType(builtin);
}

// The class written at \a line and \a column of \a filePath, read by
// whichever front end is running.
GeneratedClass readTheClass(const CppQuickFixInterface &interface, const ReadWith &readWith,
                            Class *clazz)
{
    const GeneratedClass builtin = GeneratedClass::readBuiltin(clazz, readWith);
#ifdef QTC_WITH_CXX_FRONTEND
    if (builtin.isValid() && clazz->line() > 0 && cxxFrontendModelRequested()) {
        return GeneratedClass(
            std::make_shared<CxxClassReading>(interface.filePath(), clazz->line(),
                                              clazz->column(), builtin.reading(), readWith));
    }
#endif
    return builtin;
}

struct ExistingGetterSetterData
{
    // What was read of the class and of the member the functions are for.
    // Which front end read them is this much and no more: the writing
    // asks them questions and never looks inside.
    GeneratedClass clazz;
    GeneratedType declaredType;
    // Where the member's definition goes, which is what the insertion
    // locator needs of a declaration.
    DeclarationToDefine toDefine;
    QString getterName;
    QString setterName;
    QString resetName;
    QString signalName;
    QString bindableName;
    QString qPropertyName;
    QString memberVariableName;
    Document::Ptr doc;

    int computePossibleFlags() const;
};

// FIXME: Should be a member?
static void findExistingFunctions(ExistingGetterSetterData &existing, QStringList memberFunctionNames)
{
    const CppQuickFixSettings *settings = cppQuickFixSettingsForProject(
        ProjectExplorer::ProjectTree::currentProject());
    const QString lowerBaseName = CppQuickFixSettings::memberBaseName(existing.memberVariableName).toLower();
    const QStringList getterNames{lowerBaseName,
                                  "get_" + lowerBaseName,
                                  "get" + lowerBaseName,
                                  "is_" + lowerBaseName,
                                  "is" + lowerBaseName,
                                  settings->getGetterName(lowerBaseName, existing.memberVariableName)};
    const QStringList setterNames{"set_" + lowerBaseName,
                                  "set" + lowerBaseName,
                                  settings->getSetterName(lowerBaseName, existing.memberVariableName)};
    const QStringList resetNames{"reset_" + lowerBaseName,
                                 "reset" + lowerBaseName,
                                 settings->getResetName(lowerBaseName, existing.memberVariableName)};
    const QStringList signalNames{lowerBaseName + "_changed",
                                  lowerBaseName + "changed",
                                  settings->getSignalName(lowerBaseName, existing.memberVariableName)};
    for (const auto &memberFunctionName : memberFunctionNames) {
        const QString lowerName = memberFunctionName.toLower();
        if (getterNames.contains(lowerName))
            existing.getterName = memberFunctionName;
        else if (setterNames.contains(lowerName))
            existing.setterName = memberFunctionName;
        else if (resetNames.contains(lowerName))
            existing.resetName = memberFunctionName;
        else if (signalNames.contains(lowerName))
            existing.signalName = memberFunctionName;
    }
}

// What a Q_PROPERTY the cursor is on says. A property declares nothing --
// no symbol of it stands anywhere -- so what is read of one is the text
// it was written with and the place it was written at, and that is what
// each front end hands over.
struct WrittenProperty
{
    QString name;
    QString type; // as written: a type and not a declaration of one
    QString getterName;         // READ
    QString setterName;         // WRITE
    QString resetName;          // RESET
    QString signalName;         // NOTIFY
    QString memberVariableName; // MEMBER
    QString bindableName;       // BINDABLE

    // Where the Q_PROPERTY begins, in the text of the file.
    int startPosition = 0;

    bool isValid() const { return !type.isEmpty(); }
};

// One of its items, by the word moc reads it by.
static void readItem(WrittenProperty &property, const QString &item, const QString &value)
{
    if (item == "READ")
        property.getterName = value;
    else if (item == "WRITE")
        property.setterName = value;
    else if (item == "RESET")
        property.resetName = value;
    else if (item == "NOTIFY")
        property.signalName = value;
    else if (item == "MEMBER")
        property.memberVariableName = value;
    else if (item == "BINDABLE")
        property.bindableName = value;
}

// As the built-in front end reads it: a node of its own, which the
// cursor has to be on.
static WrittenProperty builtinWrittenProperty(const CppQuickFixInterface &interface)
{
    const QList<AST *> &path = interface.path();
    if (path.isEmpty())
        return {};
    QtPropertyDeclarationAST * const declaration = path.last()->asQtPropertyDeclaration();
    if (!declaration || !declaration->type_id)
        return {};

    const CppRefactoringFilePtr file = interface.currentFile();
    WrittenProperty written;
    written.name = file->textOf(declaration->property_name);
    written.type = file->textOf(declaration->type_id);
    written.startPosition = file->startOf(declaration);
    for (QtPropertyDeclarationItemListAST *it = declaration->property_declaration_item_list; it;
         it = it->next) {
        readItem(written,
                 QString::fromUtf8(file->tokenAt(it->value->item_name_token).spell()),
                 file->textOf(it->value->expression));
    }
    return written;
}

// And as the cxx-frontend model reads it, where it has the file.
static WrittenProperty writtenPropertyAt(const CppQuickFixInterface &interface)
{
#ifdef QTC_WITH_CXX_FRONTEND
    const QTextCursor cursor = interface.currentFile()->cursor();
    int line = 0, column = 0;
    Utils::Text::convertPosition(interface.currentFile()->document(), cursor.position(),
                                 &line, &column);
    if (const std::optional<CxxFrontendDocument::QtProperty> property
        = cxxFrontendQtPropertyAt(interface.filePath(), line, column + 1)) {
        WrittenProperty written;
        written.name = property->name;
        written.type = property->type;
        written.startPosition = interface.currentFile()->position(property->startLine,
                                                                  property->startColumn);
        for (const QPair<QString, QString> &item : property->items)
            readItem(written, item.first, item.second);
        return written;
    }
#endif
    return builtinWrittenProperty(interface);
}

class GetterSetterRefactoringHelper
{
public:
    GetterSetterRefactoringHelper(CppQuickFixOperation *operation,
                                  const GeneratedClass &clazz);
    void performGeneration(const ExistingGetterSetterData &data, int generationFlags);
    void applyChanges();
    bool hasSourceFile() const { return m_headerFile != m_sourceFile; }

protected:
    void insertAndIndent(
            const RefactoringFilePtr &file, const InsertionLocation &loc, const QString &text);

    void addHeaderCode(InsertionPointLocator::AccessSpec spec, const QString &code);
    void addSourceFileCode(const QString &code);

    InsertionLocation headerLocationFor(InsertionPointLocator::AccessSpec spec);
    InsertionLocation sourceLocationFor(const DeclarationToDefine &declaration,
                                        QStringList *insertedNamespaces = nullptr);

protected:
    CppQuickFixOperation *const m_operation;
    const CppRefactoringChanges m_changes;
    const InsertionPointLocator m_locator;
    const CppRefactoringFilePtr m_headerFile;
    bool m_isHeaderHeaderFile = false; // the "header" (where the class is defined) can be a source file
    const CppRefactoringFilePtr m_sourceFile = determineSourceFile();
    CppQuickFixSettings *const m_settings = cppQuickFixSettingsForProject(
        ProjectTree::currentProject());
    const GeneratedClass m_class;

private:
    class Data {
    public:
        Data(GetterSetterRefactoringHelper *q) : q(q) {}
        void setup(const ExistingGetterSetterData &data, int generateFlags);

        const QString &getterName() const { return m_data.getterName; }
        const QString &setterName() const { return m_data.setterName; }
        const QString &signalName() const { return m_data.signalName; }
        const QString &resetName() const { return m_data.resetName; }
        const QString &bindableName() const { return m_data.bindableName; }
        const QString &qPropertyName() const { return m_data.qPropertyName; }
        const QString &memberVarName() const { return m_data.memberVariableName; }
        const QString &parameterName() const { return m_parameterName; }
        const CppQuickFixSettings::GetterSetterTemplate &getSetTemplate() const;
        const GeneratedType &returnTypeHeader() const;
        const GeneratedType &returnTypeClass() const;
        const GeneratedType &returnTypeTemplateParameter() const;
        InsertionPointLocator::AccessSpec setterAccessSpec() const;
        bool isValueType() const;

        bool generateGetter() const { return m_generateFlags & GenerateFlag::GenerateGetter; }
        bool generateSetter() const { return m_generateFlags & GenerateFlag::GenerateSetter; }
        bool generateReset() const { return m_generateFlags & GenerateFlag::GenerateReset; }
        bool generateSignal() const { return m_generateFlags & GenerateFlag::GenerateSignal; }
        bool generateBindable() const { return m_generateFlags & GenerateFlag::GenerateBindable; }
        bool generateMemberVar() const;
        bool generateConstQProperty() const;
        bool generateQProperty() const;

        const GeneratedClass &theClass() const { return q->m_class; }
        bool isStatic() const { return m_data.declaredType.isStatic(); }

        // As the member was declared, const and static and all, which two
        // of the definitions written outside the class are made from.
        const GeneratedType &declaredType() const { return m_data.declaredType; }
        // Where its definition goes, which is all the insertion locator
        // needs of a declaration.
        const DeclarationToDefine &toDefine() const { return m_data.toDefine; }
        GeneratedType memberVarType() const;
        GeneratedType parameterType() const;
    private:
        enum class HeaderContext { InsideClass, OutsideClass };
        GeneratedType getReturnTypeHeader(HeaderContext headerContext) const;

        GetterSetterRefactoringHelper *q;
        QString m_parameterName;
        mutable std::optional<CppQuickFixSettings::GetterSetterTemplate> m_getSetTemplate;
        mutable std::optional<GeneratedType> m_returnTypeHeader;
        mutable std::optional<GeneratedType> m_returnTypeClass;
        mutable std::optional<GeneratedType> m_returnTypeTemplateParameter;
        mutable std::optional<InsertionPointLocator::AccessSpec> m_setterAccessSpec;
        mutable std::optional<bool> m_isValueType;
        ExistingGetterSetterData m_data;
        int m_generateFlags = 0;
    };

    void generateGetter();
    void generateSetter();
    void generateReset();
    void generateSignal();
    void generateMemberVariable();
    void generateQProperty();
    void generateBindable();
    QString setterBodyWithSignal();
    CppRefactoringFilePtr determineSourceFile();

    ChangeSet m_headerFileChangeSet;
    ChangeSet m_sourceFileChangeSet;
    QMap<InsertionPointLocator::AccessSpec, InsertionLocation> m_headerInsertionPoints;
    InsertionLocation m_sourceFileInsertionPoint;
    QString m_sourceFileCode;
    QMap<InsertionPointLocator::AccessSpec, QString> m_headerFileCode;

    Data m_data{this};
};

struct ParentClassConstructorInfo;

class ConstructorMemberInfo
{
public:
    ConstructorMemberInfo(const QString &name, Symbol *symbol, int numberOfMember)
        : memberVariableName(name)
        , parameterName(CppQuickFixSettings::memberBaseName(name))
        , symbol(symbol)
        , numberOfMember(numberOfMember)
    {}
    ConstructorMemberInfo(const QString &memberName,
                          const QString &paramName,
                          const QString &defaultValue,
                          Symbol *symbol,
                          const ParentClassConstructorInfo *parentClassConstructor)
        : parentClassConstructor(parentClassConstructor)
        , memberVariableName(memberName)
        , parameterName(paramName)
        , defaultValue(defaultValue)
        , init(defaultValue.isEmpty())
        , symbol(symbol)
    {}
    const ParentClassConstructorInfo *parentClassConstructor = nullptr;
    QString memberVariableName;
    QString parameterName;
    QString defaultValue;
    bool init = true;
    bool customValueType; // for the generation later
    Symbol *symbol; // for the right type later
    int numberOfMember; // first member, second member, ...
};

class ConstructorParams : public QAbstractTableModel
{
    Q_OBJECT
    std::list<ConstructorMemberInfo> candidates;
    std::vector<ConstructorMemberInfo *> infos;

    void validateOrder()
    {
        // parameters with default values must be at the end
        bool foundWithDefault = false;
        for (auto info : infos) {
            if (info->init) {
                if (foundWithDefault && info->defaultValue.isEmpty()) {
                    emit validOrder(false);
                    return;
                }
                foundWithDefault |= !info->defaultValue.isEmpty();
            }
        }
        emit validOrder(true);
    }

public:
    enum Column { ShouldInitColumn, MemberNameColumn, ParameterNameColumn, DefaultValueColumn };
    template<typename... _Args>
    void emplaceBackParameter(_Args &&...__args)
    {
        candidates.emplace_back(std::forward<_Args>(__args)...);
        infos.push_back(&candidates.back());
    }
    const std::vector<ConstructorMemberInfo *> &getInfos() const { return infos; }
    void addRow(ConstructorMemberInfo *info)
    {
        beginInsertRows({}, rowCount(), rowCount());
        infos.push_back(info);
        endInsertRows();
        validateOrder();
    }
    void removeRow(ConstructorMemberInfo *info)
    {
        for (auto iter = infos.begin(); iter != infos.end(); ++iter) {
            if (*iter == info) {
                const auto index = iter - infos.begin();
                beginRemoveRows({}, index, index);
                infos.erase(iter);
                endRemoveRows();
                validateOrder();
                return;
            }
        }
    }

    int selectedCount() const
    {
        return Utils::count(infos, [](const ConstructorMemberInfo *mi) {
            return mi->init && !mi->parentClassConstructor;
        });
    }
    int memberCount() const
    {
        return Utils::count(infos, [](const ConstructorMemberInfo *mi) {
            return !mi->parentClassConstructor;
        });
    }

    int rowCount(const QModelIndex & /*parent*/ = {}) const override { return int(infos.size()); }
    int columnCount(const QModelIndex & /*parent*/ = {}) const override { return 4; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (index.row() < 0 || index.row() >= rowCount())
            return {};
        if (role == Qt::CheckStateRole && index.column() == ShouldInitColumn
            && !infos[index.row()]->parentClassConstructor)
            return infos[index.row()]->init ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::DisplayRole && index.column() == MemberNameColumn)
            return infos[index.row()]->memberVariableName;
        if ((role == Qt::DisplayRole || role == Qt::EditRole)
            && index.column() == ParameterNameColumn)
            return infos[index.row()]->parameterName;
        if ((role == Qt::DisplayRole || role == Qt::EditRole)
            && index.column() == DefaultValueColumn)
            return infos[index.row()]->defaultValue;
        if ((role == Qt::ToolTipRole) && index.column() > 0)
            return Overview{}.prettyType(infos[index.row()]->symbol->type());
        return {};
    }
    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (index.column() == ShouldInitColumn && role == Qt::CheckStateRole) {
            if (infos[index.row()]->parentClassConstructor)
                return false;
            infos[index.row()]->init = value.toInt() == Qt::Checked;
            emit dataChanged(this->index(index.row(), 0), this->index(index.row(), columnCount()));
            validateOrder();
            return true;
        }
        if (index.column() == ParameterNameColumn && role == Qt::EditRole) {
            infos[index.row()]->parameterName = value.toString();
            return true;
        }
        if (index.column() == DefaultValueColumn && role == Qt::EditRole) {
            infos[index.row()]->defaultValue = value.toString();
            validateOrder();
            return true;
        }
        return false;
    }
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;

        Qt::ItemFlags f{};
        if (infos[index.row()]->init) {
            f |= Qt::ItemIsDragEnabled;
            f |= Qt::ItemIsSelectable;
        }

        if (index.column() == ShouldInitColumn && !infos[index.row()]->parentClassConstructor)
            return f | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
        if (!infos[index.row()]->init)
            return f;
        if (index.column() == MemberNameColumn)
            return f | Qt::ItemIsEnabled;
        if (index.column() == ParameterNameColumn || index.column() == DefaultValueColumn)
            return f | Qt::ItemIsEnabled | Qt::ItemIsEditable;
        return {};
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            switch (section) {
            case ShouldInitColumn:
                return Tr::tr("Initialize in Constructor");
            case MemberNameColumn:
                return Tr::tr("Member Name");
            case ParameterNameColumn:
                return Tr::tr("Parameter Name");
            case DefaultValueColumn:
                return Tr::tr("Default Value");
            }
        }
        return {};
    }
    bool dropMimeData(const QMimeData *data,
                      Qt::DropAction /*action*/,
                      int row,
                      int /*column*/,
                      const QModelIndex & /*parent*/) override
    {
        if (row == -1)
            row = rowCount();
        bool ok;
        int sourceRow = data->data("application/x-qabstractitemmodeldatalist").toInt(&ok);
        if (ok) {
            if (sourceRow == row || row == sourceRow + 1)
                return false;
            beginMoveRows({}, sourceRow, sourceRow, {}, row);
            infos.insert(infos.begin() + row, infos.at(sourceRow));
            if (row < sourceRow)
                ++sourceRow;
            infos.erase(infos.begin() + sourceRow);
            endMoveRows();
            validateOrder();
            return true;
        }
        return false;
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        for (const auto &i : indexes) {
            if (!i.isValid())
                continue;
            auto data = new QMimeData();
            data->setData("application/x-qabstractitemmodeldatalist",
                          QString::number(i.row()).toLatin1());
            return data;
        }
        return nullptr;
    }

    class TableViewStyle : public QProxyStyle
    {
    public:
        TableViewStyle(QStyle *style)
            : QProxyStyle(style)
        {}

        void drawPrimitive(PrimitiveElement element,
                           const QStyleOption *option,
                           QPainter *painter,
                           const QWidget *widget) const override
        {
            if (element == QStyle::PE_IndicatorItemViewItemDrop && !option->rect.isNull()) {
                QStyleOption opt(*option);
                opt.rect.setLeft(0);
                if (widget)
                    opt.rect.setRight(widget->width());
                QProxyStyle::drawPrimitive(element, &opt, painter, widget);
                return;
            }
            QProxyStyle::drawPrimitive(element, option, painter, widget);
        }
    };
signals:
    void validOrder(bool valid);
};

class TopMarginDelegate : public QStyledItemDelegate
{
public:
    TopMarginDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        Q_ASSERT(index.isValid());
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        if (opt.rect.height() > 20)
            opt.rect.adjust(0, 5, 0, 0);
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
    }
};

struct ParentClassConstructorParameter : public ConstructorMemberInfo
{
    QString originalDefaultValue;
    QString declaration; // displayed in the treeView
    ParentClassConstructorParameter(const QString &name,
                                    const QString &defaultValue,
                                    Symbol *symbol,
                                    const ParentClassConstructorInfo *parentClassConstructor);

    ParentClassConstructorParameter(const ParentClassConstructorParameter &) = delete;
    ParentClassConstructorParameter(ParentClassConstructorParameter &&) = default;
};

struct ParentClassConstructorInfo
{
    ParentClassConstructorInfo(const QString &name, ConstructorParams &model)
        : className(name)
        , model(model)
    {}
    bool useInConstructor = false;
    const QString className;
    QString declaration;
    std::vector<ParentClassConstructorParameter> parameters;
    ConstructorParams &model;

    ParentClassConstructorInfo(const ParentClassConstructorInfo &) = delete;
    ParentClassConstructorInfo(ParentClassConstructorInfo &&) = default;

    void addParameter(ParentClassConstructorParameter &param) { model.addRow(&param); }
    void removeParameter(ParentClassConstructorParameter &param) { model.removeRow(&param); }
    void removeAllParameters()
    {
        for (auto &param : parameters)
            model.removeRow(&param);
    }
};

ParentClassConstructorParameter::ParentClassConstructorParameter(
    const QString &name,
    const QString &defaultValue,
    Symbol *symbol,
    const ParentClassConstructorInfo *parentClassConstructor)
    : ConstructorMemberInfo(parentClassConstructor->className + "::" + name,
                            name,
                            defaultValue,
                            symbol,
                            parentClassConstructor)
    , originalDefaultValue(defaultValue)
    , declaration(Overview{}.prettyType(symbol->type(), name)
                  + (defaultValue.isEmpty() ? QString{} : " = " + defaultValue))
{}

using ParentClassConstructors = std::vector<ParentClassConstructorInfo>;

class ParentClassesModel : public QAbstractItemModel
{
    ParentClassConstructors &constructors;

public:
    ParentClassesModel(QObject *parent, ParentClassConstructors &constructors)
        : QAbstractItemModel(parent)
        , constructors(constructors)
    {}
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override
    {
        if (!parent.isValid())
            return createIndex(row, column, nullptr);
        if (parent.internalPointer())
            return {};
        auto index = createIndex(row, column, &constructors.at(parent.row()));
        return index;
    }
    QModelIndex parent(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return {};
        auto *parent = static_cast<ParentClassConstructorInfo *>(index.internalPointer());
        if (!parent)
            return {};
        int i = 0;
        for (const auto &info : constructors) {
            if (&info == parent)
                return createIndex(i, 0, nullptr);
            ++i;
        }
        return {};
    }
    int rowCount(const QModelIndex &parent = {}) const override
    {
        if (!parent.isValid())
            return static_cast<int>(constructors.size());
        auto info = static_cast<ParentClassConstructorInfo *>(parent.internalPointer());
        if (!info)
            return static_cast<int>(constructors.at(parent.row()).parameters.size());
        return 0;
    }
    int columnCount(const QModelIndex & /*parent*/ = {}) const override { return 1; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return {};
        auto info = static_cast<ParentClassConstructorInfo *>(index.internalPointer());

        if (info) {
            const auto &parameter = info->parameters.at(index.row());
            if (role == Qt::CheckStateRole)
                return parameter.init ? Qt::Checked : Qt::Unchecked;
            if (role == Qt::DisplayRole)
                return parameter.declaration;
            return {};
        }
        const auto &constructor = constructors.at(index.row());
        if (role == Qt::CheckStateRole)
            return constructor.useInConstructor ? Qt::PartiallyChecked : Qt::Unchecked;
        if (role == Qt::DisplayRole)
            return constructor.declaration;

        // Highlight the selected items
        if (role == Qt::FontRole && constructor.useInConstructor) {
            QFont font = QApplication::font();
            font.setBold(true);
            return font;
        }
        // Create a margin between sets of constructors for base classes
        if (role == Qt::SizeHintRole && index.row() > 0
            && constructor.className != constructors.at(index.row() - 1).className) {
            return QSize(-1, 25);
        }
        return {};
    }
    bool setData(const QModelIndex &index, const QVariant &value, int /*role*/) override
    {
        if (index.isValid() && index.column() == 0) {
            auto info = static_cast<ParentClassConstructorInfo *>(index.internalPointer());
            if (info) {
                const bool nowUse = value.toBool();
                auto &param = info->parameters.at(index.row());
                param.init = nowUse;
                if (nowUse)
                    info->addParameter(param);
                else
                    info->removeParameter(param);
                return true;
            }
            auto &newConstructor = constructors.at(index.row());
            // You have to select a base class constructor
            if (newConstructor.useInConstructor)
                return false;
            auto c = std::find_if(constructors.begin(), constructors.end(), [&](const auto &c) {
                return c.className == newConstructor.className && c.useInConstructor;
            });
            QTC_ASSERT(c == constructors.end(), return false;);
            c->useInConstructor = false;
            newConstructor.useInConstructor = true;
            emit dataChanged(this->index(index.row(), 0), this->index(index.row(), columnCount()));
            auto parentIndex = this->index(index.row(), 0);
            emit dataChanged(this->index(0, 0, parentIndex),
                             this->index(rowCount(parentIndex), columnCount()));
            const int oldIndex = c - constructors.begin();
            emit dataChanged(this->index(oldIndex, 0), this->index(oldIndex, columnCount()));
            parentIndex = this->index(oldIndex, 0);
            emit dataChanged(this->index(0, 0, parentIndex),
                             this->index(rowCount(parentIndex), columnCount()));
            // update other table
            c->removeAllParameters();
            for (auto &p : newConstructor.parameters)
                if (p.init)
                    newConstructor.addParameter(p);
            return true;
        }
        return false;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            switch (section) {
            case 0:
                return Tr::tr("Base Class Constructors");
            }
        }
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (index.isValid()) {
            Qt::ItemFlags f;
            auto info = static_cast<ParentClassConstructorInfo *>(index.internalPointer());
            if (!info || info->useInConstructor) {
                f |= Qt::ItemIsEnabled;
            }
            f |= Qt::ItemIsUserCheckable;

            return f;
        }
        return {};
    }
};

class GenerateConstructorDialog : public QDialog
{
public:
    GenerateConstructorDialog(ConstructorParams *constructorParamsModel,
                              ParentClassConstructors &constructors)
    {
        setWindowTitle(Tr::tr("Constructor"));

        const auto treeModel = new ParentClassesModel(this, constructors);
        const auto treeView = new QTreeView(this);
        treeView->setModel(treeModel);
        treeView->setItemDelegate(new TopMarginDelegate(this));
        treeView->expandAll();

        const auto view = new QTableView(this);
        view->setModel(constructorParamsModel);
        int optimalWidth = 0;
        for (int i = 0; i < constructorParamsModel->columnCount(QModelIndex{}); ++i) {
            view->resizeColumnToContents(i);
            optimalWidth += view->columnWidth(i);
        }
        view->resizeRowsToContents();
        view->verticalHeader()->setDefaultSectionSize(view->rowHeight(0));
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->setDragEnabled(true);
        view->setDropIndicatorShown(true);
        view->setDefaultDropAction(Qt::MoveAction);
        view->setDragDropMode(QAbstractItemView::InternalMove);
        view->setDragDropOverwriteMode(false);
        view->horizontalHeader()->setStretchLastSection(true);
        view->setStyle(new ConstructorParams::TableViewStyle(view->style()));

        const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

        const auto errorLabel = new QLabel(
            Tr::tr("Parameters without default value must come before parameters with default value."));
        errorLabel->setStyleSheet("color: #ff0000");
        errorLabel->setVisible(false);
        QSizePolicy labelSizePolicy = errorLabel->sizePolicy();
        labelSizePolicy.setRetainSizeWhenHidden(true);
        errorLabel->setSizePolicy(labelSizePolicy);
        connect(constructorParamsModel, &ConstructorParams::validOrder, this,
                [errorLabel, button = buttonBox->button(QDialogButtonBox::Ok)](bool valid) {
                    button->setEnabled(valid);
                    errorLabel->setVisible(!valid);
                });

        // setup select all/none checkbox
        QCheckBox *const checkBox = new QCheckBox(Tr::tr("Initialize all members"));
        checkBox->setChecked(true);
        connect(checkBox, &QCheckBox::stateChanged, this,
                [model = constructorParamsModel](int state) {
                    if (state != Qt::PartiallyChecked) {
                        for (int i = 0; i < model->rowCount(); ++i)
                            model->setData(model->index(i, ConstructorParams::ShouldInitColumn),
                                           state,
                                           Qt::CheckStateRole);
                    }
                });
        connect(checkBox, &QCheckBox::clicked, this, [checkBox] {
            if (checkBox->checkState() == Qt::PartiallyChecked)
                checkBox->setCheckState(Qt::Checked);
        });
        connect(constructorParamsModel,
                &QAbstractItemModel::dataChanged,
                this,
                [model = constructorParamsModel, checkBox] {
                    const auto state = [model, selectedCount = model->selectedCount()]() {
                        if (selectedCount == 0)
                            return Qt::Unchecked;
                        if (static_cast<int>(model->memberCount() == selectedCount))
                            return Qt::Checked;
                        return Qt::PartiallyChecked;
                    }();
                    checkBox->setCheckState(state);
                });

        using A = InsertionPointLocator::AccessSpec;
        auto accessCombo = new QComboBox;
        connect(accessCombo, &QComboBox::currentIndexChanged, this, [this, accessCombo] {
            const auto data = accessCombo->currentData();
            m_accessSpec = static_cast<A>(data.toInt());
        });
        for (auto a : {A::Public, A::Protected, A::Private})
            accessCombo->addItem(InsertionPointLocator::accessSpecToString(a), a);
        const auto row = new QHBoxLayout();
        row->addWidget(new QLabel(Tr::tr("Access") + ":"));
        row->addWidget(accessCombo);
        row->addSpacerItem(new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Minimum));

        const auto mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(
            new QLabel(Tr::tr("Select the members to be initialized in the constructor.\n"
                              "Use drag and drop to change the order of the parameters.")));
        mainLayout->addLayout(row);
        mainLayout->addWidget(checkBox);
        mainLayout->addWidget(view);
        mainLayout->addWidget(treeView);
        mainLayout->addWidget(errorLabel);
        mainLayout->addWidget(buttonBox);
        int left, right;
        mainLayout->getContentsMargins(&left, nullptr, &right, nullptr);
        optimalWidth += left + right;
        resize(optimalWidth, mainLayout->sizeHint().height());
    }

    InsertionPointLocator::AccessSpec accessSpec() const { return m_accessSpec; }

private:
    InsertionPointLocator::AccessSpec m_accessSpec;
};

class MemberInfo
{
public:
    MemberInfo(ExistingGetterSetterData data, int possibleFlags)
        : data(data)
        , possibleFlags(possibleFlags)
    {}

    ExistingGetterSetterData data;
    int possibleFlags;
    int requestedFlags = 0;
};
using GetterSetterCandidates = std::vector<MemberInfo>;

class GenerateConstructorOperation : public CppQuickFixOperation
{
public:
    GenerateConstructorOperation(const CppQuickFixInterface &interface)
        : CppQuickFixOperation(interface)
    {
        setDescription(Tr::tr("Generate Constructor"));

        m_classAST = astForClassOperations(interface);
        if (!m_classAST)
            return;
        Class *const theClass = m_classAST->symbol;
        if (!theClass)
            return;

        // Go through all members and find member variable declarations
        int memberCounter = 0;
        for (auto it = theClass->memberBegin(); it != theClass->memberEnd(); ++it) {
            Symbol *const s = *it;
            if (!s->identifier() || !s->type() || s->type().isTypedef())
                continue;
            if ((s->asDeclaration() && s->type()->asFunctionType()) || s->asFunction())
                continue;
            if (s->asDeclaration() && (s->isPrivate() || s->isProtected()) && !s->isStatic()) {
                const auto name = QString::fromUtf8(s->identifier()->chars(),
                                                    s->identifier()->size());
                parameterModel.emplaceBackParameter(name, s, memberCounter++);
            }
        }
        Overview o = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        o.showArgumentNames = true;
        o.showReturnTypes = true;
        o.showDefaultArguments = true;
        o.showTemplateParameters = true;
        o.showFunctionSignatures = true;
        LookupContext context(currentFile()->cppDocument(), interface.snapshot());
        for (BaseClass *bc : theClass->baseClasses()) {
            const QString className = o.prettyName(bc->name());

            ClassOrNamespace *localLookupType = context.lookupType(bc);
            QList<LookupItem> localLookup = localLookupType->lookup(bc->name());
            for (auto &li : localLookup) {
                Symbol *d = li.declaration();
                if (!d->asClass())
                    continue;
                for (auto i = d->asClass()->memberBegin(); i != d->asClass()->memberEnd(); ++i) {
                    Symbol *s = *i;
                    if (s->isProtected() || s->isPublic()) {
                        if (s->name()->match(d->name())) {
                            // we have found a constructor
                            Function *func = s->type().type()->asFunctionType();
                            if (!func)
                                continue;
                            const bool isFirst = parentClassConstructors.empty()
                                                 || parentClassConstructors.back().className
                                                        != className;
                            parentClassConstructors.emplace_back(className, parameterModel);
                            ParentClassConstructorInfo &constructor = parentClassConstructors.back();
                            constructor.declaration = className + o.prettyType(func->type());
                            constructor.declaration.replace("std::__1::__get_nullptr_t()",
                                                            "nullptr");
                            constructor.useInConstructor = isFirst;
                            for (auto arg = func->memberBegin(); arg != func->memberEnd(); ++arg) {
                                Symbol *param = *arg;
                                Argument *argument = param->asArgument();
                                if (!argument) // can also be a block
                                    continue;
                                const QString name = o.prettyName(param->name());
                                const StringLiteral *ini = argument->initializer();
                                QString defaultValue;
                                if (ini)
                                    defaultValue = QString::fromUtf8(ini->chars(), ini->size())
                                                       .replace("std::__1::__get_nullptr_t()",
                                                                "nullptr");
                                constructor.parameters.emplace_back(name,
                                                                    defaultValue,
                                                                    param,
                                                                    &constructor);
                                // do not show constructors like QObject(QObjectPrivate & dd, ...)
                                ReferenceType *ref = param->type()->asReferenceType();
                                if (ref && name == "dd") {
                                    auto type = o.prettyType(ref->elementType());
                                    if (type.startsWith("Q") && type.endsWith("Private")) {
                                        parentClassConstructors.pop_back();
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // add params to parameter lists
        for (auto &c : parentClassConstructors)
            if (c.useInConstructor)
                for (auto &p : c.parameters)
                    if (p.init)
                        c.addParameter(p);
    }

    bool isApplicable() const
    {
        return parameterModel.rowCount() > 0
               || Utils::anyOf(parentClassConstructors,
                               [](const auto &parent) { return !parent.parameters.empty(); });
    }

    void setTest(bool isTest = true) { m_test = isTest; }

private:
    void perform() override
    {
        auto infos = parameterModel.getInfos();

        InsertionPointLocator::AccessSpec accessSpec = InsertionPointLocator::Public;
        if (!m_test) {
            GenerateConstructorDialog dlg(&parameterModel, parentClassConstructors);
            if (dlg.exec() == QDialog::Rejected)
                return;
            accessSpec = dlg.accessSpec();
            infos = parameterModel.getInfos();
        } else {
#ifdef WITH_TESTS
            ParentClassesModel model(nullptr, parentClassConstructors);
            QAbstractItemModelTester tester(&model);
#endif
            if (infos.size() >= 3) {
                // if we are testing and have 3 or more members => change the order
                // move first element to the back
                infos.push_back(infos[0]);
                infos.erase(infos.begin());
            }
            for (auto info : infos) {
                if (info->memberVariableName.startsWith("di_"))
                    info->defaultValue = "42";
            }
            for (auto &c : parentClassConstructors) {
                if (c.useInConstructor) {
                    for (auto &p : c.parameters) {
                        if (!p.init && p.parameterName.startsWith("use_")) {
                            infos.push_back(&p);
                            p.init = true;
                        }
                    }
                }
            }
        }
        if (infos.empty())
            return;
        struct GenerateConstructorRefactoringHelper : public GetterSetterRefactoringHelper
        {
            const ClassSpecifierAST *m_classAST;
            InsertionPointLocator::AccessSpec m_accessSpec;
            GenerateConstructorRefactoringHelper(CppQuickFixOperation *operation,
                                                 const GeneratedClass &clazz,
                                                 const ClassSpecifierAST *classAST,
                                                 InsertionPointLocator::AccessSpec accessSpec)
                : GetterSetterRefactoringHelper(operation, clazz)
                , m_classAST(classAST)
                , m_accessSpec(accessSpec)
            {}
            void generateConstructor(std::vector<ConstructorMemberInfo *> members,
                                     const ParentClassConstructors &parentClassConstructors)
            {
                auto constructorLocation = m_settings->determineSetterLocation(int(members.size()));
                if (constructorLocation == CppQuickFixSettings::FunctionLocation::CppFile
                    && !hasSourceFile())
                    constructorLocation = CppQuickFixSettings::FunctionLocation::OutsideClass;

                Overview overview = CppCodeStyleSettings::currentProjectCodeStyleOverview();
                overview.showTemplateParameters = true;

                InsertionLocation implLoc;
                QString implCode;
                CppRefactoringFilePtr implFile;
                QString className = m_class.name();
                QStringList insertedNamespaces;
                if (constructorLocation == CppQuickFixSettings::FunctionLocation::CppFile) {
                    implLoc = sourceLocationFor(m_class.toDefine(m_changes), &insertedNamespaces);
                    implFile = m_sourceFile;
                    if (m_settings->rewriteTypesinCppFile())
                        implCode = m_class.writtenAt(m_sourceFile, implLoc);
                    else
                        implCode = className;
                    implCode += "::" + className + "(";
                } else if (constructorLocation
                           == CppQuickFixSettings::FunctionLocation::OutsideClass) {
                    implLoc = insertLocationForMethodDefinition(m_class.toDefine(m_changes),
                                                                false,
                                                                NamespaceHandling::Ignore,
                                                                m_changes,
                                                                m_headerFile->filePath(),
                                                                &insertedNamespaces);
                    implFile = m_headerFile;
                    implCode = m_class.writtenAt(m_headerFile, implLoc);
                    implCode += "::" + className + "(";
                }

                // Where it is going in the class, since a type written
                // there is written as the class sees it and a parameter
                // taken from a class above was not written there.
                const InsertionLocation inClassLocation
                    = m_locator.constructorDeclarationInClass(
                        m_headerFile->cppDocument()->translationUnit(),
                        m_classAST,
                        m_accessSpec,
                        int(members.size()));

                QString inClassDeclaration = m_class.name() + "(";
                QString constructorBody = members.empty() ? QString(") {}") : QString(") : ");
                for (auto &member : members) {
                    GeneratedType type = GeneratedType::readBuiltin(
                        member->symbol->type(),
                        member->symbol->enclosingScope(),
                        m_class.readWith());
                    type = type.isValueType(&member->customValueType)
                               ? type.withoutConst()
                               : type.constReference();

                    inClassDeclaration += type.writtenAt(m_headerFile, inClassLocation)
                                              .asDeclarationOf(member->parameterName);
                    if (!member->defaultValue.isEmpty())
                        inClassDeclaration += " = " + member->defaultValue;
                    inClassDeclaration += ", ";
                    if (implFile) {
                        implCode += type.writtenAt(implFile, implLoc, insertedNamespaces)
                                        .asDeclarationOf(member->parameterName)
                                    + ", ";
                    }
                }
                Utils::sort(members, &ConstructorMemberInfo::numberOfMember);
                // first, do the base classes
                for (const auto &parent : parentClassConstructors) {
                    if (!parent.useInConstructor)
                        continue;
                    // Check if we really need a constructor
                    if (Utils::anyOf(parent.parameters, [](const auto &param) {
                            return param.init || param.originalDefaultValue.isEmpty();
                        })) {
                        int defaultAtEndCount = 0;
                        for (auto i = parent.parameters.crbegin(); i != parent.parameters.crend();
                             ++i) {
                            if (i->init || i->originalDefaultValue.isEmpty())
                                break;
                            ++defaultAtEndCount;
                        }
                        const int numberOfParameters = static_cast<int>(parent.parameters.size())
                                                       - defaultAtEndCount;
                        constructorBody += parent.className + "(";
                        int counter = 0;
                        for (const auto &param : parent.parameters) {
                            if (++counter > numberOfParameters)
                                break;
                            if (param.init) {
                                if (param.customValueType)
                                    constructorBody += "std::move(" + param.parameterName + ')';
                                else
                                    constructorBody += param.parameterName;
                            } else if (!param.originalDefaultValue.isEmpty())
                                constructorBody += param.originalDefaultValue;
                            else
                                constructorBody += "/* insert value */";
                            constructorBody += ", ";
                        }
                        constructorBody.resize(constructorBody.size() - 2);
                        constructorBody += "),\n";
                    }
                }
                for (auto &member : members) {
                    if (member->parentClassConstructor)
                        continue;
                    QString param = member->parameterName;
                    if (member->customValueType)
                        param = "std::move(" + member->parameterName + ')';
                    constructorBody += member->memberVariableName + '(' + param + "),\n";
                }
                if (!members.empty()) {
                    inClassDeclaration.resize(inClassDeclaration.size() - 2);
                    constructorBody.remove(constructorBody.size() - 2, 1); // ..),\n => ..)\n
                    constructorBody += "{}";
                    if (!implCode.isEmpty())
                        implCode.resize(implCode.size() - 2);
                }
                implCode += constructorBody;

                if (constructorLocation == CppQuickFixSettings::FunctionLocation::InsideClass)
                    inClassDeclaration += constructorBody;
                else
                    inClassDeclaration += QLatin1String(");");

                insertAndIndent(m_headerFile, inClassLocation, inClassDeclaration);

                if (constructorLocation == CppQuickFixSettings::FunctionLocation::CppFile) {
                    addSourceFileCode(implCode);
                } else if (constructorLocation
                           == CppQuickFixSettings::FunctionLocation::OutsideClass) {
                    if (m_isHeaderHeaderFile)
                        implCode.prepend("inline ");
                    insertAndIndent(m_headerFile, implLoc, implCode);
                }
            }
        };
        GenerateConstructorRefactoringHelper helper(
            this,
            readTheClass(*this, std::make_shared<CppQuickFixInterface>(*this),
                         m_classAST->symbol),
            m_classAST,
            accessSpec);

        auto members = Utils::filtered(infos, [](const auto mi) {
            return mi->init || mi->parentClassConstructor;
        });
        helper.generateConstructor(std::move(members), parentClassConstructors);
        helper.applyChanges();
    }

    ConstructorParams parameterModel;
    ParentClassConstructors parentClassConstructors;
    const ClassSpecifierAST *m_classAST = nullptr;
    bool m_test = false;
};

class GenerateGetterSetterOp : public CppQuickFixOperation
{
public:
    GenerateGetterSetterOp(const CppQuickFixInterface &interface,
                           ExistingGetterSetterData data,
                           int generateFlags,
                           int priority,
                           const QString &description)
        : CppQuickFixOperation(interface)
        , m_generateFlags(generateFlags)
        , m_data(data)
    {
        setDescription(description);
        setPriority(priority);
    }

    static void generateQuickFixes(QuickFixOperations &results,
                                   const CppQuickFixInterface &interface,
                                   const ExistingGetterSetterData &data,
                                   const int possibleFlags)
    {
        // flags can have the value HaveExistingQProperty or a combination of all other values
        // of the enum 'GenerateFlag'
        int p = 0;
        if (possibleFlags & HaveExistingQProperty) {
            const QString desc = Tr::tr("Generate Missing Q_PROPERTY Members");
            results << new GenerateGetterSetterOp(interface, data, possibleFlags, ++p, desc);
        } else {
            if (possibleFlags & GenerateSetter) {
                const QString desc = Tr::tr("Generate Setter");
                results << new GenerateGetterSetterOp(interface, data, GenerateSetter, ++p, desc);
            }
            if (possibleFlags & GenerateGetter) {
                const QString desc = Tr::tr("Generate Getter");
                results << new GenerateGetterSetterOp(interface, data, GenerateGetter, ++p, desc);
            }
            if (possibleFlags & GenerateGetter && possibleFlags & GenerateSetter) {
                const QString desc = Tr::tr("Generate Getter and Setter");
                const int flags = GenerateGetter | GenerateSetter;
                results << new GenerateGetterSetterOp(interface, data, flags, ++p, desc);
            }

            if (possibleFlags & GenerateConstantProperty) {
                const QString desc = Tr::tr("Generate Constant Q_PROPERTY and Missing Members");
                const int flags = possibleFlags & ~(GenerateSetter | GenerateSignal | GenerateReset);
                results << new GenerateGetterSetterOp(interface, data, flags, ++p, desc);
            }
            if (possibleFlags & GenerateProperty) {
                if (possibleFlags & GenerateReset) {
                    const QString desc = Tr::tr(
                        "Generate Q_PROPERTY and Missing Members with Reset Function");
                    const int flags = possibleFlags & ~GenerateConstantProperty;
                    results << new GenerateGetterSetterOp(interface, data, flags, ++p, desc);
                }
                const QString desc = Tr::tr("Generate Q_PROPERTY and Missing Members");
                const int flags = possibleFlags & ~GenerateConstantProperty & ~GenerateReset;
                results << new GenerateGetterSetterOp(interface, data, flags, ++p, desc);
            }
        }
    }

    void perform() override
    {
        GetterSetterRefactoringHelper helper(this, m_data.clazz);
        helper.performGeneration(m_data, m_generateFlags);
        helper.applyChanges();
    }

private:
    int m_generateFlags;
    ExistingGetterSetterData m_data;
};

class CandidateTreeItem : public TreeItem
{
public:
    enum Column {
        NameColumn,
        GetterColumn,
        SetterColumn,
        SignalColumn,
        ResetColumn,
        QPropertyColumn,
        ConstantQPropertyColumn
    };
    constexpr static GenerateFlag ColumnFlag[] = {
        GenerateFlag::Invalid,
        GenerateFlag::GenerateGetter,
        GenerateFlag::GenerateSetter,
        GenerateFlag::GenerateSignal,
        GenerateFlag::GenerateReset,
        GenerateFlag::GenerateProperty,
        GenerateFlag::GenerateConstantProperty,
    };

    CandidateTreeItem(MemberInfo *memberInfo)
        : m_memberInfo(memberInfo)
    {}

private:
    QVariant data(int column, int role) const override
    {
        if (role == Qt::DisplayRole && column == NameColumn)
            return m_memberInfo->data.memberVariableName;
        if (role == Qt::CheckStateRole && column > 0
            && column <= static_cast<int>(std::size(ColumnFlag))) {
            return m_memberInfo->requestedFlags & ColumnFlag[column] ? Qt::Checked : Qt::Unchecked;
        }
        return {};
    }

    bool setData(int column, const QVariant &data, int role) override
    {
        if (column < 1 || column > static_cast<int>(std::size(ColumnFlag)))
            return false;
        if (role != Qt::CheckStateRole)
            return false;
        if (!(m_memberInfo->possibleFlags & ColumnFlag[column]))
            return false;
        const bool nowChecked = data.toInt() == Qt::Checked;
        if (nowChecked)
            m_memberInfo->requestedFlags |= ColumnFlag[column];
        else
            m_memberInfo->requestedFlags &= ~ColumnFlag[column];

        if (nowChecked) {
            if (column == QPropertyColumn) {
                m_memberInfo->requestedFlags |= GenerateFlag::GenerateGetter;
                m_memberInfo->requestedFlags |= GenerateFlag::GenerateSetter;
                m_memberInfo->requestedFlags |= GenerateFlag::GenerateSignal;
                m_memberInfo->requestedFlags &= ~GenerateFlag::GenerateConstantProperty;
            } else if (column == ConstantQPropertyColumn) {
                m_memberInfo->requestedFlags |= GenerateFlag::GenerateGetter;
                m_memberInfo->requestedFlags &= ~GenerateFlag::GenerateSetter;
                m_memberInfo->requestedFlags &= ~GenerateFlag::GenerateSignal;
                m_memberInfo->requestedFlags &= ~GenerateFlag::GenerateReset;
                m_memberInfo->requestedFlags &= ~GenerateFlag::GenerateProperty;
            } else if (column == SetterColumn || column == SignalColumn || column == ResetColumn) {
                m_memberInfo->requestedFlags &= ~GenerateFlag::GenerateConstantProperty;
            }
        } else {
            if (column == SignalColumn)
                m_memberInfo->requestedFlags &= ~GenerateFlag::GenerateProperty;
        }
        for (int i = 0; i < 16; ++i) {
            const bool allowed = m_memberInfo->possibleFlags & (1 << i);
            if (!allowed)
                m_memberInfo->requestedFlags &= ~(1 << i); // clear bit
        }
        update();
        return true;
    }

    Qt::ItemFlags flags(int column) const override
    {
        if (column == NameColumn)
            return Qt::ItemIsEnabled;
        if (column < 1 || column > static_cast<int>(std::size(ColumnFlag)))
            return {};
        if (m_memberInfo->possibleFlags & ColumnFlag[column])
            return Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
        return {};
    }

    MemberInfo *const m_memberInfo;
};

class GenerateGettersSettersDialog : public QDialog
{
    static constexpr CandidateTreeItem::Column CheckBoxColumn[4]
        = {CandidateTreeItem::Column::GetterColumn,
           CandidateTreeItem::Column::SetterColumn,
           CandidateTreeItem::Column::SignalColumn,
           CandidateTreeItem::Column::QPropertyColumn};

public:
    GenerateGettersSettersDialog(const GetterSetterCandidates &candidates)
        : QDialog()
        , m_candidates(candidates)
    {
        setWindowTitle(Tr::tr("Getters and Setters"));
        const auto model = new TreeModel<TreeItem, CandidateTreeItem>(this);
        model->setHeader(QStringList({
            Tr::tr("Member"),
            Tr::tr("Getter"),
            Tr::tr("Setter"),
            Tr::tr("Signal"),
            Tr::tr("Reset"),
            Tr::tr("QProperty"),
            Tr::tr("Constant QProperty"),
        }));
        for (MemberInfo &candidate : m_candidates)
            model->rootItem()->appendChild(new CandidateTreeItem(&candidate));
        const auto view = new BaseTreeView(this);
        view->setModel(model);
        int optimalWidth = 0;
        for (int i = 0; i < model->columnCount(QModelIndex{}); ++i) {
            view->resizeColumnToContents(i);
            optimalWidth += view->columnWidth(i);
        }

        const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

        const auto setCheckStateForAll = [model](int column, int checkState) {
            for (int i = 0; i < model->rowCount(); ++i)
                model->setData(model->index(i, column), checkState, Qt::CheckStateRole);
        };
        const auto preventPartiallyChecked = [](QCheckBox *checkbox) {
            if (checkbox->checkState() == Qt::PartiallyChecked)
                checkbox->setCheckState(Qt::Checked);
        };
        using Column = CandidateTreeItem::Column;
        const auto createConnections = [this, setCheckStateForAll, preventPartiallyChecked](
                                           QCheckBox *checkbox, Column column) {
            connect(checkbox, &QCheckBox::stateChanged, this, [setCheckStateForAll, column](int state) {
                if (state != Qt::PartiallyChecked)
                    setCheckStateForAll(column, state);
            });
            connect(checkbox, &QCheckBox::clicked, this, [checkbox, preventPartiallyChecked] {
                preventPartiallyChecked(checkbox);
            });
        };
        std::array<QCheckBox *, 4> checkBoxes = {};

        static_assert(std::size(CheckBoxColumn) == checkBoxes.size(),
                      "Must contain the same number of elements");
        for (std::size_t i = 0; i < checkBoxes.size(); ++i) {
            if (Utils::anyOf(candidates, [i](const MemberInfo &mi) {
                    return mi.possibleFlags & CandidateTreeItem::ColumnFlag[CheckBoxColumn[i]];
                })) {
                const Column column = CheckBoxColumn[i];
                if (column == Column::GetterColumn)
                    checkBoxes[i] = new QCheckBox(Tr::tr("Create getters for all members"));
                else if (column == Column::SetterColumn)
                    checkBoxes[i] = new QCheckBox(Tr::tr("Create setters for all members"));
                else if (column == Column::SignalColumn)
                    checkBoxes[i] = new QCheckBox(Tr::tr("Create signals for all members"));
                else if (column == Column::QPropertyColumn)
                    checkBoxes[i] = new QCheckBox(Tr::tr("Create Q_PROPERTY for all members"));

                createConnections(checkBoxes[i], column);
            }
        }
        connect(model, &QAbstractItemModel::dataChanged, this, [this, checkBoxes] {
            const auto countExisting = [this](GenerateFlag flag) {
                return Utils::count(m_candidates, [flag](const MemberInfo &mi) {
                    return !(mi.possibleFlags & flag);
                });
            };
            const auto countRequested = [this](GenerateFlag flag) {
                return Utils::count(m_candidates, [flag](const MemberInfo &mi) {
                    return mi.requestedFlags & flag;
                });
            };
            const auto countToState = [this](int requestedCount, int alreadyExistsCount) {
                if (requestedCount == 0)
                    return Qt::Unchecked;
                if (int(m_candidates.size()) - requestedCount == alreadyExistsCount)
                    return Qt::Checked;
                return Qt::PartiallyChecked;
            };
            for (std::size_t i = 0; i < checkBoxes.size(); ++i) {
                if (checkBoxes[i]) {
                    const GenerateFlag flag = CandidateTreeItem::ColumnFlag[CheckBoxColumn[i]];
                    checkBoxes[i]->setCheckState(
                        countToState(countRequested(flag), countExisting(flag)));
                }
            }
        });

        const auto mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(new QLabel(Tr::tr("Select the getters and setters "
                                                "to be created.")));
        for (auto checkBox : checkBoxes) {
            if (checkBox)
                mainLayout->addWidget(checkBox);
        }
        mainLayout->addWidget(view);
        mainLayout->addWidget(buttonBox);
        int left, right;
        mainLayout->getContentsMargins(&left, nullptr, &right, nullptr);
        optimalWidth += left + right;
        resize(optimalWidth, mainLayout->sizeHint().height());
    }

    GetterSetterCandidates candidates() const { return m_candidates; }

private:
    GetterSetterCandidates m_candidates;
};

class GenerateGettersSettersOperation : public CppQuickFixOperation
{
public:
    GenerateGettersSettersOperation(const CppQuickFixInterface &interface)
        : CppQuickFixOperation(interface)
    {
        setDescription(Tr::tr("Create Getter and Setter Member Functions"));

        m_classAST = astForClassOperations(interface);
        if (!m_classAST)
            return;
        Class * const theClass = m_classAST->symbol;
        if (!theClass)
            return;

        // Go through all data members and try to find out whether they have getters and/or setters.
        QList<Symbol *> dataMembers;
        QList<Symbol *> memberFunctions;
        for (auto it = theClass->memberBegin(); it != theClass->memberEnd(); ++it) {
            Symbol *const s = *it;
            if (!s->identifier() || !s->type() || s->type().isTypedef())
                continue;
            if ((s->asDeclaration() && s->type()->asFunctionType()) || s->asFunction())
                memberFunctions << s;
            else if (s->asDeclaration() && (s->isPrivate() || s->isProtected()))
                dataMembers << s;
        }

        auto file = interface.currentFile();
        QStringList qPropertyNames; // name after MEMBER or name of the property
        for (auto it = m_classAST->member_specifier_list; it; it = it->next) {
            if (it->value->asQtPropertyDeclaration()) {
                auto propDecl = it->value->asQtPropertyDeclaration();
                // iterator over 'READ ...', ... and check if we have a MEMBER
                for (auto p = propDecl->property_declaration_item_list; p; p = p->next) {
                    const char *tokenString = file->tokenAt(p->value->item_name_token).spell();
                    if (!qstrcmp(tokenString, "MEMBER"))
                        qPropertyNames << file->textOf(p->value->expression);
                }
                // no MEMBER, but maybe the property name is the same
                qPropertyNames << file->textOf(propDecl->property_name);
            }
        }
        const QStringList memberFunctionsAsStrings = toStringList(memberFunctions);
        const ReadWith readWith = std::make_shared<CppQuickFixInterface>(interface);
        const CppRefactoringChanges changes(interface.snapshot());

        for (Symbol *const member : std::as_const(dataMembers)) {
            ExistingGetterSetterData existing;
            existing.memberVariableName = QString::fromUtf8(member->identifier()->chars(),
                                                            member->identifier()->size());
            existing.declaredType = readTypeOfTheMember(interface, readWith, member->type(),
                                                       member->enclosingScope(),
                                                       member->line(), member->column());
            existing.toDefine = declarationToDefine(member, changes);
            existing.clazz = readTheClass(interface, readWith, theClass);

            // check if a Q_PROPERTY exist
            const QString baseName = CppQuickFixSettings::memberBaseName(existing.memberVariableName);
            if (qPropertyNames.contains(baseName)
                || qPropertyNames.contains(existing.memberVariableName))
                continue;

            findExistingFunctions(existing, memberFunctionsAsStrings);
            existing.qPropertyName = baseName;

            int possibleFlags = existing.computePossibleFlags();
            if (possibleFlags == 0)
                continue;
            m_candidates.emplace_back(existing, possibleFlags);
        }
    }

    GetterSetterCandidates candidates() const { return m_candidates; }
    bool isApplicable() const { return !m_candidates.empty(); }

    void setGetterSetterData(const GetterSetterCandidates &data)
    {
        m_candidates = data;
        m_hasData = true;
    }

private:
    void perform() override
    {
        if (!m_hasData) {
            GenerateGettersSettersDialog dlg(m_candidates);
            if (dlg.exec() == QDialog::Rejected)
                return;
            m_candidates = dlg.candidates();
        }
        if (m_candidates.empty())
            return;
        GetterSetterRefactoringHelper helper(this, m_candidates.front().data.clazz);
        for (MemberInfo &mi : m_candidates) {
            if (mi.requestedFlags != 0) {
                helper.performGeneration(mi.data, mi.requestedFlags);
            }
        }
        helper.applyChanges();
    }

    GetterSetterCandidates m_candidates;
    const ClassSpecifierAST *m_classAST = nullptr;
    bool m_hasData = false;
};

int ExistingGetterSetterData::computePossibleFlags() const
{
    const bool isConst = declaredType.isConst();
    const bool isStatic = declaredType.isStatic();
    int generateFlags = 0;
    if (getterName.isEmpty())
        generateFlags |= GenerateFlag::GenerateGetter;
    if (!isConst) {
        if (resetName.isEmpty())
            generateFlags |= GenerateFlag::GenerateReset;
        if (!isStatic && signalName.isEmpty() && setterName.isEmpty())
            generateFlags |= GenerateFlag::GenerateSignal;
        if (setterName.isEmpty())
            generateFlags |= GenerateFlag::GenerateSetter;
    }
    if (!isStatic) {
        const bool hasSignal = !signalName.isEmpty() || generateFlags & GenerateFlag::GenerateSignal;
        if (!isConst && hasSignal)
            generateFlags |= GenerateFlag::GenerateProperty;
    }
    if (setterName.isEmpty() && signalName.isEmpty())
        generateFlags |= GenerateFlag::GenerateConstantProperty;
    return generateFlags;
}

GetterSetterRefactoringHelper::GetterSetterRefactoringHelper(
        CppQuickFixOperation *operation, const GeneratedClass &clazz)
    : m_operation(operation)
    , m_changes(m_operation->snapshot())
    , m_locator(m_changes)
    , m_headerFile(operation->currentFile())
    , m_class(clazz)
{}
void GetterSetterRefactoringHelper::performGeneration(
        const ExistingGetterSetterData &data, int generateFlags)
{
    m_data = {this};
    m_data.setup(data, generateFlags);

    generateGetter();
    generateSetter();
    generateReset();
    generateSignal();
    generateMemberVariable();
    generateQProperty();
    generateBindable();
}

void GetterSetterRefactoringHelper::applyChanges()
{
    const auto classLayout = {
        InsertionPointLocator::Public,
        InsertionPointLocator::PublicSlot,
        InsertionPointLocator::Signals,
        InsertionPointLocator::Protected,
        InsertionPointLocator::ProtectedSlot,
        InsertionPointLocator::PrivateSlot,
        InsertionPointLocator::Private,
    };
    for (auto spec : classLayout) {
        const auto iter = m_headerFileCode.find(spec);
        if (iter != m_headerFileCode.end()) {
            const InsertionLocation loc = headerLocationFor(spec);
            m_headerFile->setOpenEditor(true, m_headerFile->position(loc.line(), loc.column()));
            insertAndIndent(m_headerFile, loc, *iter);
        }
    }
    if (!m_sourceFileCode.isEmpty() && m_sourceFileInsertionPoint.isValid()) {
        m_sourceFile->setOpenEditor(
                    true,
                    m_sourceFile
                    ->position(m_sourceFileInsertionPoint.line(), m_sourceFileInsertionPoint.column()));
        insertAndIndent(m_sourceFile, m_sourceFileInsertionPoint, m_sourceFileCode);
    }

    m_headerFile->apply(m_headerFileChangeSet);
    m_sourceFile->apply(m_sourceFileChangeSet);
}

void GetterSetterRefactoringHelper::insertAndIndent(
        const RefactoringFilePtr &file, const InsertionLocation &loc, const QString &text)
{
    int targetPosition = file->position(loc.line(), loc.column());
    ChangeSet &changeSet = file == m_headerFile ? m_headerFileChangeSet : m_sourceFileChangeSet;
    changeSet.insert(targetPosition, loc.prefix() + text + loc.suffix());
}

static FullySpecifiedType makeConstRef(const CppQuickFixInterface &readWith,
                                      FullySpecifiedType type)
{
    type.setConst(true);
    return readWith.currentFile()->cppDocument()->control()->referenceType(type, false);
}

static FullySpecifiedType addConstToReference(const CppQuickFixInterface &readWith,
                                              FullySpecifiedType type)
{
    if (ReferenceType *ref = type.type()->asReferenceType()) {
        FullySpecifiedType elemType = ref->elementType();
        if (elemType.isConst())
            return type;
        elemType.setConst(true);
        return readWith.currentFile()->cppDocument()->control()->referenceType(elemType, false);
    }
    return type;
}

static bool isValueType(const CppQuickFixInterface &readWith,
                        FullySpecifiedType type, Scope *enclosingScope, bool *customValueType)
{
    if (customValueType)
        *customValueType = false;
    // a type is a value type if it is one of the following
    const auto isTypeValueType = [](const FullySpecifiedType &t) {
        return t->asPointerType() || t->asEnumType() || t->asIntegerType() || t->asFloatType()
                || t->asReferenceType();
    };
    if (type->asNamedType()) {
        // we need a recursive search and a lookup context
        LookupContext context(readWith.currentFile()->cppDocument(), readWith.snapshot());
        auto isValueType = [settings = cppQuickFixSettingsForProject(
                                ProjectTree::currentProject()),
                &customValueType,
                &context,
                &isTypeValueType](const Name *name, Scope *scope, auto &isValueType) {
            // maybe the type is a custom value type by name
            if (const Identifier *id = name->identifier()) {
                if (settings->isValueType(QString::fromUtf8(id->chars(), id->size()))) {
                    if (customValueType)
                        *customValueType = true;
                    return true;
                }
            }
            // search for the type declaration
            QList<LookupItem> localLookup = context.lookup(name, scope);
            for (auto &&i : localLookup) {
                if (isTypeValueType(i.type()))
                    return true;
                if (i.type()->asNamedType()) { // check if we have to search recursively
                    const Name *newName = i.type()->asNamedType()->name();
                    Scope *newScope = i.declaration()->enclosingScope();
                    if (Matcher::match(newName, name)
                            && Matcher::match(newScope->name(), scope->name())) {
                        continue; // we have found the start location of the search
                    }
                    return isValueType(newName, newScope, isValueType);
                }
                return false;
            }
            return false;
        };
        // start recursion
        return isValueType(type->asNamedType()->name(), enclosingScope, isValueType);
    }
    return isTypeValueType(type);
}

// What the built-in front end says about a type it read: one of its own
// types, the scope it was read in, and what it was read with.
class BuiltinTypeReading : public TypeReading
{
public:
    BuiltinTypeReading(const FullySpecifiedType &type, Scope *scope, const ReadWith &readWith)
        : m_type(type)
        , m_scope(scope)
        , m_readWith(readWith)
    {}

    bool isPointer() const override { return m_type.isValid() && m_type->asPointerType(); }
    bool isConst() const override { return m_type.isConst(); }
    bool isStatic() const override { return m_type.isStatic(); }

    bool isValueType(bool *saidByName) const override
    {
        return ::CppEditor::Internal::isValueType(*m_readWith, m_type, m_scope, saidByName);
    }

    Ptr asDeclared() const override
    {
        FullySpecifiedType type = m_type;
        type.setConst(false);
        type.setStatic(false);
        return madeOf(type);
    }

    Ptr withoutConst() const override
    {
        FullySpecifiedType type = m_type;
        type.setConst(false);
        return madeOf(type);
    }

    Ptr asValue() const override
    {
        FullySpecifiedType type = m_type;
        if (ReferenceType *reference = type.type()->asReferenceType())
            type = reference->elementType();
        type.setConst(false);
        return madeOf(type);
    }

    Ptr constReference() const override { return madeOf(makeConstRef(*m_readWith, m_type)); }

    Ptr withConstOnReference() const override
    {
        return madeOf(addConstToReference(*m_readWith, m_type));
    }

    Ptr firstTemplateArgument() const override
    {
        if (const std::optional<FullySpecifiedType> argument = getFirstTemplateParameter(m_type))
            return madeOf(*argument);
        return {};
    }

    Ptr writtenAt(const CppRefactoringFilePtr &file, const InsertionLocation &location,
                  const QStringList &namespacesOpenedThere) const override
    {
        return madeOf(typeAtDifferentLocation(*m_readWith, m_type, m_scope, file, location,
                                              namespacesOpenedThere));
    }

    QString asDeclarationOf(const QString &name) const override
    {
        return generationOverview().prettyType(m_type, name);
    }

    QString asTextWithoutTemplateParameters() const override
    {
        Overview overview = generationOverview();
        overview.showTemplateParameters = false;
        return overview.prettyType(m_type);
    }

private:
    Ptr madeOf(const FullySpecifiedType &type) const
    {
        return std::make_shared<BuiltinTypeReading>(type, m_scope, m_readWith);
    }

    FullySpecifiedType m_type;
    Scope *m_scope = nullptr;
    ReadWith m_readWith;
};

TypeReading::Ptr GeneratedType::builtinReading(const FullySpecifiedType &type, Scope *scope,
                                               const ReadWith &readWith)
{
    if (!type.isValid())
        return {};
    return std::make_shared<BuiltinTypeReading>(type, scope, readWith);
}

GeneratedType GeneratedType::readBuiltin(const FullySpecifiedType &type, Scope *scope,
                                         const ReadWith &readWith)
{
    return GeneratedType(builtinReading(type, scope, readWith));
}

GeneratedType GeneratedType::fromText(const QString &text, const ReadWith &readWith)
{
    // Through a named type of the front end's, so that a type nobody read
    // is written by whatever writes the ones that were read.
    Control *control = readWith->currentFile()->cppDocument()->control();
    const std::string utf8 = text.toUtf8().toStdString();
    return readBuiltin(FullySpecifiedType(control->namedType(control->identifier(utf8.c_str()))),
                       nullptr,
                       readWith);
}

// And what it says about a class.
class BuiltinClassReading : public ClassReading
{
public:
    BuiltinClassReading(Class *clazz, const ReadWith &readWith)
        : m_class(clazz)
        , m_readWith(readWith)
    {}

    QString name() const override
    {
        if (isAnonymous())
            return {};
        const Identifier *identifier = m_class->name()->identifier();
        return QString::fromUtf8(identifier->chars(), identifier->size());
    }

    bool isAnonymous() const override { return m_class->name()->asAnonymousNameId() != nullptr; }

    bool isQObject() const override
    {
        // Not by the Q_OBJECT it wrote but by the connect() it inherited:
        // what makes a slot a slot is standing below QObject.
        const QByteArray connectName = "connect";
        const Identifier connectId(connectName.data(), connectName.size());
        const QList<LookupItem> items = m_readWith->context().lookup(&connectId, m_class);
        for (const LookupItem &item : items) {
            if (item.declaration() && item.declaration()->enclosingClass()
                && generationOverview().prettyName(item.declaration()->enclosingClass()->name())
                       == "QObject") {
                return true;
            }
        }
        return false;
    }

    QString writtenAt(const CppRefactoringFilePtr &file,
                      const InsertionLocation &location) const override
    {
        return symbolAtDifferentLocation(*m_readWith, m_class, file, location);
    }

    InsertionLocation placeItIsWrittenAt(const Utils::FilePath &filePath) const override
    {
        return InsertionLocation(filePath, {}, {}, m_class->line(), m_class->column());
    }

    InsertionLocation placeForDeclaration(
        const InsertionPointLocator &locator,
        const Utils::FilePath &filePath,
        InsertionPointLocator::AccessSpec spec) const override
    {
        return locator.methodDeclarationInClass(filePath, m_class, spec,
                                                InsertionPointLocator::ForceAccessSpec::Yes);
    }

    DeclarationToDefine toDefine(const CppRefactoringChanges &changes) const override
    {
        return declarationToDefine(m_class, changes);
    }

    ReadWith readWith() const override { return m_readWith; }

private:
    Class *m_class = nullptr;
    ReadWith m_readWith;
};

GeneratedClass GeneratedClass::readBuiltin(Class *clazz, const ReadWith &readWith)
{
    if (!clazz)
        return {};
    return GeneratedClass(std::make_shared<BuiltinClassReading>(clazz, readWith));
}





void GetterSetterRefactoringHelper::addHeaderCode(
        InsertionPointLocator::AccessSpec spec, const QString &code)
{
    QString &existing = m_headerFileCode[spec];
    existing += code;
    if (!existing.endsWith('\n'))
        existing += '\n';
}

InsertionLocation GetterSetterRefactoringHelper::headerLocationFor(
        InsertionPointLocator::AccessSpec spec)
{
    const auto insertionPoint = m_headerInsertionPoints.find(spec);
    if (insertionPoint != m_headerInsertionPoints.end())
        return *insertionPoint;
    const InsertionLocation loc
            = m_class.placeForDeclaration(m_locator, m_headerFile->filePath(), spec);
    m_headerInsertionPoints.insert(spec, loc);
    return loc;
}

InsertionLocation GetterSetterRefactoringHelper::sourceLocationFor(
        const DeclarationToDefine &declaration, QStringList *insertedNamespaces)
{
    if (m_sourceFileInsertionPoint.isValid())
        return m_sourceFileInsertionPoint;
    m_sourceFileInsertionPoint = insertLocationForMethodDefinition(
                declaration,
                false,
                m_settings->createMissingNamespacesinCppFile() ? NamespaceHandling::CreateMissing
                                                               : NamespaceHandling::Ignore,
                m_changes,
                m_sourceFile->filePath(),
                insertedNamespaces);
    if (m_settings->addUsingNamespaceinCppFile()) {
        // check if we have to insert a using namespace ...
        const QStringList requiredNamespaces = declaration.enclosingNamespaces;
        NSCheckerVisitor visitor(
                    m_sourceFile.get(),
                    requiredNamespaces,
                    m_sourceFile
                    ->position(m_sourceFileInsertionPoint.line(), m_sourceFileInsertionPoint.column()));
        visitor.accept(m_sourceFile->cppDocument()->translationUnit()->ast());
        if (insertedNamespaces)
            insertedNamespaces->clear();
        if (auto rns = visitor.remainingNamespaces(); !rns.empty()) {
            QString ns = "using namespace ";
            for (auto &n : rns) {
                if (!n.isEmpty()) { // we have to ignore unnamed namespaces
                    ns += n;
                    ns += "::";
                    if (insertedNamespaces)
                        insertedNamespaces->append(n);
                }
            }
            ns.resize(ns.size() - 2); // remove last '::'
            ns += ";\n";
            const auto &loc = m_sourceFileInsertionPoint;
            m_sourceFileInsertionPoint = InsertionLocation(
                        loc.filePath(), loc.prefix() + ns, loc.suffix(), loc.line(), loc.column());
        }
    }
    return m_sourceFileInsertionPoint;
}

void GetterSetterRefactoringHelper::generateGetter()
{
    if (!m_data.generateGetter())
        return;

    // maybe we added 'this->' to memberVariableName because of a collision with parameterName
    // but here the 'this->' is not needed
    const QString returnExpression
            = QString{m_data.getSetTemplate().returnExpression}.replace("this->", "");
    QString getterInClassDeclaration = m_data.returnTypeClass().asDeclarationOf(m_data.getterName())
            + QLatin1String("()");
    if (m_data.isStatic())
        getterInClassDeclaration.prepend(QLatin1String("static "));
    else
        getterInClassDeclaration += QLatin1String(" const");
    getterInClassDeclaration.prepend(m_settings->getterAttributes + QLatin1Char(' '));

    auto getterLocation = m_settings->determineGetterLocation(1);
    // if we have an anonymous class we must add code inside the class
    if (m_data.theClass().isAnonymous())
        getterLocation = CppQuickFixSettings::FunctionLocation::InsideClass;

    if (getterLocation == CppQuickFixSettings::FunctionLocation::InsideClass) {
        getterInClassDeclaration += QLatin1String("\n{\nreturn ") + returnExpression
                + QLatin1String(";\n}\n");
    } else {
        getterInClassDeclaration += QLatin1String(";\n");
    }
    addHeaderCode(InsertionPointLocator::Public, getterInClassDeclaration);
    if (getterLocation == CppQuickFixSettings::FunctionLocation::CppFile && !hasSourceFile())
        getterLocation = CppQuickFixSettings::FunctionLocation::OutsideClass;

    if (getterLocation != CppQuickFixSettings::FunctionLocation::InsideClass) {
        const auto getReturnTypeAt = [&](CppRefactoringFilePtr targetFile,
                InsertionLocation targetLoc) {
            if (m_data.getSetTemplate().returnTypeTemplate.has_value()) {
                QString returnType = *m_data.getSetTemplate().returnTypeTemplate;
                if (m_data.returnTypeTemplateParameter().isValid()) {
                    returnType.replace(
                                CppQuickFixSettings::GetterSetterTemplate::TEMPLATE_PARAMETER_PATTERN,
                                m_data.returnTypeTemplateParameter()
                                    .writtenAt(targetFile, targetLoc).asText());
                }
                if (returnType.contains(CppQuickFixSettings::GetterSetterTemplate::TYPE_PATTERN)) {
                    returnType.replace(
                                CppQuickFixSettings::GetterSetterTemplate::TYPE_PATTERN,
                                m_data.memberVarType().writtenAt(targetFile, targetLoc).asText());
                }
                return GeneratedType::fromText(returnType, m_class.readWith());
            } else {
                const GeneratedType returnType
                        = m_data.memberVarType().writtenAt(targetFile, targetLoc);
                if (m_settings->returnByConstRef && !m_data.isValueType())
                    return returnType.constReference();
                return returnType;
            }
        };
        const QString constSpec = m_data.isStatic() ? QLatin1String("")
                                                    : QLatin1String(" const");
        if (getterLocation == CppQuickFixSettings::FunctionLocation::CppFile) {
            InsertionLocation loc = sourceLocationFor(m_data.toDefine());
            GeneratedType returnType;
            QString clazz;
            if (m_settings->rewriteTypesinCppFile()) {
                returnType = getReturnTypeAt(m_sourceFile, loc);
                clazz = m_data.theClass().writtenAt(m_sourceFile, loc);
            } else {
                returnType = m_data.returnTypeHeader();
                clazz = m_data.theClass().name();
            }
            const QString code = returnType.asDeclarationOf(clazz + "::" + m_data.getterName())
                    + "()" + constSpec + "\n{\nreturn " + returnExpression + ";\n}";
            addSourceFileCode(code);
        } else if (getterLocation == CppQuickFixSettings::FunctionLocation::OutsideClass) {
            InsertionLocation loc = insertLocationForMethodDefinition(
                        m_data.toDefine(), false, NamespaceHandling::Ignore, m_changes,
                        m_headerFile->filePath());
            const GeneratedType returnType = getReturnTypeAt(m_headerFile, loc);
            const QString clazz = m_data.theClass().writtenAt(m_headerFile, loc);
            QString code = returnType.asDeclarationOf(clazz + "::" + m_data.getterName())
                    + "()" + constSpec + "\n{\nreturn " + returnExpression + ";\n}";
            if (m_isHeaderHeaderFile)
                code.prepend("inline ");
            insertAndIndent(m_headerFile, loc, code);
        }
    }
}

void GetterSetterRefactoringHelper::generateSetter()
{
    if (!m_data.generateSetter())
        return;

    QString headerDeclaration
            = "void " + m_data.setterName() + '('
            + m_data.parameterType().withConstOnReference()
                  .asDeclarationOf(m_data.parameterName()) + ")";
    if (m_data.isStatic())
        headerDeclaration.prepend("static ");
    QString body = "\n{\n";
    if (m_data.signalName().isEmpty())
        body += m_data.getSetTemplate().assignment + ";\n";
    else
        body += setterBodyWithSignal();
    body += "}";

    auto setterLocation = m_settings->determineSetterLocation(body.count('\n') - 2);
    // if we have an anonymous class we must add code inside the class
    if (m_data.theClass().isAnonymous())
        setterLocation = CppQuickFixSettings::FunctionLocation::InsideClass;

    if (setterLocation == CppQuickFixSettings::FunctionLocation::CppFile && !hasSourceFile())
        setterLocation = CppQuickFixSettings::FunctionLocation::OutsideClass;

    if (setterLocation == CppQuickFixSettings::FunctionLocation::InsideClass) {
        headerDeclaration += body;
    } else {
        headerDeclaration += ";\n";
        if (setterLocation == CppQuickFixSettings::FunctionLocation::CppFile) {
            InsertionLocation loc = sourceLocationFor(m_data.toDefine());
            QString clazz;
            GeneratedType newParameterType = m_data.parameterType();
            if (m_settings->rewriteTypesinCppFile()) {
                newParameterType = m_data.memberVarType().writtenAt(m_sourceFile, loc);
                if (!m_data.isValueType())
                    newParameterType = newParameterType.constReference();
                clazz = m_data.theClass().writtenAt(m_sourceFile, loc);
            } else {
                clazz = m_data.theClass().name();
            }
            newParameterType = newParameterType.withConstOnReference();
            const QString code = "void " + clazz + "::" + m_data.setterName() + '('
                    + newParameterType.asDeclarationOf(m_data.parameterName()) + ')'
                    + body;
            addSourceFileCode(code);
        } else if (setterLocation == CppQuickFixSettings::FunctionLocation::OutsideClass) {
            InsertionLocation loc = insertLocationForMethodDefinition(
                        m_data.toDefine(), false, NamespaceHandling::Ignore, m_changes,
                        m_headerFile->filePath());

            GeneratedType newParameterType
                    = m_data.declaredType().writtenAt(m_headerFile, loc);
            if (!m_data.isValueType())
                newParameterType = newParameterType.constReference();
            newParameterType = newParameterType.withConstOnReference();
            QString clazz = m_data.theClass().writtenAt(m_headerFile, loc);

            QString code = "void " + clazz + "::" + m_data.setterName() + '('
                    + newParameterType.asDeclarationOf(m_data.parameterName()) + ')' + body;
            if (m_isHeaderHeaderFile)
                code.prepend("inline ");
            insertAndIndent(m_headerFile, loc, code);
        }
    }
    addHeaderCode(m_data.setterAccessSpec(), headerDeclaration);
}

void GetterSetterRefactoringHelper::generateReset()
{
    if (!m_data.generateReset())
        return;

    QString headerDeclaration = "void " + m_data.resetName() + "()";
    if (m_data.isStatic())
        headerDeclaration.prepend("static ");
    QString body = "\n{\n";
    if (!m_data.setterName().isEmpty()) {
        body += m_data.setterName() + "({}); // TODO: Adapt to use your actual default value\n";
    } else {
        body += "static $TYPE defaultValue{}; "
                "// TODO: Adapt to use your actual default value\n";
        if (m_data.signalName().isEmpty())
            body += m_data.getSetTemplate().assignment + ";\n";
        else
            body += setterBodyWithSignal();
    }
    body += "}";

    // the template use <parameterName> as new value name, but we want to use 'defaultValue'
    body.replace(QRegularExpression("\\b" + m_data.parameterName() + "\\b"), "defaultValue");
    // body.count('\n') - 2 : do not count the 2 at start
    auto resetLocation = m_settings->determineSetterLocation(body.count('\n') - 2);
    // if we have an anonymous class we must add code inside the class
    if (m_data.theClass().isAnonymous())
        resetLocation = CppQuickFixSettings::FunctionLocation::InsideClass;

    if (resetLocation == CppQuickFixSettings::FunctionLocation::CppFile && !hasSourceFile())
        resetLocation = CppQuickFixSettings::FunctionLocation::OutsideClass;

    if (resetLocation == CppQuickFixSettings::FunctionLocation::InsideClass) {
        headerDeclaration += body.replace("$TYPE", m_data.memberVarType().asText());
    } else {
        headerDeclaration += ";\n";
        if (resetLocation == CppQuickFixSettings::FunctionLocation::CppFile) {
            const InsertionLocation loc = sourceLocationFor(m_data.toDefine());
            QString clazz;
            GeneratedType type = m_data.memberVarType();
            if (m_settings->rewriteTypesinCppFile()) {
                type = m_data.memberVarType().writtenAt(m_sourceFile, loc);
                clazz = m_data.theClass().writtenAt(m_sourceFile, loc);
            } else {
                clazz = m_data.theClass().name();
            }
            const QString code = "void " + clazz + "::" + m_data.resetName() + "()"
                    + body.replace("$TYPE", type.asText());
            addSourceFileCode(code);
        } else if (resetLocation == CppQuickFixSettings::FunctionLocation::OutsideClass) {
            const InsertionLocation loc = insertLocationForMethodDefinition(
                        m_data.toDefine(), false, NamespaceHandling::Ignore, m_changes,
                        m_headerFile->filePath());
            const GeneratedType type = m_data.declaredType().writtenAt(m_headerFile, loc);
            const QString clazz = m_data.theClass().writtenAt(m_headerFile, loc);
            QString code = "void " + clazz + "::" + m_data.resetName() + "()"
                    + body.replace("$TYPE", type.asText());
            if (m_isHeaderHeaderFile)
                code.prepend("inline ");
            insertAndIndent(m_headerFile, loc, code);
        }
    }
    addHeaderCode(m_data.setterAccessSpec(), headerDeclaration);
}

void GetterSetterRefactoringHelper::generateSignal()
{
    if (!m_data.generateSignal())
        return;
    const QString parameter = m_data.returnTypeClass().asDeclarationOf(m_data.qPropertyName());
    const QString newValue = m_settings->signalWithNewValue ? parameter : QString();
    const QString declaration = QString("void %1(%2);\n").arg(m_data.signalName(), newValue);
    addHeaderCode(InsertionPointLocator::Signals, declaration);
}

void GetterSetterRefactoringHelper::generateQProperty()
{
    if (!m_data.generateQProperty())
        return;

    // Use the returnTypeHeader as base because of custom types in getSetTemplates.
    // Remove const reference from type.
    const GeneratedType type = m_data.returnTypeClass().asValue();

    QString propertyDeclaration
            = QLatin1String("Q_PROPERTY(")
            + type.asDeclarationOf(CppQuickFixSettings::memberBaseName(m_data.memberVarName()));
    bool needMember = false;
    if (m_data.getterName().isEmpty())
        needMember = true;
    else
        propertyDeclaration += QLatin1String(" READ ") + m_data.getterName();
    if (m_data.generateConstQProperty()) {
        if (needMember)
            propertyDeclaration += QLatin1String(" MEMBER ") + m_data.memberVarName();
        propertyDeclaration.append(QLatin1String(" CONSTANT"));
    } else {
        if (m_data.setterName().isEmpty()) {
            needMember = true;
        } else if (!m_data.getSetTemplate().returnTypeTemplate.has_value()) {
            // if the return type of the getter and then Q_PROPERTY is different than
            // the setter type, we should not add WRITE to the Q_PROPERTY
            propertyDeclaration.append(QLatin1String(" WRITE ")).append(m_data.setterName());
        }
        if (needMember)
            propertyDeclaration += QLatin1String(" MEMBER ") + m_data.memberVarName();
        if (!m_data.resetName().isEmpty())
            propertyDeclaration += QLatin1String(" RESET ") + m_data.resetName();
        propertyDeclaration.append(QLatin1String(" NOTIFY ")).append(m_data.signalName());
    }

    propertyDeclaration.append(QLatin1String(" FINAL)\n"));
    addHeaderCode(InsertionPointLocator::Private, propertyDeclaration);
}

void GetterSetterRefactoringHelper::generateBindable()
{
    if (!m_data.generateBindable())
        return;
    const QString typeName = m_data.memberVarType().asText();
    const QString code = "QBindable<" + typeName + "> " + m_data.bindableName()
                         + "() { return &" + m_data.memberVarName() + "; }\n";
    addHeaderCode(InsertionPointLocator::Public, code);
}

QString GetterSetterRefactoringHelper::setterBodyWithSignal()
{
    QString body;
    QTextStream setter(&body);
    setter << "if (" << m_data.getSetTemplate().equalComparison << ")\nreturn;\n";

    setter << m_data.getSetTemplate().assignment << ";\n";
    if (m_settings->signalWithNewValue)
        setter << "emit " << m_data.signalName() << "(" << m_data.getSetTemplate().returnExpression << ");\n";
    else
        setter << "emit " << m_data.signalName() << "();\n";

    return body;
}

CppRefactoringFilePtr GetterSetterRefactoringHelper::determineSourceFile()
{
    const FilePath cppFilePath
            = correspondingHeaderOrSource(m_headerFile->filePath(), &m_isHeaderHeaderFile);
    if (!m_isHeaderHeaderFile || !cppFilePath.exists())
        return m_headerFile; // there is no "source" file
    return m_changes.cppFile(cppFilePath);
}

void GetterSetterRefactoringHelper::generateMemberVariable()
{
    if (!m_data.generateMemberVar())
        return;

    QString storageDeclaration;
    if (m_data.generateBindable()) {
        const QString className = m_data.theClass().name();
        const QString typeName = m_data.memberVarType().asText();
        if (m_class.isQObject()) {
            storageDeclaration = "Q_OBJECT_BINDABLE_PROPERTY(" + className + ", " + typeName
                                 + ", " + m_data.memberVarName();
            if (!m_data.signalName().isEmpty())
                storageDeclaration += ", &" + className + "::" + m_data.signalName();
            storageDeclaration += ");\n";
        } else {
            storageDeclaration = "QProperty<" + typeName + "> " + m_data.memberVarName() + ";\n";
        }
    } else {
        storageDeclaration = m_data.memberVarType().asDeclarationOf(m_data.memberVarName());
        if (m_data.memberVarType().isPointer()
                && m_operation->semanticInfo().doc->translationUnit()->languageFeatures().cxx11Enabled) {
            storageDeclaration.append(" = nullptr");
        }
        storageDeclaration.append(";\n");
    }
    addHeaderCode(InsertionPointLocator::Private, storageDeclaration);
}

void GetterSetterRefactoringHelper::addSourceFileCode(const QString &code)
{
    while (!m_sourceFileCode.isEmpty() && !m_sourceFileCode.endsWith("\n\n"))
        m_sourceFileCode += '\n';
    m_sourceFileCode += code;
}

void GetterSetterRefactoringHelper::Data::setup(
        const ExistingGetterSetterData &data, int generateFlags)
{
    m_data = data;
    m_generateFlags = generateFlags;

    if (generateGetter() && getterName().isEmpty()) {
        m_data.getterName = q->m_settings->getGetterName(qPropertyName(), memberVarName());
        if (getterName() == memberVarName())
            m_data.getterName = "get" + memberVarName().left(1).toUpper() + memberVarName().mid(1);
    }
    if (generateSetter() && setterName().isEmpty())
        m_data.setterName = q->m_settings->getSetterName(qPropertyName(), memberVarName());
    if (generateSignal() && signalName().isEmpty())
        m_data.signalName = q->m_settings->getSignalName(qPropertyName(), memberVarName());
    if (generateReset() && resetName().isEmpty())
        m_data.resetName = q->m_settings->getResetName(qPropertyName(), memberVarName());

    QString baseName = CppQuickFixSettings::memberBaseName(memberVarName());
    if (baseName.isEmpty())
        baseName = memberVarName();

    m_parameterName = q->m_settings->getSetterParameterName(baseName, memberVarName());
    if (m_parameterName == memberVarName())
        m_data.memberVariableName = "this->" + memberVarName();
}

const CppQuickFixSettings::GetterSetterTemplate &GetterSetterRefactoringHelper::Data::getSetTemplate() const
{
    if (!m_getSetTemplate) {
        // TODO does not work with using. e.g. 'using foo = std::unique_ptr<int>'
        // TODO must be fully qualified
        m_getSetTemplate = q->m_settings->findGetterSetterTemplate(
                    memberVarType().asTextWithoutTemplateParameters());
        m_getSetTemplate->replacePlaceholders(memberVarName(), m_parameterName);
    }
    return *m_getSetTemplate;
}

const GeneratedType &GetterSetterRefactoringHelper::Data::returnTypeHeader() const
{
    if (!m_returnTypeHeader)
        m_returnTypeHeader = getReturnTypeHeader(HeaderContext::OutsideClass);
    return *m_returnTypeHeader;
}

const GeneratedType &GetterSetterRefactoringHelper::Data::returnTypeClass() const
{
    if (!m_returnTypeClass)
        m_returnTypeClass = getReturnTypeHeader(HeaderContext::InsideClass);
    return *m_returnTypeClass;
}

const GeneratedType &GetterSetterRefactoringHelper::Data::returnTypeTemplateParameter() const
{
    if (!m_returnTypeTemplateParameter) {
        m_returnTypeTemplateParameter.emplace();
        if (getSetTemplate().returnTypeTemplate.has_value()) {
            QString returnTypeTemplate = *getSetTemplate().returnTypeTemplate;
            if (returnTypeTemplate.contains(CppQuickFixSettings::GetterSetterTemplate::TEMPLATE_PARAMETER_PATTERN)) {
                const GeneratedType parameter = declaredType().firstTemplateArgument();
                if (parameter.isValid())
                    m_returnTypeTemplateParameter = parameter;
                else
                    QTC_CHECK(false); // Maybe report error to the user
            }
        }
    }
    return *m_returnTypeTemplateParameter;
}

InsertionPointLocator::AccessSpec GetterSetterRefactoringHelper::Data::setterAccessSpec() const
{
    if (!m_setterAccessSpec) {
        m_setterAccessSpec = q->m_settings->setterAsSlot && q->m_class.isQObject()
                ? InsertionPointLocator::PublicSlot
                : InsertionPointLocator::Public;
    }
    return *m_setterAccessSpec;
}

bool GetterSetterRefactoringHelper::Data::isValueType() const
{
    if (!m_isValueType) {
        // If a type is a Named type we have to search recursively for the real type
        m_isValueType = memberVarType().isValueType();
    }
    return *m_isValueType;
}

bool GetterSetterRefactoringHelper::Data::generateMemberVar() const
{
    return m_generateFlags & GenerateFlag::GenerateMemberVariable;
}

bool GetterSetterRefactoringHelper::Data::generateConstQProperty() const
{
    return m_generateFlags & GenerateFlag::GenerateConstantProperty;
}

bool GetterSetterRefactoringHelper::Data::generateQProperty() const
{
    return (m_generateFlags & GenerateFlag::GenerateProperty) || generateConstQProperty();
}

GeneratedType GetterSetterRefactoringHelper::Data::getReturnTypeHeader(
        HeaderContext headerContext) const
{
    if (!getSetTemplate().returnTypeTemplate.has_value()) {
        const GeneratedType t = q->m_settings->returnByConstRef ? parameterType() : memberVarType();
        if (headerContext == HeaderContext::InsideClass)
            return t;
        return t.writtenAt(q->m_headerFile,
                           q->m_class.placeItIsWrittenAt(q->m_headerFile->filePath()));
    }
    QString typeTemplate = *getSetTemplate().returnTypeTemplate;
    if (returnTypeTemplateParameter().isValid())
        typeTemplate.replace(
                    CppQuickFixSettings::GetterSetterTemplate::TEMPLATE_PARAMETER_PATTERN,
                    returnTypeTemplateParameter().asText());
    if (typeTemplate.contains(CppQuickFixSettings::GetterSetterTemplate::TYPE_PATTERN))
        typeTemplate.replace(
                    CppQuickFixSettings::GetterSetterTemplate::TYPE_PATTERN,
                    declaredType().asText());
    return GeneratedType::fromText(typeTemplate, q->m_class.readWith());
}

GeneratedType GetterSetterRefactoringHelper::Data::memberVarType() const
{
    return declaredType().asDeclared();
}

GeneratedType GetterSetterRefactoringHelper::Data::parameterType() const
{
    return isValueType() ? memberVarType() : memberVarType().constReference();
}

//! Generate constructor
class GenerateConstructor : public CppQuickFixFactory
{
private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const auto op = QSharedPointer<GenerateConstructorOperation>::create(interface);
        if (!op->isApplicable())
            return;
        op->setTest(testMode());
        result << op;
    }
};

//! Adds getter and setter functions for a member variable
class GenerateGetterSetter : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        ExistingGetterSetterData existing;

        const QList<AST *> &path = interface.path();
        // We expect something like
        // [0] TranslationUnitAST
        // [1] NamespaceAST
        // [2] LinkageBodyAST
        // [3] SimpleDeclarationAST
        // [4] ClassSpecifierAST
        // [5] SimpleDeclarationAST
        // [6] DeclaratorAST
        // [7] DeclaratorIdAST
        // [8] SimpleNameAST

        const int n = path.size();
        if (n < 6)
            return;

        int i = 1;
        const auto variableNameAST = path.at(n - i++)->asSimpleName();
        const auto declaratorId = path.at(n - i++)->asDeclaratorId();
        // DeclaratorAST might be preceded by PointerAST, e.g. for the case
        // "class C { char *@s; };", where '@' denotes the text cursor position.
        auto declarator = path.at(n - i++)->asDeclarator();
        if (!declarator) {
            --i;
            if (path.at(n - i++)->asPointer()) {
                if (n < 7)
                    return;
                declarator = path.at(n - i++)->asDeclarator();
            }
            if (!declarator)
                return;
        }
        const auto variableDecl = path.at(n - i++)->asSimpleDeclaration();
        const auto classSpecifier = path.at(n - i++)->asClassSpecifier();
        const auto classDecl = path.at(n - i++)->asSimpleDeclaration();

        if (!(variableNameAST && declaratorId && variableDecl && classSpecifier && classDecl))
            return;

        // Do not get triggered on member functconstions and arrays
        if (declarator->postfix_declarator_list) {
            return;
        }

        // Construct getter and setter names
        const Name *variableName = variableNameAST->name;
        if (!variableName) {
            return;
        }
        const Identifier *variableId = variableName->identifier();
        if (!variableId) {
            return;
        }
        existing.memberVariableName = QString::fromUtf8(variableId->chars(), variableId->size());

        // Find the right symbol (for typeName) in the simple declaration
        Symbol *symbol = nullptr;
        const List<Symbol *> *symbols = variableDecl->symbols;
        QTC_ASSERT(symbols, return );
        for (; symbols; symbols = symbols->next) {
            Symbol *s = symbols->value;
            if (const Name *name = s->name()) {
                if (const Identifier *id = name->identifier()) {
                    const QString symbolName = QString::fromUtf8(id->chars(), id->size());
                    if (symbolName == existing.memberVariableName) {
                        symbol = s;
                        break;
                    }
                }
            }
        }
        if (!symbol) {
            // no type can be determined
            return;
        }
        if (!symbol->asDeclaration()) {
            return;
        }
        Class *const clazz = classSpecifier->symbol;
        if (!clazz)
            return;

        const ReadWith readWith = std::make_shared<CppQuickFixInterface>(interface);
        existing.declaredType = readTypeOfTheMember(interface, readWith, symbol->type(),
                                                   symbol->enclosingScope(),
                                                   symbol->line(), symbol->column());
        existing.toDefine = declarationToDefine(symbol, CppRefactoringChanges(interface.snapshot()));
        existing.clazz = readTheClass(interface, readWith, clazz);

        auto file = interface.currentFile();
        // check if a Q_PROPERTY exist
        const QString baseName = CppQuickFixSettings::memberBaseName(existing.memberVariableName);
        // eg: we have 'int m_test' and now 'Q_PROPERTY(int foo WRITE setTest MEMBER m_test NOTIFY tChanged)'
        for (auto it = classSpecifier->member_specifier_list; it; it = it->next) {
            if (it->value->asQtPropertyDeclaration()) {
                auto propDecl = it->value->asQtPropertyDeclaration();
                // iterator over 'READ ...', ...
                auto p = propDecl->property_declaration_item_list;
                // first check, if we have a MEMBER and the member is equal to the baseName
                for (; p; p = p->next) {
                    const char *tokenString = file->tokenAt(p->value->item_name_token).spell();
                    if (!qstrcmp(tokenString, "MEMBER")) {
                        if (baseName == file->textOf(p->value->expression))
                            return;
                    }
                }
                // no MEMBER, but maybe the property name is the same
                const QString propertyName = file->textOf(propDecl->property_name);
                // we compare the baseName. e.g. 'test' instead of 'm_test'
                if (propertyName == baseName)
                    return; // TODO Maybe offer quick fix "Add missing Q_PROPERTY Members"
            }
        }

        findExistingFunctions(existing, toStringList(getMemberFunctions(clazz)));
        existing.qPropertyName = CppQuickFixSettings::memberBaseName(existing.memberVariableName);

        const int possibleFlags = existing.computePossibleFlags();
        GenerateGetterSetterOp::generateQuickFixes(result, interface, existing, possibleFlags);
    }
};

//! Adds getter and setter functions for several member variables
class GenerateGettersSettersForClass : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const auto op = QSharedPointer<GenerateGettersSettersOperation>::create(interface);
        if (!op->isApplicable())
            return;
        if (testMode()) {
            GetterSetterCandidates candidates = op->candidates();
            for (MemberInfo &mi : candidates) {
                mi.requestedFlags = mi.possibleFlags;
                mi.requestedFlags &= ~GenerateFlag::GenerateConstantProperty;
            }
            op->setGetterSetterData(candidates);
        }
        result << op;
    }
};

//! Adds missing members for a Q_PROPERTY
class InsertQtPropertyMembers : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        ExistingGetterSetterData existing;

        // What the property says, read by whichever front end is running.
        const WrittenProperty written = writtenPropertyAt(interface);
        if (!written.isValid())
            return;

        // The built-in tree is still what says where a definition of the
        // member goes: a property declares nothing, so one is made to
        // stand for the member about to be written, and that is a symbol.
        const QList<AST *> &path = interface.path();
        AST *const ast = path.isEmpty() ? nullptr : path.last();
        QtPropertyDeclarationAST * const qtPropertyDeclaration
            = ast ? ast->asQtPropertyDeclaration() : nullptr;
        if (!qtPropertyDeclaration)
            return;

        ClassSpecifierAST *klass = nullptr;
        for (int i = path.size() - 2; i >= 0; --i) {
            klass = path.at(i)->asClassSpecifier();
            if (klass)
                break;
        }
        if (!klass)
            return;
        Class *const clazz = klass->symbol;

        const ReadWith readWith = std::make_shared<CppQuickFixInterface>(interface);
        existing.clazz = readTheClass(interface, readWith, clazz);

        CppRefactoringFilePtr file = interface.currentFile();
        existing.qPropertyName = written.name;
        existing.getterName = written.getterName;
        existing.setterName = written.setterName;
        existing.resetName = written.resetName;
        existing.signalName = written.signalName;
        existing.memberVariableName = written.memberVariableName;
        existing.bindableName = written.bindableName;

        Control *control = interface.currentFile()->cppDocument()->control();

        // A property declares no member, so one is made to stand for the
        // member that is about to be written -- where the property is
        // written, which is where its definition is looked for from.
        Declaration *const declaration
            = control->newDeclaration(ast->firstToken(),
                                      qtPropertyDeclaration->property_name->name);
        declaration->setVisibility(Symbol::Private);
        declaration->setEnclosingScope(clazz);
        existing.toDefine = declarationToDefine(declaration,
                                                CppRefactoringChanges(interface.snapshot()));

        {
            // create a 'right' Type Object
            // if we have Q_PROPERTY(int test ...) then we only get a NamedType for 'int', but we want
            // a IntegerType. So create a new dummy file with a dummy declaration to get the right
            // object
            const QByteArray type = written.type.toUtf8();
            QByteArray newSource = file->document()
                                       ->toPlainText()
                                       .insert(written.startPosition,
                                               QString::fromUtf8(type + " __dummy;\n"))
                                       .toUtf8();

            Document::Ptr doc = interface.snapshot().preprocessedDocument(newSource, "___quickfix.h");
            if (!doc->parse(Document::ParseTranslationUnit))
                return;
            doc->check();
            class TypeFinder : public ASTVisitor
            {
            public:
                FullySpecifiedType type;
                TypeFinder(TranslationUnit *u)
                    : ASTVisitor(u)
                {}
                bool visit(SimpleDeclarationAST *ast) override
                {
                    if (ast->symbols && !ast->symbols->next) {
                        const Name *name = ast->symbols->value->name();
                        if (name && name->asNameId() && name->asNameId()->identifier()) {
                            const Identifier *id = name->asNameId()->identifier();
                            if (QString::fromUtf8(id->chars(), id->size()) == "__dummy")
                                type = ast->symbols->value->type();
                        }
                    }
                    return true;
                }
            };
            TypeFinder finder(doc->translationUnit());
            finder.accept(doc->translationUnit()->ast());
            if (finder.type.type()->isUndefinedType())
                return;
            declaration->setType(finder.type);
            existing.declaredType = GeneratedType::readBuiltin(finder.type, clazz, readWith);
            existing.doc = doc; // to hold type
        }
        // check which methods are already there
        const bool haveFixMemberVariableName = !existing.memberVariableName.isEmpty();
        int generateFlags = GenerateFlag::GenerateMemberVariable;
        if (!existing.resetName.isEmpty())
            generateFlags |= GenerateFlag::GenerateReset;
        if (!existing.setterName.isEmpty())
            generateFlags |= GenerateFlag::GenerateSetter;
        if (!existing.getterName.isEmpty())
            generateFlags |= GenerateFlag::GenerateGetter;
        if (!existing.signalName.isEmpty())
            generateFlags |= GenerateFlag::GenerateSignal;
        if (!existing.bindableName.isEmpty())
            generateFlags |= GenerateFlag::GenerateBindable;
        Overview overview;
        for (int i = 0; i < clazz->memberCount(); ++i) {
            Symbol *member = clazz->memberAt(i);
            FullySpecifiedType type = member->type();
            if (member->asFunction() || (type.isValid() && type->asFunctionType())) {
                const QString name = overview.prettyName(member->name());
                if (name == existing.getterName)
                    generateFlags &= ~GenerateFlag::GenerateGetter;
                else if (name == existing.setterName)
                    generateFlags &= ~GenerateFlag::GenerateSetter;
                else if (name == existing.resetName)
                    generateFlags &= ~GenerateFlag::GenerateReset;
                else if (name == existing.signalName)
                    generateFlags &= ~GenerateFlag::GenerateSignal;
                else if (name == existing.bindableName)
                    generateFlags &= ~GenerateFlag::GenerateBindable;
            } else if (member->asDeclaration()) {
                const QString name = overview.prettyName(member->name());
                if (haveFixMemberVariableName) {
                    if (name == existing.memberVariableName) {
                        generateFlags &= ~GenerateFlag::GenerateMemberVariable;
                    }
                } else {
                    const QString baseName = CppQuickFixSettings::memberBaseName(name);
                    if (existing.qPropertyName == baseName) {
                        existing.memberVariableName = name;
                        generateFlags &= ~GenerateFlag::GenerateMemberVariable;
                    }
                }
            }
        }
        if (generateFlags & GenerateFlag::GenerateMemberVariable) {
            CppQuickFixSettings *settings = cppQuickFixSettingsForProject(
                ProjectExplorer::ProjectTree::currentProject());
            existing.memberVariableName = settings->getMemberVariableName(existing.qPropertyName);
        }
        if (generateFlags == 0) {
            // everything is already there
            return;
        }
        generateFlags |= GenerateFlag::HaveExistingQProperty;
        GenerateGetterSetterOp::generateQuickFixes(result, interface, existing, generateFlags);
    }
};


#ifdef WITH_TESTS
using namespace Tests;

class GenerateGetterSetterTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT

public:
    using CppQuickFixTestObject::CppQuickFixTestObject;

private:
    void modifySettings(QuickFixSettings &s, const QVariantMap &, const QByteArray &dataTag) override
    {
        if (dataTag == "function-as-template-arg") {
            s->getterOutsideClassFrom = 0;
            s->getterInCppFileFrom = 0;
            s->returnByConstRef = true;
        } else if (dataTag == "Only setter in header file with include guard") {
            s->setterOutsideClassFrom = 1;
            s->setterParameterNameTemplate = "\"value\"";
        } else if (dataTag == "inline") {
            s->setterOutsideClassFrom = 1;
            s->getterOutsideClassFrom = 1;
            s->setterParameterNameTemplate = "\"value\"";
        } else if (dataTag == "anonymous-class") {
            s->setterInCppFileFrom = 1;
            s->getterNameTemplate = "name";
            s->setterParameterNameTemplate = "\"value\"";
        } else if (dataTag == "only-setter") {
            s->setterInCppFileFrom = 1;
            s->setterParameterNameTemplate = "\"value\"";
        } else if (dataTag == "only-getter") {
            s->getterInCppFileFrom = 1;
        } else if (dataTag == "this-required") {
            s->setterParameterNameTemplate = "name";
            s->setterInCppFileFrom = 0;
        } else if (dataTag == "custom-template") {
            s->cppFileNamespaceHandling
                    = CppQuickFixSettings::MissingNamespaceHandling::AddUsingDirective;
            s->getterInCppFileFrom = 1;
            s->signalWithNewValue = true;
            CppQuickFixSettings::CustomTemplate t;
            t.types.append("custom");
            t.equalComparison = "<cur>.equals(<new>)";
            t.returnExpression = "<cur>.get()";
            t.returnType = "<T> *";
            t.assignment = "<cur>.assign(<new>)";
            s->customTemplates.push_back(t);
        } else if (dataTag.startsWith("value-types")) {
            s->setterInCppFileFrom = 0;
            s->getterInCppFileFrom = 0;
            s->valueTypes << "Value";
            s->returnByConstRef = true;
        } else if (dataTag.startsWith("custom names")) {
            s->setterInCppFileFrom = 0;
            s->getterInCppFileFrom = 0;
            s->setterNameTemplate =  R"js("Seet_" + name[0].toUpperCase() + name.slice(1))js";
            s->getterNameTemplate = R"js("give_me_" + name.replace(/([A-Z])/g,
                function(v) { return "_" + v.toLowerCase(); }))js";
            s->signalNameTemplate = R"js("new" + name[0].toUpperCase()
                + name.slice(1).replace(/_([a-z])/g, function(v) { return v.slice(1).toUpperCase(); })
                + "Value")js";
            s->setterParameterNameTemplate = R"js("New_" + name[0].toUpperCase()
                + name.slice(1).replace(/([A-Z])/g,
                    "_" + "$1").replace(/(_[a-z])/g, function(v) { return v.toUpperCase(); }))js";
            s->resetNameTemplate = R"js("set_" + name.replace(/_([a-z])/g,
                function(v) { return v.slice(1).toUpperCase(); }) + "_toDefault")js";
            s->memberVariableNameTemplate = R"js("mem_" + name)js";
        } else if (dataTag.contains("qualify")) {
            s->cppFileNamespaceHandling = CppQuickFixSettings::MissingNamespaceHandling::RewriteType;
            s->setterParameterNameTemplate = "\"value\"";
            s->setterInCppFileFrom = 1;
        } else if (dataTag.startsWith("namespace-handling-add-using")) {
            s->cppFileNamespaceHandling
                    = CppQuickFixSettings::MissingNamespaceHandling::AddUsingDirective;
            s->setterParameterNameTemplate = "\"value\"";
            s->setterInCppFileFrom = 1;
        } else if (dataTag.startsWith("namespace-handling")) {
            s->cppFileNamespaceHandling
                    = CppQuickFixSettings::MissingNamespaceHandling::CreateMissing;
            s->setterParameterNameTemplate = "\"value\"";
            s->setterInCppFileFrom = 1;
            s->getterInCppFileFrom = 1;
        } else if (dataTag.startsWith("general-")) {
            s->getterNameTemplate = "name";
            s->setterParameterNameTemplate = "name";
            s->getterInCppFileFrom = 1;
            s->setterInCppFileFrom = 1;
        }
    }
};

class GenerateGettersSettersForClassTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
private:
    void modifySettings(QuickFixSettings &s, const QVariantMap &, const QByteArray &) override
    {
        s->setterParameterNameTemplate = "\"value\"";
        s->setterOutsideClassFrom = 1;
        s->getterOutsideClassFrom = 1;
        s->returnByConstRef = true;
    }
};

class InsertQtPropertyMembersTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT

public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
private:
    void modifySettings(QuickFixSettings &s, const QVariantMap &, const QByteArray &dataTag) override
    {
        if (dataTag.startsWith("custom names")) {
            s->setterInCppFileFrom = 0;
            s->getterInCppFileFrom = 0;
            s->setterNameTemplate =  R"js("Seet_" + name[0].toUpperCase() + name.slice(1))js";
            s->getterNameTemplate = R"js("give_me_" + name.replace(/([A-Z])/g,
                function(v) { return "_" + v.toLowerCase(); }))js";
            s->signalNameTemplate = R"js("new" + name[0].toUpperCase()
                + name.slice(1).replace(/_([a-z])/g, function(v) { return v.slice(1).toUpperCase(); })
                + "Value")js";
            s->setterParameterNameTemplate = R"js("New_" + name[0].toUpperCase()
                + name.slice(1).replace(/([A-Z])/g,
                    "_" + "$1").replace(/(_[a-z])/g, function(v) { return v.toUpperCase(); }))js";
            s->resetNameTemplate = R"js("set_" + name.replace(/_([a-z])/g,
                function(v) { return v.slice(1).toUpperCase(); }) + "_toDefault")js";
            s->memberVariableNameTemplate = R"js("mem_" + name)js";
            if (dataTag == "custom names: create methods with given member variable")
                s->nameFromMemberVariableTemplate = R"js(name.slice(4))js";
        } else {
            s->setterAsSlot = true;
            s->setterInCppFileFrom = 0;
            s->setterParameterNameTemplate = "name";
            s->signalWithNewValue = true;
        }
    }
};

class GenerateConstructorTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT

public:
    using CppQuickFixTestObject::CppQuickFixTestObject;

private:
    void modifySettings(
            QuickFixSettings &s, const QVariantMap &properties, const QByteArray &) override
    {
        s->valueTypes << "CustomType";
        const auto location = static_cast<ConstructorLocation>(
                    properties.value("constructor-location").toInt());
        switch (location) {
        case ConstructorLocation::Inside:
            s->setterInCppFileFrom = -1;
            s->setterOutsideClassFrom = -1;
            break;
        case ConstructorLocation::Outside:
            s->setterInCppFileFrom = -1;
            s->setterOutsideClassFrom = 1;
            break;
        default:
            s->setterInCppFileFrom = 1;
            s->setterOutsideClassFrom = -1;
            using Handling = CppQuickFixSettings::MissingNamespaceHandling;
            if (location == ConstructorLocation::CppGenNamespace)
                s->cppFileNamespaceHandling = Handling::CreateMissing;
            else if (location == ConstructorLocation::CppGenUsingDirective)
                s->cppFileNamespaceHandling = Handling::AddUsingDirective;
            else if (location == ConstructorLocation::CppRewriteType)
                s->cppFileNamespaceHandling = Handling::RewriteType;
        }
    }

    enum ConstructorLocation {
        Inside,
        Outside,
        CppGenNamespace,
        CppGenUsingDirective,
        CppRewriteType
    };
};

#endif // WITH_TESTS

} // namespace

void registerCodeGenerationQuickfixes()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(GenerateGetterSetter);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(GenerateGettersSettersForClass);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(GenerateConstructor);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(InsertQtPropertyMembers);
}

} // namespace CppEditor::Internal

#include <cppcodegenerationquickfixes.moc>
