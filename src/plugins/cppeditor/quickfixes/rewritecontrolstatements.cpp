// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "rewritecontrolstatements.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cppeditorwidget.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#endif

#include <functional>

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

using namespace CPlusPlus;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

template<typename Statement> Statement *asControlStatement(AST *node)
{
    if constexpr (std::is_same_v<Statement, IfStatementAST>)
        return node->asIfStatement();
    if constexpr (std::is_same_v<Statement, WhileStatementAST>)
        return node->asWhileStatement();
    if constexpr (std::is_same_v<Statement, ForStatementAST>)
        return node->asForStatement();
    if constexpr (std::is_same_v<Statement, RangeBasedForStatementAST>)
        return node->asRangeBasedForStatement();
    if constexpr (std::is_same_v<Statement, DoStatementAST>)
        return node->asDoStatement();
    return nullptr;
}

// The parts of a control statement the two brace fixes work on, whichever
// front end read it: the keywords the fix is offered on, the body, the token
// the "{" goes after, and the token the "}" goes before -- none, when it goes
// after the body instead. \a spaceAfterCloseBrace is for the two statements
// that continue on the same line, where "} " keeps "while" and "else" apart.
struct ControlStatementParts
{
    QList<int> triggers;
    StatementAST *body = nullptr;
    int openBraceAfter = 0;
    int closeBraceBefore = 0;
    bool spaceAfterCloseBrace = false;
};

template<typename Statement>
ControlStatementParts partsOf(Statement *statement)
{
    if constexpr (std::is_same_v<Statement, IfStatementAST>) {
        return {{statement->if_token, statement->else_token}, statement->statement,
                statement->rparen_token,
                statement->else_statement ? statement->else_token : 0, true};
    }
    if constexpr (std::is_same_v<Statement, DoStatementAST>) {
        return {{statement->do_token}, statement->statement, statement->do_token,
                statement->while_token, true};
    }
    if constexpr (std::is_same_v<Statement, WhileStatementAST>)
        return {{statement->while_token}, statement->statement, statement->rparen_token};
    if constexpr (std::is_same_v<Statement, ForStatementAST>
                  || std::is_same_v<Statement, RangeBasedForStatementAST>) {
        return {{statement->for_token}, statement->statement, statement->rparen_token};
    }
}

// Both operations below take the edits they make rather than the nodes those
// were read off: which node a statement is depends on which front end read
// the file, and where the braces go does not.

class AddBracesToControlStatementOp : public CppQuickFixOperation
{
public:
    AddBracesToControlStatementOp(const CppQuickFixInterface &interface,
                                  const QList<std::pair<int, QString>> &insertions)
        : CppQuickFixOperation(interface, 0)
        , m_insertions(insertions)
    {
        setDescription(Tr::tr("Add Curly Braces"));
    }

    void perform() override
    {
        ChangeSet changes;
        for (const auto &[position, text] : m_insertions)
            changes.insert(position, text);

        currentFile()->setChangeSet(changes);
        currentFile()->apply();
    }

private:
    const QList<std::pair<int, QString>> m_insertions;
};

class RemoveBracesFromControlStatementOp : public CppQuickFixOperation
{
public:
    // One braced body: where the two braces stand, whether what follows the
    // closing one needs the space it sat on, and where a ";" goes when there
    // is no statement left to stand for the body.
    struct Braces
    {
        int lbrace = 0;
        int rbrace = 0;
        bool spaceAfterRbrace = false;
        std::optional<int> semicolonAt;
    };

    RemoveBracesFromControlStatementOp(const CppQuickFixInterface &interface,
                                       const QList<Braces> &braces)
        : CppQuickFixOperation(interface, 0)
        , m_braces(braces)
    {
        setDescription(Tr::tr("Remove Curly Braces"));
    }

    void perform() override
    {
        ChangeSet changes;
        const auto findNewline = [&](int bracePos, int diff, int &newlinePos, int *nextNonSpacePos) {
            for (int i = bracePos + diff; true; i += diff) {
                const QChar &c = currentFile()->charAt(i);
                if (c == '\n' || c == QChar::ParagraphSeparator) {
                    newlinePos = i;
                    break;
                }
                if (!c.isSpace())
                    break;
                if (nextNonSpacePos)
                    ++*nextNonSpacePos;
            }
        };
        const auto removeBraceAndPossiblyLine = [&](int bracePos, bool removeTrailingSpace) {
            int prevNewline = -1;
            int nextNewline = -1;
            int start = bracePos;
            int end = bracePos + 1;
            findNewline(bracePos, -1, prevNewline, nullptr);
            findNewline(bracePos, 1, nextNewline, removeTrailingSpace ? &end : nullptr);
            if (prevNewline != -1 && nextNewline != -1) {
                start = prevNewline;
                end = nextNewline;
            }
            changes.remove(start, end);
        };
        for (const Braces &braces : m_braces) {
            removeBraceAndPossiblyLine(braces.lbrace, false);
            removeBraceAndPossiblyLine(braces.rbrace, braces.spaceAfterRbrace);
            if (braces.semicolonAt)
                changes.insert(*braces.semicolonAt, "\n;");
        }

        currentFile()->setChangeSet(changes);
        currentFile()->apply();
    }

private:
    const QList<Braces> m_braces;
};

// Whether the fix applies to a body, and whether what is written there rules
// it out for the whole statement rather than just for this body.
using StmtConstraint = std::function<bool(AST *, bool &)>;

// The bodies one of these fixes rewrites: the statement the cursor is on and,
// down an else-if chain, every branch of it -- braces are added to and taken
// from an if/else as a whole. \a makeOp is handed them, so that each fix
// works out its own edits from the same walk.
using MakeBraceOp = std::function<void(const QList<ControlStatementParts> &,
                                       StatementAST *elseStatement, int elseToken)>;

