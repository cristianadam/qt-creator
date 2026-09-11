// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpppointerdeclarationformatter.h"

#include <cplusplus/ASTPath.h>
#include <cplusplus/Overview.h>

#include <QDebug>
#include <QTextCursor>

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendDocument.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#include <cxx/ast_cursor.h>
#include <cxx/names.h>
#include <cxx/translation_unit.h>
#endif

#define DEBUG_OUTPUT 0

#if DEBUG_OUTPUT
#  include <typeinfo>
#  ifdef __GNUC__
#    include <cxxabi.h>
#  endif
#endif

#define CHECK_RV(cond, err, r) \
    if (!(cond)) { if (DEBUG_OUTPUT) qDebug() << "Discarded:" << (err); return r; }
#define CHECK_R(cond, err) \
    if (!(cond)) { if (DEBUG_OUTPUT) qDebug() << "Discarded:" << (err); return; }
#define CHECK_C(cond, err) \
    if (!(cond)) { if (DEBUG_OUTPUT) qDebug() << "Discarded:" << (err); continue; }

namespace CppEditor::Internal {

/*!
   Skips specifiers that are not type relevant and returns the index of the
          first specifier token which is not followed by __attribute__
          ((T___ATTRIBUTE__)).

   This is used to get 'correct' start of the activation range in
   simple declarations.

   Consider these cases:

    \list
        \li \c {static char *s = 0;}
        \li \c {typedef char *s cp;}
        \li \c {__attribute__((visibility("default"))) char *f();}
    \endlist

   For all these cases we want to skip all the specifiers that are not type
   relevant
   (since these are not part of the type and thus are not rewritten).

   \a list is the specifier list to iterate and \a translationUnit is the
   translation unit.
   \a endToken is the last token to check.
   \a found is an output parameter that must not be 0.
 */
static unsigned firstTypeSpecifierWithoutFollowingAttribute(
    SpecifierListAST *list, TranslationUnit *translationUnit, unsigned endToken, bool *found)
{
    *found = false;
    if (!list || !translationUnit || !endToken)
        return 0;

    for (SpecifierListAST *it = list; it; it = it->next) {
        SpecifierAST *specifier = it->value;
        CHECK_RV(specifier, "No specifier", 0);
        const unsigned index = specifier->firstToken();
        CHECK_RV(index < endToken, "EndToken reached", 0);

        const int tokenKind = translationUnit->tokenKind(index);
        switch (tokenKind) {
        case T_VIRTUAL:
        case T_INLINE:
        case T_FRIEND:
        case T_REGISTER:
        case T_STATIC:
        case T_EXTERN:
        case T_MUTABLE:
        case T_TYPEDEF:
        case T_CONSTEXPR:
        case T___ATTRIBUTE__:
        case T___DECLSPEC:
            continue;
        default:
            // Check if attributes follow
            for (unsigned i = index; i <= endToken; ++i) {
                const int tokenKind = translationUnit->tokenKind(i);
                if (tokenKind == T___ATTRIBUTE__ || tokenKind == T___DECLSPEC)
                    return 0;
            }
            *found = true;
            return index;
        }
    }

    return 0;
}

/*!
    Filters the results of ASTPath: the constructs a reformatting activates
    on, each of them once and innermost first, which is the order they are
    offered in.
*/
static QList<AST *> constructsToFormat(const QList<AST *> &astPath)
{
    QList<AST *> filtered;
    bool hasSimpleDeclaration = false;
    bool hasFunctionDefinition = false;
    bool hasParameterDeclaration = false;
    bool hasIfStatement = false;
    bool hasWhileStatement = false;
    bool hasForStatement = false;
    bool hasForeachStatement = false;

    for (int i = astPath.size() - 1; i >= 0; --i) {
        AST * const ast = astPath.at(i);
        const auto take = [&](bool &seen) {
            if (seen)
                return;
            seen = true;
            filtered.append(ast);
        };
        if (ast->asSimpleDeclaration())
            take(hasSimpleDeclaration);
        else if (ast->asFunctionDefinition())
            take(hasFunctionDefinition);
        else if (ast->asParameterDeclaration())
            take(hasParameterDeclaration);
        else if (ast->asIfStatement())
            take(hasIfStatement);
        else if (ast->asWhileStatement())
            take(hasWhileStatement);
        else if (ast->asForStatement())
            take(hasForStatement);
        else if (ast->asForeachStatement())
            take(hasForeachStatement);
    }
    return filtered;
}


#ifdef QTC_WITH_CXX_FRONTEND

// What the cxx-frontend model reads: the same constructs, the same ranges,
// and the type printed the same way -- said over that tree.
//
// One thing is decided differently. The built-in path skips a declaration
// whose tokens a macro wrote, which is the same rule as "this front end
// reads the file as the compiler does": a place nobody wrote is a place with
// nothing to rewrite. Here that shows up as a range whose edges are not in
// the file, and cxxAstRangeOf says so by answering nothing.
class CxxDeclarationReader
{
public:
    CxxDeclarationReader(const CPlusPlus::CxxFrontendDocument &document,
                         const CppRefactoringFilePtr &file, const Overview &overview)
        : m_document(document), m_file(file), m_overview(overview)
    {}

