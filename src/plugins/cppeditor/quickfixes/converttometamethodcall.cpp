// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "converttometamethodcall.h"

#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"
#include "cppquickfixhelpers.h"

#include <cplusplus/ASTPath.h>
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

// What the fix rewrites, which is a call a meta object could make instead:
// where it stands, what is called on what, and what it is called with.
// Everything here is text as the file writes it, since that is what the
// new call is written out of.
class MetaMethodCall
{
public:
    // An argument as Q_ARG takes it: the type it has and the text that was
    // written for it.
    class Argument
    {
    public:
        QString type;
        QString text;
    };

    // What is replaced, taking in the "emit" or "Q_EMIT" in front of the
    // call where one stands there.
    int startPosition = -1;
    int endPosition = -1;

    QString baseExpression;
    // Whether what it is called on is already a pointer. QMetaObject wants
    // one, so anything else has to have its address taken.
    bool baseIsPointer = false;

    QString methodName;
    QList<Argument> arguments;

    // Whether the file can name QMetaObject already, which decides whether
    // the include has to be written.
    bool metaObjectIsDeclared = false;

    [[nodiscard]] bool isValid() const { return startPosition >= 0 && !methodName.isEmpty(); }
};

class ConvertToMetaMethodCallOp : public CppQuickFixOperation
{
public:
    ConvertToMetaMethodCallOp(const CppQuickFixInterface &interface, MetaMethodCall call)
        : CppQuickFixOperation(interface), m_call(std::move(call))
    {
        setDescription(Tr::tr("Convert Function Call to Qt Meta-Method Invocation"));
    }

private:
    void perform() override
    {
        QStringList arguments;
        for (const MetaMethodCall::Argument &argument : m_call.arguments)
            arguments << QString("Q_ARG(%1, %2)").arg(argument.type, argument.text);
        QString argsString = arguments.join(", ");
        if (!argsString.isEmpty())
            argsString.prepend(", ");

        const QString qMetaObject = "QMetaObject";
        QString baseExpr = m_call.baseExpression;
        if (!m_call.baseIsPointer)
            baseExpr.prepend('&');
        const QString newCall = QString("%1::invokeMethod(%2, \"%3\"%4)")
                                    .arg(qMetaObject, baseExpr, m_call.methodName, argsString);

        ChangeSet changes;
        changes.replace(m_call.startPosition, m_call.endPosition, newCall);

        if (!m_call.metaObjectIsDeclared) {
            insertNewIncludeDirective('<' + qMetaObject + '>', currentFile(), semanticInfo().doc,
                                      changes);
        }

        currentFile()->apply(changes);
    }

    const MetaMethodCall m_call;
};

// What the built-in model makes of the call at the cursor. Nothing unless
// it is a member function call of something Qt can invoke by name.
MetaMethodCall builtinMetaMethodCallAt(const CppQuickFixInterface &interface)
{
    const Document::Ptr &cppDoc = interface.currentFile()->cppDocument();
    const QList<AST *> path = ASTPath(cppDoc)(interface.cursor());
    if (path.isEmpty())
        return {};

    CallAST *callAst = nullptr;
    for (auto it = path.crbegin(); it != path.crend(); ++it) {
        if ((callAst = (*it)->asCall()))
            break;
    }
    if (!callAst || !callAst->base_expression)
        return {};
    const MemberAccessAST * const memberAccessAst = callAst->base_expression->asMemberAccess();
    if (!memberAccessAst)
        return {};
    ExpressionAST * const baseExpr = memberAccessAst->base_expression;
    const NameAST * const nameAst = memberAccessAst->member_name;
    if (!baseExpr || !nameAst || !nameAst->name)
        return {};

    // The function being called, and whether Qt can invoke it by name.
    Scope *scope = cppDoc->globalNamespace();
    for (auto it = path.crbegin(); it != path.crend(); ++it) {
        if (const CompoundStatementAST * const stmtAst = (*it)->asCompoundStatement()) {
            scope = stmtAst->symbol;
            break;
        }
    }
    const LookupContext context(cppDoc, interface.snapshot());
    TypeOfExpression exprType;
    exprType.setExpandTemplates(true);
    exprType.init(cppDoc, interface.snapshot());
    bool isInvokable = false;
    for (const LookupItem &item : exprType(callAst->base_expression, cppDoc, scope)) {
        if (const auto func = item.type()->asFunctionType(); func && func->methodKey()) {
            isInvokable = true;
            break;
        }
    }
    if (!isInvokable)
        return {};

    MetaMethodCall call;

    Overview ov;
    for (ExpressionListAST *it = callAst->expression_list; it; it = it->next) {
        if (!it->value)
            return {};
        const FullySpecifiedType argType
            = typeOfExpr(it->value, interface.currentFile(), interface.snapshot(), context);
        if (!argType.isValid())
            return {};
        call.arguments << MetaMethodCall::Argument{ov.prettyType(argType),
                                                   interface.currentFile()->textOf(it->value)};
    }

    call.baseExpression = interface.currentFile()->textOf(baseExpr);
    const FullySpecifiedType baseExprType
        = typeOfExpr(baseExpr, interface.currentFile(), interface.snapshot(), context);
    if (!baseExprType.isValid())
        return {};
    call.baseIsPointer = baseExprType->asPointerType() != nullptr;
    call.methodName = interface.currentFile()->textOf(nameAst);

    // The "emit" in front of the call is part of what is being replaced,
    // there being nothing to emit afterwards.
    int firstToken = callAst->firstToken();
    if (firstToken > 0) {
        switch (cppDoc->translationUnit()->tokenKind(firstToken - 1)) {
        case T_EMIT: case T_Q_EMIT: --firstToken; break;
        default: break;
        }
    }
    const TranslationUnit * const tu = cppDoc->translationUnit();
    call.startPosition = tu->getTokenPositionInDocument(firstToken, interface.textDocument());
    call.endPosition = tu->getTokenPositionInDocument(callAst->lastToken(),
                                                     interface.textDocument());

    const Identifier qMetaObjectId("QMetaObject", 11);
    Scope * const scopeAtCall = interface.currentFile()->scopeAt(firstToken);
    for (const LookupItem &item : context.lookup(&qMetaObjectId, scopeAtCall)) {
        if (Symbol * const declaration = item.declaration(); declaration && declaration->asClass()) {
            call.metaObjectIsDeclared = true;
            break;
        }
    }

    return call;
}