template<typename Statement>
bool checkControlStatementsHelper(
    const CppQuickFixInterface &interface,
    const StmtConstraint &constraint,
    const MakeBraceOp &makeOp)
{
    Statement * const statement = asControlStatement<Statement>(interface.path().last());
    if (!statement)
        return false;

    const ControlStatementParts parts = partsOf(statement);
    if (!Utils::anyOf(parts.triggers, [&](int tok) { return interface.isCursorOn(tok); }))
        return false;

    // More than one, since braces are added to and taken from an if/else as
    // a whole; every other statement kind stands alone.
    QList<ControlStatementParts> statements;

    bool abort = false;
    if (parts.body && constraint(parts.body, abort))
        statements << parts;
    if (abort)
        return false;

    StatementAST *elseStmt = nullptr;
    int elseToken = 0;
    if constexpr (std::is_same_v<Statement, IfStatementAST>) {
        IfStatementAST *currentIfStmt = statement;
        for (elseStmt = currentIfStmt->else_statement, elseToken = currentIfStmt->else_token;
             elseStmt && (currentIfStmt = elseStmt->asIfStatement());
             elseStmt = currentIfStmt->else_statement, elseToken = currentIfStmt->else_token) {
            if (currentIfStmt->statement && constraint(currentIfStmt->statement, abort))
                statements << partsOf(currentIfStmt);
            if (abort)
                return false;
        }
        if (elseStmt && (elseStmt->asIfStatement() || !constraint(elseStmt, abort))) {
            if (abort)
                return false;
            elseStmt = nullptr;
            elseToken = 0;
        }
    }

    if (!statements.isEmpty() || elseStmt) {
        makeOp(statements, elseStmt, elseToken);
        return false;
    }
    return true;
}

template<typename... Statements>
void checkControlStatements(
    const CppQuickFixInterface &interface,
    const StmtConstraint &constraint,
    const MakeBraceOp &makeOp)
{
    (... || checkControlStatementsHelper<Statements>(interface, constraint, makeOp));
}

#ifdef QTC_WITH_CXX_FRONTEND
// The same statement, on the cxx-frontend model's tree. One struct for every
// kind of control statement, as above; cxx points at a token where the
// built-in front end counts them, which is the only difference.
struct CxxControlStatementParts
{
    QList<cxx::SourceLocation> triggers;
    cxx::StatementAST *body = nullptr;
    cxx::SourceLocation openBraceAfter;
    cxx::SourceLocation closeBraceBefore;
    bool spaceAfterCloseBrace = false;
};

std::optional<CxxControlStatementParts> cxxPartsOf(cxx::AST *node)
{
    if (auto * const s = dynamic_cast<cxx::IfStatementAST *>(node)) {
        return CxxControlStatementParts{{s->ifLoc, s->elseLoc}, s->statement, s->rparenLoc,
                                        s->elseStatement ? s->elseLoc : cxx::SourceLocation{},
                                        true};
    }
    if (auto * const s = dynamic_cast<cxx::DoStatementAST *>(node))
        return CxxControlStatementParts{{s->doLoc}, s->statement, s->doLoc, s->whileLoc, true};
    if (auto * const s = dynamic_cast<cxx::WhileStatementAST *>(node))
        return CxxControlStatementParts{{s->whileLoc}, s->statement, s->rparenLoc, {}, false};
    if (auto * const s = dynamic_cast<cxx::ForStatementAST *>(node))
        return CxxControlStatementParts{{s->forLoc}, s->statement, s->rparenLoc, {}, false};
    if (auto * const s = dynamic_cast<cxx::ForRangeStatementAST *>(node))
        return CxxControlStatementParts{{s->forLoc}, s->statement, s->rparenLoc, {}, false};
    return {};
}

using CxxStmtConstraint = std::function<bool(cxx::StatementAST *, bool &)>;

// What checkControlStatementsHelper() collects, worked out on the other
// tree: the branches this fix rewrites, and the final else if it takes that
// too. Empty where the cursor is not on the keyword of a control statement,
// and where what is written rules the fix out -- in either case the built-in
// path answers, and answers the same nothing.
struct CxxControlStatements
{
    QList<CxxControlStatementParts> statements;
    cxx::StatementAST *elseStatement = nullptr;
    cxx::SourceLocation elseLoc;

    bool isEmpty() const { return statements.isEmpty() && !elseStatement; }
};

