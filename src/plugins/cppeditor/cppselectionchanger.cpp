// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppselectionchanger.h"

#include <utils/textutils.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QString>
#include <QTextBlock>
#include <QTextDocument>

#include <cplusplus/AST.h>
#include <cplusplus/ASTPath.h>
#include <cplusplus/TranslationUnit.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#endif

using namespace CPlusPlus;
using namespace Utils::Text;

enum {
    debug = false
};

namespace CppEditor {

namespace Internal {
const int kChangeSelectionNodeIndexNotSet = -1;
const int kChangeSelectionNodeIndexWholeDocoument = -2;
} // namespace Internal
using namespace Internal;

CppSelectionChanger::CppSelectionChanger(QObject *parent)
    : QObject(parent)
    , m_changeSelectionNodeIndex(kChangeSelectionNodeIndexNotSet)
    , m_nodeCurrentStep(kChangeSelectionNodeIndexNotSet)
{
}

void CppSelectionChanger::onCursorPositionChanged(const QTextCursor &newCursor)
{
    // Reset the text cursor to be used for initial change selection behavior, only in the case
    // that the cursor is not being modified by the actual change selection methods.
    if (!m_inChangeSelection) {
        m_initialChangeSelectionCursor = newCursor;
        setNodeIndexAndStep(NodeIndexAndStepNotSet);
        if (debug)
            qDebug() << "Updating change selection cursor position:" << newCursor.position();
    }
}

namespace {

bool hasNoSelectionAndShrinking(
        CppSelectionChanger::Direction direction,
        const QTextCursor &cursor)
{
    if (direction == CppSelectionChanger::ShrinkSelection && !cursor.hasSelection()) {
        if (debug)
            qDebug() << "No selection to shrink, exiting early.";
        return true;
    }
    return false;
}

void ensureCursorSelectionIsNotFlipped(QTextCursor &cursor)
{
    if (cursor.hasSelection() && (cursor.anchor() > cursor.position()))
        cursor = flippedCursor(cursor);

    if (debug) {
        int l, c;
        convertPosition(cursor.document(), cursor.position(), &l, &c);

        qDebug() << "Cursor details: " << cursor.anchor() << cursor.position()
                 << " l,c:" << l << ":" << c;
    }
}

bool isDocumentAvailable(const CPlusPlus::Document::Ptr doc)
{
    if (!doc) {
        if (debug)
            qDebug() << "Document is not available.";
        return false;
    }
    return true;
}

QTextCursor getWholeDocumentCursor(const QTextCursor &cursor)
{
    QTextCursor newWholeDocumentCursor(cursor);
    newWholeDocumentCursor.setPosition(0, QTextCursor::MoveAnchor);
    newWholeDocumentCursor.setPosition(cursor.document()->characterCount() - 1,
                                       QTextCursor::KeepAnchor);
    return newWholeDocumentCursor;
}

bool isWholeDocumentSelectedAndExpanding(
        CppSelectionChanger::Direction direction,
        const QTextCursor &cursor)
{
    if (direction == CppSelectionChanger::ExpandSelection && cursor.hasSelection()) {
        const QTextCursor wholeDocumentCursor = getWholeDocumentCursor(cursor);
        if (wholeDocumentCursor == cursor) {
            if (debug)
                qDebug() << "Selection is whole document, nothing to expand, exiting early.";
            return true;
        }
    }
    return false;
}

// Reading the built-in syntax tree: what the nodes the cursor is in offer.
//
// One case per construct that has places inside it worth stopping at, and the
// node's own extent for everything else. A node says up front what each of
// its steps selects, so the walk itself has nothing to know about C++ -- and
// where the answer depends on which part of the construct the cursor is in,
// that is decided here, once, rather than again at every step.
class BuiltinTree
{
public:
    BuiltinTree(const Document::Ptr &doc, const QTextDocument *text, const QTextCursor &cursor)
        : m_doc(doc)
        , m_unit(doc->translationUnit())
        , m_text(text)
        , m_cursorAnchor(cursor.anchor())
        , m_cursorPosition(cursor.position())
        , m_cursor(cursor)
    {}

    SelectionPath path() const
    {
        SelectionPath path;
        ASTPath astPathFinder(m_doc);
        const QList<AST *> astPath = astPathFinder(m_cursor);
        for (AST *ast : astPath)
            path.append(stepsOf(ast));
        return path;
    }

private:
    int tokenStart(unsigned tokenIndex) const
    {
        int line, column;
        m_unit->getTokenPosition(tokenIndex, &line, &column);
        return m_text->findBlockByNumber(line - 1).position() + column - 1;
    }

    int tokenEnd(unsigned tokenIndex) const
    {
        int line, column;
        m_unit->getTokenEndPosition(tokenIndex, &line, &column);
        return m_text->findBlockByNumber(line - 1).position() + column - 1;
    }