    QList<DeclarationToFormat> readEverything()
    {
        cxx::TranslationUnit * const unit = m_document.translationUnit();
        if (!unit || !unit->ast())
            return {};
        readSubtree(unit->ast());
        return m_declarations;
    }

    QList<DeclarationToFormat> readAt(const Utils::Text::Position &position)
    {
        // The innermost construct the position is in that has anything to
        // rewrite, which is the one the built-in path offers first -- and
        // everything written inside it, since a parameter of a declaration
        // the cursor is on is part of what that declaration says.
        const QList<cxx::AST *> path
            = cxxAstPathAt(m_document, position.line, position.column + 1);
        for (int index = path.size() - 1; index >= 0; --index) {
            m_declarations.clear();
            readSubtree(path.at(index));
            if (!m_declarations.isEmpty())
                return m_declarations;
        }
        return {};
    }

private:
    void readSubtree(cxx::AST *root)
    {
        for (cxx::ASTCursor cursor(root, "node"); cursor; ++cursor) {
            auto *slot = std::get_if<cxx::AST *>(&(*cursor).node);
            if (slot && *slot)
                read(*slot);
        }
    }

    // The first specifier that says something about the type, the ones that
    // say something about the declaration instead being no part of what is
    // rewritten: "static char *s" starts at "char".
    static cxx::SourceLocation firstTypeSpecifier(cxx::List<cxx::SpecifierAST *> *list)
    {
        for (auto *specifier : cxx::ListView{list}) {
            if (!specifier)
                continue;
            if (dynamic_cast<cxx::TypedefSpecifierAST *>(specifier)
                || dynamic_cast<cxx::FriendSpecifierAST *>(specifier)
                || dynamic_cast<cxx::ConstevalSpecifierAST *>(specifier)
                || dynamic_cast<cxx::ConstinitSpecifierAST *>(specifier)
                || dynamic_cast<cxx::ConstexprSpecifierAST *>(specifier)
                || dynamic_cast<cxx::InlineSpecifierAST *>(specifier)
                || dynamic_cast<cxx::NoreturnSpecifierAST *>(specifier)
                || dynamic_cast<cxx::StaticSpecifierAST *>(specifier)
                || dynamic_cast<cxx::ExternSpecifierAST *>(specifier)
                || dynamic_cast<cxx::RegisterSpecifierAST *>(specifier)
                || dynamic_cast<cxx::ThreadLocalSpecifierAST *>(specifier)
                || dynamic_cast<cxx::ThreadSpecifierAST *>(specifier)
                || dynamic_cast<cxx::MutableSpecifierAST *>(specifier)
                || dynamic_cast<cxx::VirtualSpecifierAST *>(specifier)
                || dynamic_cast<cxx::ExplicitSpecifierAST *>(specifier)) {
                continue;
            }
            return specifier->firstSourceLocation();
        }
        return {};
    }

    // Whether the declarators of this declaration are rewritten at all: a
    // class, an enum or a typedef says nothing about a pointer.
    static bool saysAType(cxx::List<cxx::SpecifierAST *> *list)
    {
        for (auto *specifier : cxx::ListView{list}) {
            if (dynamic_cast<cxx::ClassSpecifierAST *>(specifier)
                || dynamic_cast<cxx::EnumSpecifierAST *>(specifier)) {
                return false;
            }
        }
        return true;
    }

    // The name a declarator declares, which for a pointer to a function
    // stands inside parentheses: the foo of "*(*foo)(int)".
    static cxx::IdDeclaratorAST *idOf(cxx::DeclaratorAST *declarator)
    {
        while (declarator) {
            if (auto * const id = dynamic_cast<cxx::IdDeclaratorAST *>(declarator->coreDeclarator))
                return id;
            auto * const nested = dynamic_cast<cxx::NestedDeclaratorAST *>(
                declarator->coreDeclarator);
            declarator = nested ? nested->declarator : nullptr;
        }
        return nullptr;
    }

