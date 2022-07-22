/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "debugviewwidget.h"

#include <designersettings.h>
#include <qmldesignerplugin.h>

#include <coreplugin/icore.h>

#include <utils/fancylineedit.h>
#include <utils/fileutils.h>

namespace QmlDesigner {

namespace Internal {

DebugViewWidget::DebugViewWidget(QWidget *parent) : QWidget(parent)
{
    m_ui.setupUi(this);
    connect(m_ui.enabledCheckBox, &QAbstractButton::toggled,
            this, &DebugViewWidget::enabledCheckBoxToggled);

    m_ui.consoleLineEdit->setHistoryCompleter("QmlDesignerConsoleInput");

    connect(m_ui.consoleLineEdit, &Utils::FancyLineEdit::editingFinished, this, &DebugViewWidget::consoleActivated);
    connect(m_ui.javaScriptButton, &QPushButton::clicked, this, [this]() {
        const Utils::FilePath script = Utils::FileUtils::getOpenFilePath(Core::ICore::dialogParent(),
                                          tr("Open JavaScript script"),
                                          Utils::FileUtils::homePath(),
                                          tr("JavaScript script (*.js)"));
        emit runScriptFile(script);
    });
}

void DebugViewWidget::addLogMessage(const QString &topic, const QString &message, bool highlight)
{
    if (highlight) {
        m_ui.modelLog->appendHtml(QStringLiteral("<b><font color=\"blue\">")
                                  + topic
                                  + QStringLiteral("</b><br>")
                                  + message);

    } else {
        m_ui.modelLog->appendHtml(QStringLiteral("<b>")
                                  + topic
                                  + QStringLiteral("</b><br>")
                                  + message);
    }
}

void DebugViewWidget::addErrorMessage(const QString &topic, const QString &message)
{
    m_ui.instanceErrorLog->appendHtml(QStringLiteral("<b><font color=\"red\">")
                              + topic
                              + QStringLiteral("</font></b><br>")
                              + message
                                      );
}

void DebugViewWidget::addLogInstanceMessage(const QString &topic, const QString &message, bool highlight)
{
    if (highlight) {
        m_ui.instanceLog->appendHtml("<b><font color=\"blue\">"
                                     + topic
                                     + "</b><br>"
                                     + "<p>"
                                     + message
                                     + "</p>"
                                     + "<br>");

    } else {
        m_ui.instanceLog->appendHtml("<b>"
                                     + topic
                                     + "</b><br>"
                                     + "<p>"
                                     + message
                                     + "</p>"
                                     + "<br>");
    }
}

void DebugViewWidget::setPuppetStatus(const QString &text)
{
    m_ui.instanceStatus->setPlainText(text);
}

void DebugViewWidget::setDebugViewEnabled(bool b)
{
    if (m_ui.enabledCheckBox->isChecked() != b)
        m_ui.enabledCheckBox->setChecked(b);
}

void DebugViewWidget::enabledCheckBoxToggled(bool b)
{
    DesignerSettings::setValue(DesignerSettingsKey::WARNING_FOR_FEATURES_IN_DESIGNER, b);
}

void DebugViewWidget::appendConsoleOutput(const QString &string)
{
    m_ui.consoleOutput->appendPlainText(string);
}

QString DebugViewWidget::consoleString() const
{
    return m_ui.consoleLineEdit->text();
}

void DebugViewWidget::clearConsoleString()
{
    m_ui.consoleLineEdit->clear();
}

} //namespace Internal

} //namespace QmlDesigner
