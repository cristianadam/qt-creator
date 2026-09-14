// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodemodelqueries.h"

#include "symbolfinder.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"
#endif

#include <cplusplus/AST.h>
#include <cplusplus/ASTVisitor.h>
#include <cplusplus/CppDocument.h>
#include <cplusplus/ExpressionUnderCursor.h>
#include <cplusplus/Icons.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/Overview.h>
#include <cplusplus/Symbols.h>
#include <cplusplus/TypeOfExpression.h>

#include <utils/textutils.h>

#include <QTextCursor>
#include <QTextDocument>

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

// Everything \a scope declares, each entry saying what it is written
// inside. The rules are what "declares" means: a name the file only
// mentions is not one, and what a function writes inside itself is not
// either.
void collectDeclarations(const Scope *scope, const FilePath &filePath, int parent,
                         QList<WrittenDeclaration> *into)
{
    const Overview overview;
    for (int i = 0, count = scope->memberCount(); i < count; ++i) {
        Symbol * const member = scope->memberAt(i);
        if (!member)
            continue;

        if (member->asForwardClassDeclaration() || member->isExtern() || member->isFriend()
            || member->isGenerated() || member->asUsingNamespaceDirective()
            || member->asUsingDeclaration()) {
            continue;
        }

        // Written under a qualified name, which is a definition of something
        // declared where that name was given.
        if (member->name() && member->name()->asQualifiedNameId())
            continue;

        const int index = into->size();
        into->append({overview.prettyName(member->name()).trimmed(),
                      overview.prettyType(member->type()).trimmed(),
                      CPlusPlus::Icons::iconTypeForSymbol(member),
                      // The symbol's own file rather than the document's: a
                      // declaration stands where it was written.
                      member->filePath(),
                      member->line(),
                      member->column(),
                      parent,
                      member->asNamespace() != nullptr});

        if (const Scope * const inner = member->asScope(); inner && !member->asFunction())
            collectDeclarations(inner, filePath, index, into);
    }
}

#ifdef QTC_WITH_CXX_FRONTEND
// What would be written after the name, the way the built-in front end's
// prettyType writes it: a function's parameter list, the type of anything
// else, and for a scope its own name over again.
QString writtenTypeOf(const CxxFrontendDocument::Symbol &symbol)
{
    if (!symbol.signature.isEmpty())
        return symbol.signature;
    if (!symbol.valueType.isEmpty())
        return symbol.valueType;
    return symbol.name;
}
#endif


// The calls a file makes to one of several functions, with what each
// argument says where it is a literal, and the function each is written
// inside. A call written
// without its scopes counts where a using directive made it reachable, which
// is what the depth bookkeeping here is for: a directive is in force from
// where it is written to the end of the scope that holds it.
class CallsTo : protected ASTVisitor
{
public:
    CallsTo(const Document::Ptr &document, const QStringList &functionNames)
        : ASTVisitor(document->translationUnit())
        , m_document(document)
    {
        for (const QString &name : functionNames) {
            m_qualified.append(name);
            // The last part of it, and the whole of a name that has only
            // one part: lastIndexOf answers -1 where there is no "::", and
            // taking two off that cuts the first character away.
            const int afterTheScopes = name.lastIndexOf("::");
            if (afterTheScopes < 0) {
                // Asked for without scopes, so a call written without them
                // is the call: there is nothing for a directive to lend.
                m_plain.append(name);
            } else {
                m_lentByADirective.append(name.mid(afterTheScopes + 2));
            }
        }
        accept(document->translationUnit()->ast());
    }

    QList<CodeModelQueries::WrittenCall> calls() const { return m_calls; }

protected:
    bool preVisit(AST *ast) override
    {
        ++m_depth;

        // Where a using directive's reach ends: at the end of the block or
        // the namespace it stands in, which is the scope to measure against
        // rather than whatever node happens to hold the directive -- inside
        // a function that is one declaration statement, and a directive
        // written there would stop applying before the next line.
        if (ast->asCompoundStatement() || ast->asNamespace() || ast->asTranslationUnit()
            || ast->asLinkageBody()) {
            m_scopeDepths.append(m_depth);
        }
        return true;
    }

