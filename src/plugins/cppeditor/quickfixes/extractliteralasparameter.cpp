// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extractliteralasparameter.h"

#include "../cppeditortr.h"
#include "../cppeditorwidget.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"
#include "cppquickfixhelpers.h"

#include "../cppmodelmanager.h"

#include <cplusplus/ASTPath.h>
#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>

#include <utils/textutils.h>

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

// Every place a function writes the same literal, as the built-in front
// end's tree has them, and the literal as it is written -- which the token
// alone does not say, since its spelling leaves out the quotes and the
// prefix.
template <class T>
class LiteralPlaces : private ASTVisitor
{
public:
    LiteralPlaces(const CppRefactoringFilePtr &file, T *literal)
        : ASTVisitor(file->cppDocument()->translationUnit()), m_file(file), m_literal(literal)
    {
        const Token token = m_file->tokenAt(literal->firstToken());
        m_literalTokenText = token.spell();
        m_text = QLatin1String(m_literalTokenText);
        if (token.isCharLiteral()) {
            m_text.prepend(QLatin1Char('\''));
            m_text.append(QLatin1Char('\''));
            if (token.kind() == T_WIDE_CHAR_LITERAL)
                m_text.prepend(QLatin1Char('L'));
            else if (token.kind() == T_UTF16_CHAR_LITERAL)
                m_text.prepend(QLatin1Char('u'));
            else if (token.kind() == T_UTF32_CHAR_LITERAL)
                m_text.prepend(QLatin1Char('U'));
        } else if (token.isStringLiteral()) {
            m_text.prepend(QLatin1Char('"'));
            m_text.append(QLatin1Char('"'));
            if (token.kind() == T_WIDE_STRING_LITERAL)
                m_text.prepend(QLatin1Char('L'));
            else if (token.kind() == T_UTF16_STRING_LITERAL)
                m_text.prepend(QLatin1Char('u'));
            else if (token.kind() == T_UTF32_STRING_LITERAL)
                m_text.prepend(QLatin1Char('U'));
        }
    }

    QString text() const { return m_text; }

    QList<ChangeSet::Range> apply(AST *ast)
    {
        ast->accept(this);
        return m_places;
    }

private:
    bool visit(T *ast) override
    {
        if (ast != m_literal
            && strcmp(m_file->tokenAt(ast->firstToken()).spell(), m_literalTokenText) != 0) {
            return true;
        }
        int start, end;
        m_file->startAndEndOf(ast->firstToken(), &start, &end);
        m_places.append({start, end});
        return true;
    }

    const CppRefactoringFilePtr &m_file;
    T *m_literal;
    const char *m_literalTokenText = nullptr;
    QString m_text;
    QList<ChangeSet::Range> m_places;
};

// A literal somebody wants to turn into a parameter: what is written, every
// place the function writes the same thing, its type, and where the
// function's parameter list ends. What this fix needs to know about the
// code; what it does with it is write a parameter and rename it.
class WrittenLiteral
{
public:
    // As it is written, quotes, prefix and suffix included, which is what
    // the parameter's default value is set to.
    QString text;

    // The type a parameter holding it is declared with.
    QString type;

    // Every place the function writes it, in the order they appear.
    QList<ChangeSet::Range> places;

    // Just before the ')' of the function's parameter list, and whether it
    // has any -- which decides whether a comma goes in front of the new one.
    int parametersEndAt = 0;
    bool hasParameters = false;
};

// The other side of the function, where it has one: a parameter has to be
// appended there as well, and where there is none the definition's new
// parameter gets a default value instead.
class OtherSide
{
public:
    FilePath filePath;
    int parametersEndAt = 0;
    bool hasParameters = false;

    bool isValid() const { return !filePath.isEmpty(); }
};

// Looked for when the fix is chosen rather than when it is offered, since
// it reads other files. Held as a function because which front end looks is
// settled when the fix is offered.
using FindTheOtherSide = std::function<OtherSide(const CppRefactoringChanges &)>;

