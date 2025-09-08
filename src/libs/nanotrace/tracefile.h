// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "nanotraceglobals.h"
#include "nanotracehrfwd.h"

#include <memory>

namespace NanotraceHR {

#ifdef ENABLE_TRACING_FILE

NANOTRACE_EXPORT void resetTraceFilePointer();
NANOTRACE_EXPORT std::shared_ptr<EnabledTraceFile> traceFile();

#endif

} // namespace NanotraceHR
