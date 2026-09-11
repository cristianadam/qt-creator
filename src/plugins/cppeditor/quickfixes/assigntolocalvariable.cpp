// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "assigntolocalvariable.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cppeditorwidget.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"
#include "cppquickfixsettings.h"

#include <cplusplus/CppRewriter.h>
#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>
#include <projectexplorer/projecttree.h>
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

// A value somebody is throwing away: where it stands, what it is called, and
// how its type has to be written there. The whole of what this fix needs to
// know about the code -- what it does with it is name a variable and write a
// declaration in front of the expression.
class WrittenValue
{
public:
    int insertAt = 0;

    // The name of what is called, which the variable is named after.
    QString name;

    // The value's type, written as a declaration of that name. Not a type on
    // its own, because how a declarator is written around a name -- the star
    // of a pointer, the brackets of an array -- is not something a caller
    // can work out from the type.
    QString declaration;
};

class AssignToLocalVariableOperation : public CppQuickFixOperation
{
public:
    explicit AssignToLocalVariableOperation(const CppQuickFixInterface &interface,
                                            const WrittenValue &value)
        : CppQuickFixOperation(interface)
        , m_value(value)
        , m_file(interface.currentFile())
    {
        setDescription(Tr::tr("Assign to Local Variable"));
    }

private:
    void perform() override
    {
        QString type = deduceType();
        if (type.isEmpty())
            return;
        const int origNameLength = m_value.name.size();
        const QString varName = constructVarName();
        const QString insertString = type.replace(type.size() - origNameLength, origNameLength,
                                                  varName + QLatin1String(" = "));
        m_file->apply(ChangeSet::makeInsert(m_value.insertAt, insertString));

        // move cursor to new variable name
        QTextCursor c = m_file->cursor();
        c.setPosition(m_value.insertAt + insertString.size() - varName.size() - 3);
        c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        editor()->setTextCursor(c);
    }

    QString deduceType() const
    {
        const auto settings = cppQuickFixSettingsForProject(
            ProjectExplorer::ProjectTree::currentProject());
        if (m_file->cppDocument()->languageFeatures().cxx11Enabled && settings->useAuto)
            return "auto " + m_value.name;
        return m_value.declaration;
    }

    QString constructVarName() const
    {
        QString newName = m_value.name;
        if (newName.startsWith(QLatin1String("get"), Qt::CaseInsensitive)
            && newName.size() > 3
            && newName.at(3).isUpper()) {
            newName.remove(0, 3);
            newName.replace(0, 1, newName.at(0).toLower());
        } else if (newName.startsWith(QLatin1String("to"), Qt::CaseInsensitive)
                   && newName.size() > 2
                   && newName.at(2).isUpper()) {
            newName.remove(0, 2);
            newName.replace(0, 1, newName.at(0).toLower());
        } else {
            newName.replace(0, 1, newName.at(0).toUpper());
            newName.prepend(QLatin1String("local"));
        }
        return newName;
    }

    const WrittenValue m_value;
    const CppRefactoringFilePtr m_file;
};

