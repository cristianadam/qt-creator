// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aliencompletion.h"

#include "extensionhost.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/texteditor.h>

#include <languageclient/snippet.h>

#include <utils/algorithm.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextDocument>

#include <memory>

using namespace TextEditor;
using namespace Utils;

namespace Alien::Internal {

AlienAssistProcessor::~AlienAssistProcessor()
{
    *m_alive = false;
}

bool AlienAssistProcessor::running()
{
    return m_running;
}

void AlienAssistProcessor::cancel()
{
    *m_alive = false;
    m_running = false;
}

IAssistProposal *AlienAssistProcessor::request(const std::function<void()> &send)
{
    m_running = true;
    m_sending = true;
    send();
    m_sending = false;
    return std::exchange(m_immediate, nullptr);
}

void AlienAssistProcessor::deliver(IAssistProposal *proposal)
{
    m_running = false;
    // Handed over from within perform(), the proposal is perform()'s result:
    // the async handler would delete this processor a second time.
    if (m_sending)
        m_immediate = proposal;
    else
        setAsyncProposalAvailable(proposal);
}

class AlienCompletionAssistProcessor final : public AlienAssistProcessor
{
public:
    explicit AlienCompletionAssistProcessor(ExtensionHost *host)
        : m_host(host)
    {}

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

        return request([&] {
            m_host->editor().requestCompletion(
                iface->filePath(), line, character,
                guarded([this, basis](const QJsonArray &items) {
                    deliver(createCompletionProposal(basis, items, m_host));
                }));
        });
    }

private:
    ExtensionHost *m_host;
};

static int offsetOf(const QTextDocument *document, const QJsonValue &position)
{
    const QJsonObject object = position.toObject();
    const QTextBlock block = document->findBlockByNumber(object.value("line").toInt());
    if (!block.isValid())
        return document->characterCount() - 1;
    return block.position() + std::clamp(object.value("character").toInt(), 0, block.length() - 1);
}

class AlienProposalItem final : public AssistProposalItem
{
public:
    void setAdditionalEdits(const QJsonArray &edits) { m_additionalEdits = edits; }
    void setResolve(const QString &id, ExtensionHost *host) { m_id = id; m_host = host; }
    void setRange(const QJsonObject &range) { m_range = range; }
    void setCommitCharacters(const QStringList &characters) { m_commitCharacters = characters; }

    bool prematurelyApplies(const QChar &character) const override
    {
        if (!m_commitCharacters.contains(QString(character)))
            return false;
        m_committedWith = character;
        return true;
    }

    void apply(TextEditorWidget *editorWidget, int basePosition) const override
    {
        if (m_range.isEmpty()) {
            AssistProposalItem::apply(editorWidget, basePosition);
        } else {
            // The item said where it goes; the prefix the editor would have
            // guessed does not come into it.
            QTextDocument *contents = editorWidget->document();
            const int start = offsetOf(contents, m_range.value("start"));
            // What was typed since the extension answered filtered the list
            // here, the item replaces it as well.
            const int end = std::max(offsetOf(contents, m_range.value("end")),
                                     editorWidget->position());
            if (isSnippet()) {
                editorWidget->replace(start, end - start, {});
                editorWidget->insertCodeSnippet(start, data().toString(),
                                                &LanguageClient::parseSnippet);
            } else {
                editorWidget->replace(start, end - start, text());
            }
        }
        applyAdditionalEdits(editorWidget);
        if (!m_committedWith.isNull())
            editorWidget->insertPlainText(QString(m_committedWith));
        // Edits the extension works out only once the item is taken.
        if (m_host && !m_id.isEmpty()) {
            QPointer<TextEditorWidget> widget = editorWidget;
            m_host->editor().resolveCompletion(
                m_id, [widget, known = m_additionalEdits](const QJsonObject &resolved) {
                    // The resolved item is usually the item itself, completed:
                    // what it carried already has been applied.
                    QJsonArray edits;
                    for (const QJsonValue &edit : resolved.value("additionalTextEdits").toArray()) {
                        if (!known.contains(edit))
                            edits.append(edit);
                    }
                    if (widget)
                        applyEdits(widget, edits);
                });
        }
    }

    // A snippet in the language servers' notation, with ${1:...} placeholders
    // and a $0 final stop - not the one Qt Creator's own snippets use.
    void applySnippet(TextEditorWidget *editorWidget, int basePosition) const override
    {
        editorWidget->insertCodeSnippet(basePosition, data().toString(),
                                        &LanguageClient::parseSnippet);
    }

private:
    void applyAdditionalEdits(TextEditorWidget *editorWidget) const
    {
        applyEdits(editorWidget, m_additionalEdits);
    }

    static void applyEdits(TextEditorWidget *editorWidget, const QJsonArray &editsToApply)
    {
        if (editsToApply.isEmpty())
            return;
        QTextDocument *contents = editorWidget->document();
        const QList<QJsonObject> edits = editsBackToFront(contents->toPlainText(), editsToApply);
        QTextCursor cursor(contents);
        cursor.beginEditBlock();
        for (const QJsonObject &edit : edits) {
            const QJsonObject range = edit.value("range").toObject();
            cursor.setPosition(offsetOf(contents, range.value("start")));
            cursor.setPosition(offsetOf(contents, range.value("end")), QTextCursor::KeepAnchor);
            cursor.insertText(edit.value("newText").toString());
        }
        cursor.endEditBlock();
    }

    QJsonArray m_additionalEdits;
    QJsonObject m_range;
    QStringList m_commitCharacters;
    QString m_id;
    QPointer<ExtensionHost> m_host;
    mutable QChar m_committedWith;
};

IAssistProposal *createCompletionProposal(int basisPosition, const QJsonArray &items,
                                          ExtensionHost *host)
{
    QList<AssistProposalItemInterface *> proposalItems;
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString label = item.value("label").toString();
        const QString insertText = item.value("insertText").toString();

        auto proposalItem = new AlienProposalItem;
        if (item.value("isSnippet").toBool()) {
            // The list shows the label; the snippet itself is what gets
            // inserted, so it goes where AssistProposalItem looks for it.
            proposalItem->setText(label);
            proposalItem->setData(insertText);
        } else {
            proposalItem->setText(insertText.isEmpty() ? label : insertText);
        }
        proposalItem->setDetail(item.value("detail").toString());
        proposalItem->setAdditionalEdits(item.value("additionalTextEdits").toArray());
        proposalItem->setRange(item.value("range").toObject());
        QStringList commitCharacters;
        for (const QJsonValue &character : item.value("commitCharacters").toArray())
            commitCharacters << character.toString();
        proposalItem->setCommitCharacters(commitCharacters);
        proposalItem->setResolve(item.value("id").toString(), host);
        proposalItems.append(proposalItem);
    }
    return new GenericProposal(basisPosition, proposalItems);
}

AlienCompletionAssistProvider::AlienCompletionAssistProvider(ExtensionHost *host)
    : CompletionAssistProvider(host)
    , m_host(host)
{}

IAssistProcessor *AlienCompletionAssistProvider::createProcessor(const AssistInterface *) const
{
    return new AlienCompletionAssistProcessor(m_host);
}

} // namespace Alien::Internal
