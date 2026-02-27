// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "mcpserver_global.h"

#include "../schemas/schema_2025_11_25.h"

class QTcpServer;

namespace Mcp {

namespace Schema = Generated::Schema::_2025_11_25;

class ServerPrivate;

struct ClientRequests
{
    using ElicitResultCallback = std::function<void(const Utils::Result<Schema::ElicitResult> &)>;
    using Elicit
        = std::function<void(const Schema::ElicitRequestParams &, const ElicitResultCallback &)>;

    using SampleResultCallback
        = std::function<void(const Utils::Result<Schema::CreateMessageResult> &)>;
    using Sample = std::function<
        void(const Schema::CreateMessageRequestParams &, const SampleResultCallback &)>;

    using Notify = std::function<void(const Schema::ServerNotification &)>;

    Elicit elicit;
    Sample sample;
    Notify notify;
};

template<typename T>
concept IsDuration = requires(T duration)
{
    std::chrono::duration_cast<std::chrono::milliseconds>(duration);
};

template<IsDuration DurationType>
void letTaskDieIn(Schema::Task &task, DurationType dieIn)
{
    using namespace std::chrono_literals;
    const QDateTime createdAt = QDateTime::fromString(task.createdAt(), Qt::ISODate);
    const QDateTime now = QDateTime::currentDateTime();
    const std::chrono::milliseconds ttl = (now - createdAt) + dieIn;
    task.ttl(ttl.count());
}

struct ToolInterface
{
    using UpdateTaskCallback = std::function<Schema::Task(Schema::Task)>;
    using TaskResultCallback = std::function<Utils::Result<Schema::CallToolResult>()>;
    using CancelTaskCallback = std::function<void()>;
    using TaskProgressNotify = std::function<void(Schema::Task)>;

    using Finish = std::function<void(const Utils::Result<Schema::CallToolResult> &)>;

    using StartTask = std::function<Utils::Result<TaskProgressNotify>(
        int pollingIntervalMs,
        const UpdateTaskCallback &onUpdateTask,
        const TaskResultCallback &onResultCallback,
        const std::optional<CancelTaskCallback> &onCancelTaskCallback,
        std::optional<std::chrono::milliseconds> ttl)>;

    ClientRequests clientRequests;
    Schema::ClientCapabilities clientCapabilities;

    // Send the result via the responder and close the connection.
    Finish finish;

    // Upgrade the connection to a long-running task. The responder will send a CreateTaskResult to the client and close the connection.
    StartTask _startTask;

    template<IsDuration DurationType>
    Utils::Result<TaskProgressNotify> startTask(
        int pollingIntervalMs,
        const UpdateTaskCallback &onUpdateTask,
        const TaskResultCallback &onResultCallback,
        const std::optional<CancelTaskCallback> &onCancelTaskCallback,
        DurationType ttl) const
    {
        return _startTask(
            pollingIntervalMs,
            onUpdateTask,
            onResultCallback,
            onCancelTaskCallback,
            std::chrono::duration_cast<std::chrono::milliseconds>(ttl));
    }

    Utils::Result<TaskProgressNotify> startTask(
        int pollingIntervalMs,
        const UpdateTaskCallback &onUpdateTask,
        const TaskResultCallback &onResultCallback,
        const std::optional<CancelTaskCallback> &onCancelTaskCallback) const
    {
        return _startTask(
            pollingIntervalMs, onUpdateTask, onResultCallback, onCancelTaskCallback, std::nullopt);
    }
};

/*

 call tool => send reponse
 call tool =>                                   send response
        => sendNotification ... => sendNotification ...

call tool => create task => send task response
 update task => send reponse (progress)
 update task => send response (done)
 get task payload => send response (data from task)

call tool => create task => send task response
 update task => send reponse (progress)
 cancel task => send response (canceled)

*/

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
    void sendNotification(
        const Schema::ServerNotification &notification, const QString &sessionId = {});

    // Tools

    using ToolInterfaceCallback = std::function<
        Utils::Result<>(const Schema::CallToolRequestParams &, const ToolInterface &)>;
    void addTool(const Schema::Tool &tool, const ToolInterfaceCallback &callback);

    using ToolCallback
        = std::function<Utils::Result<Schema::CallToolResult>(const Schema::CallToolRequestParams &)>;
    void addTool(const Schema::Tool &tool, const ToolCallback &callback);

    void removeTool(const QString &toolName);

    // Prompts
    using PromptArguments = QMap<QString, QString>;
    using PromptMessageList = QList<Schema::PromptMessage>;
    using PromptCallback = std::function<PromptMessageList(PromptArguments)>;

    void addPrompt(const Schema::Prompt &prompt, const PromptCallback &callback);
    void removePrompt(const QString &promptName);

    // Resources
    using ResourceCallback = std::function<Utils::Result<Schema::ReadResourceResult>(
        const Schema::ReadResourceRequestParams &)>;

    void addResource(const Schema::Resource &resource, const ResourceCallback &callback);
    void removeResource(const QString &uri);
    void setResourceFallbackCallback(const ResourceCallback &callback);

    void addResourceTemplate(const Schema::ResourceTemplate &resourceTemplate);
    void removeResourceTemplate(const QString &name);

    // Completions
    using CompletionResultCallback = std::function<void(Utils::Result<Schema::CompleteResult>)>;
    using CompletionCallback = std::function<
        void(const Schema::CompleteRequestParams &, const CompletionResultCallback &)>;
    void setCompletionCallback(const CompletionCallback &callback);

private:
    std::shared_ptr<ServerPrivate> d;
};

} // namespace Mcp