    // The parameters of a function this declarator *declares*, and nothing
    // for one that declares a pointer to a function: what is rewritten of a
    // function is the type it hands back, while a pointer to one is
    // rewritten whole, the parameters of its type included.
    static cxx::FunctionDeclaratorChunkAST *functionChunkOf(cxx::DeclaratorAST *declarator)
    {
        // The stars of "char *f()" belong to the type it hands back, so
        // what tells the two apart is the name: a pointer to a function
        // writes it inside parentheses.
        if (!dynamic_cast<cxx::IdDeclaratorAST *>(declarator->coreDeclarator))
            return nullptr;
        for (auto *chunk : cxx::ListView{declarator->declaratorChunkList}) {
            if (auto * const function = dynamic_cast<cxx::FunctionDeclaratorChunkAST *>(chunk))
                return function;
        }
        return nullptr;
    }

    static bool hasPointerOperators(cxx::DeclaratorAST *declarator)
    {
        return declarator && declarator->ptrOpList;
    }

    // Where the initializer of a declarator begins, so that the range stops
    // in front of it: what is rewritten is the declaration and not the value.
    static cxx::SourceLocation equalOf(cxx::DeclaratorAST *declarator,
                                       cxx::ExpressionAST *initializer)
    {
        if (auto * const equal = dynamic_cast<cxx::EqualInitializerAST *>(initializer))
            return equal->equalLoc;
        Q_UNUSED(declarator)
        return {};
    }

    int startOf(cxx::SourceLocation location) const
    {
        const CxxAstRange range = cxxTokenRangeAt(m_document, location);
        return range.isValid() ? m_file->position(range.startLine, range.startColumn) : -1;
    }

    int endOfTokenBefore(cxx::SourceLocation location) const
    {
        if (!location || location.index() == 0)
            return -1;
        const CxxAstRange range = cxxTokenRangeAt(m_document,
                                                  cxx::SourceLocation{location.index() - 1});
        return range.isValid() ? m_file->position(range.endLine, range.endColumn) : -1;
    }

    int endOf(cxx::AST *node) const
    {
        const CxxAstRange range = cxxAstRangeOf(m_document, node);
        return range.isValid() ? m_file->position(range.endLine, range.endColumn) : -1;
    }

    // The name as it is written, the qualification in front of it included,
    // so that a rewriting loses nothing of it.
    QString writtenName(cxx::DeclaratorAST *declarator, int *nameLine, int *nameColumn) const
    {
        cxx::IdDeclaratorAST * const id = idOf(declarator);
        if (!id || !id->unqualifiedId)
            return {};
        const CxxAstRange name = cxxAstRangeOf(m_document, id->unqualifiedId);
        if (!name.isValid())
            return {};
        *nameLine = name.startLine;
        *nameColumn = name.startColumn;

        const CxxAstRange whole = cxxAstRangeOf(m_document, id);
        if (!whole.isValid())
            return {};
        return m_file->textOf(m_file->position(whole.startLine, whole.startColumn),
                              m_file->position(name.endLine, name.endColumn));
    }

    // The names the parameters of a function type are written under, which
    // a type does not carry: "char *(*f)(int n)" loses the n otherwise.
    static QStringList writtenParameterNames(cxx::DeclaratorAST *declarator)
    {
        QStringList names;
        for (auto *chunk : cxx::ListView{declarator->declaratorChunkList}) {
            auto * const function = dynamic_cast<cxx::FunctionDeclaratorChunkAST *>(chunk);
            if (!function || !function->parameterDeclarationClause)
                continue;
            for (auto *parameter :
                 cxx::ListView{function->parameterDeclarationClause->parameterDeclarationList}) {
                names.append(parameter && parameter->identifier
                                 ? QString::fromStdString(parameter->identifier->name())
                                 : QString());
            }
            break;
        }
        return names;
    }

    void note(cxx::DeclaratorAST *declarator, int start, int end, int charactersToRemove)
    {
        if (start < 0 || end < 0 || start >= end)
            return;

        // An attribute standing in what would be rewritten is no part of
        // the type, and printing the type would drop it. Read off the text,
        // since what is asked is whether the words are there at all.
        const QString original = m_file->textOf(start, end);
        if (original.contains("__attribute__") || original.contains("__declspec"))
            return;
        int nameLine = 0;
        int nameColumn = 0;
        const QString name = writtenName(declarator, &nameLine, &nameColumn);
        if (name.isEmpty())
            return;
        QString rewritten = m_document.typeDeclaredAt(nameLine, nameColumn, name, {}, m_overview,
                                                      writtenParameterNames(declarator));
        if (rewritten.isEmpty())
            return;
        rewritten.remove(0, charactersToRemove);
        m_declarations.append({{start, end}, rewritten});
    }

