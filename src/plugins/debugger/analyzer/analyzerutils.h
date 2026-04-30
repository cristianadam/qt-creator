// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <debugger/debugger_global.h>

namespace Debugger {

DEBUGGER_EXPORT void enableMainWindow(bool on);

// Convenience functions.
DEBUGGER_EXPORT void showPermanentStatusMessage(const QString &message);

} // Debugger