CxxControlStatements cxxControlStatementsUnderCursor(const CxxFrontendDocument &document,
                                                    const CppQuickFixInterface &interface,
                                                    const CxxStmtConstraint &constraint)
{
    const CppRefactoringFilePtr file = interface.currentFile();

    // The editor counts from zero and the tree from one.
    const QTextCursor cursor = file->cursor();
    const QList<cxx::AST *> path
        = cxxAstPathAt(document, cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
    if (path.isEmpty())
        return {};

    const std::optional<CxxControlStatementParts> parts = cxxPartsOf(path.last());
    if (!parts)
        return {};

    // Not something to rewrite: where the front end stumbled inside this
    // statement its tree does not match the text, and the braces would go
    // in the wrong places. The built-in path answers instead.
    if (cxxAstWasReadWithErrors(document, path.last()))
        return {};

    const bool onAKeyword = Utils::anyOf(parts->triggers, [&](cxx::SourceLocation trigger) {
        const CxxAstRange range = cxxTokenRangeAt(document, trigger);
        if (!range.isValid())
            return false;
        const int position = file->cursor().selectionStart();
        return position >= file->position(range.startLine, range.startColumn)
               && position <= file->position(range.endLine, range.endColumn);
    });
    if (!onAKeyword)
        return {};

    CxxControlStatements found;
    bool abort = false;
    if (parts->body && constraint(parts->body, abort))
        found.statements << *parts;
    if (abort)
        return {};

    if (auto * const ifStatement = dynamic_cast<cxx::IfStatementAST *>(path.last())) {
        cxx::IfStatementAST *current = ifStatement;
        for (found.elseStatement = current->elseStatement, found.elseLoc = current->elseLoc;
             found.elseStatement
             && (current = dynamic_cast<cxx::IfStatementAST *>(found.elseStatement));
             found.elseStatement = current->elseStatement, found.elseLoc = current->elseLoc) {
            if (current->statement && constraint(current->statement, abort))
                found.statements << *cxxPartsOf(current);
            if (abort)
                return {};
        }
        if (found.elseStatement
            && (dynamic_cast<cxx::IfStatementAST *>(found.elseStatement)
                || !constraint(found.elseStatement, abort))) {
            if (abort)
                return {};
            found.elseStatement = nullptr;
            found.elseLoc = {};
        }
    }

    return found;
}

// The file the cursor is in as the other front end read it, or nothing where
// it has not read it -- the model is off unless asked for. See
// cxxfrontendmodel.h.
const CxxFrontendDocument *cxxFrontendDocumentFor(const CppQuickFixInterface &interface)
{
    const CppRefactoringFilePtr file = interface.currentFile();
    const std::shared_ptr<const CxxFrontendSnapshot> model = cxxFrontendModel(file->filePath());
    if (!model)
        return nullptr;
    return model->document(file->filePath().toFSPathString());
}

// The position just after a token, and just before it, as the editor counts
// them -- or nothing where the file does not write the token, which is where
// these fixes hand back to the built-in path.
std::optional<int> endOfToken(const CxxFrontendDocument &document,
                              const CppRefactoringFilePtr &file, cxx::SourceLocation location)
{
    const CxxAstRange range = cxxTokenRangeAt(document, location);
    if (!range.isValid())
        return {};
    return file->position(range.endLine, range.endColumn);
}

std::optional<int> startOfToken(const CxxFrontendDocument &document,
                                const CppRefactoringFilePtr &file, cxx::SourceLocation location)
{
    const CxxAstRange range = cxxTokenRangeAt(document, location);
    if (!range.isValid())
        return {};
    return file->position(range.startLine, range.startColumn);
}

std::optional<int> endOfNode(const CxxFrontendDocument &document,
                             const CppRefactoringFilePtr &file, cxx::AST *node)
{
    const CxxAstRange range = cxxAstRangeOf(document, node);
    if (!range.isValid())
        return {};
    return file->position(range.endLine, range.endColumn);
}

std::optional<int> startOfNode(const CxxFrontendDocument &document,
                               const CppRefactoringFilePtr &file, cxx::AST *node)
{
    const CxxAstRange range = cxxAstRangeOf(document, node);
    if (!range.isValid())
        return {};
    return file->position(range.startLine, range.startColumn);
}

std::optional<ChangeSet::Range> rangeOfNode(const CxxFrontendDocument &document,
                                            const CppRefactoringFilePtr &file, cxx::AST *node)
{
    const CxxAstRange range = cxxAstRangeOf(document, node);
    if (!range.isValid())
        return {};
    return ChangeSet::Range(file->position(range.startLine, range.startColumn),
                            file->position(range.endLine, range.endColumn));
}

// The declaration written in a condition, past the conversions cxx records
// around it: "if (Foo *foo = g())" reads as a cast of a cast of the
// declaration, because what the statement wants there is a bool. Null where
// the condition is an ordinary expression.
cxx::ConditionExpressionAST *cxxConditionDeclaration(cxx::ExpressionAST *condition)
{
    while (auto * const cast = dynamic_cast<cxx::ImplicitCastExpressionAST *>(condition))
        condition = cast->expression;
    return dynamic_cast<cxx::ConditionExpressionAST *>(condition);
}
#endif

// Both operations below take the three stretches of text they work on -- the
// name that is declared, the condition it is declared in, and where the
// statement begins -- rather than the nodes those were read off.

class MoveDeclarationOutOfIfOp: public CppQuickFixOperation
{
public:
    MoveDeclarationOutOfIfOp(const CppQuickFixInterface &interface, int priority,
                             const ChangeSet::Range &name, const ChangeSet::Range &condition,
                             int statementStart)
        : CppQuickFixOperation(interface, priority)
        , m_name(name)
        , m_condition(condition)
        , m_statementStart(statementStart)
    {
        setDescription(Tr::tr("Move Declaration out of Condition"));
    }

    void perform() override
    {
        ChangeSet changes;

        changes.copy(m_name, m_condition.start);
        changes.move(m_condition, m_statementStart);
        changes.insert(m_statementStart, QLatin1String(";\n"));

        currentFile()->apply(changes);
    }

private:
    const ChangeSet::Range m_name;
    const ChangeSet::Range m_condition;
    const int m_statementStart;
};

class MoveDeclarationOutOfWhileOp: public CppQuickFixOperation
{
public:
    MoveDeclarationOutOfWhileOp(const CppQuickFixInterface &interface, int priority,
                                const ChangeSet::Range &name, const ChangeSet::Range &condition,
                                int statementStart)
        : CppQuickFixOperation(interface, priority)
        , m_name(name)
        , m_condition(condition)
        , m_statementStart(statementStart)
    {
        setDescription(Tr::tr("Move Declaration out of Condition"));
    }

    void perform() override
    {
        ChangeSet changes;

        changes.insert(m_condition.start, QLatin1String("("));
        changes.insert(m_condition.end, QLatin1String(") != 0"));

        // The type goes out with the declaration and the name stays behind in
        // the condition, which is now an assignment.
        changes.move(m_condition.start, m_name.start, m_statementStart);
        changes.copy(m_name, m_statementStart);
        changes.insert(m_statementStart, QLatin1String(";\n"));

        currentFile()->apply(changes);
    }

private:
    const ChangeSet::Range m_name;
    const ChangeSet::Range m_condition;
    const int m_statementStart;
};

class SplitIfStatementOp: public CppQuickFixOperation
{
public:
    SplitIfStatementOp(const CppQuickFixInterface &interface, int priority,
                       IfStatementAST *pattern, BinaryExpressionAST *condition)
        : CppQuickFixOperation(interface, priority)
        , pattern(pattern)
        , condition(condition)
    {
        setDescription(Tr::tr("Split if Statement"));
    }

    void perform() override
    {
        const Token binaryToken = currentFile()->tokenAt(condition->binary_op_token);

        if (binaryToken.is(T_AMPER_AMPER))
            splitAndCondition();
        else
            splitOrCondition();
    }

    void splitAndCondition() const
    {
        ChangeSet changes;

        int startPos = currentFile()->startOf(pattern);
        changes.insert(startPos, QLatin1String("if ("));
        changes.move(currentFile()->range(condition->left_expression), startPos);
        changes.insert(startPos, QLatin1String(") {\n"));

        const int lExprEnd = currentFile()->endOf(condition->left_expression);
        changes.remove(lExprEnd, currentFile()->startOf(condition->right_expression));
        changes.insert(currentFile()->endOf(pattern), QLatin1String("\n}"));

        currentFile()->apply(changes);
    }

    void splitOrCondition() const
    {
        ChangeSet changes;

        StatementAST *ifTrueStatement = pattern->statement;
        CompoundStatementAST *compoundStatement = ifTrueStatement->asCompoundStatement();

        int insertPos = currentFile()->endOf(ifTrueStatement);
        if (compoundStatement)
            changes.insert(insertPos, QLatin1String(" "));
        else
            changes.insert(insertPos, QLatin1String("\n"));
        changes.insert(insertPos, QLatin1String("else if ("));

        const int rExprStart = currentFile()->startOf(condition->right_expression);
        changes.move(rExprStart, currentFile()->startOf(pattern->rparen_token), insertPos);
        changes.insert(insertPos, QLatin1String(")"));

        const int rParenEnd = currentFile()->endOf(pattern->rparen_token);
        changes.copy(rParenEnd, currentFile()->endOf(pattern->statement), insertPos);

        const int lExprEnd = currentFile()->endOf(condition->left_expression);
        changes.remove(lExprEnd, currentFile()->startOf(condition->right_expression));

        currentFile()->apply(changes);
    }

private:
    IfStatementAST *pattern;
    BinaryExpressionAST *condition;
};

class OptimizeForLoopOperation: public CppQuickFixOperation
{
public:
    OptimizeForLoopOperation(const CppQuickFixInterface &interface, const ForStatementAST *forAst,
                             const bool optimizePostcrement, const ExpressionAST *expression,
                             const FullySpecifiedType &type)
        : CppQuickFixOperation(interface)
        , m_forAst(forAst)
        , m_optimizePostcrement(optimizePostcrement)
        , m_expression(expression)
        , m_type(type)
    {
        setDescription(Tr::tr("Optimize for-Loop"));
    }

    void perform() override
    {
        QTC_ASSERT(m_forAst, return);

        const CppRefactoringFilePtr file = currentFile();
        ChangeSet change;

        // Optimize post (in|de)crement operator to pre (in|de)crement operator
        if (m_optimizePostcrement && m_forAst->expression) {
            PostIncrDecrAST *incrdecr = m_forAst->expression->asPostIncrDecr();
            if (incrdecr && incrdecr->base_expression && incrdecr->incr_decr_token) {
                change.flip(file->range(incrdecr->base_expression),
                            file->range(incrdecr->incr_decr_token));
            }
        }

        // Optimize Condition
        int renamePos = -1;
        if (m_expression) {
            QString varName = QLatin1String("total");

            if (file->textOf(m_forAst->initializer).size() == 1) {
                Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();
                const QString typeAndName = oo.prettyType(m_type, varName);
                renamePos = file->endOf(m_forAst->initializer) - 1 + typeAndName.size();
                change.insert(file->endOf(m_forAst->initializer) - 1, // "-1" because of ";"
                              typeAndName + QLatin1String(" = ") + file->textOf(m_expression));
            } else {
                // Check if varName is already used
                if (DeclarationStatementAST *ds = m_forAst->initializer->asDeclarationStatement()) {
                    if (DeclarationAST *decl = ds->declaration) {
                        if (SimpleDeclarationAST *sdecl = decl->asSimpleDeclaration()) {
                            for (;;) {
                                bool match = false;
                                for (DeclaratorListAST *it = sdecl->declarator_list; it;
                                     it = it->next) {
                                    if (file->textOf(it->value->core_declarator) == varName) {
                                        varName += QLatin1Char('X');
                                        match = true;
                                        break;
                                    }
                                }
                                if (!match)
                                    break;
                            }
                        }
                    }
                }

                renamePos = file->endOf(m_forAst->initializer) + 1;
                change.insert(file->endOf(m_forAst->initializer) - 1, // "-1" because of ";"
                              QLatin1String(", ") + varName + QLatin1String(" = ")
                                  + file->textOf(m_expression));
            }

            ChangeSet::Range exprRange(file->startOf(m_expression), file->endOf(m_expression));
            change.replace(exprRange, varName);
        }

        file->apply(change);

        // Select variable name and trigger symbol rename
        if (renamePos != -1) {
            QTextCursor c = file->cursor();
            c.setPosition(renamePos);
            editor()->setTextCursor(c);
            editor()->renameSymbolUnderCursor();
            c.select(QTextCursor::WordUnderCursor);
            editor()->setTextCursor(c);
        }
    }

private:
    const ForStatementAST *m_forAst;
    const bool m_optimizePostcrement;
    const ExpressionAST *m_expression;
    const FullySpecifiedType m_type;
};

/*!
  Replace
    if (Type name = foo()) {...}

  With
    Type name = foo();
    if (name) {...}

  Activates on: the name of the introduced variable
*/
class MoveDeclarationOutOfIf: public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        const QList<AST *> &path = interface.path();
        const CppRefactoringFilePtr file = interface.currentFile();
        ASTMatcher matcher;
        ASTPatternBuilder mk;
        ConditionAST *condition = mk.Condition();
        IfStatementAST *pattern = mk.IfStatement(condition);

        int index = path.size() - 1;
        for (; index != -1; --index) {
            if (IfStatementAST *statement = path.at(index)->asIfStatement()) {
                if (statement->match(pattern, &matcher) && condition->declarator) {
                    DeclaratorAST *declarator = condition->declarator;
                    CoreDeclaratorAST * const core = declarator->core_declarator;
                    if (!core)
                        return;

                    if (interface.isCursorOn(core)) {
                        result << new MoveDeclarationOutOfIfOp(interface, index,
                                                               file->range(core),
                                                               file->range(condition),
                                                               file->startOf(pattern));
                        return;
                    }

                    condition = mk.Condition();
                    pattern = mk.IfStatement(condition);
                }
            }
        }
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same on the cxx-frontend model's tree. No question is asked here
    // about whether the front end read the statement cleanly, as the brace
    // fixes do: what this one needs is a declaration with an initializer
    // written inside a condition, and error recovery does not invent that
    // shape -- a file whose types it cannot resolve still reads the
    // declaration as one, which the existing cases are made of.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface);
        if (!document)
            return false;

        const CppRefactoringFilePtr file = interface.currentFile();
        const QTextCursor cursor = file->cursor();
        const QList<cxx::AST *> path
            = cxxAstPathAt(*document, cursor.blockNumber() + 1, cursor.positionInBlock() + 1);

        for (int index = path.size() - 1; index >= 0; --index) {
            auto * const statement = dynamic_cast<cxx::IfStatementAST *>(path.at(index));
            if (!statement)
                continue;
            cxx::ConditionExpressionAST * const condition
                = cxxConditionDeclaration(statement->condition);
            if (!condition || !condition->declarator)
                continue;
            cxx::CoreDeclaratorAST * const core = condition->declarator->coreDeclarator;
            if (!core)
                return false;

            const std::optional<ChangeSet::Range> name = rangeOfNode(*document, file, core);
            const std::optional<ChangeSet::Range> conditionRange
                = rangeOfNode(*document, file, condition);
            const std::optional<int> start = startOfNode(*document, file, statement);
            if (!name || !conditionRange || !start)
                return false;

            const int position = cursor.selectionStart();
            if (position < name->start || position > name->end)
                continue;

            result << new MoveDeclarationOutOfIfOp(interface, index, *name, *conditionRange,
                                                   *start);
            return true;
        }
        return false;
    }
