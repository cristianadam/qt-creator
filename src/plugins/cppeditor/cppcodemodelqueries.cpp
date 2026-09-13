// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodemodelqueries.h"

#include "symbolfinder.h"

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

} // namespace

WrittenClass classUsingClass(const Snapshot &snapshot, const FilePath &filePath,
                             const QString &className, int maxIncludeDepth)
{
    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc)
        return {};

    const LookupContext context(doc, snapshot);
    if (const Class * const klass = classUsing(doc->globalNamespace(), context, className)) {
        return {Overview().prettyName(klass->name()), filePath, klass->line(), klass->column()};
    }
    if (maxIncludeDepth <= 0)
        return {};

    for (const FilePath &include : doc->includedFiles()) {
        const WrittenClass found = classUsingClass(snapshot, include, className,
                                                   maxIncludeDepth - 1);
        if (found.isValid())
            return found;
    }
    return {};
}

QList<WrittenFunction> memberFunctionsOf(const Snapshot &snapshot, const WrittenClass &klass)
{
    const Class * const found = classWrittenAt(snapshot.document(klass.filePath),
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

DeclarationToDefine declarationToDefineAt(const CppRefactoringChanges &changes,
                                          const FilePath &filePath, int line, int column)
{
    const CppRefactoringFilePtr file = changes.cppFile(filePath);
    Function * const function = functionWrittenAt(file ? file->cppDocument() : Document::Ptr(),
                                                  line, column);
    return function ? declarationToDefine(function, changes) : DeclarationToDefine();
}

Link definitionOfFunctionAt(const Snapshot &snapshot, const FilePath &filePath,
                            int line, int column)
{
    Function * const function = functionWrittenAt(snapshot.document(filePath), line, column);
    if (!function)
        return {};

    SymbolFinder symbolFinder;
    const Function * const definition = symbolFinder.findMatchingDefinition(function, snapshot,
                                                                            true);
    if (!definition)
        return {};
    return {FilePath::fromUtf8(definition->fileName()), definition->line(), definition->column()};
}

} // namespace CppEditor
