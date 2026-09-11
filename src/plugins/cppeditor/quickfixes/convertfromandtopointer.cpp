// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "convertfromandtopointer.h"

#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"
#include "cppquickfixhelpers.h"

#include <cplusplus/ASTPath.h>
#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendDocument.h>

#include <cxx/ast.h>
#include <cxx/names.h>
#include <cxx/symbols.h>
#include <cxx/types.h>
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

#include <optional>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

// Which way round the variable is written now, which is what this fix turns.
enum class Written { Pointer, Value, Reference };

// How one place writes the variable: where its name stands, and what is
// written around it -- which is all that decides how that place has to
// change.
struct WrittenUse
{
    ChangeSet::Range name;

    // The declaration itself, which is a place the name is written like any
    // other and the one place that is not a use of it.
    bool isTheDeclaration = false;

    // The "." or "->" written after it, to reach a member through it.
    std::optional<ChangeSet::Range> memberAccess;
    bool memberAccessIsArrow = false;

    // The "*" or the "&" written in front of it, which says that whoever
    // wrote this place wanted the other way round already.
    std::optional<ChangeSet::Range> star;
    std::optional<ChangeSet::Range> ampersand;

    // Where the "delete" that frees it stands. A stack variable is not
    // deleted, and what to do with the line instead is nobody's guess, so it
    // is commented out rather than taken away.
    std::optional<int> deletedAt;
};

// The initializer as it is written, in the shapes this fix has something to
// say about.
struct WrittenInitializer
{
    enum class Kind {
        None,        // "S s;"
        New,         // "= new S(1)"
        Name,        // "= other"
        Call,        // "= S(1)", a value made of a type, or a call
        Parentheses, // "s(1)"
        Braces       // "s{1}"
    };
    Kind kind = Kind::None;
    ChangeSet::Range range;

    // Of a new expression: where the type it names begins, so that the "new"
    // in front of it can be taken away, and the "(...)" or "{...}" written
    // after it if there is one -- a value made without one still needs its
    // "()", and one made with nothing in it is not worth carrying over.
    int typeStart = 0;
    std::optional<ChangeSet::Range> arguments;
    bool argumentsAreEmpty = false;
};

// The declaration this fix turns round, and every place the function writes
// it. Which tree that was read off is the front end's business; none of this
// is.
struct WrittenVariable
{
    Written written = Written::Value;
    bool isAuto = false;

    // The name the type is written under, which a "new" has to name. Empty
    // where the declaration does not write one -- an auto, or a type written
    // as something other than a plain name.
    QString typeName;

    // The "*" or the "&" of the declarator, which the fix takes away.
    std::optional<ChangeSet::Range> pointerOperator;

    // Just after the declared name, which is where " = new S" goes when
    // there is no initializer to work from.
    int nameEnd = 0;

    WrittenInitializer initializer;
    QList<WrittenUse> uses;
};

class ConvertFromAndToPointerOp : public CppQuickFixOperation
{
public:
    ConvertFromAndToPointerOp(const CppQuickFixInterface &interface, int priority,
                              const WrittenVariable &variable)
        : CppQuickFixOperation(interface, priority)
        , m_variable(variable)
    {
        setDescription(m_variable.written == Written::Pointer
                           ? Tr::tr("Convert to Stack Variable")
                           : Tr::tr("Convert to Pointer"));
    }