    void read(cxx::AST *node)
    {
        if (auto * const declaration = dynamic_cast<cxx::SimpleDeclarationAST *>(node)) {
            readSimpleDeclaration(declaration);
            return;
        }
        if (auto * const definition = dynamic_cast<cxx::FunctionDefinitionAST *>(node)) {
            readFunctionDefinition(definition);
            return;
        }
        if (auto * const parameter = dynamic_cast<cxx::ParameterDeclarationAST *>(node)) {
            readParameter(parameter);
            return;
        }
        if (auto * const statement = dynamic_cast<cxx::IfStatementAST *>(node)) {
            readCondition(statement->condition);
            return;
        }
        if (auto * const statement = dynamic_cast<cxx::WhileStatementAST *>(node)) {
            readCondition(statement->condition);
            return;
        }
        if (auto * const statement = dynamic_cast<cxx::ForStatementAST *>(node)) {
            readCondition(statement->condition);
            return;
        }
    }

    void readSimpleDeclaration(cxx::SimpleDeclarationAST *declaration)
    {
        if (!declaration->declSpecifierList || !declaration->initDeclaratorList
            || !saysAType(declaration->declSpecifierList)) {
            return;
        }

        cxx::DeclaratorAST *firstDeclarator = nullptr;
        for (auto *declared : cxx::ListView{declaration->initDeclaratorList}) {
            if (declared && declared->declarator) {
                firstDeclarator = declared->declarator;
                break;
            }
        }
        if (!firstDeclarator)
            return;

        const cxx::SourceLocation typeSpecifier
            = firstTypeSpecifier(declaration->declSpecifierList);

        for (auto *declared : cxx::ListView{declaration->initDeclaratorList}) {
            if (!declared || !declared->declarator)
                continue;
            cxx::DeclaratorAST * const declarator = declared->declarator;
            const bool isFirst = declarator == firstDeclarator;

            // Every declarator is rewritten with all the type specifiers in
            // front of it, so for the ones after the first that much is cut
            // off again.
            int charactersToRemove = 0;
            if (!isFirst) {
                const int declarationStart = startOf(declaration->firstSourceLocation());
                const int firstStart = startOf(firstDeclarator->firstSourceLocation());
                if (declarationStart < 0 || firstStart < 0 || declarationStart >= firstStart)
                    continue;
                charactersToRemove = firstStart - declarationStart;
            }

            if (cxx::FunctionDeclaratorChunkAST * const function = functionChunkOf(declarator)) {
                // What is rewritten of a function is the type it hands back,
                // which stops in front of its parameters.
                const int start = isFirst ? startOf(typeSpecifier)
                                          : startOf(declarator->firstSourceLocation());
                note(declarator, start, endOfTokenBefore(function->lparenLoc),
                     charactersToRemove);
                continue;
            }

            const int start = isFirst ? startOf(typeSpecifier)
                                      : startOf(declarator->firstSourceLocation());
            const cxx::SourceLocation equal = equalOf(declarator, declared->initializer);
            note(declarator, start,
                 equal ? endOfTokenBefore(equal) : endOf(declarator),
                 charactersToRemove);
        }
    }

    void readFunctionDefinition(cxx::FunctionDefinitionAST *definition)
    {
        if (!hasPointerOperators(definition->declarator) || !definition->declSpecifierList)
            return;
        cxx::FunctionDeclaratorChunkAST * const function = functionChunkOf(definition->declarator);
        if (!function)
            return;
        note(definition->declarator, startOf(firstTypeSpecifier(definition->declSpecifierList)),
             endOfTokenBefore(function->lparenLoc), 0);
    }

    void readParameter(cxx::ParameterDeclarationAST *parameter)
    {
        if (!hasPointerOperators(parameter->declarator))
            return;
        const cxx::SourceLocation equal
            = equalOf(parameter->declarator,
                      dynamic_cast<cxx::ExpressionAST *>(parameter->expression));
        note(parameter->declarator, startOf(parameter->firstSourceLocation()),
             equal ? endOfTokenBefore(equal) : endOf(parameter->declarator), 0);
    }

    // A declaration written in the condition of an if, a while or a for.
    // The statement records what it wants there, so the declaration sits
    // under the conversions the condition asked for.
    void readCondition(cxx::ExpressionAST *condition)
    {
        while (auto * const cast = dynamic_cast<cxx::ImplicitCastExpressionAST *>(condition))
            condition = cast->expression;
        auto * const declared = dynamic_cast<cxx::ConditionExpressionAST *>(condition);
        if (!declared || !hasPointerOperators(declared->declarator))
            return;
        const cxx::SourceLocation equal = equalOf(declared->declarator, declared->initializer);
        if (!equal)
            return;
        note(declared->declarator, startOf(declared->firstSourceLocation()),
             endOfTokenBefore(equal), 0);
    }