#endif
};

/*!
  Replace
    while (Type name = foo()) {...}

  With
    Type name;
    while ((name = foo()) != 0) {...}

  Activates on: the name of the introduced variable
*/
class MoveDeclarationOutOfWhile: public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        const QList<AST *> &path = interface.path();
        const CppRefactoringFilePtr file = interface.currentFile();
        ASTMatcher matcher;
        ASTPatternBuilder mk;
        ConditionAST *condition = mk.Condition();
        WhileStatementAST *pattern = mk.WhileStatement(condition);

        int index = path.size() - 1;
        for (; index != -1; --index) {
            if (WhileStatementAST *statement = path.at(index)->asWhileStatement()) {
                if (statement->match(pattern, &matcher) && condition->declarator) {
                    DeclaratorAST *declarator = condition->declarator;
                    CoreDeclaratorAST * const core = declarator->core_declarator;

                    if (!core)
                        return;

                    if (!declarator->equal_token)
                        return;

                    if (!declarator->initializer)
                        return;

                    if (interface.isCursorOn(core)) {
                        result << new MoveDeclarationOutOfWhileOp(interface, index,
                                                                  file->range(core),
                                                                  file->range(condition),
                                                                  file->startOf(pattern));
                        return;
                    }

                    condition = mk.Condition();
                    pattern = mk.WhileStatement(condition);
                }
            }
        }
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same on the cxx-frontend model's tree, and the same demand: the
    // name must be given something with an "=", since what is left behind in
    // the condition is an assignment. cxx keeps that initializer on the
    // condition rather than on the declarator.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface);
        if (!document)
            return false;

        const CppRefactoringFilePtr file = interface.currentFile();
        const QTextCursor cursor = file->cursor();
        const QList<cxx::AST *> path
            = cxxAstPathAt(*document, cursor.blockNumber() + 1, cursor.positionInBlock() + 1);

        for (int index = path.size() - 1; index >= 0; --index) {
            auto * const statement = dynamic_cast<cxx::WhileStatementAST *>(path.at(index));
            if (!statement)
                continue;
            cxx::ConditionExpressionAST * const condition
                = cxxConditionDeclaration(statement->condition);
            if (!condition || !condition->declarator)
                continue;
            cxx::CoreDeclaratorAST * const core = condition->declarator->coreDeclarator;
            if (!core)
                return false;

            auto * const initializer
                = dynamic_cast<cxx::EqualInitializerAST *>(condition->initializer);
            if (!initializer || !initializer->expression)
                return false;

            const std::optional<ChangeSet::Range> name = rangeOfNode(*document, file, core);
            const std::optional<ChangeSet::Range> conditionRange
                = rangeOfNode(*document, file, condition);
            const std::optional<int> start = startOfNode(*document, file, statement);
            if (!name || !conditionRange || !start)
                return false;

            const int position = cursor.selectionStart();
            if (position < name->start || position > name->end)
                continue;

            result << new MoveDeclarationOutOfWhileOp(interface, index, *name, *conditionRange,
                                                      *start);
            return true;
        }
        return false;
    }
