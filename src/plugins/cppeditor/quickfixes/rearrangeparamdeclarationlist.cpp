// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "rearrangeparamdeclarationlist.h"

#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

using namespace CPlusPlus;

namespace CppEditor::Internal {
namespace {

class RearrangeParamDeclarationListOp: public CppQuickFixOperation
{
public:
    enum Target { TargetPrevious, TargetNext };

    // The two stretches of text to swap, rather than the two nodes they were
    // read off: which node a parameter is depends on which front end read the
    // file, and what is written there does not.
    RearrangeParamDeclarationListOp(const CppQuickFixInterface &interface,
                                    int currentStart, int currentEnd,
                                    int targetStart, int targetEnd, Target target)
        : CppQuickFixOperation(interface)
        , m_currentStart(currentStart)
        , m_currentEnd(currentEnd)
        , m_targetStart(targetStart)
        , m_targetEnd(targetEnd)
    {
        QString targetString;
        if (target == TargetPrevious)
            targetString = Tr::tr("Switch with Previous Parameter");
        else
            targetString = Tr::tr("Switch with Next Parameter");
        setDescription(targetString);
    }

    void perform() override
    {
        currentFile()->setOpenEditor(false, m_targetEnd);
        currentFile()->apply(Utils::ChangeSet::makeFlip(m_currentStart, m_currentEnd,
                                                        m_targetStart, m_targetEnd));
    }

private:
    int m_currentStart;
    int m_currentEnd;
    int m_targetStart;
    int m_targetEnd;
};


/*!
  Switches places of the parameter declaration under cursor
  with the next or the previous one in the parameter declaration list

  Activates on: parameter declarations
*/
class RearrangeParamDeclarationList : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        // The other model first, where it has read this file; it declines
        // when it has not, and the built-in path below then answers as it
        // always did. See cxxfrontendmodel.h.
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        const QList<AST *> path = interface.path();

        ParameterDeclarationAST *paramDecl = nullptr;
        int index = path.size() - 1;
        for (; index != -1; --index) {
            paramDecl = path.at(index)->asParameterDeclaration();
            if (paramDecl)
                break;
        }

        if (index < 1)
            return;

        ParameterDeclarationClauseAST *paramDeclClause
            = path.at(index - 1)->asParameterDeclarationClause();
        QTC_ASSERT(paramDeclClause && paramDeclClause->parameter_declaration_list, return);

        ParameterDeclarationListAST *paramListNode = paramDeclClause->parameter_declaration_list;
        ParameterDeclarationListAST *prevParamListNode = nullptr;
        while (paramListNode) {
            if (paramDecl == paramListNode->value)
                break;
            prevParamListNode = paramListNode;
            paramListNode = paramListNode->next;
        }

        if (!paramListNode)
            return;

        const CppRefactoringFilePtr file = interface.currentFile();
        const auto add = [&](AST *target, RearrangeParamDeclarationListOp::Target which) {
            result << new RearrangeParamDeclarationListOp(
                interface,
                file->startOf(paramListNode->value), file->endOf(paramListNode->value),
                file->startOf(target), file->endOf(target), which);
        };

        if (prevParamListNode)
            add(prevParamListNode->value, RearrangeParamDeclarationListOp::TargetPrevious);
        if (paramListNode->next)
            add(paramListNode->next->value, RearrangeParamDeclarationListOp::TargetNext);
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same two operations, worked out on the cxx-frontend model's syntax
    // tree: the parameter the cursor is in, and the one written before or
    // after it in the same list.
    //
    // False where the model has not read this file -- it is off unless asked
    // for -- and where the cursor is not in a parameter, which is the answer
    // either model gives there.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CppRefactoringFilePtr file = interface.currentFile();
        const std::shared_ptr<const CxxFrontendSnapshot> model
            = cxxFrontendModel(file->filePath());
        if (!model)
            return false;
        const CxxFrontendDocument * const document
            = model->document(file->filePath().toFSPathString());
        if (!document)
            return false;

        // The editor counts from zero and the tree from one.
        const QTextCursor cursor = file->cursor();
        const QList<cxx::AST *> path
            = cxxAstPathAt(*document, cursor.blockNumber() + 1, cursor.positionInBlock() + 1);

        int index = path.size() - 1;
        for (; index >= 0; --index) {
            if (dynamic_cast<cxx::ParameterDeclarationAST *>(path.at(index)))
                break;
        }
        if (index < 1)
            return false;

        auto * const parameter = static_cast<cxx::ParameterDeclarationAST *>(path.at(index));
        auto * const clause
            = dynamic_cast<cxx::ParameterDeclarationClauseAST *>(path.at(index - 1));
        if (!clause)
            return false;

        // Its neighbours in the list it is written in.
        cxx::ParameterDeclarationAST *previous = nullptr;
        cxx::ParameterDeclarationAST *next = nullptr;
        bool found = false;
        for (auto *it = clause->parameterDeclarationList; it; it = it->next) {
            if (it->value == parameter) {
                found = true;
                next = it->next ? it->next->value : nullptr;
                break;
            }
            previous = it->value;
        }
        if (!found)
            return false;

        const CxxAstRange current = cxxAstRangeOf(*document, parameter);
        if (!current.isValid())
            return false;

        const auto add = [&](cxx::ParameterDeclarationAST *target,
                             RearrangeParamDeclarationListOp::Target which) {
            const CxxAstRange range = cxxAstRangeOf(*document, target);
            if (!range.isValid())
                return;
            result << new RearrangeParamDeclarationListOp(
                interface,
                file->position(current.startLine, current.startColumn),
                file->position(current.endLine, current.endColumn),
                file->position(range.startLine, range.startColumn),
                file->position(range.endLine, range.endColumn),
                which);
        };

        if (previous)
            add(previous, RearrangeParamDeclarationListOp::TargetPrevious);
        if (next)
            add(next, RearrangeParamDeclarationListOp::TargetNext);

        // Answered, even where the parameter stands alone and there is
        // nothing to switch it with: the built-in path would find the same
        // nothing.
        return true;
    }
#endif
};

#ifdef WITH_TESTS
class RearrangeParamDeclarationListTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerRearrangeParamDeclarationListQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(RearrangeParamDeclarationList);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <rearrangeparamdeclarationlist.moc>
#endif