    void postVisit(AST *ast) override
    {
        if (ast->asCompoundStatement() || ast->asNamespace() || ast->asTranslationUnit()
            || ast->asLinkageBody()) {
            m_scopeDepths.removeLast();
        }
        --m_depth;
        m_reachableUnqualified &= m_depth >= m_usingDirectiveDepth;
        if (ast->asFunctionDefinition())
            m_insideFunction.clear();
    }

    bool visit(UsingDirectiveAST *ast) override
    {
        // Which namespace it names does not matter: the functions asked
        // about are named by their scopes, and a directive for another
        // namespace cannot make one of them reachable unqualified.
        if (ast->name && !m_scopeDepths.isEmpty()) {
            m_reachableUnqualified = true;
            m_usingDirectiveDepth = m_scopeDepths.last();
        }
        return true;
    }

    bool visit(FunctionDefinitionAST *ast) override
    {
        m_insideFunction = ast->symbol
                               ? Overview().prettyName(
                                     LookupContext::fullyQualifiedName(ast->symbol))
                               : QString();
        return true;
    }

    bool visit(CallAST *ast) override
    {
        if (!ast->base_expression)
            return true;
        IdExpressionAST * const id = ast->base_expression->asIdExpression();
        NameAST * const called = id ? id->name : nullptr;
        if (!called || !called->name)
            return true;

        const QString name = Overview().prettyName(called->name);
        const bool isOne = called->asQualifiedName()
                               ? m_qualified.contains(name)
                               : m_plain.contains(name)
                                     || (m_reachableUnqualified
                                         && m_lentByADirective.contains(name));
        if (!isOne)
            return true;

        // Each argument as it stands: what a literal says, and nothing for
        // anything else. The whole run of a literal, since adjacent ones
        // are one string.
        QStringList arguments;
        for (const ExpressionListAST *at = ast->expression_list; at; at = at->next) {
            const StringLiteralAST * const text = at->value ? at->value->asStringLiteral()
                                                            : nullptr;
            QString literal;
            for (const StringLiteralAST *piece = text; piece; piece = piece->next) {
                const Token token = m_document->translationUnit()->tokenAt(piece->literal_token);
                if (!token.isStringLiteral()) {
                    literal.clear();
                    break;
                }
                literal += QString::fromUtf8(token.spell());
            }
            arguments.append(literal);
        }

        int line = 0;
        int column = 0;
        m_document->translationUnit()->getTokenPosition(called->firstToken(), &line, &column);
        m_calls.append({m_insideFunction, arguments, line, column});
        return true;
    }

private:
    Document::Ptr m_document;
    QStringList m_qualified;        // asked for with scopes in front
    QStringList m_plain;            // asked for without any
    QStringList m_lentByADirective; // the last part of a qualified one
    QString m_insideFunction;
    QList<CodeModelQueries::WrittenCall> m_calls;
    QList<int> m_scopeDepths;
    int m_depth = 0;
    int m_usingDirectiveDepth = 0;
    bool m_reachableUnqualified = false;
};

// The classes a file hands to calls of one function, by the name of what
// each call's first argument points at. The type is looked up where the call
// stands, which is what says which class a name written there means.
class ClassesPassedTo : protected ASTVisitor
{
public:
    ClassesPassedTo(const Document::Ptr &document, const Snapshot &snapshot,
                    const QString &functionName)
        : ASTVisitor(document->translationUnit())
        , m_document(document)
        , m_snapshot(snapshot)
        , m_functionName(functionName)
    {
        accept(document->translationUnit()->ast());
    }

    QStringList classes() const { return m_classes; }

protected:
    bool visit(CompoundStatementAST *ast) override
    {
        // A call is looked up from the block it stands in, which is what
        // gives a name written there its meaning.
        m_scope = ast && ast->symbol ? ast->symbol->asScope() : nullptr;
        return m_scope != nullptr;
    }