#endif
};

/*!
  Replace
     if (something && something_else) {
     }

  with
     if (something)
        if (something_else) {
        }
     }

  and
    if (something || something_else)
      x;

  with
    if (something)
      x;
    else if (something_else)
      x;

    Activates on: && or ||
*/
class SplitIfStatement: public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        IfStatementAST *pattern = nullptr;
        const QList<AST *> &path = interface.path();

        int index = path.size() - 1;
        for (; index != -1; --index) {
            AST *node = path.at(index);
            if (IfStatementAST *stmt = node->asIfStatement()) {
                pattern = stmt;
                break;
            }
        }

        if (!pattern || !pattern->statement)
            return;

        unsigned splitKind = 0;
        for (++index; index < path.size(); ++index) {
            AST *node = path.at(index);
            BinaryExpressionAST *condition = node->asBinaryExpression();
            if (!condition)
                return;

            Token binaryToken = interface.currentFile()->tokenAt(condition->binary_op_token);

            // only accept a chain of ||s or &&s - no mixing
            if (!splitKind) {
                splitKind = binaryToken.kind();
                if (splitKind != T_AMPER_AMPER && splitKind != T_PIPE_PIPE)
                    return;
                // we can't reliably split &&s in ifs with an else branch
                if (splitKind == T_AMPER_AMPER && pattern->else_statement)
                    return;
            } else if (splitKind != binaryToken.kind()) {
                return;
            }

            if (interface.isCursorOn(condition->binary_op_token)) {
                result << new SplitIfStatementOp(interface, index, pattern, condition);
                return;
            }
        }
    }
};

