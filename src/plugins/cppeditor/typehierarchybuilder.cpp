// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "typehierarchybuilder.h"

#include <cplusplus/LookupContext.h>
#include <cplusplus/SymbolVisitor.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "cppmodelmanager.h"
#include "cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendDocument.h>
#endif

#include <utils/algorithm.h>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

QString unqualifyName(const QString &qualifiedName)
{
    const int index = qualifiedName.lastIndexOf(QLatin1String("::"));
    if (index == -1)
        return qualifiedName;
    return qualifiedName.right(qualifiedName.size() - index - 2);
}

class DerivedHierarchyVisitor : public SymbolVisitor
{
public:
    explicit DerivedHierarchyVisitor(const QString &qualifiedName, QHash<QString, QHash<QString, QString>> &cache)
        : _qualifiedName(qualifiedName)
        , _unqualifiedName(unqualifyName(qualifiedName))
        , _cache(cache)
    {}

    void execute(const Document::Ptr &doc, const Snapshot &snapshot);

    bool visit(Class *) override;

    const QList<DerivedClass> &derived() { return _derived; }
    const QSet<QString> otherBases() { return _otherBases; }

private:
    Symbol *lookup(const Name *symbolName, Scope *enclosingScope);

    LookupContext _context;
    QString _qualifiedName;
    QString _unqualifiedName;
    Overview _overview;
    // full scope name to base symbol name to fully qualified base symbol name
    QHash<QString, QHash<QString, QString>> &_cache;
    QSet<QString> _otherBases;
    QList<DerivedClass> _derived;
};

void DerivedHierarchyVisitor::execute(const Document::Ptr &doc,
                                      const Snapshot &snapshot)
{
    _derived.clear();
    _otherBases.clear();
    _context = LookupContext(doc, snapshot);

    for (int i = 0; i < doc->globalSymbolCount(); ++i)
        accept(doc->globalSymbolAt(i));
}

bool DerivedHierarchyVisitor::visit(Class *symbol)
{
    const QList<const Name *> &fullScope
            = LookupContext::fullyQualifiedName(symbol->enclosingScope());
    const QString fullScopeName = _overview.prettyName(fullScope);

    for (int i = 0; i < symbol->baseClassCount(); ++i) {
        BaseClass *baseSymbol = symbol->baseClassAt(i);

        const QString &baseName = _overview.prettyName(baseSymbol->name());
        QString fullBaseName = _cache.value(fullScopeName).value(baseName);
        if (fullBaseName.isEmpty()) {
            Symbol *actualBaseSymbol = TypeHierarchyBuilder::followTypedef(_context,
                                       baseSymbol->name(), symbol->enclosingScope()).declaration();
            if (!actualBaseSymbol)
                continue;

            const QList<const Name *> &full
                    = LookupContext::fullyQualifiedName(actualBaseSymbol);
            fullBaseName = _overview.prettyName(full);
            _cache[fullScopeName].insert(baseName, fullBaseName);
        }

        if (_qualifiedName == fullBaseName) {
            _derived.append({_overview.prettyName(LookupContext::fullyQualifiedName(symbol)),
                             symbol->line(), symbol->column()});
        } else {
            _otherBases.insert(fullBaseName);
        }
    }
    return true;
}

} // namespace

TypeHierarchy::TypeHierarchy() = default;

TypeHierarchy::TypeHierarchy(Symbol *symbol) : _symbol(symbol)
{}

Symbol *TypeHierarchy::symbol() const
{
    return _symbol;
}

const QList<TypeHierarchy> &TypeHierarchy::hierarchy() const
{
    return _hierarchy;
}

// What the built-in front end says derives from a class: every class in the
// file, with each of its bases resolved -- through an alias where one was
// written -- and compared with the class being asked about.
static DerivedFinder builtinDerivedFinder(const Snapshot &snapshot)
{
    // Full scope name to base symbol name to fully qualified base symbol
    // name, and the other bases seen per file, which is what keeps a file
    // from being read again for a class it cannot name.
    struct Cache
    {
        QHash<QString, QHash<QString, QString>> bases;
        QHash<Utils::FilePath, QSet<QString>> otherBases;
    };
    const auto cache = std::make_shared<Cache>();

    return [snapshot, cache](const Utils::FilePath &filePath, const QString &qualifiedName) {
        const Document::Ptr doc = snapshot.document(filePath);
        if (!doc)
            return QList<DerivedClass>();
        if (cache->otherBases.contains(filePath)
            && !cache->otherBases.value(filePath).contains(qualifiedName)) {
            return QList<DerivedClass>();
        }

        DerivedHierarchyVisitor visitor(qualifiedName, cache->bases);
        visitor.execute(doc, snapshot);
        cache->otherBases.insert(filePath, visitor.otherBases());
        return visitor.derived();
    };
}

#ifdef QTC_WITH_CXX_FRONTEND