    // An AST node's contents is bound by its first token start position
    // inclusively, and its last token start position exclusively. So the
    // second to last token is the last one actually included, and where there
    // is more than one token its end is where the node ends.
    SelectionStep extentOf(AST *ast) const
    {
        const unsigned firstTokenIndex = ast->firstToken();
        const unsigned lastTokenIndex = ast->lastToken();

        SelectionStep extent(tokenStart(firstTokenIndex), tokenStart(lastTokenIndex));
        if (lastTokenIndex != firstTokenIndex)
            extent.end = tokenEnd(lastTokenIndex - 1);
        return extent;
    }

    bool isCursorIn(int start, int end) const
    {
        return m_cursorAnchor >= start && m_cursorPosition <= end;
    }

    // The contents of a scope, without its braces -- or, where there is
    // nothing between them, the blank space itself.
    SelectionStep contentsOfBraces(AST *ast) const
    {
        const unsigned firstTokenIndex = ast->firstToken();
        const unsigned secondToLastTokenIndex = ast->lastToken() - 1;

        // TODO: If the empty space has a new tab character, or spaces, and the document is
        // not saved, the last semantic info is not updated, and the selection is not
        // properly computed. Figure out how to work around this.
        if (secondToLastTokenIndex - firstTokenIndex <= 1) {
            return {tokenEnd(firstTokenIndex), tokenStart(secondToLastTokenIndex)};
        }
        return {tokenStart(firstTokenIndex + 1), tokenEnd(secondToLastTokenIndex - 1)};
    }

    // What a literal says, without the quotes around it.
    SelectionStep contentsOfLiteral(const Token &token, const SelectionStep &extent) const
    {
        int end = extent.end - 1;
        int start = 0;
        if (token.isCharLiteral()) {
            start = end - token.literal->size();
        } else {
            const bool isRawLiteral = token.isRawStringLiteral();
            if (debug && isRawLiteral)
                qDebug() << "Is raw literal.";

            // A raw literal has a parenthesis inside each quote.
            if (isRawLiteral)
                --end;
            start = end - QString::fromUtf8(token.string->chars()).size();
            if (isRawLiteral)
                start += 2;
        }
        return {start, end};
    }

    SelectionSteps stepsOf(AST *ast) const;

