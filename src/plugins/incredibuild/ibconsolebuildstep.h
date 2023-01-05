// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/buildstep.h>

namespace IncrediBuild {
namespace Internal {

class IBConsoleStepFactory : public ProjectExplorer::BuildStepFactory
{
public:
    IBConsoleStepFactory();
};

} // namespace Internal
} // namespace IncrediBuild