/*!
  Add curly braces to a control statement that doesn't already contain a
  compound statement. I.e.

  if (a)
      b;
  becomes
  if (a) {
      b;
  }

  Activates on: the keyword
*/
class AddBracesToControlStatement : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        if (interface.path().isEmpty())
            return;
        const CppRefactoringFilePtr file = interface.currentFile();
        const auto constraint = [](AST *ast, bool &) { return !ast->asCompoundStatement(); };
        const auto makeOp = [&](const QList<ControlStatementParts> &statements,
                                StatementAST *elseStatement, int elseToken) {
            QList<std::pair<int, QString>> insertions;
            const auto brace = [&](const ControlStatementParts &parts) {
                insertions << std::make_pair(file->endOf(parts.openBraceAfter), QString(" {"));
                if (parts.closeBraceBefore) {
                    insertions << std::make_pair(file->startOf(parts.closeBraceBefore),
                                                 QString("} "));
                } else {
                    insertions << std::make_pair(file->endOf(parts.body->lastToken() - 1),
                                                 QString("\n}"));
                }
            };
            for (const ControlStatementParts &parts : statements)
                brace(parts);
            if (elseStatement) {
                insertions << std::make_pair(file->endOf(elseToken), QString(" {"));
                insertions << std::make_pair(file->endOf(elseStatement->lastToken() - 1),
                                             QString("\n}"));
            }
            result << new AddBracesToControlStatementOp(interface, insertions);
        };
        checkControlStatements<IfStatementAST,
                               WhileStatementAST,
                               ForStatementAST,
                               RangeBasedForStatementAST,
                               DoStatementAST>(interface, constraint, makeOp);
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same operation on the cxx-frontend model's tree: a "{" after the
    // statement's parentheses and a "}" where its body ends.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface);
        if (!document)
            return false;

        const auto constraint = [](cxx::StatementAST *statement, bool &) {
            return !dynamic_cast<cxx::CompoundStatementAST *>(statement);
        };
        const CxxControlStatements found
            = cxxControlStatementsUnderCursor(*document, interface, constraint);
        if (found.isEmpty())
            return false;

        const CppRefactoringFilePtr file = interface.currentFile();
        QList<std::pair<int, QString>> insertions;
        const auto brace = [&](cxx::SourceLocation openAfter, cxx::SourceLocation closeBefore,
                               cxx::AST *body) {
            const std::optional<int> open = endOfToken(*document, file, openAfter);
            const std::optional<int> close = closeBefore
                ? startOfToken(*document, file, closeBefore) : endOfNode(*document, file, body);
            if (!open || !close)
                return false;
            insertions << std::make_pair(*open, QString(" {"));
            insertions << std::make_pair(*close, closeBefore ? QString("} ") : QString("\n}"));
            return true;
        };
        for (const CxxControlStatementParts &parts : found.statements) {
            if (!brace(parts.openBraceAfter, parts.closeBraceBefore, parts.body))
                return false;
        }
        if (found.elseStatement
            && !brace(found.elseLoc, cxx::SourceLocation{}, found.elseStatement)) {
            return false;
        }

        result << new AddBracesToControlStatementOp(interface, insertions);
        return true;
    }
#endif
};

/*!
 * The reverse of AddBracesToControlStatement
 */
class RemoveBracesFromControlStatement : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        if (interface.path().isEmpty())
            return;
        const auto constraint = [&](AST *ast, bool &abort) {
            if (const auto compoundStmt = ast->asCompoundStatement()) {
                if (!compoundStmt->statement_list || !compoundStmt->statement_list->value)
                    return true;  // No statements.
                if (compoundStmt->statement_list->next) {
                    abort = true;
                    return false; // More than one statement.
                }

                // We have exactly one statement. Check whether it spans more than one line.
                const CppRefactoringFilePtr file = interface.currentFile();
                const ChangeSet::Range stmtRange = file->range(compoundStmt->statement_list->value);
                int startLine, startColumn, endLine, endColumn;
                file->lineAndColumn(stmtRange.start, &startLine, &startColumn);
                file->lineAndColumn(stmtRange.end, &endLine, &endColumn);
                if (startLine == endLine)
                    return true;
                abort = true;
                return false;
            }
            return false;
        };
        const auto makeOp = [&](const QList<ControlStatementParts> &statements,
                                StatementAST *elseStatement, int) {
            const CppRefactoringFilePtr file = interface.currentFile();
            QList<RemoveBracesFromControlStatementOp::Braces> braces;
            const auto unbrace = [&](StatementAST *body, bool spaceAfterRbrace) {
                const CompoundStatementAST * const compound = body->asCompoundStatement();
                QTC_ASSERT(compound, return);
                braces << RemoveBracesFromControlStatementOp::Braces{
                    file->startOf(compound->lbrace_token), file->startOf(compound->rbrace_token),
                    spaceAfterRbrace,
                    compound->statement_list ? std::nullopt
                                             : std::make_optional(file->endOf(compound))};
            };
            for (const ControlStatementParts &parts : statements)
                unbrace(parts.body, parts.spaceAfterCloseBrace);
            if (elseStatement)
                unbrace(elseStatement, true);
            result << new RemoveBracesFromControlStatementOp(interface, braces);
        };
        checkControlStatements<IfStatementAST,
                               WhileStatementAST,
                               ForStatementAST,
                               RangeBasedForStatementAST,
                               DoStatementAST>(interface, constraint, makeOp);
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same operation on the cxx-frontend model's tree, and the same rule
    // about which body may lose its braces: it must hold one statement
    // written on one line, or none at all.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface);
        if (!document)
            return false;

        const auto constraint = [&](cxx::StatementAST *statement, bool &abort) {
            auto * const compound = dynamic_cast<cxx::CompoundStatementAST *>(statement);
            if (!compound)
                return false;
            if (!compound->statementList || !compound->statementList->value)
                return true; // No statements.
            if (compound->statementList->next) {
                abort = true;
                return false; // More than one statement.
            }

            // Exactly one. It keeps its braces if it spans more than a line --
            // and if the file does not write it at all, which is a macro's
            // body and not something to count the lines of.
            const CxxAstRange range = cxxAstRangeOf(*document, compound->statementList->value);
            if (range.isValid() && range.startLine == range.endLine)
                return true;
            abort = true;
            return false;
        };
        const CxxControlStatements found
            = cxxControlStatementsUnderCursor(*document, interface, constraint);
        if (found.isEmpty())
            return false;

        const CppRefactoringFilePtr file = interface.currentFile();
        QList<RemoveBracesFromControlStatementOp::Braces> braces;
        const auto unbrace = [&](cxx::StatementAST *body, bool spaceAfterRbrace) {
            auto * const compound = dynamic_cast<cxx::CompoundStatementAST *>(body);
            QTC_ASSERT(compound, return false);
            const std::optional<int> lbrace = startOfToken(*document, file, compound->lbraceLoc);
            const std::optional<int> rbrace = startOfToken(*document, file, compound->rbraceLoc);
            if (!lbrace || !rbrace)
                return false;
            std::optional<int> semicolonAt;
            if (!compound->statementList) {
                semicolonAt = endOfNode(*document, file, compound);
                if (!semicolonAt)
                    return false;
            }
            braces << RemoveBracesFromControlStatementOp::Braces{*lbrace, *rbrace,
                                                                 spaceAfterRbrace, semicolonAt};
            return true;
        };
        for (const CxxControlStatementParts &parts : found.statements) {
            if (!unbrace(parts.body, parts.spaceAfterCloseBrace))
                return false;
        }
        if (found.elseStatement && !unbrace(found.elseStatement, true))
            return false;

        result << new RemoveBracesFromControlStatementOp(interface, braces);
        return true;
    }
