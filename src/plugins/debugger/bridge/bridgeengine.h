// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <debugger/dap/dapengine.h>

namespace Debugger::Internal {

// A debugger engine that speaks a DAP-shaped protocol to Qt Creator's own
// Python bridge (gdbbridge.py) rather than to a foreign DAP adapter. The
// control plane is reused from DapEngine; the data plane (breakpoints and
// variables) will be replaced by native, dumper-aware messages in later steps.
// See bridge-protocol-design.md.
class BridgeEngine : public DapEngine
{
public:
    BridgeEngine();

private:
    void setupEngine() override;

    bool acceptsBreakpoint(const BreakpointParameters &bp) const override;
    const QLoggingCategory &logCategory() override;
};

DebuggerEngine *createBridgeEngine();

} // namespace Debugger::Internal
