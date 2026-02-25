// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <utils/store.h>
#include <utils/aspects.h>

namespace TextEditor {

class TEXTEDITOR_EXPORT ExtraEncodingSettingsData
{
public:
    bool equals(const ExtraEncodingSettingsData &s) const;

    static QStringList lineTerminationModeNames();

    enum Utf8BomSetting {
        AlwaysAdd = 0,
        OnlyKeep = 1,
        AlwaysDelete = 2
    };
    Utf8BomSetting m_utf8BomSetting = OnlyKeep;

    enum LineEndingSetting {
      Unix = 0,
      Windows = 1
    };
    LineEndingSetting m_lineEndingSetting = Unix;
};

class TEXTEDITOR_EXPORT ExtraEncodingSettings : public Utils::AspectContainer
{
public:
    ExtraEncodingSettings();

    void setData(const ExtraEncodingSettingsData &data);
    ExtraEncodingSettingsData data() const;

    Utils::TypedSelectionAspect<ExtraEncodingSettingsData::Utf8BomSetting> utf8BomSetting;
    Utils::TypedSelectionAspect<ExtraEncodingSettingsData::LineEndingSetting> lineEndingSetting;
};

void setupExtraEncodingSettings();
void updateGlobalExtraEncodingSettings(const ExtraEncodingSettingsData &newExtraEncodingSettings);

TEXTEDITOR_EXPORT ExtraEncodingSettings &globalExtraEncodingSettings();

} // TextEditor
