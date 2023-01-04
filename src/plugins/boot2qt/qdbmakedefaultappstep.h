// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/buildstep.h>

namespace Qdb {
namespace Internal {

class QdbMakeDefaultAppStepFactory final : public ProjectExplorer::BuildStepFactory
{
public:
    QdbMakeDefaultAppStepFactory();
};

} // namespace Internal
} // namespace Qdb
