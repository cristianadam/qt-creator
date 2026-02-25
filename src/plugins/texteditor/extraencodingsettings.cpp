// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extraencodingsettings.h"

#include "texteditortr.h"
#include "texteditorsettings.h"

using namespace Utils;

namespace TextEditor {

ExtraEncodingSettings::ExtraEncodingSettings()
{
    setAutoApply(false);
    setSettingsGroup("textEditorManager");

    utf8BomSetting.setSettingsKey("Utf8BomBehavior");

    lineEndingSetting.setSettingsKey("LineEndingBehavior");
}

ExtraEncodingSettingsData ExtraEncodingSettings::data() const
{
    ExtraEncodingSettingsData d;
    d.m_utf8BomSetting = utf8BomSetting();
    d.m_lineEndingSetting = lineEndingSetting();
    return d;
}

void ExtraEncodingSettings::setData(const ExtraEncodingSettingsData &data)
{
    utf8BomSetting.setValue(data.m_utf8BomSetting);
    lineEndingSetting.setValue(data.m_lineEndingSetting);
}

bool ExtraEncodingSettingsData::equals(const ExtraEncodingSettingsData &s) const
{
    return m_utf8BomSetting == s.m_utf8BomSetting && m_lineEndingSetting == s.m_lineEndingSetting;
}

QStringList ExtraEncodingSettingsData::lineTerminationModeNames()
{
    return {Tr::tr("Unix (LF)"), Tr::tr("Windows (CRLF)")};
}

ExtraEncodingSettings &globalExtraEncodingSettings()
{
    static ExtraEncodingSettings theGlobalExtraEncodingSettings;
    return theGlobalExtraEncodingSettings;
}

void updateGlobalExtraEncodingSettings(const ExtraEncodingSettingsData &newExtraEncodingSettings)
{
    if (newExtraEncodingSettings.equals(globalExtraEncodingSettings().data()))
        return;

    globalExtraEncodingSettings().setData(newExtraEncodingSettings);
    globalExtraEncodingSettings().writeSettings();

    emit TextEditorSettings::instance()->extraEncodingSettingsChanged(newExtraEncodingSettings);
}

void setupExtraEncodingSettings()
{
    globalExtraEncodingSettings().readSettings();
}

} // TextEditor