#endif
};

/*!
  Optimizes a for loop to avoid permanent condition check and forces to use preincrement
  or predecrement operators in the expression of the for loop.
 */
class OptimizeForLoop : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const QList<AST *> path = interface.path();
        ForStatementAST *forAst = nullptr;
        if (!path.isEmpty())
            forAst = path.last()->asForStatement();
        if (!forAst || !interface.isCursorOn(forAst))
            return;

        // Check for optimizing a postcrement
        const CppRefactoringFilePtr file = interface.currentFile();
        bool optimizePostcrement = false;
        if (forAst->expression) {
            if (PostIncrDecrAST *incrdecr = forAst->expression->asPostIncrDecr()) {
                const Token t = file->tokenAt(incrdecr->incr_decr_token);
                if (t.is(T_PLUS_PLUS) || t.is(T_MINUS_MINUS))
                    optimizePostcrement = true;
            }
        }

        // Check for optimizing condition
        bool optimizeCondition = false;
        FullySpecifiedType conditionType;
        ExpressionAST *conditionExpression = nullptr;
        if (forAst->initializer && forAst->condition) {
            if (BinaryExpressionAST *binary = forAst->condition->asBinaryExpression()) {
                // Get the expression against which we should evaluate
                IdExpressionAST *conditionId = binary->left_expression->asIdExpression();
                if (conditionId) {
                    conditionExpression = binary->right_expression;
                } else {
                    conditionId = binary->right_expression->asIdExpression();
                    conditionExpression = binary->left_expression;
                }

                if (conditionId && conditionExpression
                    && !(conditionExpression->asNumericLiteral()
                         || conditionExpression->asStringLiteral()
                         || conditionExpression->asIdExpression()
                         || conditionExpression->asUnaryExpression())) {
                    // Determine type of for initializer
                    FullySpecifiedType initializerType;
                    if (DeclarationStatementAST *stmt = forAst->initializer->asDeclarationStatement()) {
                        if (stmt->declaration) {
                            if (SimpleDeclarationAST *decl = stmt->declaration->asSimpleDeclaration()) {
                                if (decl->symbols) {
                                    if (Symbol *symbol = decl->symbols->value)
                                        initializerType = symbol->type();
                                }
                            }
                        }
                    }

                    // Determine type of for condition
                    TypeOfExpression typeOfExpression;
                    typeOfExpression.init(interface.semanticInfo().doc, interface.snapshot(),
                                          interface.context().bindings());
                    typeOfExpression.setExpandTemplates(true);
                    Scope *scope = file->scopeAt(conditionId->firstToken());
                    const QList<LookupItem> conditionItems = typeOfExpression(
                        conditionId, interface.semanticInfo().doc, scope);
                    if (!conditionItems.isEmpty())
                        conditionType = conditionItems.first().type();

                    if (conditionType.isValid()
                        && (file->textOf(forAst->initializer) == QLatin1String(";")
                            || initializerType == conditionType)) {
                        optimizeCondition = true;
                    }
                }
            }
        }

        if (optimizePostcrement || optimizeCondition) {
            result << new OptimizeForLoopOperation(interface, forAst, optimizePostcrement,
                                                   optimizeCondition ? conditionExpression : nullptr,
                                                   conditionType);
        }
    }
};

