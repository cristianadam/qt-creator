// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "mcpserver_global.h"

#include "mcpserver.h"
#include "schemas/schema_2025_11_25.h"

namespace Mcp {

class MCPSERVER_EXPORT ToolRegistry : public QObject
{
    Q_OBJECT
public:
    static void registerTool(
        const Generated::Schema::_2025_11_25::Tool &tool,
        const Server::ToolInterfaceCallback &callback);
    static void registerTool(
        const Generated::Schema::_2025_11_25::Tool &tool, const Server::ToolCallback &callback);

    static const ToolRegistry &instance();

signals:
    void toolRegistered();
};

class MCPSERVER_EXPORT AutoRegisteringServer : public Server, public QObject
{
public:
    AutoRegisteringServer(Generated::Schema::_2025_11_25::Implementation serverInfo);

private:
    std::size_t m_nTools = 0;
};

} // namespace Mcp