    void perform() override
    {
        ChangeSet changes;
        if (m_variable.written == Written::Pointer)
            convertToStackVariable(changes);
        else
            convertToPointer(changes);
        currentFile()->apply(changes);
    }

private:
    void convertToStackVariable(ChangeSet &changes) const
    {
        if (m_variable.pointerOperator)
            changes.remove(*m_variable.pointerOperator);

        // What was made on the heap is now made where it stands.
        const WrittenInitializer &initializer = m_variable.initializer;
        if (initializer.kind == WrittenInitializer::Kind::New) {
            if (m_variable.isAuto) {
                // "auto s = new S" is where the type is written, so only the
                // "new" goes -- and a value has to be made of something, so
                // an S with nothing after it becomes S().
                if (!initializer.arguments)
                    changes.insert(initializer.range.end, "()");
                changes.remove(initializer.range.start, initializer.typeStart);
            } else if (initializer.arguments && !initializer.argumentsAreEmpty) {
                // The type stands in the declaration already, so what is left
                // of the initializer is its arguments: "S *s = new S(1)"
                // becomes "S s(1)".
                changes.remove(initializer.range.start, initializer.arguments->start);
                changes.remove(m_variable.nameEnd, initializer.range.start);
            } else {
                changes.remove(m_variable.nameEnd, initializer.range.end);
            }
        }

        for (const WrittenUse &use : m_variable.uses) {
            if (use.isTheDeclaration)
                continue;
            if (use.memberAccess && use.memberAccessIsArrow) {
                changes.replace(*use.memberAccess, ".");
            } else if (use.star) {
                changes.remove(*use.star);
            } else if (use.deletedAt) {
                changes.insert(*use.deletedAt, "// ");
            } else if (use.ampersand) {
                // The address of a pointer was wanted, and the address of a
                // value is one indirection short of it.
                changes.insert(use.ampersand->start, "&(");
                changes.insert(use.name.end, ")");
            } else {
                changes.insert(use.name.start, "&");
            }
        }
    }

    void convertToPointer(ChangeSet &changes) const
    {
        if (m_variable.written == Written::Reference && m_variable.pointerOperator)
            changes.remove(*m_variable.pointerOperator);

        // What was made where it stands is now made on the heap.
        const WrittenInitializer &initializer = m_variable.initializer;
        const QString &typeName = m_variable.typeName;
        switch (initializer.kind) {
        case WrittenInitializer::Kind::Name:
            changes.insert(initializer.range.start, "&");
            break;
        case WrittenInitializer::Kind::Call:
            // A call already says what it makes, unless the declaration names
            // a type of its own -- and then what the call hands back is what
            // the new value is made of.
            if (typeName.isEmpty()) {
                changes.insert(initializer.range.start, "new ");
            } else {
                changes.insert(initializer.range.start, "new " + typeName + "(");
                changes.insert(initializer.range.end, ")");
            }
            break;
        case WrittenInitializer::Kind::Parentheses:
        case WrittenInitializer::Kind::Braces:
            if (!typeName.isEmpty())
                changes.insert(initializer.range.start, " = new " + typeName);
            break;
        case WrittenInitializer::Kind::None:
            if (!typeName.isEmpty())
                changes.insert(m_variable.nameEnd, " = new " + typeName);
            break;
        case WrittenInitializer::Kind::New:
            break;
        }

        for (const WrittenUse &use : m_variable.uses) {
            // The declaration is a place the name is written, so this is
            // also where the "*" of "S *s" comes from -- except under an
            // auto, which says it for itself.
            if (use.isTheDeclaration && m_variable.isAuto)
                continue;
            if (use.memberAccess)
                changes.replace(*use.memberAccess, "->");
            else if (use.ampersand)
                changes.remove(*use.ampersand);
            else
                changes.insert(use.name.start, "*");
        }
    }

    const WrittenVariable m_variable;
};

