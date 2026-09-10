// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "logicaloperationquickfixes.h"

#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#include <cxx/token.h>
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

// Each of the three operations below is handed the stretches of text it
// rewrites, rather than the nodes they were read off: which node an operand
// is depends on which front end read the file, and what stands in the text
// does not.

class FlipLogicalOperandsOp : public CppQuickFixOperation
{
public:
    FlipLogicalOperandsOp(const CppQuickFixInterface &interface, int priority,
                          const ChangeSet::Range &left, const ChangeSet::Range &right,
                          const ChangeSet::Range &op, QString replacement)
        : CppQuickFixOperation(interface)
        , m_left(left)
        , m_right(right)
        , m_op(op)
        , m_replacement(replacement)
    {
        setPriority(priority);
    }

    QString description() const override
    {
        if (m_replacement.isEmpty())
            return Tr::tr("Swap Operands");
        else
            return Tr::tr("Rewrite Using %1").arg(m_replacement);
    }

    void perform() override
    {
        ChangeSet changes;
        changes.flip(m_left, m_right);
        if (!m_replacement.isEmpty())
            changes.replace(m_op, m_replacement);

        currentFile()->apply(changes);
    }

private:
    const ChangeSet::Range m_left;
    const ChangeSet::Range m_right;
    const ChangeSet::Range m_op;
    const QString m_replacement;
};

class InverseLogicalComparisonOp : public CppQuickFixOperation
{
public:
    // Where the negation goes. A comparison already in parentheses only needs
    // a "!" in front of them, and one that is already negated needs that "!"
    // taken away -- the parentheses stay either way, since removing them
    // could change what binds to what.
    enum class Negate { RemoveTheOneThere, BeforeTheParentheses, AroundTheComparison };

    InverseLogicalComparisonOp(const CppQuickFixInterface &interface, int priority,
                               const ChangeSet::Range &op, QString replacement,
                               Negate how, const ChangeSet::Range &where)
        : CppQuickFixOperation(interface, priority)
        , m_op(op)
        , m_replacement(replacement)
        , m_how(how)
        , m_where(where)
    {}

    QString description() const override
    {
        return Tr::tr("Rewrite Using %1").arg(m_replacement);
    }

    void perform() override
    {
        ChangeSet changes;
        switch (m_how) {
        case Negate::RemoveTheOneThere:
            changes.remove(m_where);
            break;
        case Negate::BeforeTheParentheses:
            changes.insert(m_where.start, QLatin1String("!"));
            break;
        case Negate::AroundTheComparison:
            changes.insert(m_where.start, QLatin1String("!("));
            changes.insert(m_where.end, QLatin1String(")"));
            break;
        }
        changes.replace(m_op, m_replacement);
        currentFile()->apply(changes);
    }

private:
    const ChangeSet::Range m_op;
    const QString m_replacement;
    const Negate m_how;
    const ChangeSet::Range m_where;
};

class RewriteLogicalAndOp : public CppQuickFixOperation
{
public:
    RewriteLogicalAndOp(const CppQuickFixInterface &interface, int priority,
                        const ChangeSet::Range &op, const ChangeSet::Range &leftNegation,
                        const ChangeSet::Range &rightNegation,
                        const ChangeSet::Range &expression)
        : CppQuickFixOperation(interface, priority)
        , m_op(op)
        , m_leftNegation(leftNegation)
        , m_rightNegation(rightNegation)
        , m_expression(expression)
    {
        setDescription(Tr::tr("Rewrite Condition Using ||"));
    }

    void perform() override
    {
        ChangeSet changes;
        changes.replace(m_op, QLatin1String("||"));
        changes.remove(m_leftNegation);
        changes.remove(m_rightNegation);
        changes.insert(m_expression.start, QLatin1String("!("));
        changes.insert(m_expression.end, QLatin1String(")"));

        currentFile()->apply(changes);
    }

private:
    const ChangeSet::Range m_op;
    const ChangeSet::Range m_leftNegation;
    const ChangeSet::Range m_rightNegation;
    const ChangeSet::Range m_expression;
};

