// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodemodelqueries.h"

#include "symbolfinder.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"
#endif

#include <cplusplus/CppDocument.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/Overview.h>
#include <cplusplus/Symbols.h>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor {
namespace {

// The name \a name stands for where it is written, written out in full. A
// name nothing declares -- which is what a header that was never generated
// leaves behind -- is taken as it was written.
QString fullyQualifiedName(const LookupContext &context, const Name *name, Scope *scope)
{
    if (!name || !scope)
        return QString();

    const QList<LookupItem> items = context.lookup(name, scope);
    if (items.isEmpty())
        return Overview().prettyName(name);
    return Overview().prettyName(LookupContext::fullyQualifiedName(items.first().declaration()));
}

bool inherits(const Class *klass, const QString &baseClass)
{
    const Overview overview;
    for (int b = 0, count = klass->baseClassCount(); b < count; ++b) {
        if (overview.prettyName(klass->baseClassAt(b)->name()) == baseClass)
            return true;
    }
    return false;
}

// The first class in \a scope, or in a namespace inside it, that declares a
// member of type \a className or derives from it.
const Class *classUsing(const Scope *scope, const LookupContext &context,
                        const QString &className)
{
    for (int i = 0, count = scope->memberCount(); i < count; ++i) {
        Symbol * const member = scope->memberAt(i);
        const Class * const klass = member->asClass();
        if (!klass) {
            if (const Namespace * const ns = member->asNamespace()) {
                if (const Class * const found = classUsing(ns, context, className))
                    return found;
            }
            continue;
        }

        for (int j = 0, members = klass->memberCount(); j < members; ++j) {
            Declaration * const decl = klass->memberAt(j)->asDeclaration();
            if (!decl)
                continue;
            const NamedType *named = decl->type()->asNamedType();
            if (!named) {
                if (PointerType * const pointer = decl->type()->asPointerType())
                    named = pointer->elementType()->asNamedType();
            }
            if (!named)
                continue;
            if (fullyQualifiedName(context, named->name(), decl->enclosingScope()) == className)
                return klass;
        }

        if (inherits(klass, className))
            return klass;
    }
    return nullptr;
}

// The class whose own name stands at \a line and \a column.
Class *classWrittenAt(const Document::Ptr &doc, int line, int column)
{
    if (!doc)
        return nullptr;
    QList<const Scope *> scopes{doc->globalNamespace()};
    while (!scopes.isEmpty()) {
        const Scope * const scope = scopes.takeFirst();
        for (int i = 0, count = scope->memberCount(); i < count; ++i) {
            Symbol * const symbol = scope->memberAt(i);
            if (const Scope * const inner = symbol->asScope())
                scopes << inner;
            if (Class * const klass = symbol->asClass();
                klass && klass->line() == line && klass->column() == column) {
                return klass;
            }
        }
    }
    return nullptr;
}

// The function declared at \a line and \a column -- where its own name
// stands, which is where a front end records it.
Function *functionWrittenAt(const Document::Ptr &doc, int line, int column)
{
    if (!doc)
        return nullptr;
    QList<const Scope *> scopes{doc->globalNamespace()};
    while (!scopes.isEmpty()) {
        const Scope * const scope = scopes.takeFirst();
        for (int i = 0, count = scope->memberCount(); i < count; ++i) {
            Symbol * const symbol = scope->memberAt(i);
            if (const Scope * const inner = symbol->asScope())
                scopes << inner;
            if (symbol->line() != line || symbol->column() != column)
                continue;
            if (Function * const function = symbol->type()->asFunctionType())
                return function;
        }
    }
    return nullptr;
}

// What the built-in front end writes a function's name and parameter types
// as, which is how a member is told from another of the same name.
QString signatureOf(const Function *function)
{
    const Overview overview;
    QString signature = overview.prettyName(function->name()) + '(';
    for (int i = 0, count = function->argumentCount(); i < count; ++i) {
        if (i > 0)
            signature += ", ";
        signature += overview.prettyType(function->argumentAt(i)->asArgument()->type());
    }
    return signature + ')';
}

// What the built-in front end says, which is the answer wherever the
// cxx-frontend model has not read the file.

WrittenClass builtinClassUsingClass(const Snapshot &snapshot, const FilePath &filePath,
                                    const QString &className, int maxIncludeDepth)
{
    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc)
        return {};

    const LookupContext context(doc, snapshot);
    if (const Class * const klass = classUsing(doc->globalNamespace(), context, className))
        return {Overview().prettyName(klass->name()),
                Overview().prettyName(
                    LookupContext::fullyQualifiedName(const_cast<Class *>(klass))),
                filePath, klass->line(), klass->column()};
    if (maxIncludeDepth <= 0)
        return {};

    for (const FilePath &include : doc->includedFiles()) {
        const WrittenClass found = builtinClassUsingClass(snapshot, include, className,
                                                          maxIncludeDepth - 1);
        if (found.isValid())
            return found;
    }
    return {};
}

// Every class \a scope declares, nested ones included, in the order they
// are written.
void collectClasses(const Scope *scope, const FilePath &filePath, QList<WrittenClass> *into)
{
    const Overview overview;
    for (int i = 0, count = scope->memberCount(); i < count; ++i) {
        Symbol * const member = scope->memberAt(i);
        if (const Class * const klass = member->asClass()) {
            into->append({overview.prettyName(klass->name()),
                          overview.prettyName(LookupContext::fullyQualifiedName(member)),
                          filePath, klass->line(), klass->column()});
        }
        if (const Scope * const inner = member->asScope())
            collectClasses(inner, filePath, into);
    }
}

} // namespace

