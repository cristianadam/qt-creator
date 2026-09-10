// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppoutlinemodel.h"

#include "cxxfrontendmodel.h"

#include <cplusplus/Icons.h>
#include <cplusplus/Overview.h>
#include <cplusplus/Scope.h>
#include <cplusplus/Symbols.h>

#include <utils/dropsupport.h>
#include <utils/link.h>
#include <utils/theme/theme.h>

#include <QTimer>

using namespace CPlusPlus;
namespace CppEditor::Internal {

// What the model asks of an entry whatever built it. The built-in front end
// hands out symbols and the cxx-frontend model hands out a description, and
// the three questions below are all the outline asks besides what to draw.
class OutlineItem : public Utils::TreeItem
{
public:
    virtual bool isGenerated() const { return false; }
    virtual Utils::Link link() const { return {}; }
    virtual Utils::Text::Position position() const { return {}; }
};

class SymbolItem : public OutlineItem
{
public:
    SymbolItem() = default;
    explicit SymbolItem(CPlusPlus::Symbol *symbol) : symbol(symbol) {}

    bool isGenerated() const override { return symbol && symbol->isGenerated(); }
    Utils::Link link() const override { return symbol ? symbol->toLink() : Utils::Link(); }
    Utils::Text::Position position() const override
    {
        if (!symbol)
            return {};
        return {int(symbol->line()), int(symbol->column()) - 1};
    }

    QVariant data(int column, int role) const override
    {
        Q_UNUSED(column)

        if (!symbol && parent()) { // account for no symbol item
            switch (role) {
            case Qt::DisplayRole:
                if (parent()->childCount() > 1)
                    return QString(QT_TRANSLATE_NOOP("QtC::CppEditor", "<Select Symbol>"));
                return QString(QT_TRANSLATE_NOOP("QtC::CppEditor", "<No Symbols>"));
            default:
                return QVariant();
            }
        }

        auto outlineModel = qobject_cast<const OutlineModel*>(model());
        if (!symbol || !outlineModel)
            return QVariant();

        switch (role) {
        case Qt::DisplayRole: {
            QString name = outlineModel->m_overview.prettyName(symbol->name());
            if (name.isEmpty())
                name = QLatin1String("anonymous");
            if (symbol->asObjCForwardClassDeclaration())
                name = QLatin1String("@class ") + name;
            if (symbol->asObjCForwardProtocolDeclaration() || symbol->asObjCProtocol())
                name = QLatin1String("@protocol ") + name;
            if (symbol->asObjCClass()) {
                ObjCClass *clazz = symbol->asObjCClass();
                if (clazz->isInterface())
                    name = QLatin1String("@interface ") + name;
                else
                    name = QLatin1String("@implementation ") + name;

                if (clazz->isCategory()) {
                    name += QString(" (%1)").arg(
                        outlineModel->m_overview.prettyName(clazz->categoryName()));
                }
            }
            if (symbol->asObjCPropertyDeclaration())
                name = QLatin1String("@property ") + name;
            // if symbol is a template we might change it now - so, use a copy instead as we're const
            Symbol *symbl = symbol;
            if (Template *t = symbl->asTemplate())
                if (Symbol *templateDeclaration = t->declaration()) {
                    QStringList parameters;
                    parameters.reserve(t->templateParameterCount());
                    for (int i = 0; i < t->templateParameterCount(); ++i) {
                        parameters.append(outlineModel->m_overview.prettyName(
                                              t->templateParameterAt(i)->name()));
                    }
                    name += QString("<%1>").arg(parameters.join(QLatin1String(", ")));
                    symbl = templateDeclaration;
                }
            if (symbl->asObjCMethod()) {
                ObjCMethod *method = symbl->asObjCMethod();
                if (method->isStatic())
                    name = QLatin1Char('+') + name;
                else
                    name = QLatin1Char('-') + name;
            } else if (! symbl->asScope() || symbl->asFunction()) {
                QString type = outlineModel->m_overview.prettyType(symbl->type());
                if (Function *f = symbl->type()->asFunctionType()) {
                    name += type;
                    type = outlineModel->m_overview.prettyType(f->returnType());
                }
                if (! type.isEmpty())
                    name += QLatin1String(": ") + type;
            }
            return name;
        }

        case Qt::EditRole: {
            QString name = outlineModel->m_overview.prettyName(symbol->name());
            if (name.isEmpty())
                name = QLatin1String("anonymous");
            return name;
        }

        case Qt::ForegroundRole: {
            const auto isFwdDecl = [&] {
                const FullySpecifiedType type = symbol->type();
                if (type->asForwardClassDeclarationType())
                    return true;
                if (const Template * const tmpl = type->asTemplateType())
                    return tmpl->declaration() && tmpl->declaration()->asForwardClassDeclaration();
                if (type->asObjCForwardClassDeclarationType())
                    return true;
                if (type->asObjCForwardProtocolDeclarationType())
                    return true;
                return false;
            };
            if (isFwdDecl())
                return Utils::creatorColor(Utils::Theme::TextColorDisabled);
            return TreeItem::data(column, role);
        }

        case Qt::DecorationRole:
            return Icons::iconForSymbol(symbol);

        case OutlineModel::FileNameRole:
            return QString::fromUtf8(symbol->fileName(), symbol->fileNameLength());

        case OutlineModel::LineNumberRole:
            return symbol->line();

        default:
            return QVariant();
        } // switch
    }

