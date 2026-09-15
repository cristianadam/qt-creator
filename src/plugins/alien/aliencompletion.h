// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/codeassist/completionassistprovider.h>
#include <texteditor/codeassist/iassistprocessor.h>

#include <QJsonArray>

#include <functional>
#include <memory>

namespace TextEditor { class IAssistProposal; }

namespace Alien::Internal {

class ExtensionHost;

// A processor whose proposal the host answers with. The answer can come before
// perform() returns, when there is no host to ask, or after the processor is gone.
class AlienAssistProcessor : public TextEditor::IAssistProcessor
{
public:
    ~AlienAssistProcessor() override;

    bool running() final;
    void cancel() final;

protected:
    // Returns the proposal if send() delivered it already.
    TextEditor::IAssistProposal *request(const std::function<void()> &send);
    void deliver(TextEditor::IAssistProposal *proposal);

    template<typename Reply>
    auto guarded(const Reply &reply) const
    {
        return [alive = m_alive, reply](const auto &answer) {
            if (*alive)
                reply(answer);
        };
    }

private:
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    bool m_running = false;
    bool m_sending = false;
    TextEditor::IAssistProposal *m_immediate = nullptr;
};

// The proposal the host's items make, replacing from basisPosition.
TextEditor::IAssistProposal *createCompletionProposal(int basisPosition, const QJsonArray &items,
                                                      ExtensionHost *host);

// Feeds Qt Creator's completion popup from the extension host: an assist
// processor asks the host for completion items (which the in-host extension's
// registered provider produces) and turns them into a proposal.
class AlienCompletionAssistProvider final : public TextEditor::CompletionAssistProvider
{
public:
    explicit AlienCompletionAssistProvider(ExtensionHost *host);

    TextEditor::IAssistProcessor *createProcessor(
        const TextEditor::AssistInterface *interface) const override;

private:
    ExtensionHost *m_host;
};

} // namespace Alien::Internal
