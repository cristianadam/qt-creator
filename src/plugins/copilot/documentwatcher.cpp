// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "documentwatcher.h"

#include <coreplugin/editormanager/editormanager.h>

#include <texteditor/texteditor.h>

#include <QJsonDocument>

using namespace LanguageServerProtocol;
using namespace TextEditor;

namespace CoPilot::Internal {

DocumentWatcher::DocumentWatcher(CoPilotClient *client, TextDocument *textDocument)
    : m_client(client)
    , m_textDocument(textDocument)
{
    m_lastContentSize = m_textDocument->document()->characterCount(); //toPlainText().size();
    m_debounceTimer.setInterval(500);
    m_debounceTimer.setSingleShot(true);

    connect(textDocument, &TextDocument::contentsChanged, this, [this]() {
        if (!m_isEditing) {
            const auto newSize = m_textDocument->document()->characterCount();
            if (m_lastContentSize < newSize) {
                m_debounceTimer.start();
            }
            m_lastContentSize = newSize;
        }
    });

    connect(&m_debounceTimer, &QTimer::timeout, this, [this]() { getSuggestion(); });
}

class Completion : public JsonObject
{
    static constexpr char16_t displayTextKey[] = u"displayText";
    static constexpr char16_t uuidKey[] = u"uuid";

public:
    using JsonObject::JsonObject;

    QString displayText() const { return typedValue<QString>(displayTextKey); }
    Position position() const { return typedValue<Position>(positionKey); }
    Range range() const { return typedValue<Range>(rangeKey); }
    QString text() const { return typedValue<QString>(textKey); }
    QString uuid() const { return typedValue<QString>(uuidKey); }

    bool isValid() const override
    {
        return contains(textKey) && contains(rangeKey) && contains(positionKey);
    }
};

class GetSuggestionResponse : public JsonObject
{
    static constexpr char16_t completionKey[] = u"completions";

public:
    using JsonObject::JsonObject;

    LanguageClientArray<Completion> completions() const
    {
        return clientArray<Completion>(completionKey);
    }
};

class GetSuggestionParams : public JsonObject
{
public:
    static constexpr char16_t docKey[] = u"doc";

    GetSuggestionParams();
    GetSuggestionParams(const TextDocumentIdentifier &document, const Position &position)
    {
        setTextDocument(document);
        setPosition(position);
    }
    using JsonObject::JsonObject;

    // The text document.
    TextDocumentIdentifier textDocument() const
    {
        return typedValue<TextDocumentIdentifier>(docKey);
    }
    void setTextDocument(const TextDocumentIdentifier &id) { insert(docKey, id); }

    // The position inside the text document.
    Position position() const
    {
        return fromJsonValue<Position>(value(docKey).toObject().value(positionKey));
    }
    void setPosition(const Position &position)
    {
        QJsonObject result = value(docKey).toObject();
        result[positionKey] = (QJsonObject) position;
        insert(docKey, result);
    }

    bool isValid() const override
    {
        return contains(docKey) && value(docKey).toObject().contains(positionKey);
    }
};

class GetSuggestionRequest
    : public Request<GetSuggestionResponse, std::nullptr_t, GetSuggestionParams>
{
public:
    explicit GetSuggestionRequest(const GetSuggestionParams &params)
        : Request(methodName, params)
    {}
    using Request::Request;
    constexpr static const char methodName[] = "getCompletionsCycling";
};

void DocumentWatcher::getSuggestion()
{
    auto editor = Core::EditorManager::instance()->activateEditorForDocument(m_textDocument);
    auto textEditorWidget = qobject_cast<TextEditor::TextEditorWidget *>(editor->widget());
    if (!editor || !textEditorWidget)
        return;

    auto cursor = textEditorWidget->multiTextCursor();
    if (cursor.hasMultipleCursors() || cursor.hasSelection()) {
        return;
    }

    const auto currentCursorPos = cursor.cursors().first().position();

    GetSuggestionRequest request(
        GetSuggestionParams(TextDocumentIdentifier(
                                m_client->hostPathToServerUri(m_textDocument->filePath())),
                            Position(editor->currentLine() - 1, editor->currentColumn() - 1)));

    request.setResponseCallback([this, textEditorWidget, currentCursorPos](
                                    const GetSuggestionRequest::Response &response) {
        if (response.error()) {
            qDebug() << "ERROR:" << *response.error();
            return;
        }

        const std::optional<GetSuggestionResponse> result = response.result();
        QTC_ASSERT(result, return);

        qDebug().noquote() << QString::fromUtf8(
            QJsonDocument(result->completions().toJson().toArray()).toJson());

        const auto list = result->completions().toList();

        if (list.isEmpty())
            return;

        const auto firstCompletion = list.first();

        auto cursor = textEditorWidget->multiTextCursor();

        if (cursor.hasMultipleCursors() || cursor.hasSelection())
            return;

        if (cursor.cursors().first().position() != currentCursorPos)
            return;

        const QString content = firstCompletion.text().mid(firstCompletion.position().character());

        m_isEditing = true;
        QTextCursor t = m_textDocument->setTemporaryBlock(
            {content,
             QPoint(firstCompletion.position().character(), firstCompletion.position().line())});
        cursor.setCursors({t});
        textEditorWidget->setMultiTextCursor(cursor);
        m_isEditing = false;
    });
    m_client->sendMessage(request);
}

} // namespace CoPilot::Internal