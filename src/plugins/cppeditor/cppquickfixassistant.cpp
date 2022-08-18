// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "cppquickfixassistant.h"

#include "cppeditorconstants.h"
#include "cppeditorwidget.h"
#include "cppmodelmanager.h"
#include "cppquickfixes.h"
#include "cpprefactoringchanges.h"

#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/textdocument.h>

#include <cplusplus/ASTPath.h>

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

using namespace CPlusPlus;
using namespace TextEditor;

namespace CppEditor {
namespace Internal {

// -------------------------
// CppQuickFixAssistProcessor
// -------------------------
class CppQuickFixAssistProcessor : public IAssistProcessor
{
    IAssistProposal *perform(const AssistInterface *interface) override
    {
        QSharedPointer<const AssistInterface> dummy(interface); // FIXME: Surely this cannot be our way of doing memory management???
        return GenericProposal::createProposal(interface, quickFixOperations(interface));
    }
};

// -------------------------
// CppQuickFixAssistProvider
// -------------------------
IAssistProvider::RunType CppQuickFixAssistProvider::runType() const
{
    return Synchronous;
}

IAssistProcessor *CppQuickFixAssistProvider::createProcessor(const AssistInterface *) const
{
    return new CppQuickFixAssistProcessor;
}

// --------------------------
// CppQuickFixAssistInterface
// --------------------------
CppQuickFixInterface::CppQuickFixInterface(CppEditorWidget *editor, AssistReason reason)
    : AssistInterface(editor->textCursor(), editor->textDocument()->filePath(), reason)
    , m_editor(editor)
    , m_semanticInfo(editor->semanticInfo())
    , m_snapshot(CppModelManager::instance()->snapshot())
    , m_currentFile(CppRefactoringChanges::file(editor, m_semanticInfo.doc))
    , m_context(m_semanticInfo.doc, m_snapshot)
{
    QTC_CHECK(m_semanticInfo.doc);
    QTC_CHECK(m_semanticInfo.doc->translationUnit());
    QTC_CHECK(m_semanticInfo.doc->translationUnit()->ast());
    ASTPath astPath(m_semanticInfo.doc);
    m_path = astPath(editor->textCursor());
}

const QList<AST *> &CppQuickFixInterface::path() const
{
    return m_path;
}

Snapshot CppQuickFixInterface::snapshot() const
{
    return m_snapshot;
}

SemanticInfo CppQuickFixInterface::semanticInfo() const
{
    return m_semanticInfo;
}

const LookupContext &CppQuickFixInterface::context() const
{
    return m_context;
}

CppEditorWidget *CppQuickFixInterface::editor() const
{
    return m_editor;
}

CppRefactoringFilePtr CppQuickFixInterface::currentFile() const
{
    return m_currentFile;
}

bool CppQuickFixInterface::isCursorOn(unsigned tokenIndex) const
{
    return currentFile()->isCursorOn(tokenIndex);
}

bool CppQuickFixInterface::isCursorOn(const AST *ast) const
{
    return currentFile()->isCursorOn(ast);
}

QuickFixOperations quickFixOperations(const TextEditor::AssistInterface *interface)
{
    const auto cppInterface = dynamic_cast<const CppQuickFixInterface *>(interface);
    QTC_ASSERT(cppInterface, return {});
    QuickFixOperations quickFixes;
    for (CppQuickFixFactory *factory : CppQuickFixFactory::cppQuickFixFactories())
        factory->match(*cppInterface, quickFixes);
    return quickFixes;
}

} // namespace Internal
} // namespace CppEditor