    bool visit(CallAST *ast) override
    {
        if (!m_scope || !ast->base_expression || !ast->expression_list
            || !ast->expression_list->value) {
            return true;
        }
        const IdExpressionAST * const id = ast->base_expression->asIdExpression();
        const NameAST * const name = id ? id->name : nullptr;
        if (!name || Overview().prettyName(name->name) != m_functionName)
            return true;

        TypeOfExpression typeOfExpression;
        typeOfExpression.init(m_document, m_snapshot);
        const QList<LookupItem> items = typeOfExpression(ast->expression_list->value,
                                                         m_document, m_scope);
        // A lookup item with no type at all is no answer: its type operator
        // hands back what it holds without looking.
        if (items.isEmpty() || !items.first().type().type())
            return true;
        if (const PointerType * const pointer = items.first().type()->asPointerType())
            m_classes.append(Overview().prettyType(pointer->elementType()));
        return true;
    }

private:
    Document::Ptr m_document;
    const Snapshot &m_snapshot;
    QString m_functionName;
    Scope *m_scope = nullptr;
    QStringList m_classes;
};

// The class \a snapshot has under \a className as the reading of \a filePath
// sees it: the name is looked up as a type from the file's own scope, so a
// class a header declares is found where a file that includes it names it.
const Class *builtinClassNamed(const Snapshot &snapshot, const FilePath &filePath,
                               const QString &className)
{
    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc || className.isEmpty())
        return nullptr;

    TypeOfExpression typeOfExpression;
    typeOfExpression.init(doc, snapshot);
    const QList<LookupItem> items = typeOfExpression(className.toUtf8(),
                                                     doc->globalNamespace());
    for (const LookupItem &item : items) {
        if (Symbol * const symbol = item.declaration()) {
            if (Class * const klass = symbol->asClass())
                return klass;
        }
    }
    return nullptr;
}

} // namespace

// What the name at \a cursor resolves to, and whether that is a function --
// the two questions functionNamedAt() and nameResolvedAt() are each half of.
namespace {
struct ResolvedName
{
    QString qualifiedName;
    bool isFunction = false;
};
}  // namespace

static ResolvedName resolveNameAt(const Snapshot &snapshot, const FilePath &filePath,
                                  const QTextCursor &cursor)
{
    // At the end of the name, which is where an expression read backwards
    // from a cursor has to start.
    QTextCursor atTheEnd = cursor;
    const QTextDocument * const text = atTheEnd.document();
    for (QChar ch = text->characterAt(atTheEnd.position());
         ch.isLetterOrNumber() || ch == '_';
         ch = text->characterAt(atTheEnd.position())) {
        atTheEnd.movePosition(QTextCursor::NextCharacter);
    }

    int line = 0;
    int column = 0;
    Utils::Text::convertPosition(text, atTheEnd.position(), &line, &column);

#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<CxxFrontendDocument::Declaration> declaration
        = Internal::cxxFrontendDeclarationAt(filePath, line, column)) {
        if (!declaration->isValid())
            return {};
        return {declaration->name,
                declaration->kind == CxxFrontendDocument::Kind::Function};
    }
#endif

    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc)
        return {};

    ExpressionUnderCursor expressionUnderCursor(doc->languageFeatures());
    const QString expression = expressionUnderCursor(atTheEnd);

    TypeOfExpression typeOfExpression;
    typeOfExpression.init(doc, snapshot);
    const QList<LookupItem> items = typeOfExpression(expression.toUtf8(),
                                                     doc->scopeAt(line, column));
    if (items.isEmpty())
        return {};

    // The first candidate, as this has always taken: which overload the name
    // means is not settled by the name alone.
    Symbol * const symbol = items.first().declaration();
    if (!symbol)
        return {};
    return {Overview().prettyName(LookupContext::fullyQualifiedName(symbol)),
            symbol->asFunction() != nullptr || symbol->type()->asFunctionType() != nullptr};
}

QString functionNamedAt(const Snapshot &snapshot, const FilePath &filePath,
                        const QTextCursor &cursor)
{
    const ResolvedName resolved = resolveNameAt(snapshot, filePath, cursor);
    return resolved.isFunction ? resolved.qualifiedName : QString();
}