    const Document::Ptr m_doc;
    TranslationUnit * const m_unit;
    const QTextDocument * const m_text;
    const int m_cursorAnchor;
    const int m_cursorPosition;
    const QTextCursor m_cursor;
};

SelectionSteps BuiltinTree::stepsOf(AST *ast) const
{
    const SelectionStep extent = extentOf(ast);

    if (ast->asCompoundStatement()) {
        // First the contents of the scope, and then the contents together
        // with the braces.
        return {contentsOfBraces(ast), extent};
    }

    if (CallAST *callAST = ast->asCall()) {
        const int lparen = tokenStart(callAST->lparen_token);
        const int rparen = tokenEnd(callAST->rparen_token);

        // With the cursor in the function name, the name is selected
        // implicitly -- it is a node of its own -- and then the whole call.
        if (m_cursorPosition <= lparen)
            return {extent};

        // With the cursor inside the parentheses: what is between them, then
        // them as well, then the whole call.
        return {{lparen + 1, rparen - 1}, {lparen, rparen}, extent};
    }

    if (StringLiteralAST *stringLiteralAST = ast->asStringLiteral()) {
        const Token token = m_unit->tokenAt(stringLiteralAST->firstToken());
        return {contentsOfLiteral(token, extent), extent};
    }

    if (NumericLiteralAST *numericLiteralAST = ast->asNumericLiteral()) {
        const Token token = m_unit->tokenAt(numericLiteralAST->firstToken());
        if (!token.isCharLiteral())
            return {extent};
        return {contentsOfLiteral(token, extent), extent};
    }

    if (ForStatementAST *forStatementAST = ast->asForStatement()) {
        const int lparen = tokenStart(forStatementAST->lparen_token);
        const int rparen = tokenEnd(forStatementAST->rparen_token);
        if (m_cursorPosition <= lparen)
            return {extent};
        return {{lparen + 1, rparen - 1}, {lparen, rparen}, extent};
    }

    if (RangeBasedForStatementAST *rangeForStatementAST = ast->asRangeBasedForStatement()) {
        const int lparen = tokenStart(rangeForStatementAST->lparen_token);
        const int rparen = tokenEnd(rangeForStatementAST->rparen_token);
        if (m_cursorPosition <= lparen)
            return {extent};
        return {{lparen + 1, rparen - 1}, {lparen, rparen}, extent};
    }

    if (ClassSpecifierAST *classSpecifierAST = ast->asClassSpecifier()) {
        const int lbrace = tokenStart(classSpecifierAST->lbrace_token);
        const int rbrace = tokenEnd(classSpecifierAST->rbrace_token);
        const int keywordStart = tokenStart(classSpecifierAST->classkey_token);
        const int keywordEnd = tokenEnd(classSpecifierAST->classkey_token);

        // Where the class is named, the name is the second half of what the
        // keyword step selects; where it is not, that step reaches to the end.
        int nameEnd = rbrace;
        bool isInClassName = false;
        if (NameAST *nameAST = classSpecifierAST->name) {
            if (SimpleNameAST *classNameAST = nameAST->asSimpleName()) {
                const unsigned identifierTokenIndex = classNameAST->identifier_token;
                nameEnd = tokenEnd(identifierTokenIndex);
                isInClassName = isCursorIn(tokenStart(identifierTokenIndex), nameEnd);
            }
        }

        if (m_cursorPosition > lbrace)
            return {{lbrace + 1, rbrace - 1}, {lbrace, rbrace}, extent};
        if (isCursorIn(keywordStart, keywordEnd))
            return {{keywordStart, keywordEnd}, {keywordStart, nameEnd}, extent};
        if (isInClassName)
            return {{keywordStart, nameEnd}, extent};
        return {extent};
    }

    if (NamespaceAST *namespaceAST = ast->asNamespace()) {
        const int keywordStart = tokenStart(namespaceAST->namespace_token);
        const int keywordEnd = tokenEnd(namespaceAST->namespace_token);
        const int identifierStart = tokenStart(namespaceAST->identifier_token);
        const int identifierEnd = tokenEnd(namespaceAST->identifier_token);

        if (m_cursorPosition <= keywordEnd)
            return {{keywordStart, keywordEnd}, {keywordStart, identifierEnd}, extent};
        if (isCursorIn(identifierStart, identifierEnd))
            return {{identifierStart, identifierEnd}, {keywordStart, identifierEnd}, extent};
        return {extent};
    }

    if (ExpressionListParenAST *parenAST = ast->asExpressionListParen()) {
        const int lparen = tokenStart(parenAST->lparen_token);
        const int rparen = tokenEnd(parenAST->rparen_token);
        return {{lparen + 1, rparen - 1}, {lparen, rparen}};
    }

    if (FunctionDeclaratorAST *functionDeclaratorAST = ast->asFunctionDeclarator()) {
        // The parameters and the parentheses around them. What is written
        // after them belongs to the declarator this is part of.
        return {{tokenStart(functionDeclaratorAST->lparen_token),
                 tokenEnd(functionDeclaratorAST->rparen_token)}};
    }

    if (FunctionDefinitionAST *functionDefinitionAST = ast->asFunctionDefinition()) {
        // Everything to the left of the braces -- the return type, the name
        // and the parameters -- before the definition as a whole, and only
        // when the cursor is not in the body.
        if (!functionDefinitionAST->function_body)
            return {extent};

        CompoundStatementAST *compoundStatementAST =
                functionDefinitionAST->function_body->asCompoundStatement();
        if (!compoundStatementAST)
            return {extent};

        if (!functionDefinitionAST->decl_specifier_list
                || !functionDefinitionAST->decl_specifier_list->value) {
            return {extent};
        }

        SimpleSpecifierAST *simpleSpecifierAST =
                functionDefinitionAST->decl_specifier_list->value->asSimpleSpecifier();
        if (!simpleSpecifierAST)
            return {extent};

        const int lbrace = tokenStart(compoundStatementAST->lbrace_token);
        if (m_cursorPosition > lbrace)
            return {extent};

        return {{tokenStart(simpleSpecifierAST->firstToken()), lbrace - 1}, extent};
    }

    if (DeclaratorAST *declaratorAST = ast->asDeclarator()) {
        // The declarator without its cv qualifiers, before the whole of it.
        PostfixDeclaratorListAST *list = declaratorAST->postfix_declarator_list;
        if (!list || !list->value)
            return {extent};

        FunctionDeclaratorAST *functionDeclarator = list->value->asFunctionDeclarator();
        if (!functionDeclarator)
            return {extent};

        SpecifierListAST *cvList = functionDeclarator->cv_qualifier_list;
        if (!cvList || !cvList->value)
            return {extent};

        const int cvStart = tokenStart(cvList->value->firstToken());
        if (m_cursorPosition >= cvStart)
            return {extent};

        return {{extent.start, cvStart - 1}, extent};
    }

    if (TemplateIdAST *templateIdAST = ast->asTemplateId()) {
        // The name a template is instantiated by, before the instantiation.
        const int identifierStart = tokenStart(templateIdAST->identifier_token);
        const int identifierEnd = tokenEnd(templateIdAST->identifier_token);
        if (!isCursorIn(identifierStart, identifierEnd))
            return {extent};
        return {{identifierStart, identifierEnd}, extent};
    }

    if (TemplateDeclarationAST *templateDeclarationAST = ast->asTemplateDeclaration()) {
        const int keywordStart = tokenStart(templateDeclarationAST->template_token);
        const int keywordEnd = tokenEnd(templateDeclarationAST->template_token);
        if (!isCursorIn(keywordStart, keywordEnd))
            return {extent};

        // The keyword, then the keyword with the parameters it introduces.
        return {{keywordStart, keywordEnd},
                {keywordStart, tokenEnd(templateDeclarationAST->greater_token)},
                extent};
    }

    if (LambdaExpressionAST *lambdaExpressionAST = ast->asLambdaExpression()) {
        // TODO: Fix more lambda cases.
        LambdaDeclaratorAST *lambdaDeclaratorAST = lambdaExpressionAST->lambda_declarator;
        if (!lambdaDeclaratorAST)
            return {extent};

        const int lbracket
            = tokenStart(lambdaExpressionAST->lambda_introducer->lbracket_token);
        const int rparen = tokenEnd(lambdaDeclaratorAST->rparen_token);
        if (!isCursorIn(lbracket, rparen))
            return {extent};

        // The capture group with the arguments, then the prototype where
        // there is a return type written after them, then the whole lambda.
        SelectionSteps steps{{lbracket, rparen}};
        if (TrailingReturnTypeAST *trailingReturnTypeAST
            = lambdaDeclaratorAST->trailing_return_type) {
            steps.append({lbracket, tokenEnd(trailingReturnTypeAST->lastToken()) - 2});
        }
        steps.append(extent);
        return steps;
    }

    return {extent};
}

#ifdef QTC_WITH_CXX_FRONTEND

// The same, read off the cxx-frontend model's tree.
//
// Which constructs have places inside them worth stopping at is a fact about
// C++ rather than about a tree, so these are the cases above, one for one.
// What differs is where each part is written down: cxx keeps the parameters
// of a lambda on the lambda itself and the qualifiers of a member function on
// a chunk of its declarator, and it says which token is which rather than
// counting from the ends of a node.
class CxxTree
{
public:
    CxxTree(const CxxFrontendDocument &document, QTextDocument *text, const QTextCursor &cursor)
        : m_document(document)
        , m_text(text)
        , m_cursorAnchor(cursor.anchor())
        , m_cursorPosition(cursor.position())
        , m_line(cursor.blockNumber() + 1)
        , m_column(cursor.positionInBlock() + 1)
    {}