// Every place the function writes the variable, as the built-in front end
// reads them: the tree around each use says what is written there.
QList<WrittenUse> builtinUsesOf(const CppQuickFixInterface &interface, Symbol *symbol,
                                const DeclaratorAST *declarator)
{
    const CppRefactoringFilePtr file = interface.currentFile();
    ASTPath astPath(interface.semanticInfo().doc);

    QList<WrittenUse> written;
    const QList<SemanticInfo::Use> uses = interface.semanticInfo().localUses.value(symbol);
    for (const SemanticInfo::Use &use : uses) {
        const QList<AST *> path = astPath(use.line, use.column);
        if (path.isEmpty())
            continue;

        AST * const idAST = path.last();
        WrittenUse one;
        one.name = ChangeSet::Range(file->startOf(idAST), file->endOf(idAST->firstToken()));

        for (int i = path.count() - 2; i >= 0; --i) {
            if (path.at(i) == declarator) {
                one.isTheDeclaration = true;
                break;
            }
            if (MemberAccessAST *memberAccess = path.at(i)->asMemberAccess()) {
                if (one.memberAccess)
                    continue;
                const bool isArrow
                    = file->tokenAt(memberAccess->access_token).kind() == T_ARROW;
                const int pos = file->startOf(memberAccess->access_token);
                one.memberAccess = ChangeSet::Range(pos, pos + (isArrow ? 2 : 1));
                one.memberAccessIsArrow = isArrow;
                if (isArrow)
                    break;
                // A "." reaches a member of what is written there, whichever
                // way round that is. Standing on a pointer it says nothing
                // about this fix, so whatever else is written around the name
                // still counts.
            } else if (DeleteExpressionAST *deleted = path.at(i)->asDeleteExpression()) {
                one.deletedAt = file->startOf(deleted->delete_token);
                break;
            } else if (UnaryExpressionAST *unary = path.at(i)->asUnaryExpression()) {
                const Token tk = file->tokenAt(unary->unary_op_token);
                const int pos = file->startOf(unary->unary_op_token);
                if (tk.kind() == T_STAR) {
                    if (!one.star)
                        one.star = ChangeSet::Range(pos, pos + 1);
                } else if (tk.kind() == T_AMPER) {
                    one.ampersand = ChangeSet::Range(pos, pos + 1);
                }
            } else if (PointerAST *pointer = path.at(i)->asPointer()) {
                if (!one.star) {
                    const int pos = file->startOf(pointer->star_token);
                    one.star = ChangeSet::Range(pos, pos);
                }
            } else if (path.at(i)->asFunctionDefinition()) {
                break;
            }
        }
        written << one;
    }
    return written;
}

// The initializer of a declaration, as the built-in front end reads it.
WrittenInitializer builtinInitializerOf(const CppQuickFixInterface &interface,
                                        const DeclaratorAST *declarator)
{
    const CppRefactoringFilePtr file = interface.currentFile();
    ExpressionAST * const initializer = declarator->initializer;
    if (!initializer)
        return {};

    WrittenInitializer written;
    written.range = ChangeSet::Range(file->startOf(initializer), file->endOf(initializer));

    if (NewExpressionAST *made = initializer->asNewExpression()) {
        written.kind = WrittenInitializer::Kind::New;
        written.typeStart = made->new_type_id ? file->startOf(made->new_type_id)
                                              : written.range.end;
        if (made->new_initializer) {
            ExpressionListAST *arguments = nullptr;
            if (ExpressionListParenAST *parenthesized = made->new_initializer
                                                            ->asExpressionListParen()) {
                arguments = parenthesized->expression_list;
            } else if (BracedInitializerAST *braced = made->new_initializer
                                                          ->asBracedInitializer()) {
                arguments = braced->expression_list;
            }
            written.arguments = ChangeSet::Range(file->startOf(made->new_initializer),
                                                 file->endOf(made->new_initializer));
            written.argumentsAreEmpty = !arguments;
        }
    } else if (initializer->asIdExpression()) {
        written.kind = WrittenInitializer::Kind::Name;
    } else if (initializer->asCall()) {
        written.kind = WrittenInitializer::Kind::Call;
    } else if (initializer->asExpressionListParen()) {
        written.kind = WrittenInitializer::Kind::Parentheses;
    } else if (initializer->asBracedInitializer()) {
        written.kind = WrittenInitializer::Kind::Braces;
    }
    return written;
}

// The name a declaration writes its type under, empty where it does not
// write one as a plain name.
QString builtinTypeNameOf(const SimpleDeclarationAST *declaration)
{
    if (!declaration || !declaration->decl_specifier_list
        || !declaration->decl_specifier_list->value) {
        return {};
    }
    NamedTypeSpecifierAST * const namedType
        = declaration->decl_specifier_list->value->asNamedTypeSpecifier();
    if (!namedType)
        return {};

    Overview overview;
    return overview.prettyName(namedType->name->name);
}