// The value being thrown away at the cursor, as the built-in front end reads
// it: a walk of the tree around the call, because asking whether anybody uses
// the value is what that walk is for.
std::optional<WrittenValue> builtinDiscardedValueAt(const CppQuickFixInterface &interface)
{
        const QList<AST *> &path = interface.path();
        AST *outerAST = nullptr;
        SimpleNameAST *nameAST = nullptr;

        for (int i = path.size() - 3; i >= 0; --i) {
            if (CallAST *callAST = path.at(i)->asCall()) {
                if (!interface.isCursorOn(callAST))
                    return std::nullopt;
                if (i - 2 >= 0) {
                    const int idx = i - 2;
                    if (path.at(idx)->asSimpleDeclaration())
                        return std::nullopt;
                    if (path.at(idx)->asExpressionStatement())
                        return std::nullopt;
                    if (path.at(idx)->asMemInitializer())
                        return std::nullopt;
                    if (path.at(idx)->asCall()) { // Fallback if we have a->b()->c()...
                        --i;
                        continue;
                    }
                }
                for (int a = i - 1; a > 0; --a) {
                    if (path.at(a)->asBinaryExpression())
                        return std::nullopt;
                    if (path.at(a)->asReturnStatement())
                        return std::nullopt;
                    if (path.at(a)->asCall())
                        return std::nullopt;
                }

                if (MemberAccessAST *member = path.at(i + 1)->asMemberAccess()) { // member
                    if (NameAST *name = member->member_name)
                        nameAST = name->asSimpleName();
                } else if (QualifiedNameAST *qname = path.at(i + 2)->asQualifiedName()) { // static or
                    nameAST = qname->unqualified_name->asSimpleName();                    // func in ns
                } else { // normal
                    nameAST = path.at(i + 2)->asSimpleName();
                }

                if (nameAST) {
                    outerAST = callAST;
                    break;
                }
            } else if (NewExpressionAST *newexp = path.at(i)->asNewExpression()) {
                if (!interface.isCursorOn(newexp))
                    return std::nullopt;
                if (i - 2 >= 0) {
                    const int idx = i - 2;
                    if (path.at(idx)->asSimpleDeclaration())
                        return std::nullopt;
                    if (path.at(idx)->asExpressionStatement())
                        return std::nullopt;
                    if (path.at(idx)->asMemInitializer())
                        return std::nullopt;
                }
                for (int a = i - 1; a > 0; --a) {
                    if (path.at(a)->asReturnStatement())
                        return std::nullopt;
                    if (path.at(a)->asCall())
                        return std::nullopt;
                }

                if (NamedTypeSpecifierAST *ts = path.at(i + 2)->asNamedTypeSpecifier()) {
                    nameAST = ts->name->asSimpleName();
                    outerAST = newexp;
                    break;
                }
            }
        }

        if (outerAST && nameAST) {
            const CppRefactoringFilePtr file = interface.currentFile();
            QList<LookupItem> items;
            TypeOfExpression typeOfExpression;
            typeOfExpression.init(interface.semanticInfo().doc, interface.snapshot(),
                                  interface.context().bindings());
            typeOfExpression.setExpandTemplates(true);

            // If items are empty, AssignToLocalVariableOperation will fail.
            items = typeOfExpression(file->textOf(outerAST).toUtf8(),
                                     file->scopeAt(outerAST->firstToken()),
                                     TypeOfExpression::Preprocess);
            if (items.isEmpty())
                return std::nullopt;
            const FullySpecifiedType outerType = items.first().type();

            if (CallAST *callAST = outerAST->asCall()) {
                items = typeOfExpression(file->textOf(callAST->base_expression).toUtf8(),
                                         file->scopeAt(callAST->base_expression->firstToken()),
                                         TypeOfExpression::Preprocess);
            } else {
                items = typeOfExpression(file->textOf(nameAST).toUtf8(),
                                         file->scopeAt(nameAST->firstToken()),
                                         TypeOfExpression::Preprocess);
            }

            for (const LookupItem &item : std::as_const(items)) {
                if (!item.declaration())
                    continue;

                if (Function *func = item.declaration()->asFunction()) {
                    if (func->isSignal() || func->returnType()->asVoidType())
                        return std::nullopt;
                } else if (Declaration *dec = item.declaration()->asDeclaration()) {
                    if (Function *func = dec->type()->asFunctionType()) {
                        if (func->isSignal() || func->returnType()->asVoidType())
                            return std::nullopt;
                    }
                }

                const Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();

                // The type as it has to be written where the declaration is
                // going, which is what UseMinimalNames answers: the shortest
                // spelling that still finds it from there.
                SubstitutionEnvironment env;
                env.setContext(interface.context());
                env.switchScope(items.first().scope());
                Scope * const scope = file->scopeAt(outerAST->firstToken());
                ClassOrNamespace *con = interface.context().lookupType(scope);
                if (!con)
                    con = interface.context().globalNamespace();
                UseMinimalNames q(con);
                env.enter(&q);
                Control * const control = interface.context().bindings()->control().get();

                return WrittenValue{file->startOf(outerAST),
                                    oo.prettyName(nameAST->name),
                                    oo.prettyType(rewriteType(outerType, &env, control),
                                                  nameAST->name)};
            }
        }
    return std::nullopt;
}

std::optional<WrittenValue> discardedValueAt(const CppQuickFixInterface &interface)
{
#ifdef QTC_WITH_CXX_FRONTEND
    const Utils::Text::Position at = Utils::Text::Position::fromPositionInDocument(
        interface.textDocument(), interface.position());
    if (const std::optional<CxxFrontendDocument::DiscardedValue> found
        = cxxFrontendDiscardedValueAt(interface.filePath(), at.line, at.column + 1);
        found && found->isValid()) {
        return WrittenValue{Utils::Text::Position{found->line, found->column - 1}
                                .toPositionInDocument(interface.textDocument()),
                            found->name,
                            found->declaration};
    }
#endif
    return builtinDiscardedValueAt(interface);
}

//! Assigns the return value of a function call or a new expression to a local variable
class AssignToLocalVariable : public CppQuickFixFactory
{
public:
    AssignToLocalVariable()
    {
        setClangdReplacement({20});
    }

private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const std::optional<WrittenValue> value = discardedValueAt(interface);
        if (!value)
            return;
        result << new AssignToLocalVariableOperation(interface, *value);
    }
};

#ifdef WITH_TESTS
class AssignToLocalVariableTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerAssignToLocalVariableQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(AssignToLocalVariable);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <assigntolocalvariable.moc>
#endif