#ifdef QTC_WITH_CXX_FRONTEND
// The same, off the cxx-frontend model. Nothing where it has no such file,
// where the position is on no call Qt can make by name, or where the text
// the new call is built from is not all there.
std::optional<MetaMethodCall> modelMetaMethodCallAt(const CppQuickFixInterface &interface)
{
    const int line = interface.currentFile()->cursor().blockNumber() + 1;
    const int column = interface.currentFile()->cursor().positionInBlock() + 1;
    const std::optional<CPlusPlus::CxxFrontendDocument::MetaMethodCall> read
        = cxxFrontendMetaMethodCallAt(interface.currentFile()->filePath(), line, column);
    if (!read)
        return std::nullopt;

    const auto textOf = [&](const CPlusPlus::CxxFrontendDocument::Extent &extent) {
        const int start = interface.currentFile()->position(extent.startLine, extent.startColumn);
        const int end = interface.currentFile()->position(extent.endLine, extent.endColumn);
        return interface.currentFile()->textOf(start, end);
    };

    MetaMethodCall call;
    call.startPosition = interface.currentFile()->position(read->replaced.startLine,
                                                           read->replaced.startColumn);
    call.endPosition = interface.currentFile()->position(read->replaced.endLine,
                                                         read->replaced.endColumn);
    call.baseExpression = textOf(read->base);
    call.baseIsPointer = read->baseIsPointer;
    call.methodName = read->methodName;
    for (const auto &argument : read->arguments)
        call.arguments << MetaMethodCall::Argument{argument.type, textOf(argument.written)};

    // Whether the file can name QMetaObject already, which is a lookup
    // from where the call stands.
    if (const std::optional<CPlusPlus::CxxFrontendDocument::Declaration> declared
        = cxxFrontendLookup(interface.currentFile()->filePath(), "QMetaObject")) {
        call.metaObjectIsDeclared = declared->isValid()
                                    && declared->kind
                                           == CPlusPlus::CxxFrontendDocument::Kind::Class;
    }

    return call;
}
#endif

//! Converts a normal function call into a meta method invocation, if the functions is
//! marked as invokable.
class ConvertToMetaMethodCall : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (const std::optional<MetaMethodCall> onTheModel = modelMetaMethodCallAt(interface)) {
            if (onTheModel->isValid())
                result << new ConvertToMetaMethodCallOp(interface, *onTheModel);
            return;
        }
#endif
        const MetaMethodCall call = builtinMetaMethodCallAt(interface);
        if (!call.isValid())
            return;
        result << new ConvertToMetaMethodCallOp(interface, call);
    }
};

#ifdef WITH_TESTS
class ConvertToMetaMethodCallTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerConvertToMetaMethodCallQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(ConvertToMetaMethodCall);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <converttometamethodcall.moc>
#endif
