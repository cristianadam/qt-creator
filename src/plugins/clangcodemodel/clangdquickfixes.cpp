// Copyright  (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "clangdquickfixes.h"

#include "clangdclient.h"
#include "clangmodelmanagersupport.h"

#include <texteditor/codeassist/genericproposal.h>

using namespace LanguageClient;
using namespace LanguageServerProtocol;
using namespace TextEditor;

namespace ClangCodeModel {
namespace Internal {

ClangdQuickFixFactory::ClangdQuickFixFactory() = default;

void ClangdQuickFixFactory::match(const CppEditor::Internal::CppQuickFixInterface &interface,
                                  QuickFixOperations &result)
{
    const auto client = ClangModelManagerSupport::clientForFile(interface.filePath());
    if (!client)
        return;

    const auto uri = DocumentUri::fromFilePath(interface.filePath());
    QTextCursor cursor(interface.textDocument());
    cursor.setPosition(interface.position());
    cursor.select(QTextCursor::LineUnderCursor);
    const QList<Diagnostic> &diagnostics = client->diagnosticsAt(uri, cursor);
    for (const Diagnostic &diagnostic : diagnostics) {
        ClangdDiagnostic clangdDiagnostic(diagnostic);
        if (const auto actions = clangdDiagnostic.codeActions()) {
            for (const CodeAction &action : *actions)
                result << new LanguageClient::CodeActionQuickFixOperation(action, client);
        }
    }
}

class ClangdQuickFixProcessor : public LanguageClientQuickFixAssistProcessor
{
public:
    ClangdQuickFixProcessor(LanguageClient::Client *client)
        : LanguageClientQuickFixAssistProcessor(client)
    {
    }

private:
    IAssistProposal *perform(const AssistInterface *interface) override
    {
        m_interface = interface;

        // Step 1: Collect clangd code actions asynchronously
        LanguageClientQuickFixAssistProcessor::perform(interface);

        // Step 2: Collect built-in quickfixes synchronously
        m_builtinOps = CppEditor::quickFixOperations(interface);

        return nullptr;
    }

    TextEditor::GenericProposal *handleCodeActionResult(const CodeActionResult &result) override
    {
        auto toOperation =
            [=](const Utils::variant<Command, CodeAction> &item) -> QuickFixOperation * {
            if (auto action = Utils::get_if<CodeAction>(&item)) {
                const Utils::optional<QList<Diagnostic>> diagnostics = action->diagnostics();
                if (!diagnostics.has_value() || diagnostics->isEmpty())
                    return new CodeActionQuickFixOperation(*action, client());
            }
            if (auto command = Utils::get_if<Command>(&item))
                return new CommandQuickFixOperation(*command, client());
            return nullptr;
        };

        if (auto list = Utils::get_if<QList<Utils::variant<Command, CodeAction>>>(&result)) {
            QuickFixOperations ops;
            for (const Utils::variant<Command, CodeAction> &item : *list) {
                if (QuickFixOperation *op = toOperation(item)) {
                    op->setDescription("clangd: " + op->description());
                    ops << op;
                }
            }
            return GenericProposal::createProposal(m_interface, ops + m_builtinOps);
        }
        return nullptr;
    }

    QuickFixOperations m_builtinOps;
    const AssistInterface *m_interface = nullptr;
};

ClangdQuickFixProvider::ClangdQuickFixProvider(ClangdClient *client)
    : LanguageClientQuickFixProvider(client) {}

IAssistProcessor *ClangdQuickFixProvider::createProcessor(const AssistInterface *) const
{
    return new ClangdQuickFixProcessor(client());
}

} // namespace Internal
} // namespace ClangCodeModel