// The same question on the cxx-frontend model: what a file's classes
// inherit, read once per file and asked about every class the walk gets to.
//
// A base named through an alias needs no rule of its own here -- the parser
// resolved it -- and two classes of one name are told apart because what is
// compared is the path and not the name.
//
// A file this model cannot read is read by the other one. This search looks
// at every file that depends on the one declaring the class, so leaving one
// out would lose whatever derives from it there.
static DerivedFinder modelDerivedFinder(const Snapshot &snapshot,
                                        const DerivedFinder &builtinFinder)
{
    const auto read = std::make_shared<
        QHash<Utils::FilePath, std::optional<QList<CxxFrontendDocument::ClassWithBases>>>>();

    return [snapshot, builtinFinder, read](const Utils::FilePath &filePath,
                                           const QString &qualifiedName) {
        const auto known = read->constFind(filePath);
        if (known == read->constEnd()) {
            read->insert(filePath,
                         cxxFrontendClassesIn(CppModelManager::workingCopy(), filePath));
        }
        const std::optional<QList<CxxFrontendDocument::ClassWithBases>> &classes
            = read->value(filePath);
        if (!classes)
            return builtinFinder(filePath, qualifiedName);

        QList<DerivedClass> derived;
        for (const CxxFrontendDocument::ClassWithBases &written : *classes) {
            if (written.bases.contains(qualifiedName))
                derived.append({written.qualifiedName, written.place.line, written.place.column});
        }
        return derived;
    };
}

#endif // QTC_WITH_CXX_FRONTEND

// Which front end says what derives from a class.
static DerivedFinder derivedFinder(const Snapshot &snapshot)
{
    const DerivedFinder builtin = builtinDerivedFinder(snapshot);
#ifdef QTC_WITH_CXX_FRONTEND
    if (cxxFrontendModelRequested())
        return modelDerivedFinder(snapshot, builtin);
#endif
    return builtin;
}

TypeHierarchy TypeHierarchyBuilder::buildDerivedTypeHierarchy(Symbol *symbol,
              const Snapshot &snapshot, const std::optional<QFuture<void>> &future)
{
    TypeHierarchy hierarchy(symbol);
    TypeHierarchyBuilder builder(derivedFinder(snapshot));
    builder.buildDerived(future, &hierarchy, snapshot);
    return hierarchy;
}

LookupItem TypeHierarchyBuilder::followTypedef(const LookupContext &context, const Name *symbolName,
                                               Scope *enclosingScope,
                                               std::set<const Symbol *> typedefs)
{
    const QList<LookupItem> items = context.lookup(symbolName, enclosingScope);

    Symbol *actualBaseSymbol = nullptr;
    LookupItem matchingItem;

    for (const LookupItem &item : items) {
        Symbol *s = item.declaration();
        if (!s)
            continue;
        if (!s->asClass() && !s->asTemplate() && !s->isTypedef())
            continue;
        if (!typedefs.insert(s).second)
            continue;
        actualBaseSymbol = s;
        matchingItem = item;
        break;
    }

    if (!actualBaseSymbol)
        return LookupItem();

    if (actualBaseSymbol->isTypedef()) {
        NamedType *namedType = actualBaseSymbol->type()->asNamedType();
        if (!namedType) {
            // Anonymous aggregate such as: typedef struct {} Empty;
            return LookupItem();
        }
        return followTypedef(context, namedType->name(), actualBaseSymbol->enclosingScope(),
                             typedefs);
    }

    return matchingItem;
}

static FilePaths filesDependingOn(const Snapshot &snapshot, Symbol *symbol)
{
    if (!symbol)
        return {};

    const FilePath file = symbol->filePath();
    return FilePaths{file} + snapshot.filesDependingOn(file);
}

// The class written at \a line and \a column of \a document, which is how a
// place handed back by either front end becomes the symbol the hierarchy
// hands out.
static Class *classWrittenAt(const Document::Ptr &document, const DerivedClass &derived)
{
    class Find : public SymbolVisitor
    {
    public:
        Find(int line, int column) : _line(line), _column(column) {}

        bool preVisit(Symbol *symbol) override
        {
            if (_found)
                return false;
            if (Class * const cls = symbol->asClass();
                cls && cls->line() == _line && cls->column() == _column) {
                _found = cls;
                return false;
            }
            return true;
        }

        Class *found() const { return _found; }

    private:
        const int _line;
        const int _column;
        Class *_found = nullptr;
    } find(derived.line, derived.column);

    for (int i = 0; i < document->globalSymbolCount(); ++i)
        find.accept(document->globalSymbolAt(i));
    return find.found();
}

void TypeHierarchyBuilder::buildDerived(const std::optional<QFuture<void>> &future,
                                        TypeHierarchy *typeHierarchy,
                                        const Snapshot &snapshot)
{
    Symbol *symbol = typeHierarchy->_symbol;
    if (!Utils::insert(_visited, symbol))
        return;

    const QString &symbolName = _overview.prettyName(LookupContext::fullyQualifiedName(symbol));
    const FilePaths dependingFiles = filesDependingOn(snapshot, symbol);

    for (const FilePath &fileName : dependingFiles) {
        if (future && future->isCanceled())
            return;
        const Document::Ptr doc = snapshot.document(fileName);

        // A file that never wrote the name cannot name the class, which is
        // what keeps this from reading the project.
        if (!doc || !symbol->identifier()
            || !doc->control()->findIdentifier(symbol->identifier()->chars(),
                                               symbol->identifier()->size())) {
            continue;
        }

        for (const DerivedClass &derived : _finder(fileName, symbolName)) {
            Class * const derivedClass = classWrittenAt(doc, derived);
            if (!derivedClass)
                continue;
            TypeHierarchy derivedHierarchy(derivedClass);
            buildDerived(future, &derivedHierarchy, snapshot);
            if (future && future->isCanceled())
                return;
            typeHierarchy->_hierarchy.append(derivedHierarchy);
        }
    }
}

} // CppEditor::Internal
