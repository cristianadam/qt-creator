// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "completeswitchstatement.h"

#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"

#include <utils/textutils.h>
#include "cppquickfix.h"

#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

class CaseStatementCollector : public ASTVisitor
{
public:
    CaseStatementCollector(Document::Ptr document, const Snapshot &snapshot,
                           Scope *scope)
        : ASTVisitor(document->translationUnit()),
        document(document),
        scope(scope)
    {
        typeOfExpression.init(document, snapshot);
    }

    QStringList operator ()(AST *ast)
    {
        values.clear();
        foundCaseStatementLevel = false;
        accept(ast);
        return values;
    }

    bool preVisit(AST *ast) override {
        if (CaseStatementAST *cs = ast->asCaseStatement()) {
            foundCaseStatementLevel = true;
            if (ExpressionAST *csExpression = cs->expression) {
                if (ExpressionAST *expression = csExpression->asIdExpression()) {
                    QList<LookupItem> candidates = typeOfExpression(expression, document, scope);
                    if (!candidates.isEmpty() && candidates.first().declaration()) {
                        Symbol *decl = candidates.first().declaration();
                        values << prettyPrint.prettyName(LookupContext::fullyQualifiedName(decl));
                    }
                }
            }
            return true;
        } else if (foundCaseStatementLevel) {
            return false;
        }
        return true;
    }

    Overview prettyPrint;
    bool foundCaseStatementLevel = false;
    QStringList values;
    TypeOfExpression typeOfExpression;
    Document::Ptr document;
    Scope *scope;
};

// A switch to complete: where its body opens, and which values of the
// enumeration it does not handle yet. All this fix needs of a front end --
// telling a handled value from a missing one means naming both the same way,
// so the two come from the same place or from neither.
class WrittenSwitch
{
public:
    int bodyOpensAt = 0; // just after the '{', a position in the document
    QStringList missingValues;
};

class CompleteSwitchCaseStatementOp: public CppQuickFixOperation
{
public:
    CompleteSwitchCaseStatementOp(const CppQuickFixInterface &interface, int priority,
                                  const WrittenSwitch &statement)
        : CppQuickFixOperation(interface, priority)
        , m_statement(statement)
    {
        setDescription(Tr::tr("Complete Switch Statement"));
    }

    void perform() override
    {
        currentFile()->apply(ChangeSet::makeInsert(
            m_statement.bodyOpensAt,
            QLatin1String("\ncase ")
                + m_statement.missingValues.join(QLatin1String(":\nbreak;\ncase "))
                + QLatin1String(":\nbreak;")));
    }

private:
    const WrittenSwitch m_statement;
};

static Enum *findEnum(const QList<LookupItem> &results, const LookupContext &ctxt)
{
    for (const LookupItem &result : results) {
        const FullySpecifiedType fst = result.type();

        Type *type = result.declaration() ? result.declaration()->type().type()
                                          : fst.type();

        if (!type)
            continue;
        if (Enum *e = type->asEnumType())
            return e;
        if (const NamedType *namedType = type->asNamedType()) {
            if (ClassOrNamespace *con = ctxt.lookupType(namedType->name(), result.scope())) {
                QList<Enum *> enums = con->unscopedEnums();
                const QList<Symbol *> symbols = con->symbols();
                for (Symbol * const s : symbols) {
                    if (const auto e = s->asEnum())
                        enums << e;
                }
                const Name *referenceName = namedType->name();
                if (const QualifiedNameId *qualifiedName = referenceName->asQualifiedNameId())
                    referenceName = qualifiedName->name();
                for (Enum *e : std::as_const(enums)) {
                    if (const Name *candidateName = e->name()) {
                        if (candidateName->match(referenceName))
                            return e;
                    }
                }
            }
        }
    }

    return nullptr;
}

