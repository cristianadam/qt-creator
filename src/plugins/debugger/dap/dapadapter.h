// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../debugger_global.h"

#include <utils/commandline.h>
#include <utils/processinterface.h>

#include <QJsonObject>
#include <QString>

namespace Debugger {

// Where a debug adapter is, and how to reach it.
class DEBUGGER_EXPORT DapAdapterDescriptor
{
public:
    enum class Kind { Executable, Server, Pipe };

    Kind kind = Kind::Executable;
    // Executable: what to run, and the environment to run it in.
    Utils::CommandLine command;
    Utils::ProcessRunData runData;
    // Server: where it is already listening.
    QString host;
    quint16 port = 0;
    // Pipe: the local socket or named pipe it is already listening on.
    QString pipePath;
};

class DEBUGGER_EXPORT DapAdapterStartData
{
public:
    DapAdapterDescriptor adapter;
    // What to call ourselves in the initialize request.
    QString adapterId = "qtcreator";
    // The launch or attach body. It follows the adapter's own schema, so it is
    // passed through untouched.
    QJsonObject configuration;
    bool attach = false;
};

} // namespace Debugger