    SelectionPath path() const
    {
        SelectionPath path;
        const QList<cxx::AST *> nodes = cxxAstPathAt(m_document, m_line, m_column);
        for (cxx::AST *node : nodes) {
            // A node with nothing written where it stands -- one a macro
            // stood for -- is not a place a selection can grow to.
            const SelectionSteps steps = stepsOf(node);
            if (!steps.isEmpty())
                path.append(steps);
        }
        return path;
    }

private:
    int startOf(const CxxAstRange &range) const
    {
        return m_text->findBlockByNumber(range.startLine - 1).position() + range.startColumn - 1;
    }

    int endOf(const CxxAstRange &range) const
    {
        return m_text->findBlockByNumber(range.endLine - 1).position() + range.endColumn - 1;
    }

    SelectionStep extentOf(cxx::AST *node) const
    {
        if (!node)
            return {};
        const CxxAstRange range = cxxAstRangeOf(m_document, node);
        if (!range.isValid())
            return {};
        return {startOf(range), endOf(range)};
    }

    // Where one token stands, and where a pair of them stands together.
    SelectionStep placeOf(cxx::SourceLocation location) const
    {
        const CxxAstRange range = cxxTokenRangeAt(m_document, location);
        if (!range.isValid())
            return {};
        return {startOf(range), endOf(range)};
    }

    SelectionStep placeBetween(cxx::SourceLocation first, cxx::SourceLocation last) const
    {
        const SelectionStep firstPlace = placeOf(first);
        const SelectionStep lastPlace = placeOf(last);
        if (!firstPlace || !lastPlace)
            return {};
        return {firstPlace.start, lastPlace.end};
    }

    bool isCursorIn(const SelectionStep &step) const
    {
        return m_cursorAnchor >= step.start && m_cursorPosition <= step.end;
    }

    QString textOf(const SelectionStep &step) const
    {
        QTextCursor cursor(m_text);
        cursor.setPosition(step.start);
        cursor.setPosition(step.end, QTextCursor::KeepAnchor);
        return cursor.selectedText();
    }

    // What a literal says, without the quotes around it -- and, where it is a
    // raw string, without the parentheses inside them either. Read off the
    // text: how much of a literal is punctuation is a question about how it
    // was written, prefix and delimiter and all.
    SelectionStep contentsOfLiteral(const SelectionStep &extent) const
    {
        const QString spelling = textOf(extent);

        // The first quote of either kind: a string may hold an apostrophe and
        // a char literal a quotation mark, so which one opens the literal is
        // whichever comes first, and it is the one that closes it too.
        qsizetype firstQuote = -1;
        for (qsizetype i = 0; i < spelling.size() && firstQuote < 0; ++i) {
            if (spelling.at(i) == u'"' || spelling.at(i) == u'\'')
                firstQuote = i;
        }
        if (firstQuote < 0)
            return {};
        const QChar quote = spelling.at(firstQuote);

        const bool isRaw = firstQuote > 0 && spelling.at(firstQuote - 1) == u'R';
        const qsizetype open = isRaw ? spelling.indexOf(u'(') : firstQuote;
        const qsizetype close = isRaw ? spelling.lastIndexOf(u')') : spelling.lastIndexOf(quote);
        if (open < 0 || close <= open)
            return {};

        return {extent.start + int(open) + 1, extent.start + int(close)};
    }

