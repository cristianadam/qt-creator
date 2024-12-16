// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmljscodestylepreferenceswidget.h"

#include "qmljscodestylesettings.h"
#include "qmljscodestylesettingswidget.h"
#include "qmlformatsettingswidget.h"

#include <QLabel>
#include <QVBoxLayout>

namespace QmlJSTools {

QmlJSCodeStylePreferencesWidget::QmlJSCodeStylePreferencesWidget(QWidget *parent) :
      QWidget(parent)
{
    m_codeStyleSettingsWidget = new QmlJSCodeStyleSettingsWidget(this);
    m_qmlformatSettingsWidget = new QmlJSTools::QmlFormatSettingsWidget(this);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_codeStyleSettingsWidget);
    layout->addWidget(m_qmlformatSettingsWidget);
    layout->setContentsMargins(QMargins());
}

void QmlJSCodeStylePreferencesWidget::setPreferences(QmlJSCodeStylePreferences *preferences)
{
    if (m_preferences == preferences)
        return; // nothing changes

    slotCurrentPreferencesChanged(preferences);

    // cleanup old
    if (m_preferences) {
        disconnect(m_preferences, &QmlJSCodeStylePreferences::currentValueChanged, this, nullptr);
        disconnect(m_preferences, &QmlJSCodeStylePreferences::currentPreferencesChanged,
                   this, &QmlJSCodeStylePreferencesWidget::slotCurrentPreferencesChanged);
        disconnect(m_codeStyleSettingsWidget, &QmlJSCodeStyleSettingsWidget::settingsChanged,
                   this, &QmlJSCodeStylePreferencesWidget::slotSettingsChanged);
    }
    m_preferences = preferences;
    // fillup new
    if (m_preferences) {
        m_codeStyleSettingsWidget->setCodeStyleSettings(m_preferences->currentCodeStyleSettings());

        connect(m_preferences, &QmlJSCodeStylePreferences::currentValueChanged, this, [this] {
            m_codeStyleSettingsWidget->setCodeStyleSettings(m_preferences->currentCodeStyleSettings());
        });
        connect(m_preferences, &QmlJSCodeStylePreferences::currentPreferencesChanged,
                this, &QmlJSCodeStylePreferencesWidget::slotCurrentPreferencesChanged);
        connect(m_codeStyleSettingsWidget, &QmlJSCodeStyleSettingsWidget::settingsChanged,
                this, &QmlJSCodeStylePreferencesWidget::slotSettingsChanged);
    }
}

void QmlJSCodeStylePreferencesWidget::slotCurrentPreferencesChanged(TextEditor::ICodeStylePreferences *preferences)
{
    const bool enableWidgets = preferences && preferences->currentPreferences() &&
                                          !preferences->currentPreferences()->isReadOnly();
    m_codeStyleSettingsWidget->setEnabled(enableWidgets);
    m_qmlformatSettingsWidget->setEnabled(enableWidgets);
}

void QmlJSCodeStylePreferencesWidget::slotSettingsChanged(const QmlJSCodeStyleSettings &settings)
{
    if (!m_preferences)
        return;

    QmlJSCodeStylePreferences *current = dynamic_cast<QmlJSCodeStylePreferences*>(m_preferences->currentPreferences());
    if (!current)
        return;

    current->setCodeStyleSettings(settings);
}

} // namespace QmlJSTools
