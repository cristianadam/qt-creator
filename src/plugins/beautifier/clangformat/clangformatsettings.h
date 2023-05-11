// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../abstractsettings.h"

namespace Beautifier::Internal {

class ClangFormatSettings : public AbstractSettings
{
public:
    explicit ClangFormatSettings();

    void createDocumentationFile() const override;
    QStringList completerWords() override;

    Utils::BoolAspect usePredefinedStyle;
    Utils::SelectionAspect predefinedStyle;
    Utils::SelectionAspect fallbackStyle;
    Utils::StringAspect customStyle;

    // Not saved
    Utils::BoolAspect useCustomizedStyle; // Inverse of usePredefinedStyle

    QStringList predefinedStyles() const;
    QStringList fallbackStyles() const;

    QString styleFileName(const QString &key) const override;

private:
    void readStyles() override;

    QWidget *m_optionsPanelStore = nullptr;
    class ConfigurationPanel *m_configurations = nullptr;
};

} // Beautifier::Internal