    // The statements between the braces of a scope, or -- where there are
    // none -- the blank space itself.
    SelectionStep contentsOfBraces(cxx::List<cxx::StatementAST *> *statements,
                                   cxx::SourceLocation lbraceLoc,
                                   cxx::SourceLocation rbraceLoc) const
    {
        const SelectionStep lbrace = placeOf(lbraceLoc);
        const SelectionStep rbrace = placeOf(rbraceLoc);
        if (!lbrace || !rbrace)
            return {};
        if (!statements)
            return {lbrace.end, rbrace.start};

        cxx::List<cxx::StatementAST *> *last = statements;
        while (last->next)
            last = last->next;

        const SelectionStep first = extentOf(statements->value);
        const SelectionStep lastStatement = extentOf(last->value);
        if (!first || !lastStatement)
            return {};
        return {first.start, lastStatement.end};
    }

    // What is inside the parentheses, then them as well, then the whole
    // construct -- and with the cursor in front of them, only the last of
    // those, since what is inside is a node of its own.
    SelectionSteps stepsAroundParentheses(cxx::SourceLocation lparenLoc,
                                          cxx::SourceLocation rparenLoc,
                                          const SelectionStep &extent) const
    {
        const SelectionStep parens = placeBetween(lparenLoc, rparenLoc);
        if (!parens || m_cursorPosition <= parens.start)
            return {extent};
        return {{parens.start + 1, parens.end - 1}, parens, extent};
    }

    SelectionSteps stepsOfClass(cxx::ClassSpecifierAST *specifier,
                                const SelectionStep &extent) const
    {
        const SelectionStep braces = placeBetween(specifier->lbraceLoc, specifier->rbraceLoc);
        const SelectionStep keyword = placeOf(specifier->classLoc);
        if (!braces || !keyword)
            return {extent};

        int nameEnd = braces.end;
        bool isInClassName = false;
        if (auto * const name = dynamic_cast<cxx::NameIdAST *>(specifier->unqualifiedId)) {
            if (const SelectionStep identifier = placeOf(name->identifierLoc)) {
                nameEnd = identifier.end;
                isInClassName = isCursorIn(identifier);
            }
        }

        if (m_cursorPosition > braces.start)
            return {{braces.start + 1, braces.end - 1}, braces, extent};
        if (isCursorIn(keyword))
            return {keyword, {keyword.start, nameEnd}, extent};
        if (isInClassName)
            return {{keyword.start, nameEnd}, extent};
        return {extent};
    }

    SelectionSteps stepsOfNamespace(cxx::NamespaceDefinitionAST *definition,
                                    const SelectionStep &extent) const
    {
        const SelectionStep keyword = placeOf(definition->namespaceLoc);
        const SelectionStep identifier = placeOf(definition->identifierLoc);
        if (!keyword || !identifier)
            return {extent};

        if (m_cursorPosition <= keyword.end)
            return {keyword, {keyword.start, identifier.end}, extent};
        if (isCursorIn(identifier))
            return {identifier, {keyword.start, identifier.end}, extent};
        return {extent};
    }

    SelectionSteps stepsOfFunctionDefinition(cxx::FunctionDefinitionAST *definition,
                                             const SelectionStep &extent) const
    {
        // Everything to the left of the body, before the definition as a
        // whole, and only with the cursor outside that body.
        auto * const body
            = dynamic_cast<cxx::CompoundStatementFunctionBodyAST *>(definition->functionBody);
        if (!body || !body->statement || !definition->declSpecifierList)
            return {extent};

        const SelectionStep lbrace = placeOf(body->statement->lbraceLoc);
        const SelectionStep specifier = extentOf(definition->declSpecifierList->value);
        if (!lbrace || !specifier || m_cursorPosition > lbrace.start)
            return {extent};

        return {{specifier.start, lbrace.start - 1}, extent};
    }

    SelectionSteps stepsOfDeclarator(cxx::DeclaratorAST *declarator,
                                     const SelectionStep &extent) const
    {
        // The declarator without the qualifiers written after its parameters,
        // before the whole of it.
        cxx::List<cxx::DeclaratorChunkAST *> * const chunks = declarator->declaratorChunkList;
        if (!chunks)
            return {extent};

        auto * const chunk = dynamic_cast<cxx::FunctionDeclaratorChunkAST *>(chunks->value);
        if (!chunk || !chunk->cvQualifierList)
            return {extent};

        const SelectionStep cvQualifier = extentOf(chunk->cvQualifierList->value);
        if (!cvQualifier || m_cursorPosition >= cvQualifier.start)
            return {extent};

        return {{extent.start, cvQualifier.start - 1}, extent};
    }

