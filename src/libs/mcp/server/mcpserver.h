// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "mcpserver_global.h"

#include "../schemas/schema_2025_11_25.h"

class QTcpServer;

namespace Mcp {

namespace Schema = Generated::Schema::_2025_11_25;

class ServerPrivate;

class MCPSERVER_EXPORT Server
{
public:
    Server(Schema::Implementation serverInfo, bool enableSSETest = false);
    ~Server();

    bool bind(QTcpServer *server);
    QList<QTcpServer *> boundTcpServers() const;

    // Enable Cross-Origin Resource Sharing, necessary for browser based clients.
    void setCorsEnabled(bool enabled);

    // Manually send / receive JSONRPC messages over custom IO streams instead of HTTP
    Utils::Result<std::function<void(QByteArray)>> bindIO(
        std::function<void(QByteArray)> outputHandler);

    // Notifications
    void sendNotification(const Schema::ServerNotification &notification);

    // Tools
    using ToolCallback
        = std::function<Utils::Result<Schema::CallToolResult>(Schema::CallToolRequestParams)>;
    void addTool(const Schema::Tool &tool, const ToolCallback &callback);

    using AsyncToolResultCallback = std::function<void(Utils::Result<Schema::CallToolResult>)>;
    using AsyncToolCallback
        = std::function<void(const Schema::CallToolRequestParams &, const AsyncToolResultCallback &)>;
    void addTool(const Schema::Tool &tool, const AsyncToolCallback &callback);

    using UpdateTaskCallback = std::function<void(Schema::Task &)>;
    using TaskResultCallback = std::function<Utils::Result<Schema::CallToolResult>()>;
    using CancelTaskCallback = std::function<void()>;

    struct TaskCallbacks
    {
        UpdateTaskCallback updateTask;
        TaskResultCallback result;
        std::optional<CancelTaskCallback> cancelTask;
        int pollingIntervalMs{1000};
    };

    using TaskToolCallback
        = std::function<Utils::Result<TaskCallbacks>(const Schema::CallToolRequestParams &)>;
    void addTool(const Schema::Tool &tool, const TaskToolCallback &callback);

    void removeTool(const QString &toolName);

    // Prompts
    using PromptArguments = QMap<QString, QString>;
    using PromptMessageList = QList<Schema::PromptMessage>;
    using PromptCallback = std::function<PromptMessageList(PromptArguments)>;

    void addPrompt(const Schema::Prompt &prompt, const PromptCallback &callback);
    void removePrompt(const QString &promptName);

    // Resources
    using ResourceCallback
        = std::function<Utils::Result<Schema::ReadResourceResult>(Schema::ReadResourceRequestParams)>;

    void addResource(const Schema::Resource &resource, const ResourceCallback &callback);
    void removeResource(const QString &uri);
    void setResourceFallbackCallback(const ResourceCallback &callback);

    void addResourceTemplate(const Schema::ResourceTemplate &resourceTemplate);
    void removeResourceTemplate(const QString &name);

    // Completions
    using CompletionResultCallback = std::function<void(Utils::Result<Schema::CompleteResult>)>;
    using CompletionCallback = std::function<void(const Schema::CompleteRequestParams &, const CompletionResultCallback &)>;
    void setCompletionCallback(const CompletionCallback &callback);

private:
    std::unique_ptr<ServerPrivate> d;
};

} // namespace Mcp