class ExtractLiteralAsParameterOp : public CppQuickFixOperation
{
public:
    ExtractLiteralAsParameterOp(const CppQuickFixInterface &interface, int priority,
                                const WrittenLiteral &literal,
                                const FindTheOtherSide &findTheOtherSide)
        : CppQuickFixOperation(interface, priority)
        , m_literal(literal)
        , m_findTheOtherSide(findTheOtherSide)
    {
        setDescription(Tr::tr("Extract Constant as Function Parameter"));
    }

private:
    void perform() override
    {
        const CppRefactoringChanges refactoring(snapshot());
        const OtherSide other = m_findTheOtherSide(refactoring);

        ChangeSet changes;
        for (const ChangeSet::Range &place : m_literal.places)
            changes.replace(place.start, place.end, parameterName());

        // Where the declaration is somewhere else, the definition's new
        // parameter needs no default value: the declaration carries it.
        changes.insert(m_literal.parametersEndAt,
                       parameterDeclaration(m_literal.hasParameters, !other.isValid()));

        if (other.isValid()) {
            const QString declaration = parameterDeclaration(other.hasParameters, true);
            if (other.filePath == currentFile()->filePath()) {
                changes.insert(other.parametersEndAt, declaration);
            } else {
                const CppRefactoringFilePtr file = refactoring.cppFile(other.filePath);
                file->apply(ChangeSet::makeInsert(other.parametersEndAt, declaration));
            }
        }

        currentFile()->apply(changes);
        QTextCursor c = currentFile()->cursor();
        c.setPosition(c.position() - parameterName().size());
        editor()->setTextCursor(c);
        editor()->renameSymbolUnderCursor();
    }

    static QString parameterName() { return QLatin1String("newParameter"); }

    QString parameterDeclaration(bool afterOthers, bool withADefaultValue) const
    {
        QString str;
        if (afterOthers)
            str = QLatin1String(", ");
        str += m_literal.type;
        if (!m_literal.type.endsWith(QLatin1Char('*')))
            str += QLatin1Char(' ');
        str += parameterName();
        if (withADefaultValue)
            str += QLatin1String(" = ") + m_literal.text;
        return str;
    }

    const WrittenLiteral m_literal;
    const FindTheOtherSide m_findTheOtherSide;
};

// The function declarator of a declaration, whichever of its declarators is
// one.
FunctionDeclaratorAST *functionDeclaratorOf(DeclaratorAST *ast)
{
    for (PostfixDeclaratorListAST *pds = ast->postfix_declarator_list; pds; pds = pds->next) {
        if (FunctionDeclaratorAST *funcdecl = pds->value->asFunctionDeclarator())
            return funcdecl;
    }
    return nullptr;
}

FunctionDeclaratorAST *functionDeclaratorOf(SimpleDeclarationAST *ast)
{
    for (DeclaratorListAST *decls = ast->declarator_list; decls; decls = decls->next) {
        if (FunctionDeclaratorAST * const found = functionDeclaratorOf(decls->value))
            return found;
    }
    return nullptr;
}

bool hasParameters(FunctionDeclaratorAST *ast)
{
    return ast->parameter_declaration_clause
           && ast->parameter_declaration_clause->parameter_declaration_list
           && ast->parameter_declaration_clause->parameter_declaration_list->value;
}