    SelectionSteps stepsOfLambda(cxx::LambdaExpressionAST *lambda,
                                 const SelectionStep &extent) const
    {
        const SelectionStep captures = placeBetween(lambda->lbracketLoc, lambda->rparenLoc);
        if (!captures || !isCursorIn(captures))
            return {extent};

        const SelectionStep returnType = lambda->trailingReturnType
                                             ? extentOf(lambda->trailingReturnType)
                                             : SelectionStep();
        SelectionSteps steps;

        // What a lambda declares of itself: its parameters, and the return
        // type where one is written after them. The built-in tree has a node
        // for exactly that much and cxx keeps the parts on the lambda, so the
        // step is written out here rather than found -- under the same
        // condition a node would put it: that the cursor is in it.
        if (const SelectionStep parameters = placeBetween(lambda->lparenLoc, lambda->rparenLoc)) {
            const SelectionStep declarator = returnType
                                                 ? SelectionStep(parameters.start, returnType.end)
                                                 : parameters;
            if (isCursorIn(declarator))
                steps.append(declarator);
        }

        // Then the capture group with the parameters, the prototype where
        // there is a return type, and the whole lambda.
        steps.append(captures);
        if (returnType)
            steps.append({captures.start, returnType.end});
        steps.append(extent);
        return steps;
    }

    SelectionSteps stepsOf(cxx::AST *node) const;