    CPlusPlus::Symbol *symbol = nullptr; // not owned
};

// The same item over what the cxx-frontend model says about a declaration.
// It says it in pieces -- the name, a function's parameter list, the type
// after the colon, the icon -- so drawing it is putting them together, and
// the composing above is the same rule read the other way round.
class CxxFrontendSymbolItem : public OutlineItem
{
public:
    CxxFrontendSymbolItem(const CxxFrontendOutlineEntry &entry, const Utils::FilePath &filePath)
        : m_entry(entry)
        , m_filePath(filePath)
    {}

    bool isGenerated() const override { return m_entry.isGenerated; }
    Utils::Link link() const override
    {
        return Utils::Link(m_filePath, m_entry.line, m_entry.column - 1);
    }
    Utils::Text::Position position() const override
    {
        return {m_entry.line, m_entry.column - 1};
    }

    QVariant data(int column, int role) const override
    {
        switch (role) {
        case Qt::DisplayRole: {
            QString name = m_entry.name.isEmpty() ? QLatin1String("anonymous") : m_entry.name;
            name += m_entry.signature;
            if (!m_entry.valueType.isEmpty())
                name += QLatin1String(": ") + m_entry.valueType;
            return name;
        }
        case Qt::EditRole:
            return m_entry.name.isEmpty() ? QLatin1String("anonymous") : m_entry.name;
        case Qt::ForegroundRole:
            if (m_entry.isForwardDeclaration)
                return Utils::creatorColor(Utils::Theme::TextColorDisabled);
            return TreeItem::data(column, role);
        case Qt::DecorationRole:
            return Utils::CodeModelIcon::iconForType(m_entry.icon);
        case OutlineModel::FileNameRole:
            return m_filePath.toUrlishString();
        case OutlineModel::LineNumberRole:
            return unsigned(m_entry.line);
        default:
            return QVariant();
        }
    }

private:
    const CxxFrontendOutlineEntry m_entry;
    const Utils::FilePath m_filePath;
};

int OutlineModel::globalSymbolCount() const
{
    int count = 0;
    if (m_cppDocument)
        count += m_cppDocument->globalSymbolCount();
    return count;
}

Symbol *OutlineModel::globalSymbolAt(int index) const
{ return m_cppDocument->globalSymbolAt(index); }

Symbol *OutlineModel::symbolFromIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    auto item = static_cast<const SymbolItem*>(itemForIndex(index));
    return item ? item->symbol : nullptr;
}

OutlineModel::OutlineModel(QObject *parent)
    : Utils::TreeModel<>(parent)
{
    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(500);
    connect(m_updateTimer, &QTimer::timeout, this, &OutlineModel::rebuild);
}

Qt::ItemFlags OutlineModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

Qt::DropActions OutlineModel::supportedDragActions() const
{
    return Qt::MoveAction;
}

QStringList OutlineModel::mimeTypes() const
{
    return Utils::DropSupport::mimeTypesForFilePaths();
}

QMimeData *OutlineModel::mimeData(const QModelIndexList &indexes) const
{
    auto mimeData = new Utils::DropMimeData;
    for (const QModelIndex &index : indexes) {
        const QVariant fileName = data(index, FileNameRole);
        if (!fileName.canConvert<QString>())
            continue;
        const QVariant lineNumber = data(index, LineNumberRole);
        if (!lineNumber.canConvert<unsigned>())
            continue;
        mimeData->addFile(Utils::FilePath::fromVariant(fileName),
                          static_cast<int>(lineNumber.value<unsigned>()));
    }
    return mimeData;
}