QString nameResolvedAt(const Snapshot &snapshot, const FilePath &filePath,
                       const QTextCursor &cursor)
{
    return resolveNameAt(snapshot, filePath, cursor).qualifiedName;
}

QString classAround(const Snapshot &snapshot, const FilePath &filePath, int line, int column)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QString> klass
        = Internal::cxxFrontendClassAround(filePath, line, column)) {
        return *klass;
    }
#endif

    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc)
        return {};

    Scope * const scope = doc->scopeAt(line, column);
    if (!scope || !scope->asClass())
        return {};
    return Overview().prettyName(LookupContext::fullyQualifiedName(scope));
}

EnclosingFunction functionAround(const Snapshot &snapshot, const FilePath &filePath,
                                 int line, int column)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<Internal::CxxFrontendEnclosingFunction> function
        = Internal::cxxFrontendFunctionAround(filePath, line, column)) {
        return {function->qualifiedName, function->fromLine, function->toLine};
    }
#endif

    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc)
        return {};

    EnclosingFunction function;
    function.qualifiedName = doc->functionAt(line, column, &function.fromLine,
                                             &function.toLine);
    return function;
}

class CodeModelQueries::Private
{
public:
    Snapshot snapshot;
    WorkingCopy workingCopy;
#ifdef QTC_WITH_CXX_FRONTEND
    // In an optional because a reading needs the snapshot and the working
    // copy to be built, and those are members here rather than arguments.
    std::optional<Internal::CxxFrontendReading> model;
#endif

    // Parsed again, tree and all: what the model manager leaves in the
    // snapshot has had its source and syntax tree released, and a walk over
    // the tree is what the built-in front end answers some of this with.
    //
    // Kept for as long as this object is, so that asking several questions
    // about one file costs one parse. A file does not change underneath an
    // object that lives for one question or two.
    Document::Ptr reparse(const FilePath &filePath) const
    {
        const auto known = reparsed.constFind(filePath);
        if (known != reparsed.constEnd())
            return *known;

        QByteArray contents;
        if (const auto source = workingCopy.source(filePath))
            contents = *source;
        else if (const Result<QByteArray> read = filePath.fileContents())
            contents = *read;
        else
            return {};

        const Document::Ptr doc = snapshot.preprocessedDocument(contents, filePath);
        if (doc)
            doc->check();
        reparsed.insert(filePath, doc);
        return doc;
    }

    mutable QHash<FilePath, Document::Ptr> reparsed;
};

CodeModelQueries::CodeModelQueries(const Snapshot &snapshot, const WorkingCopy &workingCopy)
    : d(new Private)
{
    d->snapshot = snapshot;
    d->workingCopy = workingCopy;
#ifdef QTC_WITH_CXX_FRONTEND
    d->model.emplace(snapshot, workingCopy);
#endif
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
        = d->model->classesUsing(filePath, className)) {
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
        = d->model->memberFunctionsIn(klass.filePath, klass.filePath, klass.line, klass.column);
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
    // An empty list is an answer: a file that declares nothing of its own
    // declares nothing. Handing the question back would answer it off the
    // built-in front end, whose global namespace holds what the headers
    // declare as well -- so a file with nothing in it would be shown
    // everything its headers have.
    if (const std::optional<QList<CxxFrontendDocument::Symbol>> symbols
        = d->model->symbolsIn(filePath)) {
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

QList<WrittenDeclaration> CodeModelQueries::declarationsIn(const FilePath &filePath) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // An empty list is an answer: a file that declares nothing of its own
    // declares nothing. Handing the question back would answer it off the
    // built-in front end, whose global namespace holds what the headers
    // declare as well -- so a file with nothing in it would be shown
    // everything its headers have.
    if (const std::optional<QList<CxxFrontendDocument::Symbol>> symbols
        = d->model->symbolsIn(filePath)) {
        QList<WrittenDeclaration> declarations;

        // Which entry each symbol became, since what is left out takes what
        // is written inside it along. The list has a scope before its
        // members, so the answer is always already here.
        QList<int> entryFor(symbols->size(), -1);

        for (int i = 0; i < symbols->size(); ++i) {
            const CxxFrontendDocument::Symbol &symbol = symbols->at(i);

            // A name a macro's body wrote stands nowhere a reader could be
            // taken to, a class named without its body declares nothing
            // here to say anything about, something extern is a promise
            // about a declaration elsewhere, and a using declaration makes
            // a name reachable rather than declaring it.
            if (symbol.isGenerated || symbol.isForwardDeclaration || symbol.isExtern
                || symbol.kind == CxxFrontendDocument::Kind::UsingDeclaration) {
                continue;
            }

            int parent = -1;
            if (symbol.parent >= 0) {
                parent = entryFor.at(symbol.parent);
                if (parent < 0)
                    continue;
            }

            entryFor[i] = int(declarations.size());
            declarations.append({symbol.name,
                                 writtenTypeOf(symbol),
                                 symbol.icon,
                                 filePath,
                                 symbol.line,
                                 symbol.column,
                                 parent,
                                 symbol.kind == CxxFrontendDocument::Kind::Namespace});
        }
        return declarations;
    }
#endif

    const Document::Ptr doc = d->snapshot.document(filePath);
    if (!doc)
        return {};

    QList<WrittenDeclaration> declarations;
    collectDeclarations(doc->globalNamespace(), filePath, -1, &declarations);
    return declarations;
}

QList<CodeModelQueries::WrittenMacroUse> CodeModelQueries::macroUsesIn(
    const FilePath &filePath) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QList<CxxFrontendDocument::MacroUse>> uses
        = d->model->macroUsesIn(filePath)) {
        QList<WrittenMacroUse> written;
        for (const CxxFrontendDocument::MacroUse &use : *uses)
            written.append({use.name, use.arguments});
        return written;
    }