#ifdef QTC_WITH_CXX_FRONTEND
// What the model paths below all begin with: the file the cursor is in, as
// the other front end read it, or nothing where it has not read it -- it is
// off unless asked for. See cxxfrontendmodel.h.
const CxxFrontendDocument *cxxFrontendDocumentFor(const CppQuickFixInterface &interface)
{
    const CppRefactoringFilePtr file = interface.currentFile();
    const std::shared_ptr<const CxxFrontendSnapshot> model = cxxFrontendModel(file->filePath());
    if (!model)
        return nullptr;
    return model->document(file->filePath().toFSPathString());
}

// The innermost binary expression the cursor is in, with the cursor on its
// operator -- which is what all three fixes activate on -- and where in the
// path it was found, since two of them read the nodes around it. Null
// otherwise.
cxx::BinaryExpressionAST *binaryUnderCursor(const CxxFrontendDocument &document,
                                            const CppQuickFixInterface &interface,
                                            const QList<cxx::AST *> &path, int *index)
{
    const CppRefactoringFilePtr file = interface.currentFile();
    for (*index = path.size() - 1; *index >= 0; --(*index)) {
        auto * const binary = dynamic_cast<cxx::BinaryExpressionAST *>(path.at(*index));
        if (!binary)
            continue;

        // Asked here rather than left to the path: the cursor is in the whole
        // expression at any of its characters, and these fixes are offered on
        // the operator alone.
        const CxxAstRange op = cxxTokenRangeAt(document, binary->opLoc);
        if (!op.isValid())
            return nullptr;
        const int cursorPosition = file->cursor().selectionStart();
        if (cursorPosition < file->position(op.startLine, op.startColumn)
            || cursorPosition > file->position(op.endLine, op.endColumn)) {
            return nullptr;
        }
        return binary;
    }
    return nullptr;
}

// A node's text as the editor counts it, or nothing where the file does not
// write the node -- inside a macro's body, which is where these fixes give up
// and let the built-in path answer.
std::optional<ChangeSet::Range> rangeOf(const CxxFrontendDocument &document,
                                        const CppRefactoringFilePtr &file, cxx::AST *node)
{
    const CxxAstRange range = cxxAstRangeOf(document, node);
    if (!range.isValid())
        return {};
    return ChangeSet::Range(file->position(range.startLine, range.startColumn),
                            file->position(range.endLine, range.endColumn));
}

// And the same for one token.
std::optional<ChangeSet::Range> rangeAt(const CxxFrontendDocument &document,
                                        const CppRefactoringFilePtr &file,
                                        cxx::SourceLocation location)
{
    const CxxAstRange range = cxxTokenRangeAt(document, location);
    if (!range.isValid())
        return {};
    return ChangeSet::Range(file->position(range.startLine, range.startColumn),
                            file->position(range.endLine, range.endColumn));
}