static bool isQtShareableClass(const QByteArray &name)
{
    // Full list from https://doc.qt.io/qt-6/shared.html
    static const QSet<QByteArray> types{
        "QBitArray",
        "QBitmap",
        "QBrush",
        "QByteArray",
        "QByteArrayList",
        "QByteArrayView",
        "QCache",
        "QCollator",
        "QCollatorSortKey",
        "QCommandLineOption",
        "QContiguousCache",
        "QCursor",
        "QDBusPendingCall",
        "QDBusUnixFileDescriptor",
        "QDateTime",
        "QDebug",
        "QDir",
        "QDnsDomainNameRecord",
        "QDnsHostAddressRecord",
        "QDnsMailExchangeRecord",
        "QDnsServiceRecord",
        "QDnsTextRecord",
        "QDnsTlsAssociationRecord",
        "QFileInfo",
        "QFont",
        "QFontInfo",
        "QFontMetrics",
        "QFontMetricsF",
        "QFontVariableAxis",
        "QFormDataBuilder",
        "QFormDataPartBuilder",
        "QGeoAreaMonitorInfo",
        "QGeoPositionInfo",
        "QGeoSatelliteInfo",
        "QGlyphRun",
        "QGradient",
        "QHash",
        "QHostAddress",
        "QHttp1Configuration",
        "QHttp2Configuration",
        "QHttpPart",
        "QIcon",
        "QImage",
        "QJsonArray",
        "QJsonDocument",
        "QJsonObject",
        "QJsonParseError",
        "QJsonValue",
        "QKeySequence",
        "QLinkedList",
        "QList",
        "QLocale",
        "QLowEnergyAdvertisingData",
        "QLowEnergyAdvertisingParameters",
        "QLowEnergyCharacteristicData",
        "QLowEnergyConnectionParameters",
        "QLowEnergyDescriptorData",
        "QLowEnergyServiceData",
        "QMap",
        "QMimeType",
        "QMqttTopicFilter",
        "QMqttTopicName",
        "QMultiHash",
        "QMultiMap",
        "QNetworkAddressEntry",
        "QNetworkCacheMetaData",
        "QNetworkCookie",
        "QNetworkInterface",
        "QNetworkProxy",
        "QNetworkProxyQuery",
        "QNetworkRequest",
        "QNetworkRequestFactory",
        "QOpenGLDebugMessage",
        "QPageRanges",
        "QPalette",
        "QPen",
        "QPersistentModelIndex",
        "QPicture",
        "QPixmap",
        "QPolygon",
        "QPolygonF",
        "QProcessEnvironment",
        "QQueue",
        "QRawFont",
        "QRegExp",
        "QRegion",
        "QRegularExpression",
        "QRegularExpressionMatch",
        "QRegularExpressionMatchIterator",
        "QSet",
        "QSqlField",
        "QSqlQuery",
        "QSqlRecord",
        "QSslCertificate",
        "QSslCertificateExtension",
        "QSslCipher",
        "QSslConfiguration",
        "QSslDiffieHellmanParameters",
        "QSslError",
        "QSslKey",
        "QSslPreSharedKeyAuthenticator",
        "QStack",
        "QStaticText",
        "QStorageInfo",
        "QString",
        "QStringList",
        "QTextBlockFormat",
        "QTextBoundaryFinder",
        "QTextCharFormat",
        "QTextCursor",
        "QTextDocumentFragment",
        "QTextFormat",
        "QTextFrameFormat",
        "QTextImageFormat",
        "QTextListFormat",
        "QTextTableCellFormat",
        "QTextTableFormat",
        "QUrl",
        "QUrlQuery",
        "QVariant",
        "QVector", // Qt5 compat alias for QList
    };
    return types.contains(name);
}

class WrapInStdAsConstOp : public CppQuickFixOperation
{
public:
    WrapInStdAsConstOp(const CppQuickFixInterface &interface,
                                  ExpressionAST *expression)
        : CppQuickFixOperation(interface, 0)
        , m_expression(expression)
    {
        setDescription(Tr::tr("Wrap in std::as_const()"));
    }

private:
    void perform() override
    {
        const int startPos = currentFile()->startOf(m_expression);
        const int endPos = currentFile()->endOf(m_expression);
        ChangeSet changes;
        changes.insert(startPos, "std::as_const(");
        changes.insert(endPos, ")");
        currentFile()->apply(changes);
    }

    ExpressionAST * const m_expression;
};

/*!
  Wraps the container expression of a range-based for loop in std::as_const() if
  the container is a non-const Qt implicit-sharing type. This prevents an unintended
  detach (copy) of the shared data during iteration.

  Activates on: the container expression of a range-based for loop.
*/
class WrapInStdAsConst : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const QList<AST *> &path = interface.path();
        if (path.isEmpty())
            return;

        // Find the innermost range-based for statement containing the cursor.
        const RangeBasedForStatementAST *forStmt = nullptr;
        for (int i = path.size() - 1; i >= 0; --i) {
            if ((forStmt = path.at(i)->asRangeBasedForStatement()))
                break;
        }
        if (!forStmt || !forStmt->expression)
            return;

        // Cursor must be on the container expression (after the colon).
        if (!interface.isCursorOn(forStmt->expression))
            return;

        // std::as_const() takes a non-const lvalue reference, so it cannot wrap a temporary
        // returned by a function call. Reject call expressions to avoid a compile error.
        if (forStmt->expression->asCall())
            return;

        // Determine the type of the container expression.
        TypeOfExpression typeOfExpression;
        typeOfExpression.init(interface.semanticInfo().doc, interface.snapshot(),
                              interface.context().bindings());
        const CppRefactoringFilePtr file = interface.currentFile();
        Scope * const scope = file->scopeAt(forStmt->expression->firstToken());
        const QList<LookupItem> items = typeOfExpression(
            file->textOf(forStmt->expression).toUtf8(),
            scope,
            TypeOfExpression::Preprocess);
        if (items.isEmpty())
            return;

        FullySpecifiedType type = items.first().type();

        // Strip a reference layer, if present.
        if (const ReferenceType *ref = type->asReferenceType())
            type = ref->elementType();

        // Container must not already be const.
        if (type.isConst())
            return;

        // Container must be a Qt implicit-sharing (shareable) type.
        const NamedType * const namedType = type->asNamedType();
        if (!namedType)
            return;
        const Name * const name = namedType->name();
        if (!name)
            return;
        const Identifier * const id = name->identifier();
        if (!id)
            return;
        if (!isQtShareableClass(id->chars()))
            return;

        result << new WrapInStdAsConstOp(interface, forStmt->expression);
    }
};

#ifdef WITH_TESTS
class AddBracesToControlStatementTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
class RemoveBracesFromControlStatementTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
class MoveDeclarationOutOfIfTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
class MoveDeclarationOutOfWhileTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
class OptimizeForLoopTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
class SplitIfStatementTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};

class WrapInStdAsConstTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerRewriteControlStatementQuickfixes()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(AddBracesToControlStatement);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(RemoveBracesFromControlStatement);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(MoveDeclarationOutOfIf);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(MoveDeclarationOutOfWhile);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(OptimizeForLoop);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(WrapInStdAsConst);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(SplitIfStatement);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <rewritecontrolstatements.moc>
#endif