#endif

    const Document::Ptr doc = d->snapshot.document(filePath);
    if (!doc)
        return {};

    // The text each argument stands in, which the document does not keep:
    // what it records is where they are.
    QByteArray contents;
    if (const auto source = d->workingCopy.source(filePath))
        contents = *source;
    else if (const Result<QByteArray> read = filePath.fileContents())
        contents = *read;
    else
        return {};

    QList<WrittenMacroUse> uses;
    for (const Document::MacroUse &use : doc->macroUses()) {
        if (!use.isFunctionLike() || use.arguments().isEmpty())
            continue;
        WrittenMacroUse written;
        written.name = QString::fromUtf8(use.macro().name());
        for (const Document::Block &argument : use.arguments()) {
            if (int(argument.bytesEnd()) > contents.size())
                continue;
            written.arguments.append(
                QString::fromUtf8(contents.mid(int(argument.bytesBegin()),
                                               int(argument.bytesEnd() - argument.bytesBegin())))
                    .trimmed());
        }
        uses.append(written);
    }
    return uses;
}

QList<CodeModelQueries::WrittenCall> CodeModelQueries::callsTo(
    const FilePath &filePath, const QStringList &functionNames) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QList<CxxFrontendDocument::WrittenCall>> calls
        = d->model->callsIn(filePath, functionNames)) {
        QList<WrittenCall> written;
        for (const CxxFrontendDocument::WrittenCall &call : *calls)
            written.append({call.insideFunction, call.arguments, call.line, call.column});
        return written;
    }
#endif

    const Document::Ptr doc = d->reparse(filePath);
    if (!doc || !doc->translationUnit() || !doc->translationUnit()->ast())
        return {};
    return CallsTo(doc, functionNames).calls();
}

QStringList CodeModelQueries::classesPassedTo(const FilePath &filePath,
                                              const QString &functionName) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QStringList> classes
        = d->model->classesPassedToIn(filePath, functionName)) {
        return *classes;
    }
#endif

    const Document::Ptr doc = d->reparse(filePath);
    if (!doc || !doc->translationUnit() || !doc->translationUnit()->ast())
        return {};
    return ClassesPassedTo(doc, d->snapshot, functionName).classes();
}