    const CxxFrontendDocument &m_document;
    QTextDocument * const m_text;
    const int m_cursorAnchor;
    const int m_cursorPosition;
    const int m_line;
    const int m_column;
};

SelectionSteps CxxTree::stepsOf(cxx::AST *node) const
{
    const SelectionStep extent = extentOf(node);
    if (!extent)
        return {};

    // An initializer is a node here and part of the declarator in the
    // built-in tree, and what it stands for -- the equals sign with the value
    // after it -- is not something to select: the value is a node of its own,
    // and what has the equals sign in it as well is the declaration. So it is
    // no stop on the way out.
    if (dynamic_cast<cxx::EqualInitializerAST *>(node))
        return {};

    if (auto * const statement = dynamic_cast<cxx::CompoundStatementAST *>(node)) {
        const SelectionStep contents = contentsOfBraces(statement->statementList,
                                                        statement->lbraceLoc,
                                                        statement->rbraceLoc);
        if (!contents)
            return {extent};
        return {contents, extent};
    }

    if (auto * const call = dynamic_cast<cxx::CallExpressionAST *>(node))
        return stepsAroundParentheses(call->lparenLoc, call->rparenLoc, extent);

    if (dynamic_cast<cxx::StringLiteralExpressionAST *>(node)
        || dynamic_cast<cxx::CharLiteralExpressionAST *>(node)) {
        const SelectionStep contents = contentsOfLiteral(extent);
        if (!contents)
            return {extent};
        return {contents, extent};
    }

    if (auto * const statement = dynamic_cast<cxx::ForStatementAST *>(node))
        return stepsAroundParentheses(statement->lparenLoc, statement->rparenLoc, extent);

    if (auto * const statement = dynamic_cast<cxx::ForRangeStatementAST *>(node))
        return stepsAroundParentheses(statement->lparenLoc, statement->rparenLoc, extent);

    if (auto * const specifier = dynamic_cast<cxx::ClassSpecifierAST *>(node))
        return stepsOfClass(specifier, extent);

    if (auto * const definition = dynamic_cast<cxx::NamespaceDefinitionAST *>(node))
        return stepsOfNamespace(definition, extent);

    if (auto * const initializer = dynamic_cast<cxx::ParenInitializerAST *>(node)) {
        const SelectionStep parens = placeBetween(initializer->lparenLoc,
                                                  initializer->rparenLoc);
        if (!parens)
            return {extent};
        return {{parens.start + 1, parens.end - 1}, parens};
    }

    if (auto * const chunk = dynamic_cast<cxx::FunctionDeclaratorChunkAST *>(node)) {
        // The parameters and the parentheses around them. What is written
        // after them belongs to the declarator this is a chunk of.
        const SelectionStep parens = placeBetween(chunk->lparenLoc, chunk->rparenLoc);
        if (!parens)
            return {extent};
        return {parens};
    }

    if (auto * const definition = dynamic_cast<cxx::FunctionDefinitionAST *>(node))
        return stepsOfFunctionDefinition(definition, extent);

    if (auto * const declarator = dynamic_cast<cxx::DeclaratorAST *>(node))
        return stepsOfDeclarator(declarator, extent);

    if (auto * const templateId = dynamic_cast<cxx::SimpleTemplateIdAST *>(node)) {
        // The name a template is instantiated by, before the instantiation.
        const SelectionStep identifier = placeOf(templateId->identifierLoc);
        if (!identifier || !isCursorIn(identifier))
            return {extent};
        return {identifier, extent};
    }

    if (auto * const declaration = dynamic_cast<cxx::TemplateDeclarationAST *>(node)) {
        const SelectionStep keyword = placeOf(declaration->templateLoc);
        const SelectionStep greater = placeOf(declaration->greaterLoc);
        if (!keyword || !greater || !isCursorIn(keyword))
            return {extent};

        // The keyword, then the keyword with the parameters it introduces.
        return {keyword, {keyword.start, greater.end}, extent};
    }

    if (auto * const lambda = dynamic_cast<cxx::LambdaExpressionAST *>(node))
        return stepsOfLambda(lambda, extent);

    return {extent};
}

#endif // QTC_WITH_CXX_FRONTEND

// The places to stop at around the cursor.
SelectionPath selectionPathAt(const Document::Ptr &doc, QTextDocument *text,
                              const QTextCursor &cursor)
{
#ifdef QTC_WITH_CXX_FRONTEND
    // The other model, where it has read this file. It has not unless it was
    // asked for, and a file it could not read at all offers nothing, so an
    // empty answer means the built-in tree answers the way it always did.
    if (const std::shared_ptr<const CxxFrontendSnapshot> model
        = Internal::cxxFrontendModel(doc->filePath())) {
        if (const CxxFrontendDocument * const document
            = model->document(doc->filePath().toFSPathString())) {
            const SelectionPath path = CxxTree(*document, text, cursor).path();
            if (!path.isEmpty())
                return path;
        }
    }
#endif

    return BuiltinTree(doc, text, cursor).path();
}

} // end of anonymous namespace

bool CppSelectionChanger::shouldSkipStep(
        const SelectionStep &step,
        const QTextCursor &cursor) const
{
    bool shouldSkipNode = false;

    bool isEqual = cursor.anchor() == step.start && cursor.position() == step.end;

    // New selections should include initial selection.
    bool includesInitialSelection =
            m_initialChangeSelectionCursor.anchor() >= step.start &&
            m_initialChangeSelectionCursor.position() <= step.end;

    // Prefer new selections to start with initial cursor if anchor == position.
    if (!m_initialChangeSelectionCursor.hasSelection())
        includesInitialSelection = m_initialChangeSelectionCursor.position() < step.end;

    // When expanding: Skip if new selection is smaller than current cursor selection.
    // When shrinking: Skip if new selection is bigger than current cursor selection.
    bool isNewSelectionSmaller = step.start > cursor.anchor() || step.end < cursor.position();
    bool isNewSelectionBigger = step.start < cursor.anchor() || step.end > cursor.position();

    if (m_direction == CppSelectionChanger::ExpandSelection
        && (isNewSelectionSmaller || isEqual || !includesInitialSelection)) {
        shouldSkipNode = true;
    } else if (m_direction == CppSelectionChanger::ShrinkSelection
               && (isNewSelectionBigger || isEqual || !includesInitialSelection)) {
        shouldSkipNode = true;
    }

    if (debug && shouldSkipNode) {
        qDebug() << "isEqual:" << isEqual << "includesInitialSelection:" << includesInitialSelection
                 << "isNewSelectionSmaller:" << isNewSelectionSmaller << "isNewSelectionBigger:"
                 << isNewSelectionBigger;
    }

    return shouldSkipNode;
}

void CppSelectionChanger::updateCursorSelection(
        QTextCursor &cursorToModify,
        SelectionStep step)
{
    m_workingCursor.setPosition(step.start, QTextCursor::MoveAnchor);
    m_workingCursor.setPosition(step.end, QTextCursor::KeepAnchor);
    cursorToModify = m_workingCursor;

    if (debug) {
        qDebug() << "Anchor is now: " << m_workingCursor.anchor();
        qDebug() << "Position is now: " << m_workingCursor.position();
    }
}

// The first step of the node at \a nodeIndex, and the walk is in that node
// from now on. Nothing where the path does not reach that far.
SelectionStep CppSelectionChanger::stepInNode(int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= m_path.size() || m_path.at(nodeIndex).isEmpty()) {
        setNodeIndexAndStep(NodeIndexAndStepNotSet);
        return {};
    }

    const SelectionSteps &steps = m_path.at(nodeIndex);
    m_changeSelectionNodeIndex = nodeIndex;
    m_nodeCurrentStep = m_direction == ExpandSelection ? 1 : steps.size();

    if (debug)
        qDebug() << "Walking node" << nodeIndex << "of" << steps.size() << "steps.";

    return steps.at(m_nodeCurrentStep - 1);
}

// The next step of the node the walk is in, or the first step of the node
// outside it -- inside it, when shrinking.
SelectionStep CppSelectionChanger::stepInNextNodeOrStep()
{
    // The file may have been parsed again between two steps, and its tree no
    // longer reach where the walk was.
    if (m_changeSelectionNodeIndex >= m_path.size())
        return {};

    const SelectionSteps &steps = m_path.at(m_changeSelectionNodeIndex);
    if (m_nodeCurrentStep > steps.size())
        return {};

    const bool isLastStep = m_direction == ExpandSelection ? m_nodeCurrentStep == steps.size()
                                                           : m_nodeCurrentStep == 1;
    if (isLastStep) {
        const int nextNodeIndex
            = m_changeSelectionNodeIndex + (m_direction == ExpandSelection ? -1 : 1);
        if (nextNodeIndex < 0 || nextNodeIndex >= m_path.size()) {
            if (debug)
                qDebug() << "Skipping expansion because there is no available next AST node.";
            return {};
        }
        return stepInNode(nextNodeIndex);
    }

    m_nodeCurrentStep += m_direction == ExpandSelection ? 1 : -1;
    if (debug)
        qDebug() << "Moved to step" << m_nodeCurrentStep << "of the same node.";

    return steps.at(m_nodeCurrentStep - 1);
}

