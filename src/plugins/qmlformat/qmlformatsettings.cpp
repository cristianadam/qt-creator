// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlformatsettings.h"

#include <coreplugin/icore.h>
namespace QmlJSEditor {

namespace Constants {

static inline const char SETTINGS_ID[] = "QmlFormat";
static inline const char USE_TABS[] = "QmlFormat.UseTabs";
static inline const char INDENT_WIDTH[] = "QmlFormat.IndentWidth";
static inline const char MAX_COLUMN_WIDTH[] = "QmlFormat.MaxColumnWidth";
static inline const char NORMALIZE[] = "QmlFormat.NormalizeOrder";
static inline const char OBJECTS_SPACING[] = "QmlFormat.ObjectsSpacing";
static inline const char FUNCTIONS_SPACING[] = "QmlFormat.FunctionsSpacing";

} // namespace Constants

QmlFormatSettings &QmlFormatSettings::instance()
{
    static QmlFormatSettings settings;
    return settings;
}

QmlFormatSettings::QmlFormatSettings()
{
    readSettings();
}

void QmlFormatSettings::write() const
{
    Utils::QtcSettings *settings = Core::ICore::settings();
    if (!settings) {
        qWarning() << "Failed to get settings instance";
        return;
    }

    settings->beginGroup(Constants::SETTINGS_ID);
    settings->setValue(Constants::USE_TABS, m_useTabs);
    settings->setValue(Constants::INDENT_WIDTH, m_indentWidth);
    settings->setValue(Constants::MAX_COLUMN_WIDTH, m_maxColumnWidth);
    settings->setValue(Constants::NORMALIZE, m_normalize);
    settings->setValue(Constants::OBJECTS_SPACING, m_objectsSpacing);
    settings->setValue(Constants::FUNCTIONS_SPACING, m_functionsSpacing);
    settings->endGroup();
}

void QmlFormatSettings::readSettings()
{
    Utils::QtcSettings *settings = Core::ICore::settings();
    if (!settings) {
        qWarning() << "Failed to get settings instance";
        return;
    }

    settings->beginGroup(Constants::SETTINGS_ID);

    // Store default values to use in case of read errors
    const bool defaultUseTabs = false;
    const int defaultIndentWidth = 4;
    const int defaultMaxColumnWidth = -1;
    const bool defaultNormalize = false;
    const bool defaultObjectsSpacing = false;
    const bool defaultFunctionsSpacing = false;

    bool ok = true;
    m_useTabs = settings->value(Constants::USE_TABS, defaultUseTabs).toBool();

    m_indentWidth = settings->value(Constants::INDENT_WIDTH, defaultIndentWidth).toInt(&ok);
    if (!ok || m_indentWidth < 0) {
        qWarning() << "Failed to read INDENT_WIDTH setting or invalid value, using default value";
        m_indentWidth = defaultIndentWidth;
    }

    m_maxColumnWidth = settings->value(Constants::MAX_COLUMN_WIDTH, defaultMaxColumnWidth).toInt(&ok);
    if (!ok || m_maxColumnWidth < -1) {
        qWarning() << "Failed to read MAX_COLUMN_WIDTH setting, using default value";
        m_maxColumnWidth = defaultMaxColumnWidth;
    }

    m_normalize = settings->value(Constants::NORMALIZE, defaultNormalize).toBool();
    m_objectsSpacing = settings->value(Constants::OBJECTS_SPACING, defaultObjectsSpacing).toBool();
    m_functionsSpacing = settings->value(Constants::FUNCTIONS_SPACING, defaultFunctionsSpacing).toBool();

    settings->endGroup();
}

void QmlFormatSettings::setUseCustomSettings(bool enable)
{
    m_useCustomSettings = enable;
}

bool QmlFormatSettings::useCustomSettings() const
{
    return m_useCustomSettings;
}

void QmlFormatSettings::setFormatOnSave(bool enable)
{
    m_formatOnSave = enable;
}

bool QmlFormatSettings::formatOnSave() const
{
    return m_formatOnSave;
}

void QmlFormatSettings::setIndentWidth(int width)
{
    if (width <= 0) {
        qWarning() << "Invalid indent width value" << width << "- must be greater than 0";
        return;
    }
    m_indentWidth = width;
}

void QmlFormatSettings::setMaxColumnWidth(int width)
{
    if (width < -1) {
        qWarning() << "Invalid max column width value" << width << "- must be -1 or greater";
        return;
    }
    m_maxColumnWidth = width;
}

void QmlFormatSettings::setObjectsSpacing(bool spacing)
{
    m_objectsSpacing = spacing;
}

void QmlFormatSettings::setFunctionsSpacing(bool spacing)
{
    m_functionsSpacing = spacing;
}

void QmlFormatSettings::setNormalize(bool normalize)
{
    m_normalize = normalize;
}

void QmlFormatSettings::setUseTabs(bool useTabs)
{
    m_useTabs = useTabs;
}

void QmlFormatSettings::resetToDefaults()
{
    m_useTabs = false;
    m_indentWidth = 4;
    m_maxColumnWidth = -1;
    m_normalize = false;
    m_objectsSpacing = false;
    m_functionsSpacing = false;
    m_useCustomSettings = false;
    m_formatOnSave = false;

    write();
}

} // namespace QmlJSEditor
