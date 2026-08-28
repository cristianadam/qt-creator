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
                setAsyncProposalAvailable(createCompletionProposal(basis, items, m_host));
            });
        return nullptr;
    }

private:
    ExtensionHost *m_host;
    bool m_running = false;
    std::shared_ptr<std::atomic_bool> m_alive;
};

static int offsetOf(const QTextDocument *document, const QJsonValue &position)
{
    const QJsonObject object = position.toObject();
    const QTextBlock block = document->findBlockByNumber(object.value("line").toInt());
    return block.position() + object.value("character").toInt();
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
            const int end = offsetOf(contents, m_range.value("end"));
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
            m_host->resolveCompletion(m_id, [this, widget](const QJsonObject &resolved) {
                const QJsonArray edits = resolved.value("additionalTextEdits").toArray();
                if (widget && edits != m_additionalEdits)
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
        QList<QJsonObject> edits;
        for (const QJsonValue &value : editsToApply)
            edits.append(value.toObject());
        // Back to front, so that one edit does not move the next one's place.
        Utils::sort(edits, [contents](const QJsonObject &a, const QJsonObject &b) {
            return offsetOf(contents, a.value("range").toObject().value("start"))
                   > offsetOf(contents, b.value("range").toObject().value("start"));
        });
        QTextCursor cursor(contents);
        cursor.beginEditBlock();
        for (const QJsonObject &edit : std::as_const(edits)) {
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
