// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlformatsettingswidget.h"
#include "qmlformatsettings.h"
#include "qmljstoolstr.h"

#include <texteditor/snippets/snippeteditor.h>
#include <utils/layoutbuilder.h>

#include <QVBoxLayout>

namespace QmlJSTools {

QmlFormatSettingsWidget::QmlFormatSettingsWidget(QWidget *parent, QmlJSCodeStylePreferences *preferences)
    : QWidget(parent)
    , m_preferences(preferences)
{
    m_qmlformatConfigTextEdit = new TextEditor::SnippetEditorWidget;
    QSizePolicy sp(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    sp.setHorizontalStretch(1);
    m_qmlformatConfigTextEdit->setSizePolicy(sp);

    using namespace Layouting;
    // clang-format off
    Column {
        Group {
            title(Tr::tr("Global qmlformat Configuration")),
            Column {
                m_qmlformatConfigTextEdit,
            },
        },
        noMargin
    }.attachTo(this);
    // clang-format on

    connect(
        m_qmlformatConfigTextEdit,
        // &TextEditor::SnippetEditorWidget::snippetContentChanged,
        &TextEditor::SnippetEditorWidget::textChanged,
        [this]() {
            // write to settings
            if (!m_preferences)
                return;
            QmlJSCodeStyleSettings settings =  m_preferences->currentCodeStyleSettings();
            settings.qmlformatIniContent = m_qmlformatConfigTextEdit->toPlainText();
            QmlFormatSettings::instance().globalQmlFormatIniFile().writeFileContents(settings.qmlformatIniContent.toUtf8());
            m_preferences->setCodeStyleSettings(settings);
        });
}

void QmlFormatSettingsWidget::setCodeStyleSettings(const QmlJSCodeStyleSettings& s)
{
    if (s.qmlformatIniContent != m_qmlformatConfigTextEdit->toPlainText()) {
        m_qmlformatConfigTextEdit->setPlainText(s.qmlformatIniContent);
    }
}

void QmlFormatSettingsWidget::setPreferences(QmlJSCodeStylePreferences *preferences)
{
    m_preferences = preferences;
}

} // namespace QmlJSTools
