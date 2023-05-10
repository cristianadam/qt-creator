// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../abstractsettings.h"

namespace Beautifier::Internal {

class UncrustifySettings : public AbstractSettings
{
    Q_OBJECT

public:
    UncrustifySettings();

    Utils::BoolAspect useOtherFiles;
    Utils::BoolAspect useHomeFile;
    Utils::BoolAspect useCustomStyle;
    Utils::StringAspect customStyle;
    Utils::BoolAspect formatEntireFileFallback;
    Utils::StringAspect specificConfigFile;
    Utils::BoolAspect useSpecificConfigFile;

    void createDocumentationFile() const override;
};

} // Beautifier::Internal