CodeModelQueries::ClassWithPrivateSlots CodeModelQueries::classWithPrivateSlots(
    const FilePath &filePath, const QString &className) const
{
    const int afterTheScopes = className.lastIndexOf("::");
    const QString ownName = afterTheScopes < 0 ? className : className.mid(afterTheScopes + 2);

#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<CxxFrontendDocument::Place> place
        = d->model->classNamedIn(filePath, className);
        place && place->line > 0) {
        const FilePath classFile = FilePath::fromUserInput(place->filePath);

        ClassWithPrivateSlots answer;
        answer.klass = {ownName, className, classFile, place->line, place->column};

        // Read where the class is written rather than where it was named:
        // what it declares is the same either way, and the file that writes
        // it is the one a reader is sent to.
        if (const std::optional<QList<CxxFrontendDocument::MemberFunction>> members
            = d->model->memberFunctionsIn(classFile, classFile, place->line, place->column)) {
            for (const CxxFrontendDocument::MemberFunction &member : *members) {
                if (member.access != CxxFrontendDocument::Access::Private
                    || member.qtMethod != CxxFrontendDocument::QtMethod::Slot) {
                    continue;
                }
                answer.privateSlots << WrittenFunction{member.unqualifiedName, member.signature,
                                                       FilePath::fromUserInput(member.filePath),
                                                       member.line, member.column};
            }
        }
        if (const std::optional<QStringList> bases
            = d->model->basesOfTheClassIn(classFile, place->line, place->column)) {
            answer.baseClasses = *bases;
        }
        return answer;
    }
#endif

    const Class * const klass = builtinClassNamed(d->snapshot, filePath, className);
    if (!klass)
        return {};

    const Overview overview;
    ClassWithPrivateSlots answer;
    answer.klass = {overview.prettyName(klass->name()),
                    overview.prettyName(
                        LookupContext::fullyQualifiedName(const_cast<Class *>(klass))),
                    FilePath::fromUtf8(klass->fileName()),
                    klass->line(),
                    klass->column()};

    for (int i = 0, count = klass->memberCount(); i < count; ++i) {
        Symbol * const member = klass->memberAt(i);
        Function * const function = member->type().type()
                                        ? member->type().type()->asFunctionType()
                                        : nullptr;
        if (!function || !function->isSlot() || !member->isPrivate())
            continue;
        answer.privateSlots << WrittenFunction{overview.prettyName(function->name()),
                                               signatureOf(function),
                                               FilePath::fromUtf8(member->fileName()),
                                               member->line(),
                                               member->column()};
    }

    for (int i = 0, count = klass->baseClassCount(); i < count; ++i) {
        if (BaseClass * const base = klass->baseClassAt(i))
            answer.baseClasses << overview.prettyName(LookupContext::fullyQualifiedName(base));
    }
    return answer;
}

DeclarationToDefine CodeModelQueries::declarationToDefineAt(const CppRefactoringChanges &changes,
                                                            const FilePath &filePath,
                                                            int line, int column) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<DeclarationToDefine> declaration
        = d->model->declarationToDefineIn(filePath, line, column);
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
        = d->model->definitionOfFunctionIn(filePath, line, column);
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
    // Through toLink(), which is what says how a link counts columns: from
    // zero, where a symbol counts them from one.
    return definition->toLink();
}

Link CodeModelQueries::definitionOfWhatIsDeclaredAt(const FilePath &filePath,
                                                    int line, int column) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // The other model answers for a function. A variable declared in one
    // file and defined in another is a question about the project that it
    // does not take, so that one is left to the front end that does.
    if (const std::optional<Link> definition
        = d->model->definitionOfFunctionIn(filePath, line, column);
        definition && definition->hasValidTarget()) {
        return *definition;
    }
#endif

    const Document::Ptr doc = d->snapshot.document(filePath);
    if (!doc)
        return {};

    Symbol * const symbol = doc->lastVisibleSymbolAt(line, column);
    if (!symbol || !symbol->type().type())
        return {};

    SymbolFinder symbolFinder;
    const Symbol * const definition
        = symbol->type().type()->asFunctionType()
              ? symbolFinder.findMatchingDefinition(symbol, d->snapshot, false)
              : symbolFinder.findMatchingVarDefinition(symbol, d->snapshot);
    return definition ? definition->toLink() : Link();
}

} // namespace CppEditor