    const CPlusPlus::CxxFrontendDocument &m_document;
    const CppRefactoringFilePtr m_file;
    const Overview m_overview;
    QList<DeclarationToFormat> m_declarations;
};

// The model's reading of the file being formatted, or nothing where it has
// not read that file or could not read it properly -- a construct the front
// end stumbled over is not one to rewrite by.
std::optional<QList<DeclarationToFormat>> cxxDeclarations(
    const CppRefactoringFilePtr &file, const Overview &overview,
    const std::optional<Utils::Text::Position> &position)
{
    const std::shared_ptr<const CPlusPlus::CxxFrontendSnapshot> model
        = cxxFrontendModel(file->filePath());
    if (!model)
        return std::nullopt;
    const CPlusPlus::CxxFrontendDocument * const document
        = model->document(file->filePath().toFSPathString());
    if (!document || !document->translationUnit())
        return std::nullopt;
    if (Utils::anyOf(document->diagnostics(),
                     [](const CPlusPlus::CxxFrontendDocument::Diagnostic &diagnostic) {
                         return diagnostic.isError;
                     })) {
        return std::nullopt;
    }

    CxxDeclarationReader reader(*document, file, overview);
    if (position)
        return reader.readAt(*position);
    return reader.readEverything();
}

#endif // QTC_WITH_CXX_FRONTEND

Utils::ChangeSet PointerDeclarationFormatter::formatEverything()
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QList<DeclarationToFormat>> onTheModel
        = cxxDeclarations(m_cppRefactoringFile, m_overview, {})) {
        return changesForDeclarations(m_cppRefactoringFile, m_cursorHandling, *onTheModel);
    }
#endif
    const Document::Ptr document = m_cppRefactoringFile->cppDocument();
    AST * const ast = document && document->translationUnit()
                          ? document->translationUnit()->ast()
                          : nullptr;
    return changesForDeclarations(m_cppRefactoringFile, m_cursorHandling, read(ast));
}

Utils::ChangeSet PointerDeclarationFormatter::formatAt(const Utils::Text::Position &position)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QList<DeclarationToFormat>> onTheModel
        = cxxDeclarations(m_cppRefactoringFile, m_overview, position)) {
        return changesForDeclarations(m_cppRefactoringFile, m_cursorHandling, *onTheModel);
    }
#endif
    const Document::Ptr document = m_cppRefactoringFile->cppDocument();
    if (!document)
        return {};

    // The first construct with anything to change, which is what a reader
    // with the cursor in several of them is offered.
    const QList<AST *> path = ASTPath(document)(position.line, position.column + 1);
    for (AST * const construct : constructsToFormat(path)) {
        const Utils::ChangeSet changes
            = changesForDeclarations(m_cppRefactoringFile, m_cursorHandling, read(construct));
        if (!changes.isEmpty())
            return changes;
    }
    return {};
}

QList<DeclarationToFormat> PointerDeclarationFormatter::read(AST *ast)
{
    m_declarations.clear();
    if (ast)
        accept(ast);
    return m_declarations;
}

PointerDeclarationFormatter::PointerDeclarationFormatter(
        const CppRefactoringFilePtr &refactoringFile,
        Overview &overview,
        CursorHandling cursorHandling)
    : ASTVisitor(refactoringFile->cppDocument()->translationUnit())
    , m_cppRefactoringFile(refactoringFile)
    , m_overview(overview)
    , m_cursorHandling(cursorHandling)
{}

/*!
    Handle
      (1) Simple declarations like in "char *s, *t, *int foo();"
      (2) Return types of function declarations.
 */