// The path the model's tree gives for where the cursor is.
QList<cxx::AST *> pathUnderCursor(const CxxFrontendDocument &document,
                                  const CppQuickFixInterface &interface)
{
    // The editor counts from zero and the tree from one.
    const QTextCursor cursor = interface.currentFile()->cursor();
    return cxxAstPathAt(document, cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
}
#endif

/*!
  Rewrite
    a op b

  As
    b flipop a

  Activates on: <= < > >= == != && ||
*/
class FlipLogicalOperands : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        const QList<AST *> &path = interface.path();
        if (path.isEmpty())
            return;
        CppRefactoringFilePtr file = interface.currentFile();

        int index = path.size() - 1;
        BinaryExpressionAST *binary = path.at(index)->asBinaryExpression();
        if (!binary)
            return;
        if (!interface.isCursorOn(binary->binary_op_token))
            return;

        Kind flipToken;
        switch (file->tokenAt(binary->binary_op_token).kind()) {
        case T_LESS_EQUAL:
            flipToken = T_GREATER_EQUAL;
            break;
        case T_LESS:
            flipToken = T_GREATER;
            break;
        case T_GREATER:
            flipToken = T_LESS;
            break;
        case T_GREATER_EQUAL:
            flipToken = T_LESS_EQUAL;
            break;
        case T_EQUAL_EQUAL:
        case T_EXCLAIM_EQUAL:
        case T_AMPER_AMPER:
        case T_PIPE_PIPE:
            flipToken = T_EOF_SYMBOL;
            break;
        default:
            return;
        }

        QString replacement;
        if (flipToken != T_EOF_SYMBOL) {
            Token tok;
            tok.f.kind = flipToken;
            replacement = QLatin1String(tok.spell());
        }

        result << new FlipLogicalOperandsOp(interface, index,
                                            file->range(binary->left_expression),
                                            file->range(binary->right_expression),
                                            file->range(binary->binary_op_token), replacement);
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same operation on the cxx-frontend model's tree: the two operands
    // swap places, and a comparison's operator turns round with them.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface);
        if (!document)
            return false;

        int index = 0;
        const QList<cxx::AST *> path = pathUnderCursor(*document, interface);
        cxx::BinaryExpressionAST * const binary
            = binaryUnderCursor(*document, interface, path, &index);
        if (!binary)
            return false;

        cxx::TokenKind flipTo = cxx::TokenKind::T_EOF_SYMBOL;
        switch (binary->op) {
        case cxx::TokenKind::T_LESS_EQUAL:
            flipTo = cxx::TokenKind::T_GREATER_EQUAL;
            break;
        case cxx::TokenKind::T_LESS:
            flipTo = cxx::TokenKind::T_GREATER;
            break;
        case cxx::TokenKind::T_GREATER:
            flipTo = cxx::TokenKind::T_LESS;
            break;
        case cxx::TokenKind::T_GREATER_EQUAL:
            flipTo = cxx::TokenKind::T_LESS_EQUAL;
            break;
        case cxx::TokenKind::T_EQUAL_EQUAL:
        case cxx::TokenKind::T_EXCLAIM_EQUAL:
        case cxx::TokenKind::T_AMP_AMP:
        case cxx::TokenKind::T_BAR_BAR:
            break;
        default:
            return false;
        }

        const CppRefactoringFilePtr file = interface.currentFile();
        const std::optional<ChangeSet::Range> left
            = rangeOf(*document, file, binary->leftExpression);
        const std::optional<ChangeSet::Range> right
            = rangeOf(*document, file, binary->rightExpression);
        const std::optional<ChangeSet::Range> op = rangeAt(*document, file, binary->opLoc);
        // An operand a macro wrote stands nowhere in this file, so there is
        // nothing here to swap it with. Declined rather than answered wrongly:
        // the built-in path, whose token stream keeps the macro's name, does
        // the swap.
        if (!left || !right || !op)
            return false;

        QString replacement;
        if (flipTo != cxx::TokenKind::T_EOF_SYMBOL)
            replacement = QString::fromStdString(cxx::Token::spell(flipTo));

        result << new FlipLogicalOperandsOp(interface, index, *left, *right, *op, replacement);
        return true;
    }
#endif
};