// The declaration of \a definition, as the built-in front end finds it: in
// the class for a member function, and in the corresponding header for a
// free one.
OtherSide builtinOtherSideOf(const CppRefactoringChanges &refactoring,
                             const LookupContext &context, const Snapshot &snapshot,
                             const FilePath &filePath, FunctionDefinitionAST *definition)
{
    const auto answer = [&](const CppRefactoringFileConstPtr &file,
                            SimpleDeclarationAST *declaration) -> OtherSide {
        FunctionDeclaratorAST * const declarator = functionDeclaratorOf(declaration);
        if (!declarator)
            return {};
        return {file->filePath(), file->startOf(declarator->rparen_token),
                hasParameters(declarator)};
    };

    Function *func = definition->symbol;
    if (Class *matchingClass = isMemberFunction(context, func)) {
        // Dealing with member functions
        const QualifiedNameId *qName = func->name()->asQualifiedNameId();
        for (Symbol *s = matchingClass->find(qName->identifier()); s; s = s->next()) {
            if (!s->name()
                || !qName->identifier()->match(s->identifier())
                || !s->type()->asFunctionType()
                || !s->type().match(func->type())
                || s->asFunction()) {
                continue;
            }

            const CppRefactoringFileConstPtr file
                = refactoring.cppFile(matchingClass->filePath());
            const QList<AST *> path = ASTPath(file->cppDocument())(s->line(), s->column());
            SimpleDeclarationAST *simpleDecl = nullptr;
            for (AST *node : path) {
                simpleDecl = node->asSimpleDeclaration();
                if (simpleDecl && simpleDecl->symbols && !simpleDecl->symbols->next)
                    return answer(file, simpleDecl);
            }
            if (simpleDecl)
                break;
        }
    } else if (Namespace *matchingNamespace = isNamespaceFunction(context, func)) {
        // Dealing with free functions and inline member functions.
        bool isHeaderFile;
        const FilePath declFilePath = correspondingHeaderOrSource(filePath, &isHeaderFile);
        if (!declFilePath.exists())
            return {};
        const CppRefactoringFileConstPtr file = refactoring.cppFile(declFilePath);
        if (!file)
            return {};
        const LookupContext lc(file->cppDocument(), snapshot);
        const QList<LookupItem> candidates = lc.lookup(func->name(), matchingNamespace);
        for (const LookupItem &candidate : candidates) {
            Symbol * const s = candidate.declaration();
            if (!s || !s->asDeclaration())
                continue;
            const QList<AST *> path = ASTPath(file->cppDocument())(s->line(), s->column());
            for (AST *node : path) {
                if (SimpleDeclarationAST * const simpleDecl = node->asSimpleDeclaration())
                    return answer(file, simpleDecl);
            }
        }
    }
    return {};
}

// The literal at the cursor, as the built-in front end reads it, with where
// the enclosing definition's parameter list ends and how to find its
// declaration.
class FoundLiteral
{
public:
    WrittenLiteral literal;
    int priority = 0;
    FindTheOtherSide findTheOtherSide;
};

std::optional<FoundLiteral> builtinLiteralAt(const CppQuickFixInterface &interface)
{
    const QList<AST *> &path = interface.path();
    if (path.count() < 2)
        return std::nullopt;

    AST * const lastAst = path.last();
    ExpressionAST *literal;
    if (!((literal = lastAst->asNumericLiteral())
          || (literal = lastAst->asStringLiteral())
          || (literal = lastAst->asBoolLiteral()))) {
        return std::nullopt;
    }

    FunctionDefinitionAST *function;
    int i = path.count() - 2;
    while (!(function = path.at(i)->asFunctionDefinition())) {
        // Ignore literals in lambda expressions for now.
        if (path.at(i)->asLambdaExpression())
            return std::nullopt;
        if (--i < 0)
            return std::nullopt;
    }

    FunctionDeclaratorAST * const declarator = functionDeclaratorOf(function->declarator);
    if (!declarator)
        return std::nullopt;
    if (declarator->parameter_declaration_clause
        && declarator->parameter_declaration_clause->dot_dot_dot_token) {
        // Do not handle functions with ellipsis parameter.
        return std::nullopt;
    }

    const CppRefactoringFilePtr file = interface.currentFile();

    FoundLiteral found;
    found.priority = path.size() - 1;
    if (NumericLiteralAST *concrete = literal->asNumericLiteral()) {
        LiteralPlaces<NumericLiteralAST> places(file, concrete);
        found.literal.places = places.apply(function->function_body);
        found.literal.text = places.text();
    } else if (StringLiteralAST *concrete = literal->asStringLiteral()) {
        LiteralPlaces<StringLiteralAST> places(file, concrete);
        found.literal.places = places.apply(function->function_body);
        found.literal.text = places.text();
    } else if (BoolLiteralAST *concrete = literal->asBoolLiteral()) {
        LiteralPlaces<BoolLiteralAST> places(file, concrete);
        found.literal.places = places.apply(function->function_body);
        found.literal.text = places.text();
    }
    if (found.literal.places.isEmpty())
        return std::nullopt;

    TypeOfExpression typeOfExpression;
    typeOfExpression.init(interface.semanticInfo().doc, interface.snapshot());
    const QList<LookupItem> items = typeOfExpression(literal, interface.semanticInfo().doc,
                                                     function->symbol->enclosingScope());
    if (items.isEmpty())
        return std::nullopt;
    found.literal.type = Overview().prettyType(items.first().type());

    found.literal.parametersEndAt = file->startOf(declarator->rparen_token);
    found.literal.hasParameters = hasParameters(declarator);

    found.findTheOtherSide = [context = interface.context(), snapshot = interface.snapshot(),
                              filePath = interface.filePath(), function]
        (const CppRefactoringChanges &refactoring) {
        return builtinOtherSideOf(refactoring, context, snapshot, filePath, function);
    };
    return found;
}