bool PointerDeclarationFormatter::visit(SimpleDeclarationAST *ast)
{
    CHECK_RV(ast, "Invalid AST", true);
    printCandidate(ast);

    const unsigned tokenKind = tokenAt(ast->firstToken()).kind();
    const bool astIsOk = tokenKind != T_CLASS && tokenKind != T_STRUCT && tokenKind != T_ENUM;
    CHECK_RV(astIsOk, "Nothing to do for class/struct/enum", true);

    DeclaratorListAST *declaratorList = ast->declarator_list;
    CHECK_RV(declaratorList, "No declarator list", true);
    DeclaratorAST *firstDeclarator = declaratorList->value;
    CHECK_RV(firstDeclarator, "No declarator", true);
    CHECK_RV(ast->symbols, "No Symbols", true);
    CHECK_RV(ast->symbols->value, "No Symbol", true);

    List<Symbol *> *sit = ast->symbols;
    DeclaratorListAST *dit = declaratorList;
    for (; sit && dit; sit = sit->next, dit = dit->next) {
        DeclaratorAST *declarator = dit->value;
        Symbol *symbol = sit->value;

        const bool isFirstDeclarator = declarator == firstDeclarator;

        // If were not handling the first declarator, we need to remove
        // characters from the beginning since our rewritten declaration
        // will contain all type specifiers.
        int charactersToRemove = 0;
        if (!isFirstDeclarator) {
            const int startAST = m_cppRefactoringFile->startOf(ast);
            const int startFirstDeclarator = m_cppRefactoringFile->startOf(firstDeclarator);
            CHECK_RV(startAST < startFirstDeclarator, "No specifier", true);
            charactersToRemove = startFirstDeclarator - startAST;
        }

        // Specify activation range
        int lastActivationToken = 0;
        TokenRange range;
        // (2) Handle function declaration's return type
        if (symbol->type()->asFunctionType()) {
            PostfixDeclaratorListAST *pfDeclaratorList = declarator->postfix_declarator_list;
            CHECK_RV(pfDeclaratorList, "No postfix declarator list", true);
            PostfixDeclaratorAST *pfDeclarator = pfDeclaratorList->value;
            CHECK_RV(pfDeclarator, "No postfix declarator", true);
            FunctionDeclaratorAST *functionDeclarator = pfDeclarator->asFunctionDeclarator();
            CHECK_RV(functionDeclarator, "No function declarator", true);
            // End the activation range before the '(' token.
            lastActivationToken = functionDeclarator->lparen_token - 1;

            SpecifierListAST *specifierList = isFirstDeclarator
                ? ast->decl_specifier_list
                : declarator->attribute_list;

            unsigned firstActivationToken = 0;
            bool foundBegin = false;
            firstActivationToken = firstTypeSpecifierWithoutFollowingAttribute(
                        specifierList,
                        m_cppRefactoringFile->cppDocument()->translationUnit(),
                        lastActivationToken,
                        &foundBegin);
            if (!foundBegin) {
                CHECK_RV(!isFirstDeclarator, "Declaration without attributes not supported", true);
                firstActivationToken = declarator->firstToken();
            }

            range.start = firstActivationToken;

        // (1) Handle 'normal' declarations.
        } else {
            if (isFirstDeclarator) {
                bool foundBegin = false;
                unsigned firstActivationToken = firstTypeSpecifierWithoutFollowingAttribute(
                            ast->decl_specifier_list,
                            m_cppRefactoringFile->cppDocument()->translationUnit(),
                            declarator->firstToken(),
                            &foundBegin);
                CHECK_RV(foundBegin, "Declaration without attributes not supported", true);
                range.start = firstActivationToken;
            } else {
                range.start = declarator->firstToken();
            }
            lastActivationToken = declarator->equal_token
                ? declarator->equal_token - 1
                : declarator->lastToken() - 1;
        }

        range.end = lastActivationToken;

        checkAndRewrite(declarator, symbol, range, charactersToRemove);
    }
    return true;
}

/*! Handle return types of function definitions */
bool PointerDeclarationFormatter::visit(FunctionDefinitionAST *ast)
{
    CHECK_RV(ast, "Invalid AST", true);
    printCandidate(ast);

    DeclaratorAST *declarator = ast->declarator;
    CHECK_RV(declarator, "No declarator", true);
    CHECK_RV(declarator->ptr_operator_list, "No Pointer or references", true);
    Symbol *symbol = ast->symbol;

    PostfixDeclaratorListAST *pfDeclaratorList = declarator->postfix_declarator_list;
    CHECK_RV(pfDeclaratorList, "No postfix declarator list", true);
    PostfixDeclaratorAST *pfDeclarator = pfDeclaratorList->value;
    CHECK_RV(pfDeclarator, "No postfix declarator", true);
    FunctionDeclaratorAST *functionDeclarator = pfDeclarator->asFunctionDeclarator();
    CHECK_RV(functionDeclarator, "No function declarator", true);

    // Specify activation range
    bool foundBegin = false;
    const unsigned lastActivationToken = functionDeclarator->lparen_token - 1;
    const unsigned firstActivationToken = firstTypeSpecifierWithoutFollowingAttribute(
        ast->decl_specifier_list,
        m_cppRefactoringFile->cppDocument()->translationUnit(),
        lastActivationToken,
        &foundBegin);
    CHECK_RV(foundBegin, "Declaration without attributes not supported", true);
    TokenRange range(firstActivationToken, lastActivationToken);

    checkAndRewrite(declarator, symbol, range);
    return true;
}