void OutlineModel::update(CPlusPlus::Document::Ptr doc)
{
    m_candidate = doc;
    m_updateTimer->start();
}

int OutlineModel::editorRevision()
{
    return m_cppDocument ? m_cppDocument->editorRevision() : -1;
}

void OutlineModel::rebuild()
{
    beginResetModel();
    m_cppDocument = m_candidate;
    m_candidate.reset();
    auto root = new SymbolItem;
    if (m_cppDocument && !buildTreeFromCxxFrontend(root))
        buildTree(root, true);
    setRootItemInternal(root);
    endResetModel();
}

// The tree as the cxx-frontend model describes it, where it has this file.
// False otherwise, and the built-in walk draws it as before. A file's own
// structure is what one document settles, so this is the whole of the
// question rather than a part of it -- see cxxfrontendmodel.h.
bool OutlineModel::buildTreeFromCxxFrontend(SymbolItem *root)
{
#ifdef QTC_WITH_CXX_FRONTEND
    const std::optional<QList<CxxFrontendOutlineEntry>> outline
        = cxxFrontendOutline(m_cppDocument->filePath());
    if (!outline)
        return false;

    // An entry always follows the one it is inside, so the item to hang each
    // one on has been made by the time it is read.
    QList<Utils::TreeItem *> items;
    items.reserve(outline->size());
    for (const CxxFrontendOutlineEntry &entry : *outline) {
        auto item = new CxxFrontendSymbolItem(entry, m_cppDocument->filePath());
        items.append(item);
        if (entry.parent >= 0 && entry.parent < items.size() - 1)
            items.at(entry.parent)->appendChild(item);
        else
            root->appendChild(item);
    }
    root->prependChild(new SymbolItem); // account for no symbol item
    return true;
#else
    Q_UNUSED(root)
    return false;
#endif
}

bool OutlineModel::isGenerated(const QModelIndex &sourceIndex) const
{
    const auto item = static_cast<const OutlineItem *>(itemForIndex(sourceIndex));
    return item && item->isGenerated();
}

Utils::Link OutlineModel::linkFromIndex(const QModelIndex &sourceIndex) const
{
    const auto item = static_cast<const OutlineItem *>(itemForIndex(sourceIndex));
    return item ? item->link() : Utils::Link();
}

Utils::Text::Position OutlineModel::positionFromIndex(const QModelIndex &sourceIndex) const
{
    const auto item = static_cast<const OutlineItem *>(itemForIndex(sourceIndex));
    return item ? item->position() : Utils::Text::Position();
}

Utils::Text::Range OutlineModel::rangeFromIndex(const QModelIndex &sourceIndex) const
{
    Utils::Text::Position lineColumn = positionFromIndex(sourceIndex);
    return {lineColumn, lineColumn};
}

void OutlineModel::buildTree(SymbolItem *root, bool isRoot)
{
    if (!root)
        return;

    if (isRoot) {
        int rows = globalSymbolCount();
        for (int row = 0; row < rows; ++row) {
            Symbol *symbol = globalSymbolAt(row);
            auto currentItem = new SymbolItem(symbol);
            buildTree(currentItem, false);
            root->appendChild(currentItem);
        }
        root->prependChild(new SymbolItem); // account for no symbol item
    } else {
        Symbol *symbol = root->symbol;
        if (Scope *scope = symbol->asScope()) {
            Scope::iterator it = scope->memberBegin();
            Scope::iterator end = scope->memberEnd();
            for ( ; it != end; ++it) {
                if (!((*it)->name()))
                    continue;
                if ((*it)->asArgument())
                    continue;
                auto currentItem = new SymbolItem(*it);
                buildTree(currentItem, false);
                root->appendChild(currentItem);
            }
        }
    }
}

QModelIndex OutlineModel::indexForPosition(
    const Utils::Text::Position &pos, const QModelIndex &rootIndex) const
{
    QModelIndex lastIndex = rootIndex;
    const int rowCount = this->rowCount(rootIndex);
    for (int row = 0; row < rowCount; ++row) {
        const QModelIndex index = this->index(row, 0, rootIndex);
        const Utils::Text::Range range = rangeFromIndex(index);
        if (range.begin.line > pos.line)
            break;
        // Skip ranges that do not include current line and column.
        if (range.end != range.begin && !range.contains(pos))
            continue;
        lastIndex = index;
    }

    if (lastIndex != rootIndex) {
        // recurse
        lastIndex = indexForPosition(pos, lastIndex);
    }

    return lastIndex;
}

} // namespace CppEditor::Internal