// The local variable the cursor is on, as the built-in front end reads it.
std::optional<WrittenVariable> builtinVariableAt(const CppQuickFixInterface &interface)
{
    const QList<AST *> &path = interface.path();
    if (path.count() < 2)
        return {};
    SimpleNameAST * const identifier = path.last()->asSimpleName();
    if (!identifier)
        return {};

    SimpleDeclarationAST *simpleDeclaration = nullptr;
    DeclaratorAST *declarator = nullptr;
    bool isFunctionLocal = false;
    bool isClassLocal = false;
    Written written = Written::Value;
    for (int i = path.count() - 2; i >= 0; --i) {
        AST *ast = path.at(i);
        if (!declarator && (declarator = ast->asDeclarator()))
            continue;
        if (!simpleDeclaration && (simpleDeclaration = ast->asSimpleDeclaration()))
            continue;
        if (declarator && simpleDeclaration) {
            if (ast->asClassSpecifier()) {
                isClassLocal = true;
            } else if (ast->asFunctionDefinition() && !isClassLocal) {
                isFunctionLocal = true;
                break;
            }
        }
    }
    if (!isFunctionLocal || !simpleDeclaration || !declarator)
        return {};

    Symbol *symbol = nullptr;
    for (List<Symbol *> *lst = simpleDeclaration->symbols; lst; lst = lst->next) {
        if (lst->value->name() == identifier->name) {
            symbol = lst->value;
            break;
        }
    }
    if (!symbol)
        return {};

    const CppRefactoringFilePtr file = interface.currentFile();
    WrittenVariable variable;

    // The "*" or "&" of the declarator, whether or not it is what says which
    // way round the variable is written: "auto *p = new S" says it with the
    // initializer and still has a star to take away.
    if (declarator->ptr_operator_list) {
        if (PointerAST *pointer = declarator->ptr_operator_list->value->asPointer()) {
            const int pos = file->startOf(pointer->star_token);
            variable.pointerOperator = ChangeSet::Range(pos, pos + 1);
        } else if (ReferenceAST *reference = declarator->ptr_operator_list->value->asReference()) {
            const int pos = file->startOf(reference->reference_token);
            variable.pointerOperator = ChangeSet::Range(pos, pos + 1);
        }
    }

    if (symbol->storage() == Symbol::Auto) {
        // For auto variables we must deduce the type from the initializer.
        if (!declarator->initializer)
            return {};

        variable.isAuto = true;
        TypeOfExpression typeOfExpression;
        typeOfExpression.init(interface.semanticInfo().doc, interface.snapshot());
        typeOfExpression.setExpandTemplates(true);
        Scope *scope = file->scopeAt(declarator->firstToken());
        QList<LookupItem> result = typeOfExpression(file->textOf(declarator->initializer).toUtf8(),
                                                    scope, TypeOfExpression::Preprocess);
        if (!result.isEmpty() && result.first().type()->asPointerType())
            written = Written::Pointer;
    } else if (declarator->ptr_operator_list) {
        for (PtrOperatorListAST *ops = declarator->ptr_operator_list; ops; ops = ops->next) {
            if (ops != declarator->ptr_operator_list) {
                // Bail out on more complex pointer types (e.g. pointer of pointer,
                // or reference of pointer).
                return {};
            }
            if (ops->value->asPointer())
                written = Written::Pointer;
            else if (ops->value->asReference())
                written = Written::Reference;
        }
    }

    variable.written = written;
    variable.typeName = builtinTypeNameOf(simpleDeclaration);
    variable.nameEnd = file->endOf(identifier->firstToken());
    variable.initializer = builtinInitializerOf(interface, declarator);
    variable.uses = builtinUsesOf(interface, symbol, declarator);
    return variable;
}

#ifdef QTC_WITH_CXX_FRONTEND
// The expression as it stands in the file, past the conversions the place it
// stands in asked for.
cxx::ExpressionAST *cxxWritten(cxx::ExpressionAST *expression)
{
    while (auto * const cast = dynamic_cast<cxx::ImplicitCastExpressionAST *>(expression))
        expression = cast->expression;
    return expression;
}