/*! Handle parameters in function declarations and definitions */
bool PointerDeclarationFormatter::visit(ParameterDeclarationAST *ast)
{
    CHECK_RV(ast, "Invalid AST", true);
    printCandidate(ast);

    DeclaratorAST *declarator = ast->declarator;
    CHECK_RV(declarator, "No declarator", true);
    CHECK_RV(declarator->ptr_operator_list, "No Pointer or references", true);
    Symbol *symbol = ast->symbol;

    // Specify activation range
    const int lastActivationToken = ast->equal_token
        ? ast->equal_token - 1
        : ast->lastToken() - 1;
    TokenRange range(ast->firstToken(), lastActivationToken);

    checkAndRewrite(declarator, symbol, range);
    return true;
}

/*! Handle declaration in foreach statement */
bool PointerDeclarationFormatter::visit(ForeachStatementAST *ast)
{
    CHECK_RV(ast, "Invalid AST", true);
    printCandidate(ast);

    DeclaratorAST *declarator = ast->declarator;
    CHECK_RV(declarator, "No declarator", true);
    CHECK_RV(declarator->ptr_operator_list, "No Pointer or references", true);
    CHECK_RV(ast->type_specifier_list, "No type specifier", true);
    SpecifierAST *firstSpecifier = ast->type_specifier_list->value;
    CHECK_RV(firstSpecifier, "No first type specifier", true);
    CHECK_RV(ast->symbol, "No symbol", true);
    Symbol *symbol = ast->symbol->memberAt(0);

    // Specify activation range
    const int lastActivationToken = declarator->equal_token
        ? declarator->equal_token - 1
        : declarator->lastToken() - 1;
    TokenRange range(firstSpecifier->firstToken(), lastActivationToken);

    checkAndRewrite(declarator, symbol, range);
    return true;
}

bool PointerDeclarationFormatter::visit(IfStatementAST *ast)
{
    CHECK_RV(ast, "Invalid AST", true);
    printCandidate(ast);
    processIfWhileForStatement(ast->condition, ast->symbol);
    return true;
}

bool PointerDeclarationFormatter::visit(WhileStatementAST *ast)
{
    CHECK_RV(ast, "Invalid AST", true);
    printCandidate(ast);
    processIfWhileForStatement(ast->condition, ast->symbol);
    return true;
}

bool PointerDeclarationFormatter::visit(ForStatementAST *ast)
{
    CHECK_RV(ast, "Invalid AST", true);
    printCandidate(ast);
    processIfWhileForStatement(ast->condition, ast->symbol);
    return true;
}

/*! Handle declaration in if, while and for statements */
void PointerDeclarationFormatter::processIfWhileForStatement(ExpressionAST *expression,
                                                             Symbol *statementSymbol)
{
    CHECK_R(expression, "No expression");
    CHECK_R(statementSymbol, "No symbol");

    ConditionAST *condition = expression->asCondition();
    CHECK_R(condition, "No condition");
    DeclaratorAST *declarator = condition->declarator;
    CHECK_R(declarator, "No declarator");
    CHECK_R(declarator->ptr_operator_list, "No Pointer or references");
    CHECK_R(declarator->equal_token, "No equal token");
    Block *block = statementSymbol->asBlock();
    CHECK_R(block, "No block");
    CHECK_R(block->memberCount() > 0, "No block members");

    // Get the right symbol
    //
    // This is especially important for e.g.
    //
    //    for (char *s = 0; char *t = 0;) {}
    //
    // The declaration for 's' will be handled in visit(SimpleDeclarationAST *ast),
    // so handle declaration for 't' here.
    Scope::iterator it = block->memberEnd() - 1;
    Symbol *symbol = *it;
    if (symbol && symbol->asScope()) { // True if there is a  "{ ... }" following.
        --it;
        symbol = *it;
    }

    // Specify activation range
    TokenRange range(condition->firstToken(), declarator->equal_token - 1);

    checkAndRewrite(declarator, symbol, range);
}

/*!
    Performs some further checks and rewrites the type and name of \a symbol
    into the substitution range in the file specified by \a tokenRange.
 */
