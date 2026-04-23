// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>
#include <utils/filepath.h>

namespace QmlJSEditor::Internal {

class DisabledMessagesAspect : public Utils::IntegersAspect
{
public:
    using Utils::IntegersAspect::IntegersAspect;

    QVariant fromSettingsValue(const QVariant &savedValue) const override;
    QVariant toSettingsValue(const QVariant &valueToSave) const override;
};

class QmlJsEditingSettings final : public Utils::AspectContainer
{
public:
    QmlJsEditingSettings();

    Utils::FilePath defaultQdsCommand() const;

    Utils::BoolAspect enableContextPane{this};
    Utils::BoolAspect pinContextPane{this};
    Utils::BoolAspect autoFormatOnSave{this};
    Utils::BoolAspect autoFormatOnlyCurrentProject{this};
    Utils::BoolAspect foldAuxData{this};
    Utils::BoolAspect useCustomAnalyzer{this};
    Utils::SelectionAspect uiQmlOpenMode{this};
    DisabledMessagesAspect disabledMessages{this};
    DisabledMessagesAspect disabledMessagesForNonQuickUi{this};
    Utils::FilePathAspect qdsCommand{this};
};

QmlJsEditingSettings &settings();

void setupQmlJsEditingSettings();

} // QmlJSEditor::Internal