/*!
  Rewrite
    a op b -> !(a invop b)
    (a op b) -> !(a invop b)
    !(a op b) -> (a invob b)

  Activates on: <= < > >= == !=
*/
class InverseLogicalComparison : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        CppRefactoringFilePtr file = interface.currentFile();

        const QList<AST *> &path = interface.path();
        if (path.isEmpty())
            return;
        int index = path.size() - 1;
        BinaryExpressionAST *binary = path.at(index)->asBinaryExpression();
        if (!binary)
            return;
        if (!interface.isCursorOn(binary->binary_op_token))
            return;

        Kind invertToken;
        switch (file->tokenAt(binary->binary_op_token).kind()) {
        case T_LESS_EQUAL:
            invertToken = T_GREATER;
            break;
        case T_LESS:
            invertToken = T_GREATER_EQUAL;
            break;
        case T_GREATER:
            invertToken = T_LESS_EQUAL;
            break;
        case T_GREATER_EQUAL:
            invertToken = T_LESS;
            break;
        case T_EQUAL_EQUAL:
            invertToken = T_EXCLAIM_EQUAL;
            break;
        case T_EXCLAIM_EQUAL:
            invertToken = T_EQUAL_EQUAL;
            break;
        default:
            return;
        }

        Token tok;
        tok.f.kind = invertToken;
        const QString replacement = QLatin1String(tok.spell());

        // The parentheses that are already there, and the negation that is
        // already in front of them.
        NestedExpressionAST * const nested
            = index - 1 >= 0 ? path.at(index - 1)->asNestedExpression() : nullptr;
        UnaryExpressionAST *negation = nullptr;
        if (nested && index - 2 >= 0) {
            negation = path.at(index - 2)->asUnaryExpression();
            if (negation && !file->tokenAt(negation->unary_op_token).is(T_EXCLAIM))
                negation = nullptr;
        }

        auto how = InverseLogicalComparisonOp::Negate::AroundTheComparison;
        ChangeSet::Range where = file->range(binary);
        if (negation) {
            how = InverseLogicalComparisonOp::Negate::RemoveTheOneThere;
            where = file->range(negation->unary_op_token);
        } else if (nested) {
            how = InverseLogicalComparisonOp::Negate::BeforeTheParentheses;
            where = file->range(nested);
        }

        result << new InverseLogicalComparisonOp(
            interface, index, file->range(binary->binary_op_token), replacement, how, where);
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same operation on the cxx-frontend model's tree: the comparison is
    // written the other way round and the whole of it is negated. Where the
    // negation goes is read off the two nodes around it, as above.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface);
        if (!document)
            return false;

        int index = 0;
        const QList<cxx::AST *> path = pathUnderCursor(*document, interface);
        cxx::BinaryExpressionAST * const binary
            = binaryUnderCursor(*document, interface, path, &index);
        if (!binary)
            return false;

        cxx::TokenKind invertTo = cxx::TokenKind::T_EOF_SYMBOL;
        switch (binary->op) {
        case cxx::TokenKind::T_LESS_EQUAL:
            invertTo = cxx::TokenKind::T_GREATER;
            break;
        case cxx::TokenKind::T_LESS:
            invertTo = cxx::TokenKind::T_GREATER_EQUAL;
            break;
        case cxx::TokenKind::T_GREATER:
            invertTo = cxx::TokenKind::T_LESS_EQUAL;
            break;
        case cxx::TokenKind::T_GREATER_EQUAL:
            invertTo = cxx::TokenKind::T_LESS;
            break;
        case cxx::TokenKind::T_EQUAL_EQUAL:
            invertTo = cxx::TokenKind::T_EXCLAIM_EQUAL;
            break;
        case cxx::TokenKind::T_EXCLAIM_EQUAL:
            invertTo = cxx::TokenKind::T_EQUAL_EQUAL;
            break;
        default:
            return false;
        }

        auto * const nested = index - 1 >= 0
            ? dynamic_cast<cxx::NestedExpressionAST *>(path.at(index - 1)) : nullptr;
        cxx::UnaryExpressionAST *negation = nullptr;
        if (nested && index - 2 >= 0) {
            negation = dynamic_cast<cxx::UnaryExpressionAST *>(path.at(index - 2));
            if (negation && negation->op != cxx::TokenKind::T_EXCLAIM)
                negation = nullptr;
        }

        const CppRefactoringFilePtr file = interface.currentFile();
        const std::optional<ChangeSet::Range> op = rangeAt(*document, file, binary->opLoc);
        if (!op)
            return false;

        auto how = InverseLogicalComparisonOp::Negate::AroundTheComparison;
        std::optional<ChangeSet::Range> where = rangeOf(*document, file, binary);
        if (negation) {
            how = InverseLogicalComparisonOp::Negate::RemoveTheOneThere;
            where = rangeAt(*document, file, negation->opLoc);
        } else if (nested) {
            how = InverseLogicalComparisonOp::Negate::BeforeTheParentheses;
            where = rangeOf(*document, file, nested);
        }
        // Nothing to negate where a macro wrote what would be negated; the
        // built-in path answers there.
        if (!where)
            return false;

        result << new InverseLogicalComparisonOp(
            interface, index, *op, QString::fromStdString(cxx::Token::spell(invertTo)), how,
            *where);
        return true;
    }