// A place in the file as the editor counts it, or nothing where the file
// does not write it -- which is where these fixes hand back.
std::optional<ChangeSet::Range> cxxRangeOfNode(const CxxFrontendDocument &document,
                                               const CppRefactoringFilePtr &file,
                                               cxx::AST *node)
{
    const CxxAstRange range = cxxAstRangeOf(document, node);
    if (!range.isValid())
        return {};
    return ChangeSet::Range(file->position(range.startLine, range.startColumn),
                            file->position(range.endLine, range.endColumn));
}

std::optional<ChangeSet::Range> cxxRangeOfToken(const CxxFrontendDocument &document,
                                                const CppRefactoringFilePtr &file,
                                                cxx::SourceLocation location)
{
    const CxxAstRange range = cxxTokenRangeAt(document, location);
    if (!range.isValid())
        return {};
    return ChangeSet::Range(file->position(range.startLine, range.startColumn),
                            file->position(range.endLine, range.endColumn));
}

// Every place the function writes the variable, as the cxx-frontend model
// read them: what stands around each one is what the tree there says.
//
// Nothing where one of them cannot be read -- a place left as it is would
// leave the file saying something else, so the fix hands back instead.
std::optional<QList<WrittenUse>> cxxUsesOf(const CxxFrontendDocument &document,
                                           const CppRefactoringFilePtr &file,
                                           const QList<CxxFrontendDocument::Occurrence> &places,
                                           cxx::AST *declaration)
{
    QList<WrittenUse> written;
    for (const CxxFrontendDocument::Occurrence &place : places) {
        const QList<cxx::AST *> path = cxxAstPathAt(document, place.line, place.column);
        if (path.isEmpty())
            return {};

        WrittenUse one;
        const int start = file->position(place.line, place.column);
        one.name = ChangeSet::Range(start, start + place.length);

        for (int i = path.size() - 2; i >= 0; --i) {
            cxx::AST * const node = path.at(i);
            if (node == declaration) {
                one.isTheDeclaration = true;
                break;
            }
            if (auto * const member = dynamic_cast<cxx::MemberExpressionAST *>(node)) {
                if (one.memberAccess)
                    continue;
                const std::optional<ChangeSet::Range> at
                    = cxxRangeOfToken(document, file, member->accessLoc);
                if (!at)
                    return {};
                one.memberAccess = at;
                one.memberAccessIsArrow = member->accessOp == cxx::TokenKind::T_MINUS_GREATER;
                if (one.memberAccessIsArrow)
                    break;
            } else if (auto * const deleted = dynamic_cast<cxx::DeleteExpressionAST *>(node)) {
                const std::optional<ChangeSet::Range> at
                    = cxxRangeOfToken(document, file, deleted->deleteLoc);
                if (!at)
                    return {};
                one.deletedAt = at->start;
                break;
            } else if (auto * const unary = dynamic_cast<cxx::UnaryExpressionAST *>(node)) {
                if (unary->op != cxx::TokenKind::T_STAR && unary->op != cxx::TokenKind::T_AMP)
                    continue;
                const std::optional<ChangeSet::Range> at
                    = cxxRangeOfToken(document, file, unary->opLoc);
                if (!at)
                    return {};
                if (unary->op == cxx::TokenKind::T_STAR) {
                    if (!one.star)
                        one.star = at;
                } else {
                    one.ampersand = at;
                }
            } else if (dynamic_cast<cxx::FunctionDefinitionAST *>(node)) {
                break;
            }
        }
        written << one;
    }
    return written;
}

