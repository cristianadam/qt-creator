// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace Debugger::Internal {

class RegisterPostMortemAction : public Utils::BoolAspect
{
public:
    explicit RegisterPostMortemAction(Utils::AspectContainer *container = nullptr);

    void readSettings() override;
    void writeSettings() const override {}
};

} // Debugger::Internal
