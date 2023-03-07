// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "terminalpane.h"

#include <utils/processinterface.h>

#include <utils/debughelperinterface.h>

#include <QTemporaryDir>

namespace Terminal {

class TerminalProcessInterface : public Utils::DebugHelperInterface
{
    Q_OBJECT

public:
    TerminalProcessInterface(TerminalPane *terminalPane);
};

} // namespace Terminal