SelectionStep CppSelectionChanger::findNextStep()
{
    if (m_path.isEmpty())
        return {};

    SelectionStep step;
    if (m_changeSelectionNodeIndex == kChangeSelectionNodeIndexNotSet) {
        // The first step of the walk: the innermost node when expanding, the
        // outermost one when shrinking.
        step = stepInNode(m_direction == ExpandSelection ? m_path.size() - 1 : 0);
    } else if (m_changeSelectionNodeIndex == kChangeSelectionNodeIndexWholeDocoument) {
        // Can't expand more, because whole document is selected. In case of
        // shrink, select the next smaller selection.
        if (m_direction == ExpandSelection)
            return {};
        step = stepInNode(0);
    } else {
        step = stepInNextNodeOrStep();
    }

    if (debug) {
        qDebug() << "m_changeSelectionNodeIndex:" << m_changeSelectionNodeIndex
                 << "current step:" << m_nodeCurrentStep;
    }

    QTC_ASSERT(m_nodeCurrentStep >= 1, return {});

    return step;
}

bool CppSelectionChanger::performSelectionChange(QTextCursor &cursorToModify)
{
    forever {
        if (const SelectionStep step = findNextStep()) {
            if (!shouldSkipStep(step, m_workingCursor)) {
                updateCursorSelection(cursorToModify, step);
                return true;
            } else {
                if (debug)
                    qDebug() << "Skipping node.";
            }
        } else if (m_direction == ShrinkSelection) {
            // The last possible action to do, if there was no step with a smaller selection, is
            // to set the cursor to the initial change selection cursor, without an anchor.
            QTextCursor finalCursor(m_initialChangeSelectionCursor);
            finalCursor.setPosition(finalCursor.position(), QTextCursor::MoveAnchor);
            cursorToModify = finalCursor;
            setNodeIndexAndStep(NodeIndexAndStepNotSet);
            if (debug)
                qDebug() << "Final shrink selection case.";
            return true;
        } else if (m_direction == ExpandSelection) {
            // The last possible action to do, if there was no step with a bigger selection, is
            // to set the cursor to the whole document including header inclusions.
            QTextCursor finalCursor = getWholeDocumentCursor(m_initialChangeSelectionCursor);
            cursorToModify = finalCursor;
            setNodeIndexAndStep(NodeIndexAndStepWholeDocument);
            if (debug)
                qDebug() << "Final expand selection case.";
            return true;
        }
        // Break out of the loop, because no further modification of the selection can be done.
        else break;
    }

    // No next step found for given direction, return early without modifying the cursor.
    return false;
}

void CppSelectionChanger::setNodeIndexAndStep(NodeIndexAndStepState state)
{
    switch (state) {
        case NodeIndexAndStepWholeDocument:
            m_changeSelectionNodeIndex = kChangeSelectionNodeIndexWholeDocoument;
            m_nodeCurrentStep = kChangeSelectionNodeIndexWholeDocoument;
            break;
        case NodeIndexAndStepNotSet:
        default:
            m_changeSelectionNodeIndex = kChangeSelectionNodeIndexNotSet;
            m_nodeCurrentStep = kChangeSelectionNodeIndexNotSet;
            break;
    }
}

bool CppSelectionChanger::changeSelection(
        Direction direction,
        QTextCursor &cursorToModify,
        const CPlusPlus::Document::Ptr doc)
{
    m_workingCursor = cursorToModify;

    if (hasNoSelectionAndShrinking(direction, m_workingCursor))
        return false;

    if (isWholeDocumentSelectedAndExpanding(direction, m_workingCursor))
        return false;

    if (!isDocumentAvailable(doc)) {
        return false;
    }

    ensureCursorSelectionIsNotFlipped(m_workingCursor);

    m_direction = direction;

    // The path is read from the initial change selection cursor, the one the
    // walk started at, rather than from the selection it has grown to.
    m_path = selectionPathAt(doc, m_workingCursor.document(), m_initialChangeSelectionCursor);

    return performSelectionChange(cursorToModify);
}

void CppSelectionChanger::startChangeSelection()
{
    // Stop cursorPositionChanged signal handler from setting the initial
    // change selection cursor, when the cursor is being changed as a result of the change
    // selection operation.
    m_inChangeSelection = true;
}

void CppSelectionChanger::stopChangeSelection()
{
    m_inChangeSelection = false;
}

} // namespace CppEditor