// The initializer of a declaration, as the cxx-frontend model read it.
std::optional<WrittenInitializer> cxxInitializerOf(const CxxFrontendDocument &document,
                                                   const CppRefactoringFilePtr &file,
                                                   cxx::InitDeclaratorAST *declared)
{
    if (!declared->initializer)
        return WrittenInitializer{};

    // "= x" is a node of its own here, and what the fix works on is what
    // stands after the "=".
    cxx::ExpressionAST *initializer = declared->initializer;
    if (auto * const equals = dynamic_cast<cxx::EqualInitializerAST *>(initializer))
        initializer = equals->expression;
    initializer = cxxWritten(initializer);
    if (!initializer)
        return WrittenInitializer{};

    const std::optional<ChangeSet::Range> range = cxxRangeOfNode(document, file, initializer);
    if (!range)
        return {};

    WrittenInitializer written;
    written.range = *range;
    if (auto * const made = dynamic_cast<cxx::NewExpressionAST *>(initializer)) {
        written.kind = WrittenInitializer::Kind::New;
        written.typeStart = range->end;
        for (auto *specifier : cxx::ListView{made->typeSpecifierList}) {
            const std::optional<ChangeSet::Range> type
                = cxxRangeOfNode(document, file, specifier);
            if (!type)
                return {};
            written.typeStart = type->start;
            break;
        }
        if (made->newInitalizer) {
            const std::optional<ChangeSet::Range> arguments
                = cxxRangeOfNode(document, file, made->newInitalizer);
            if (!arguments)
                return {};
            written.arguments = arguments;
            if (auto * const parenthesized
                = dynamic_cast<cxx::NewParenInitializerAST *>(made->newInitalizer)) {
                written.argumentsAreEmpty = !parenthesized->expressionList;
            } else if (auto * const braced = dynamic_cast<cxx::NewBracedInitializerAST *>(
                           made->newInitalizer)) {
                written.argumentsAreEmpty = !braced->bracedInitList
                                            || !braced->bracedInitList->expressionList;
            }
        }
    } else if (dynamic_cast<cxx::IdExpressionAST *>(initializer)) {
        written.kind = WrittenInitializer::Kind::Name;
    } else if (dynamic_cast<cxx::CallExpressionAST *>(initializer)
               || dynamic_cast<cxx::TypeConstructionAST *>(initializer)) {
        // Making a value of a type is a node of its own here; the built-in
        // tree calls both of them a call.
        written.kind = WrittenInitializer::Kind::Call;
    } else if (dynamic_cast<cxx::ParenInitializerAST *>(initializer)) {
        written.kind = WrittenInitializer::Kind::Parentheses;
    } else if (dynamic_cast<cxx::BracedInitListAST *>(initializer)) {
        written.kind = WrittenInitializer::Kind::Braces;
    }
    return written;
}

