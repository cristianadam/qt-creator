// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "toolregistry.h"

namespace Mcp {

namespace Internal {

struct Tool
{
    Schema::Tool metadata;
    std::variant<Server::ToolInterfaceCallback, Server::ToolCallback> callback;
};

struct Registry
{
    std::vector<Tool> tools;
};

static Registry &registry()
{
    static Registry r;
    return r;
}

} // namespace Internal

ToolRegistry &toolRegistry()
{
    static ToolRegistry registry;
    return registry;
}

void ToolRegistry::registerTool(
    const Generated::Schema::_2025_11_25::Tool &tool, const Server::ToolInterfaceCallback &callback)
{
    Internal::registry().tools.push_back({tool, callback});
    emit toolRegistry().toolRegistered();
}

void ToolRegistry::registerTool(
    const Generated::Schema::_2025_11_25::Tool &tool, const Server::ToolCallback &callback)
{
    Internal::registry().tools.push_back({tool, callback});
    emit toolRegistry().toolRegistered();
}

const ToolRegistry &ToolRegistry::instance()
{
    return toolRegistry();
}

AutoRegisteringServer::AutoRegisteringServer(
    Generated::Schema::_2025_11_25::Implementation serverInfo)
    : Server(serverInfo)
{
    for (const auto &tool : Internal::registry().tools) {
        std::visit(
            [this, &tool](auto &&callback) { addTool(tool.metadata, callback); }, tool.callback);
    }
    m_nTools = Internal::registry().tools.size();

    QObject::connect(&ToolRegistry::instance(), &ToolRegistry::toolRegistered, this, [this]() {
        for (auto i = m_nTools; i < Internal::registry().tools.size(); ++i) {
            const auto &tool = Internal::registry().tools[i];
            std::visit(
                [this, &tool](auto &&callback) { addTool(tool.metadata, callback); }, tool.callback);
        }
        m_nTools = Internal::registry().tools.size();
    });
}

} // namespace Mcp