class CodeModelQueries::Private
{
public:
    Snapshot snapshot;
#ifdef QTC_WITH_CXX_FRONTEND
    Internal::CxxFrontendReading model;
#endif
};

CodeModelQueries::CodeModelQueries(const Snapshot &snapshot, const WorkingCopy &workingCopy)
#ifdef QTC_WITH_CXX_FRONTEND
    : d(new Private{snapshot, {snapshot, workingCopy}})
#else
    : d(new Private{snapshot})
#endif
{
    Q_UNUSED(workingCopy)
}

CodeModelQueries::~CodeModelQueries() = default;

WrittenClass CodeModelQueries::classUsingClass(const FilePath &filePath, const QString &className,
                                               int maxIncludeDepth) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // A header is read into whoever includes it, so one document holds every
    // file this walks -- which is why the depth is applied to the answers
    // rather than to the reading.
    if (const std::optional<QList<CxxFrontendDocument::ClassUsingAClass>> classes
        = d->model.classesUsing(filePath, className)) {
        FilePaths reachable{filePath};
        if (maxIncludeDepth > 0) {
            if (const Document::Ptr doc = d->snapshot.document(filePath))
                reachable += doc->includedFiles();
        }
        for (const FilePath &candidate : std::as_const(reachable)) {
            for (const CxxFrontendDocument::ClassUsingAClass &klass : *classes) {
                if (FilePath::fromUserInput(klass.place.filePath) != candidate)
                    continue;
                return {klass.name, klass.qualifiedName, candidate, klass.place.line,
                        klass.place.column};
            }
        }
        // Nothing found is not the same as nothing there: a type nothing
        // declares names no class on this model, and a ui header that has
        // not been generated yet is exactly that. The built-in front end
        // takes such a type for a class of the name that was written, and
        // where this model has no answer that one's is the answer.
    }
#endif
    return builtinClassUsingClass(d->snapshot, filePath, className, maxIncludeDepth);
}

QList<WrittenFunction> CodeModelQueries::memberFunctionsOf(const WrittenClass &klass) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QList<CxxFrontendDocument::MemberFunction>> members
        = d->model.memberFunctionsIn(klass.filePath, klass.filePath, klass.line, klass.column);
        members && !members->isEmpty()) {
        QList<WrittenFunction> functions;
        for (const CxxFrontendDocument::MemberFunction &member : *members) {
            functions << WrittenFunction{member.unqualifiedName, member.signature,
                                         FilePath::fromUserInput(member.filePath),
                                         member.line, member.column};
        }
        return functions;
    }
#endif

    const Class * const found = classWrittenAt(d->snapshot.document(klass.filePath),
                                               klass.line, klass.column);
    if (!found)
        return {};

    const Overview overview;
    QList<WrittenFunction> functions;
    for (int i = 0, count = found->memberCount(); i < count; ++i) {
        Symbol * const member = found->memberAt(i);
        const Declaration * const decl = member->asDeclaration();
        Function * const function = decl ? decl->type()->asFunctionType() : member->asFunction();
        if (!function)
            continue;
        functions << WrittenFunction{overview.prettyName(function->name()),
                                     signatureOf(function),
                                     klass.filePath,
                                     member->line(),
                                     member->column()};
    }
    return functions;
}

QList<WrittenClass> CodeModelQueries::classesDeclaredIn(const FilePath &filePath) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QList<CxxFrontendDocument::Symbol>> symbols
        = d->model.symbolsIn(filePath);
        symbols && !symbols->isEmpty()) {
        QList<WrittenClass> classes;
        for (const CxxFrontendDocument::Symbol &symbol : *symbols) {
            // A class named without its body declares nothing to say
            // anything about, and one a macro wrote stands nowhere.
            if (symbol.kind != CxxFrontendDocument::Kind::Class || symbol.isForwardDeclaration
                || symbol.isGenerated || symbol.name.isEmpty()) {
                continue;
            }
            QStringList path = symbol.qualified;
            path << symbol.name;
            classes.append({symbol.name, path.join("::"), filePath, symbol.line, symbol.column});
        }
        return classes;
    }
#endif

    const Document::Ptr doc = d->snapshot.document(filePath);
    if (!doc)
        return {};
    QList<WrittenClass> classes;
    collectClasses(doc->globalNamespace(), filePath, &classes);
    return classes;
}

DeclarationToDefine CodeModelQueries::declarationToDefineAt(const CppRefactoringChanges &changes,
                                                            const FilePath &filePath,
                                                            int line, int column) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<DeclarationToDefine> declaration
        = d->model.declarationToDefineIn(filePath, line, column);
        declaration && declaration->isValid()) {
        return *declaration;
    }
#endif

    const CppRefactoringFilePtr file = changes.cppFile(filePath);
    Function * const function = functionWrittenAt(file ? file->cppDocument() : Document::Ptr(),
                                                  line, column);
    return function ? declarationToDefine(function, changes) : DeclarationToDefine();
}

Link CodeModelQueries::definitionOfFunctionAt(const FilePath &filePath, int line, int column) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<Link> definition
        = d->model.definitionOfFunctionIn(filePath, line, column);
        definition && definition->hasValidTarget()) {
        return *definition;
    }
#endif

    Function * const function = functionWrittenAt(d->snapshot.document(filePath), line, column);
    if (!function)
        return {};

    SymbolFinder symbolFinder;
    const Function * const definition = symbolFinder.findMatchingDefinition(function, d->snapshot,
                                                                            true);
    if (!definition)
        return {};
    return {FilePath::fromUtf8(definition->fileName()), definition->line(), definition->column()};
}

} // namespace CppEditor