// The local variable the cursor is on, as the cxx-frontend model read it.
//
// Nothing where it has not read the file, where the cursor is on something
// other than the name of a function-local variable, or where any part of
// what the fix rewrites could not be read; the built-in path then answers,
// as it did before.
std::optional<WrittenVariable> cxxVariableAt(const CppQuickFixInterface &interface)
{
    const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface);
    if (!document)
        return {};

    const CppRefactoringFilePtr file = interface.currentFile();

    // The editor counts from zero and the tree from one.
    const QTextCursor cursor = file->cursor();
    const int line = cursor.blockNumber() + 1;
    const int column = cursor.positionInBlock() + 1;
    const QList<cxx::AST *> path = cxxAstPathAt(*document, line, column);
    if (path.isEmpty())
        return {};

    auto * const name = dynamic_cast<cxx::NameIdAST *>(path.last());
    if (!name || !name->identifier)
        return {};

    // What declares it, and whether that is a variable of a function rather
    // than a member of a class written inside one.
    cxx::InitDeclaratorAST *declared = nullptr;
    cxx::SimpleDeclarationAST *declaration = nullptr;
    bool isFunctionLocal = false;
    bool isClassLocal = false;
    for (int i = path.size() - 2; i >= 0; --i) {
        cxx::AST * const node = path.at(i);
        if (!declared && (declared = dynamic_cast<cxx::InitDeclaratorAST *>(node)))
            continue;
        if (!declaration && (declaration = dynamic_cast<cxx::SimpleDeclarationAST *>(node)))
            continue;
        if (declared && declaration) {
            if (dynamic_cast<cxx::ClassSpecifierAST *>(node)) {
                isClassLocal = true;
            } else if (dynamic_cast<cxx::FunctionDefinitionAST *>(node) && !isClassLocal) {
                isFunctionLocal = true;
                break;
            }
        }
    }
    if (!isFunctionLocal || !declared || !declaration || !declared->declarator)
        return {};

    // Not something to rewrite: where the front end stumbled inside the
    // declaration its tree does not match the text.
    if (cxxAstWasReadWithErrors(*document, declaration))
        return {};

    WrittenVariable variable;

    // The "*" or "&" of the declarator, whether or not it is what says which
    // way round the variable is written.
    cxx::PtrOperatorAST *pointerOperator = nullptr;
    for (auto *op : cxx::ListView{declared->declarator->ptrOpList}) {
        if (pointerOperator)
            return {}; // A pointer to a pointer is more than this fix says.
        pointerOperator = op;
    }
    Written written = Written::Value;
    if (pointerOperator) {
        cxx::SourceLocation at;
        if (auto * const star = dynamic_cast<cxx::PointerOperatorAST *>(pointerOperator)) {
            written = Written::Pointer;
            at = star->starLoc;
        } else if (auto * const reference
                   = dynamic_cast<cxx::ReferenceOperatorAST *>(pointerOperator)) {
            written = Written::Reference;
            at = reference->refLoc;
        }
        const std::optional<ChangeSet::Range> range = cxxRangeOfToken(*document, file, at);
        if (!range)
            return {};
        // One character of it, which is what an "&&" is left half of, as the
        // built-in path leaves it too.
        variable.pointerOperator = ChangeSet::Range(range->start, range->start + 1);
    }

    // An auto says which way round it is with its initializer, not with a
    // star, so the type the front end deduced is what answers.
    for (auto *specifier : cxx::ListView{declaration->declSpecifierList}) {
        if (dynamic_cast<cxx::AutoTypeSpecifierAST *>(specifier))
            variable.isAuto = true;
    }
    if (variable.isAuto) {
        if (!declared->initializer || !declared->symbol)
            return {};
        if (cxx::type_cast<cxx::PointerType>(declared->symbol->type()))
            written = Written::Pointer;
        else
            written = Written::Value;
    }
    variable.written = written;

    // The name the type is written under, which only a declaration that
    // writes one as a plain name has.
    for (auto *specifier : cxx::ListView{declaration->declSpecifierList}) {
        if (auto * const named = dynamic_cast<cxx::NamedTypeSpecifierAST *>(specifier)) {
            const std::optional<ChangeSet::Range> type
                = cxxRangeOfNode(*document, file, named);
            if (!type)
                return {};
            variable.typeName = file->textOf(*type);
        }
        break;
    }

    const std::optional<ChangeSet::Range> nameRange = cxxRangeOfNode(*document, file, name);
    if (!nameRange)
        return {};
    variable.nameEnd = nameRange->end;

    const std::optional<WrittenInitializer> initializer
        = cxxInitializerOf(*document, file, declared);
    if (!initializer)
        return {};
    variable.initializer = *initializer;

    // Which of the function's locals this is: two variables of the same name
    // in blocks of their own are two locals, each with the places that write
    // it.
    for (const CxxFrontendDocument::Local &local : document->localsAt(line, column)) {
        const bool isTheOne = Utils::anyOf(local.places,
                                           [&](const CxxFrontendDocument::Occurrence &place) {
                                               return file->position(place.line, place.column)
                                                      == nameRange->start;
                                           });
        if (!isTheOne)
            continue;
        const std::optional<QList<WrittenUse>> uses
            = cxxUsesOf(*document, file, local.places, declared);
        if (!uses)
            return {};
        variable.uses = *uses;
        return variable;
    }
    return {};
}
#endif

std::optional<WrittenVariable> variableAt(const CppQuickFixInterface &interface)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<WrittenVariable> variable = cxxVariableAt(interface))
        return variable;
#endif
    return builtinVariableAt(interface);
}

/*!
  Converts the selected variable to a pointer if it is a stack variable or reference, or vice versa.
  Activates on variable declarations.
 */
class ConvertFromAndToPointer : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const std::optional<WrittenVariable> variable = variableAt(interface);
        if (!variable)
            return;

        const int priority = interface.path().size() - 1;
        result << new ConvertFromAndToPointerOp(interface, priority, *variable);
    }
};

#ifdef WITH_TESTS
class ConvertFromAndToPointerTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerConvertFromAndToPointerQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(ConvertFromAndToPointer);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <convertfromandtopointer.moc>
#endif
