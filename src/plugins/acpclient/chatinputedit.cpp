// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "chatinputedit.h"
#include "acpclienttr.h"

#include <utils/historycompleter.h>

#include <texteditor/fontsettings.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditorconstants.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextLayout>

using namespace TextEditor;

namespace AcpClient::Internal {

ChatInputEdit::ChatInputEdit(QWidget *parent)
    : TextEditorWidget(parent)
{
    setupFallBackEditor(Utils::Id("AcpClient.ChatInput"));
    setTabChangesFocus(true);
    setPlaceholderText(Tr::tr("Type your message..."));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFrameShape(QFrame::NoFrame);
    setHighlightCurrentLine(false);
    setLineNumbersVisible(false);
    setMarksVisible(false);
    setMinimapVisible(false);

    auto applyWidgetColors = [this] {
        FontSettings fontSettings = textDocument()->fontSettings();
        const QFont appFont = QApplication::font();
        fontSettings.setFamily(appFont.family());
        fontSettings.setFontSize(appFont.pointSize());
        fontSettings.setFontZoom(100);
        fontSettings.formatFor(C_TEXT).setForeground(QApplication::palette().color(QPalette::Text));
        fontSettings.formatFor(C_TEXT).setBackground(QColor());
        textDocument()->setFontSettings(fontSettings);
    };
    applyWidgetColors();
    connect(textDocument(), &TextEditor::TextDocument::fontSettingsChanged, this, applyWidgetColors);

    m_history = new Utils::HistoryCompleter("AcpChatInput", 100, this);

    updateHeight();
    connect(this, &TextEditor::TextEditorWidget::textChanged, this, &ChatInputEdit::updateHeight);
}

void ChatInputEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (event->modifiers() & Qt::ShiftModifier) {
            TextEditorWidget::keyPressEvent(event);
        } else {
            const QString text = toPlainText().trimmed();
            if (!text.isEmpty())
                m_history->addEntry(text);
            m_historyIndex = -1;
            m_editBuffer.clear();
            event->accept();
            emit sendRequested();
        }
        return;
    }

    if (event->key() == Qt::Key_Up && !(event->modifiers() & Qt::ShiftModifier)) {
        if (textCursor().block() == document()->firstBlock()) {
            historyUp();
            return;
        }
    }

    if (event->key() == Qt::Key_Down && !(event->modifiers() & Qt::ShiftModifier)) {
        if (textCursor().block() == document()->lastBlock()) {
            historyDown();
            return;
        }
    }

    TextEditorWidget::keyPressEvent(event);
}

void ChatInputEdit::updateHeight()
{
    // Count visual (wrapped) lines across all blocks
    int visualLines = 0;
    QTextBlock block = document()->begin();
    while (block.isValid()) {
        const QTextLayout *layout = block.layout();
        visualLines += layout ? qMax(1, layout->lineCount()) : 1;
        block = block.next();
    }

    const int lineCount = qBound(1, visualLines, 5);
    const QMargins cm = contentsMargins();
    const int docMargin = static_cast<int>(document()->documentMargin());
    const int h = fontMetrics().lineSpacing() * lineCount
                  + cm.top() + cm.bottom() + 2 * docMargin + 2 * frameWidth();
    setFixedHeight(h);
    setVerticalScrollBarPolicy(visualLines > 5 ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
}

void ChatInputEdit::historyUp()
{
    const QAbstractItemModel *model = m_history->model();
    const int count = model->rowCount();
    if (count == 0)
        return;

    if (m_historyIndex == -1) {
        m_editBuffer = toPlainText();
        m_historyIndex = 0;
    } else if (m_historyIndex < count - 1) {
        ++m_historyIndex;
    } else {
        return;
    }

    setPlainText(model->data(model->index(m_historyIndex, 0)).toString());
    moveCursor(QTextCursor::End);
}

void ChatInputEdit::historyDown()
{
    if (m_historyIndex == -1)
        return;

    if (m_historyIndex > 0) {
        --m_historyIndex;
        const QAbstractItemModel *model = m_history->model();
        setPlainText(model->data(model->index(m_historyIndex, 0)).toString());
    } else {
        m_historyIndex = -1;
        setPlainText(m_editBuffer);
        m_editBuffer.clear();
    }
    moveCursor(QTextCursor::End);
}

} // namespace AcpClient::Internal
