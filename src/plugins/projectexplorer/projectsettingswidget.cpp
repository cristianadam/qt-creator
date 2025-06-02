// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectsettingswidget.h"

#include <coreplugin/icore.h>

#include <utils/layoutbuilder.h>

#include <QCheckBox>
#include <QLabel>

namespace ProjectExplorer {

ProjectSettingsWidget::ProjectSettingsWidget(QWidget *parent)
    : QWidget(parent)
{}

void ProjectSettingsWidget::setUseGlobalSettings(bool useGlobalSettings)
{
    if (m_useGlobalSettings == useGlobalSettings)
        return;
    m_useGlobalSettings = useGlobalSettings;
    emit useGlobalSettingsChanged(useGlobalSettings);
}

bool ProjectSettingsWidget::useGlobalSettings() const
{
    return m_useGlobalSettings;
}

void ProjectSettingsWidget::setUseGlobalSettingsCheckBoxEnabled(bool enabled)
{
    if (m_useGlobalSettingsCheckBoxEnabled == enabled)
        return;
    m_useGlobalSettingsCheckBoxEnabled = enabled;
    emit useGlobalSettingsCheckBoxEnabledChanged(enabled);
}

bool ProjectSettingsWidget::isUseGlobalSettingsCheckBoxEnabled() const
{
    return m_useGlobalSettingsCheckBoxEnabled;
}

bool ProjectSettingsWidget::isUseGlobalSettingsCheckBoxVisible() const
{
    return m_useGlobalSettingsCheckBoxVisibleVisible;
}

void ProjectSettingsWidget::setUseGlobalSettingsCheckBoxVisible(bool visible)
{
    m_useGlobalSettingsCheckBoxVisibleVisible = visible;
}

bool ProjectSettingsWidget::isUseGlobalSettingsLabelVisible() const
{
    return m_useGlobalSettingsLabelVisibleVisible;
}

void ProjectSettingsWidget::setUseGlobalSettingsLabelVisible(bool visible)
{
    m_useGlobalSettingsLabelVisibleVisible = visible;
}

Utils::Id ProjectSettingsWidget::globalSettingsId() const
{
    return m_globalSettingsId;
}

void ProjectSettingsWidget::setGlobalSettingsId(Utils::Id globalId)
{
    m_globalSettingsId = globalId;
}

bool ProjectSettingsWidget::expanding() const
{
    return m_expanding;
}

void ProjectSettingsWidget::setExpanding(bool expanding)
{
    m_expanding = expanding;
}

void ProjectSettingsWidget::addGlobalHandlingToLayout(QLayout *layout)
{
    QTC_ASSERT(layout, return);
    if (!isUseGlobalSettingsCheckBoxVisible() && !isUseGlobalSettingsLabelVisible())
        return;
    const auto useGlobalSettingsCheckBox = new QCheckBox;
    useGlobalSettingsCheckBox->setChecked(useGlobalSettings());
    useGlobalSettingsCheckBox->setEnabled(isUseGlobalSettingsCheckBoxEnabled());

    const QString labelText = isUseGlobalSettingsCheckBoxVisible()
                                  ? QStringLiteral("Use <a href=\"dummy\">global settings</a>")
                                  : QStringLiteral("<a href=\"dummy\">Global settings</a>");
    const auto settingsLabel = new QLabel(labelText);
    settingsLabel->setEnabled(isUseGlobalSettingsCheckBoxEnabled());

    const auto horizontalLayout = new QHBoxLayout;
    const int CONTENTS_MARGIN = 5;
    horizontalLayout->setContentsMargins(0, CONTENTS_MARGIN, 0, CONTENTS_MARGIN);
    horizontalLayout->setSpacing(CONTENTS_MARGIN);

    if (isUseGlobalSettingsCheckBoxVisible()) {
        horizontalLayout->addWidget(useGlobalSettingsCheckBox);

        connect(this, &ProjectSettingsWidget::useGlobalSettingsCheckBoxEnabledChanged,
                this, [useGlobalSettingsCheckBox, settingsLabel](bool enabled) {
                    useGlobalSettingsCheckBox->setEnabled(enabled);
                    settingsLabel->setEnabled(enabled);
                });
        connect(useGlobalSettingsCheckBox, &QCheckBox::stateChanged,
                this, &ProjectSettingsWidget::setUseGlobalSettings);
        connect(this, &ProjectSettingsWidget::useGlobalSettingsChanged,
                useGlobalSettingsCheckBox, &QCheckBox::setChecked);
    }

    if (isUseGlobalSettingsLabelVisible()) {
        horizontalLayout->addWidget(settingsLabel);
        connect(settingsLabel, &QLabel::linkActivated, this, [this] {
            Core::ICore::showOptionsDialog(globalSettingsId());
        });
    }
    horizontalLayout->addStretch(1);
    if (auto vbox = qobject_cast<QVBoxLayout *>(layout)) {
        vbox->insertWidget(0, Layouting::createHr());
        vbox->insertLayout(0, horizontalLayout);
    } else {
        QTC_CHECK(false);
    }
}

} // ProjectExplorer