std::optional<FoundLiteral> literalAt(const CppQuickFixInterface &interface)
{
#ifdef QTC_WITH_CXX_FRONTEND
    const CppRefactoringFilePtr file = interface.currentFile();
    const Utils::Text::Position at = Utils::Text::Position::fromPositionInDocument(
        interface.textDocument(), interface.position());
    if (const std::optional<CxxFrontendDocument::LiteralInAFunction> found
        = cxxFrontendLiteralInAFunctionAt(interface.filePath(), at.line, at.column + 1);
        found && found->isValid()) {
        const std::optional<CxxFrontendFunctionDeclaration> enclosing = cxxFrontendFunctionAt(
            interface.snapshot(), CppModelManager::workingCopy(), interface.filePath(),
            at.line, at.column + 1);
        if (enclosing && enclosing->isValid() && enclosing->isDefinition) {
            FoundLiteral answer;
            // Innermost, which is as deep as this fix ever goes; the
            // built-in tree's depth is what the number stands for and a
            // tree of another shape counts differently.
            answer.priority = 0;
            for (const CxxFrontendDocument::Occurrence &place : found->places) {
                const int start = file->position(place.line, place.column);
                answer.literal.places.append({start, start + place.length});
            }
            answer.literal.text = file->textOf(answer.literal.places.first().start,
                                               answer.literal.places.first().end);
            answer.literal.type = found->type;
            answer.literal.parametersEndAt = file->position(enclosing->parametersEndLine,
                                                            enclosing->parametersEndColumn);
            answer.literal.hasParameters = enclosing->hasParameters;
            answer.findTheOtherSide =
                [snapshot = interface.snapshot(), filePath = interface.filePath(),
                 nameLine = enclosing->nameLine, nameColumn = enclosing->nameColumn]
                (const CppRefactoringChanges &refactoring) -> OtherSide {
                const std::optional<CxxFrontendFunctionDeclaration> declared
                    = cxxFrontendDeclarationOfFunctionAt(snapshot,
                                                         CppModelManager::workingCopy(),
                                                         filePath, nameLine, nameColumn);
                if (!declared || !declared->isValid() || declared->isDefinition)
                    return {};
                const CppRefactoringFileConstPtr file
                    = refactoring.cppFile(declared->filePath);
                if (!file->isValid())
                    return {};
                return {declared->filePath,
                        file->position(declared->parametersEndLine,
                                       declared->parametersEndColumn),
                        declared->hasParameters};
            };
            return answer;
        }
    }
#endif
    return builtinLiteralAt(interface);
}

/*!
  Extracts the selected constant and converts it to a parameter of the current function.
  Activates on numeric, bool, character, or string literal in the function body.
 */
class ExtractLiteralAsParameter : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const std::optional<FoundLiteral> found = literalAt(interface);
        if (!found)
            return;
        result << new ExtractLiteralAsParameterOp(interface, found->priority, found->literal,
                                                  found->findTheOtherSide);
    }
};

#ifdef WITH_TESTS
class ExtractLiteralAsParameterTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerExtractLiteralAsParameterQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(ExtractLiteralAsParameter);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <extractliteralasparameter.moc>
#endif
