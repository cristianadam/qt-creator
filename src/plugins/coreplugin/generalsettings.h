// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace Core::Internal {

class CodecForLocaleAspect : public Utils::StringSelectionAspect
{
public:
    using StringSelectionAspect::StringSelectionAspect;

    void fixupComboBox(QComboBox *comboBox) override
    {
        comboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        comboBox->setMinimumContentsLength(20);
    }
};

class LanguageSelectionAspect : public Utils::StringSelectionAspect
{
public:
    using StringSelectionAspect::StringSelectionAspect;

    static inline const QString kSystemLanguage = "__system__";

    void fixupComboBox(QComboBox *comboBox) override
    {
        comboBox->setObjectName("languageBox");
        comboBox->setMinimumContentsLength(20);
    }

    QVariant toSettingsValue(const QVariant &valueToSave) const override
    {
        const QString v = valueToSave.toString();
        return v == kSystemLanguage ? QString() : v;
    }

    QVariant fromSettingsValue(const QVariant &savedValue) const override
    {
        const QString v = savedValue.toString();
        return v.isEmpty() ? kSystemLanguage : v;
    }
};

class GeneralSettings : public Utils::AspectContainer
{
public:
    GeneralSettings();

    Utils::BoolAspect showShortcutsInContextMenus{this};
    Utils::BoolAspect provideSplitterCursors{this};
    Utils::BoolAspect preferInfoBarOverPopup{this};
    Utils::BoolAspect useTabsInEditorViews{this};
    Utils::BoolAspect showOkAndCancelInSettingsMode{this};

    Utils::SelectionAspect highDpiScaleFactorRoundingPolicy{this};

    CodecForLocaleAspect codecForLocale{this};
    LanguageSelectionAspect language{this};
    Utils::ColorAspect color{this};

    static void applyToolbarStyleFromSettings();
};

GeneralSettings &generalSettings();

} // Core::Internal