/*!
    Performs some further checks and notes the type and name of \a symbol as
    what is to be written into the substitution range specified by \a
    tokenRange.
*/
void PointerDeclarationFormatter::checkAndRewrite(DeclaratorAST *declarator,
                                                  Symbol *symbol,
                                                  TokenRange tokenRange,
                                                  unsigned charactersToRemove)
{
    CHECK_R(tokenRange.end > 0, "TokenRange invalid1");
    CHECK_R(tokenRange.start < tokenRange.end, "TokenRange invalid2");
    CHECK_R(symbol, "No symbol");

    // Check for expanded tokens
    for (int token = tokenRange.start; token <= tokenRange.end; ++token)
        CHECK_R(!tokenAt(token).expanded(), "Token is expanded");

    Utils::ChangeSet::Range range(m_cppRefactoringFile->startOf(tokenRange.start),
                                  m_cppRefactoringFile->endOf(tokenRange.end));

    CHECK_R(range.start >= 0 && range.end > 0, "ChangeRange invalid1");
    CHECK_R(range.start < range.end, "ChangeRange invalid2");

    FullySpecifiedType type = symbol->type();
    if (Function *function = type->asFunctionType())
        type = function->returnType();

    // An operator's name is spaced as it was written.
    const Name *name = symbol->name();
    if (name) {
        if (name->asOperatorNameId()
                || (name->asQualifiedNameId()
                    && name->asQualifiedNameId()->name()->asOperatorNameId())) {
            const QString operatorText = m_cppRefactoringFile->textOf(declarator->core_declarator);
            m_overview.includeWhiteSpaceInOperatorName = operatorText.contains(QLatin1Char(' '));
        }
    }
    QString rewrittenDeclaration = m_overview.prettyType(type, name);
    rewrittenDeclaration.remove(0, charactersToRemove);

    m_declarations.append({range, rewrittenDeclaration});
}

Utils::ChangeSet PointerDeclarationFormatter::changesForDeclarations(
    const CppRefactoringFilePtr &file, CursorHandling cursorHandling,
    const QList<DeclarationToFormat> &declarations)
{
    Utils::ChangeSet changeSet;
    for (const DeclarationToFormat &declaration : declarations) {
        const Utils::ChangeSet::Range &range = declaration.range;

        // Check range with respect to cursor position / selection
        if (cursorHandling == RespectCursor) {
            const QTextCursor cursor = file->cursor();
            if (cursor.hasSelection()) {
                CHECK_C(cursor.selectionStart() <= range.start, "Change not in selection range");
                CHECK_C(range.end <= cursor.selectionEnd(), "Change not in selection range");
            } else {
                CHECK_C(range.start <= cursor.selectionStart(), "Cursor before activation range");
                CHECK_C(cursor.selectionEnd() <= range.end, "Cursor after activation range");
            }
        }

        // Check if pointers or references are involved
        const QString originalDeclaration = file->textOf(range);
        CHECK_C(originalDeclaration.contains(QLatin1Char('&'))
                || originalDeclaration.contains(QLatin1Char('*')), "No pointer or references");

        // Does the rewritten declaration (part) differ from the original source (part)?
        CHECK_C(originalDeclaration != declaration.rewritten, "Rewritten is same as original");
        CHECK_C(declaration.rewritten.contains(QLatin1Char('&'))
                || declaration.rewritten.contains(QLatin1Char('*')),
                "No pointer or references in rewritten declaration");

        if (DEBUG_OUTPUT) {
            qDebug("==> Rewritten: \"%s\" --> \"%s\"", originalDeclaration.toUtf8().constData(),
                   declaration.rewritten.toUtf8().constData());
        }

        // Creating the replacement in the changeset may fail due to operations
        // in the changeset that overlap with the current range.
        //
        // Consider this case:
        //
        //    void (*foo)(char * s) = 0;
        //
        // First the simple declaration is read. It creates a replacement that
        // also includes the parameter. Next the parameter declaration is read
        // with the original source. It tries to create a replacement
        // operation at this position and fails due to overlapping ranges (the
        // simple declaration range includes parameter declaration range).
        Utils::ChangeSet change(changeSet);
        if (change.replace(range, declaration.rewritten))
            changeSet = change;
        else if (DEBUG_OUTPUT)
            qDebug() << "Replacement operation failed";
    }
    return changeSet;
}

void PointerDeclarationFormatter::printCandidate(AST *ast)
{
#if DEBUG_OUTPUT
    QString tokens;
    for (int token = ast->firstToken(); token < ast->lastToken(); token++)
        tokens += QString::fromLatin1(tokenAt(token).spell()) + QLatin1Char(' ');

#  ifdef __GNUC__
    QByteArray name = abi::__cxa_demangle(typeid(*ast).name(), 0, 0, 0) + 11;
    name.truncate(name.length() - 3);
#  else
    QByteArray name = typeid(*ast).name();
#  endif
    qDebug("--> Candidate: %s: %s", name.constData(), qPrintable(tokens));
#else
    Q_UNUSED(ast)
#endif // DEBUG_OUTPUT
}

} // namespace CppEditor::Internal
