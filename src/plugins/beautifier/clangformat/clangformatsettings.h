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
    Utils::StringAspect predefinedStyle;
    Utils::StringAspect fallbackStyle;
    Utils::StringAspect customStyle;

    QStringList predefinedStyles() const;
    QStringList fallbackStyles() const;

    QString styleFileName(const QString &key) const override;

private:
    void readStyles() override;
};

} // Beautifier::Internal