// The values of the enumeration the switch's condition has, written out the
// way a case label writes them, or nothing where the condition is not of an
// enumeration.
//
// The lookup stays alive for as long as the enumeration is read, which is
// what this function is for: an Enum found this way belongs to the bindings
// that found it, and handing one back outlives them.
QStringList conditionEnumValues(const CppQuickFixInterface &interface,
                                SwitchStatementAST *statement)
{
    Block *block = statement->symbol;
    Scope *scope = interface.semanticInfo().doc->scopeAt(block->line(), block->column());
    TypeOfExpression typeOfExpression;
    typeOfExpression.setExpandTemplates(true);
    typeOfExpression.init(interface.semanticInfo().doc, interface.snapshot());
    const QList<LookupItem> results = typeOfExpression(statement->condition,
                                                       interface.semanticInfo().doc,
                                                       scope);

    Enum * const e = findEnum(results, typeOfExpression.context());
    if (!e)
        return {};

    QStringList values;
    Overview prettyPrint;
    for (int i = 0; i < e->memberCount(); ++i) {
        if (Declaration *decl = e->memberAt(i)->asDeclaration())
            values << prettyPrint.prettyName(LookupContext::fullyQualifiedName(decl));
    }
    return values;
}

// The switch to complete, as the built-in front end reads it, and how deep
// in the tree it sits -- which is what orders this fix among the others
// offered at the same place.
std::optional<std::pair<WrittenSwitch, int>> builtinSwitchAt(
    const CppQuickFixInterface &interface)
{
    const QList<AST *> &path = interface.path();

    // look for switch statement
    for (int depth = path.size() - 1; depth >= 0; --depth) {
        SwitchStatementAST * const switchStatement = path.at(depth)->asSwitchStatement();
        if (!switchStatement)
            continue;
        if (!switchStatement->statement || !switchStatement->symbol)
            return std::nullopt;
        CompoundStatementAST * const compoundStatement
            = switchStatement->statement->asCompoundStatement();
        if (!compoundStatement) // we ignore pathologic case "switch (t) case A: ;"
            return std::nullopt;

        // look if the condition's type is an enum, and what its values are
        QStringList values = conditionEnumValues(interface, switchStatement);
        if (values.isEmpty())
            return std::nullopt;

        // Get the used values
        Block * const block = switchStatement->symbol;
        CaseStatementCollector caseValues(
            interface.semanticInfo().doc, interface.snapshot(),
            interface.semanticInfo().doc->scopeAt(block->line(), block->column()));
        const QStringList usedValues = caseValues(switchStatement);
        for (const QString &usedValue : usedValues)
            values.removeAll(usedValue);

        return std::make_pair(
            WrittenSwitch{interface.semanticInfo().doc->translationUnit()
                              ->getTokenEndPositionInDocument(compoundStatement->lbrace_token,
                                                              interface.textDocument()),
                          values},
            depth);
    }
    return std::nullopt;
}

std::optional<std::pair<WrittenSwitch, int>> switchAt(const CppQuickFixInterface &interface)
{
#ifdef QTC_WITH_CXX_FRONTEND
    const Utils::Text::Position at = Utils::Text::Position::fromPositionInDocument(
        interface.textDocument(), interface.position());
    // Taken only where it has values to offer. "Nothing to complete here"
    // and "this front end could not read what the switch switches over" look
    // the same from here -- a condition it did not resolve has no type, and
    // so is not an enumeration either -- so the second must not be mistaken
    // for the first, and the built-in front end is asked in both cases.
    if (const std::optional<CxxFrontendDocument::Switch> found
        = cxxFrontendSwitchAt(interface.filePath(), at.line, at.column + 1);
        found && found->isValid() && !found->missingValues.isEmpty()) {
        // The order among the fixes offered here is the built-in tree's
        // depth, and a tree of another shape counts differently. What the
        // number stands for is "innermost first", and a switch somebody's
        // cursor is in is as deep as this fix ever goes.
        return std::make_pair(
            WrittenSwitch{Utils::Text::Position{found->bodyLine, found->bodyColumn - 1}
                              .toPositionInDocument(interface.textDocument()),
                          found->missingValues},
            0);
    }
#endif
    return builtinSwitchAt(interface);
}

//! Adds missing case statements for "switch (enumVariable)"
class CompleteSwitchStatement: public CppQuickFixFactory
{
public:
    CompleteSwitchStatement() { setClangdReplacement({12}); }

private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const std::optional<std::pair<WrittenSwitch, int>> found = switchAt(interface);
        if (!found || found->first.missingValues.isEmpty())
            return;
        result << new CompleteSwitchCaseStatementOp(interface, found->second, found->first);
    }
};

#ifdef WITH_TESTS
class CompleteSwitchStatementTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerCompleteSwitchStatementQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(CompleteSwitchStatement);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <completeswitchstatement.moc>
#endif