#endif
};

/*!
  Rewrite
    !a && !b

  As
    !(a || b)

  Activates on: &&
*/
class RewriteLogicalAnd : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        BinaryExpressionAST *expression = nullptr;
        const QList<AST *> &path = interface.path();
        CppRefactoringFilePtr file = interface.currentFile();

        int index = path.size() - 1;
        for (; index != -1; --index) {
            expression = path.at(index)->asBinaryExpression();
            if (expression)
                break;
        }

        if (!expression)
            return;

        if (!interface.isCursorOn(expression->binary_op_token))
            return;

        ASTPatternBuilder mk;
        UnaryExpressionAST * const left = mk.UnaryExpression();
        UnaryExpressionAST * const right = mk.UnaryExpression();
        BinaryExpressionAST * const pattern = mk.BinaryExpression(left, right);

        ASTMatcher matcher;

        if (expression->match(pattern, &matcher) &&
            file->tokenAt(pattern->binary_op_token).is(T_AMPER_AMPER) &&
            file->tokenAt(left->unary_op_token).is(T_EXCLAIM) &&
            file->tokenAt(right->unary_op_token).is(T_EXCLAIM)) {
            result << new RewriteLogicalAndOp(interface, index,
                                              file->range(pattern->binary_op_token),
                                              file->range(left->unary_op_token),
                                              file->range(right->unary_op_token),
                                              file->range(pattern));
        }
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same operation on the cxx-frontend model's tree. What the pattern
    // above matches is written out here instead: an && whose operands are
    // both negations. cxx keeps the operator on the node, so there is nothing
    // to look up.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface);
        if (!document)
            return false;

        int index = 0;
        const QList<cxx::AST *> path = pathUnderCursor(*document, interface);
        cxx::BinaryExpressionAST * const expression
            = binaryUnderCursor(*document, interface, path, &index);
        if (!expression || expression->op != cxx::TokenKind::T_AMP_AMP)
            return false;

        auto * const left = dynamic_cast<cxx::UnaryExpressionAST *>(expression->leftExpression);
        auto * const right = dynamic_cast<cxx::UnaryExpressionAST *>(expression->rightExpression);
        if (!left || left->op != cxx::TokenKind::T_EXCLAIM
            || !right || right->op != cxx::TokenKind::T_EXCLAIM) {
            return false;
        }

        const CppRefactoringFilePtr file = interface.currentFile();
        const std::optional<ChangeSet::Range> op = rangeAt(*document, file, expression->opLoc);
        const std::optional<ChangeSet::Range> leftNegation
            = rangeAt(*document, file, left->opLoc);
        const std::optional<ChangeSet::Range> rightNegation
            = rangeAt(*document, file, right->opLoc);
        const std::optional<ChangeSet::Range> whole = rangeOf(*document, file, expression);
        if (!op || !leftNegation || !rightNegation || !whole)
            return false;

        result << new RewriteLogicalAndOp(interface, index, *op, *leftNegation, *rightNegation,
                                          *whole);
        return true;
    }
#endif
};

#ifdef WITH_TESTS
class FlipLogicalOperandsTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};

class InverseLogicalComparisonTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};

class RewriteLogicalAndTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerLogicalOperationQuickfixes()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(FlipLogicalOperands);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(InverseLogicalComparison);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(RewriteLogicalAnd);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <logicaloperationquickfixes.moc>
#endif
