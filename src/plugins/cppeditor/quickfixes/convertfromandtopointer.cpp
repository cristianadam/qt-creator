// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "convertfromandtopointer.h"

#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#include <cplusplus/ASTPath.h>
#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>

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
    // in front of it can be taken away, and the arguments written after it
    // if there are any -- a value made without them still needs its "()".
    int typeStart = 0;
    std::optional<ChangeSet::Range> arguments;
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
            } else if (initializer.arguments) {
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
        ExpressionListAST *arguments = nullptr;
        if (made->new_initializer) {
            if (ExpressionListParenAST *parenthesized = made->new_initializer
                                                            ->asExpressionListParen()) {
                arguments = parenthesized->expression_list;
            } else if (BracedInitializerAST *braced = made->new_initializer
                                                          ->asBracedInitializer()) {
                arguments = braced->expression_list;
            }
            if (arguments) {
                written.arguments = ChangeSet::Range(file->startOf(made->new_initializer),
                                                     file->endOf(made->new_initializer));
            }
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

/*!
  Converts the selected variable to a pointer if it is a stack variable or reference, or vice versa.
  Activates on variable declarations.
 */
class ConvertFromAndToPointer : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const std::optional<WrittenVariable> variable = builtinVariableAt(interface);
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
