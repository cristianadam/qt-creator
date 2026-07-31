// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aliencompletion.h"

#include "extensionhost.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/iassistprocessor.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextDocument>

#include <atomic>
#include <memory>

using namespace TextEditor;
using namespace Utils;

namespace Alien::Internal {

class AlienCompletionAssistProcessor final : public IAssistProcessor
{
public:
    explicit AlienCompletionAssistProcessor(ExtensionHost *host)
        : m_host(host)
        , m_alive(std::make_shared<std::atomic_bool>(true))
    {}

    ~AlienCompletionAssistProcessor() override { *m_alive = false; }

    bool running() override { return m_running; }

    void cancel() override
    {
        *m_alive = false;
        m_running = false;
    }

    IAssistProposal *perform() override
    {
        const AssistInterface *iface = interface();
        QTextDocument *document = iface->textDocument();
        const int position = iface->position();

        const QTextBlock block = document->findBlock(position);
        const int line = block.blockNumber();
        const int character = position - block.position();

        // Replace from the start of the identifier under the cursor.
        int basis = position;
        while (basis > 0) {
            const QChar c = document->characterAt(basis - 1);
            if (c.isLetterOrNumber() || c == '_')
                --basis;
            else
                break;
        }

        m_running = true;
        const std::shared_ptr<std::atomic_bool> alive = m_alive;
        m_host->requestCompletion(
            iface->filePath(), line, character, [this, alive, basis](const QJsonArray &items) {
                if (!*alive)
                    return;
                m_running = false;
                setAsyncProposalAvailable(createProposal(basis, items));
            });
        return nullptr;
    }

private:
    static IAssistProposal *createProposal(int basisPosition, const QJsonArray &items)
    {
        QList<AssistProposalItemInterface *> proposalItems;
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString label = item.value("label").toString();
            const QString insertText = item.value("insertText").toString();

            auto proposalItem = new AssistProposalItem;
            proposalItem->setText(insertText.isEmpty() ? label : insertText);
            proposalItem->setDetail(item.value("detail").toString());
            proposalItems.append(proposalItem);
        }
        return new GenericProposal(basisPosition, proposalItems);
    }

    ExtensionHost *m_host;
    bool m_running = false;
    std::shared_ptr<std::atomic_bool> m_alive;
};

AlienCompletionAssistProvider::AlienCompletionAssistProvider(ExtensionHost *host)
    : CompletionAssistProvider(host)
    , m_host(host)
{}

IAssistProcessor *AlienCompletionAssistProvider::createProcessor(const AssistInterface *) const
{
    return new AlienCompletionAssistProcessor(m_host);
}

} // namespace Alien::Internal
