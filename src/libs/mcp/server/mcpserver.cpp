// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mcpserver.h"

#include <utils/co_result.h>
#include <utils/overloaded.h>
#include <utils/result.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QTimer>

#ifdef MCP_SERVER_HAS_QT_HTTP_SERVER
#  include <QHttpServer>
#  include <QHttpServerRequest>
#  include <QHttpServerResponder>
#  include <QHttpServerResponse>
#else
#  include "minihttpserver.h"
// Bring fallback types into the global namespace so the rest of the file
// compiles unchanged whether or not Qt::HttpServer is present.
using QHttpServer = MiniHttp::HttpServer;
using QHttpServerRequest = MiniHttp::HttpRequest;
using QHttpServerResponder = MiniHttp::HttpResponder;
using QHttpServerResponse = MiniHttp::HttpResponse;
#endif

Q_LOGGING_CATEGORY(mcpServerLog, "mcp.server", QtWarningMsg)

using namespace Utils;

namespace Mcp {

enum ErrorCodes {
    // Defined by JSON RPC
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    serverErrorStart = -32099,
    serverErrorEnd = -32000,
    ServerNotInitialized = -32002,
    UnknownErrorCode = -32001,
};

struct SseStream
{
    QHttpServerResponder responder;

    SseStream(QHttpServerResponder &&_responder)
        : responder(std::move(_responder))
    {
        responder.writeBeginChunked("text/event-stream", QHttpServerResponder::StatusCode::Ok);
    }

    ~SseStream() { responder.writeEndChunked({"\n\n"}); }

    bool notify(const QByteArray &data)
    {
        if (responder.isResponseCanceled())
            return false;

        QByteArray event = "data: " + data + "\n\n";
        responder.writeChunk(event);
        return true;
    }
};

static QJsonObject makeResponse(Schema::RequestId id, const Schema::ServerResult &result)
{
    QJsonObject json = Schema::toJson(Schema::JSONRPCResultResponse().id(id));
    json["result"]
        = std::visit([](const auto &v) -> QJsonObject { return Schema::toJson(v); }, result);
    return json;
};

class ServerPrivate
{
    Schema::Implementation serverInfo;

public:
    ServerPrivate(Schema::Implementation serverInfo)
        : serverInfo(serverInfo)
    {}

    bool bind(QTcpServer *server) { return m_server.bind(server); }

    struct Responder
    {
        std::function<void(QJsonDocument)> write;
        std::function<void(QHttpServerResponder::StatusCode)> writeStatus;
        std::function<void(const QByteArray &, const char *, QHttpServerResponse::StatusCode)>
            writeData;

        std::shared_ptr<QHttpServerResponder> httpResponder;
    };

    void onRequest(
        Schema::RequestId id, const Schema::ClientRequest &request, const Responder &responder)
    {
        qCDebug(mcpServerLog) << "Received JSONRPCRequest:" << Schema::dispatchValue(request);

        if (std::holds_alternative<Schema::InitializeRequest>(request)) {
            onInitialize(id, std::get<Schema::InitializeRequest>(request), responder);
            return;
        } else if (std::holds_alternative<Schema::CallToolRequest>(request)) {
            onToolCall(id, std::get<Schema::CallToolRequest>(request), responder);
            return;
        } else if (std::holds_alternative<Schema::ListToolsRequest>(request)) {
            onToolsList(id, std::get<Schema::ListToolsRequest>(request), responder);
            return;
        } else if (std::holds_alternative<Schema::ListPromptsRequest>(request)) {
            onPromptsList(id, std::get<Schema::ListPromptsRequest>(request), responder);
            return;
        } else if (std::holds_alternative<Schema::GetPromptRequest>(request)) {
            onGetPrompt(id, std::get<Schema::GetPromptRequest>(request), responder);
            return;
        } else if (std::holds_alternative<Schema::ListResourcesRequest>(request)) {
            onResourcesList(id, std::get<Schema::ListResourcesRequest>(request), responder);
            return;
        } else if (std::holds_alternative<Schema::ReadResourceRequest>(request)) {
            onReadResource(id, std::get<Schema::ReadResourceRequest>(request), responder);
            return;
        } else if (std::holds_alternative<Schema::ListResourceTemplatesRequest>(request)) {
            onListResourceTemplates(
                id, std::get<Schema::ListResourceTemplatesRequest>(request), responder);
            return;
        } else if (std::holds_alternative<Schema::CompleteRequest>(request)) {
            if (m_completionCallback) {
                m_completionCallback(
                    std::get<Schema::CompleteRequest>(request).params(),
                    [responder, id](Result<Schema::CompleteResult> result) mutable {
                        if (!result) {
                            responder.write(QJsonDocument(
                                Schema::toJson(
                                    Schema::JSONRPCErrorResponse()
                                        .error(
                                            Schema::Error()
                                                .code(InternalError)
                                                .message(QString("Unknown Error: %1")
                                                             .arg(result.error())))
                                        .id(id))));
                            return;
                        }
                        responder.write(QJsonDocument(makeResponse(id, *result)));
                    });
                return;
            }
        }

        responder.write(QJsonDocument(
            Schema::toJson(
                Schema::JSONRPCErrorResponse()
                    .error(
                        Schema::Error()
                            .code(MethodNotFound)
                            .message(QString("Method \"%1\" not implemented")
                                         .arg(Schema::dispatchValue(request))))
                    .id(id))));

        return;
    }

    void onInitialize(
        Schema::RequestId id, const Schema::InitializeRequest &request, const Responder &responder)
    {
        if (request._params._protocolVersion != "2025-11-25") {
            auto errorResponse = Schema::JSONRPCErrorResponse().id(id).error(
                Schema::Error()
                    .code(InvalidRequest)
                    .message(QString("Unsupported protocol version: %1")
                                 .arg(request._params._protocolVersion)));

            responder.write(QJsonDocument(Schema::toJson(errorResponse)));
            return;
        }

        auto caps = Schema::ServerCapabilities()
                        .prompts(Schema::ServerCapabilities::Prompts{}.listChanged(true))
                        .tools(Schema::ServerCapabilities::Tools().listChanged(true))
                        .resources(Schema::ServerCapabilities::Resources{}.listChanged(true));

        if (m_completionCallback)
            caps = caps.completions(QJsonObject());

        auto initResult = Schema::InitializeResult()
                              .protocolVersion(request._params._protocolVersion)
                              .serverInfo(serverInfo)
                              .capabilities(caps);

        responder.write(QJsonDocument(makeResponse(id, initResult)));
    }

    void onToolCall(
        Schema::RequestId id, const Schema::CallToolRequest &request, const Responder &responder)
    {
        auto toolIt = m_tools.find(request._params._name);

        if (toolIt == m_tools.end()) {
            qCWarning(mcpServerLog) << "Received call for unknown tool:" << request._params._name;

            responder.write(QJsonDocument(toJson(
                Schema::JSONRPCErrorResponse()
                    .error(
                        Schema::Error()
                            .code(MethodNotFound)
                            .message("Invalid Tool:" + request._params._name))
                    .id(id))));

            return;
        }

        qCDebug(mcpServerLog) << "Running tool" << toolIt.key();

        if (std::holds_alternative<Server::ToolCallback>(toolIt->callback)) {
            const auto cb = std::get<Server::ToolCallback>(toolIt->callback);

            Result<Schema::CallToolResult> r = cb(request._params);

            if (!r) {
                responder.write(QJsonDocument(makeResponse(
                    id,
                    Schema::CallToolResult().isError(true).content(
                        {Schema::TextContent().text(r.error())}))));
                return;
            }

            responder.write(QJsonDocument(makeResponse(id, *r)));
            return;
        }

        Server::AsyncToolCallback cb = std::get<Server::AsyncToolCallback>(toolIt->callback);

        cb(request._params, [responder, id](auto result) mutable {
            if (!result) {
                responder.write(QJsonDocument(makeResponse(
                    id,
                    Schema::CallToolResult().isError(true).content(
                        {Schema::TextContent().text(result.error())}))));
                return;
            }
            responder.write(QJsonDocument(makeResponse(id, *result)));
        });
    }

    void onToolsList(
        Schema::RequestId id, const Schema::ListToolsRequest &request, const Responder &responder)
    {
        auto it = m_tools.begin();
        if (request._params && request._params->_cursor)
            it = m_tools.find(*request._params->_cursor);

        static const int pageSize = 20;
        Schema::ListToolsResult result;
        int count = 0;
        for (; it != m_tools.end() && count < pageSize; ++it, ++count) {
            result._tools.append(it.value().tool);
        }
        if (it != m_tools.end())
            result._nextCursor = it.key();

        responder.write(QJsonDocument(makeResponse(id, result)));
    }

    void onPromptsList(
        Schema::RequestId id, const Schema::ListPromptsRequest &request, const Responder &responder)
    {
        auto it = m_prompts.begin();
        if (request._params && request._params->_cursor)
            it = m_prompts.find(*request._params->_cursor);

        static const int pageSize = 20;
        Schema::ListPromptsResult result;
        int count = 0;
        for (; it != m_prompts.end() && count < pageSize; ++it, ++count) {
            result._prompts.append(it->prompt);
        }
        if (it != m_prompts.end())
            result._nextCursor = it.key();

        responder.write(QJsonDocument(makeResponse(id, result)));
    }

    void onResourcesList(
        Schema::RequestId id,
        const Schema::ListResourcesRequest &request,
        const Responder &responder)
    {
        auto it = m_resources.begin();
        if (request._params && request._params->_cursor)
            it = m_resources.find(*request._params->_cursor);

        static const int pageSize = 20;
        Schema::ListResourcesResult result;
        int count = 0;
        for (; it != m_resources.end() && count < pageSize; ++it, ++count) {
            result._resources.append(it.value().resource);
        }
        if (it != m_resources.end())
            result._nextCursor = it.key();

        responder.write(QJsonDocument(makeResponse(id, result)));
    }

    void onReadResource(
        Schema::RequestId id, const Schema::ReadResourceRequest &request, const Responder &responder)
    {
        auto it = m_resources.find(request._params._uri);
        if (it == m_resources.end()) {
            if (m_resourceFallbackCallback) {
                Result<Schema::ReadResourceResult> r = m_resourceFallbackCallback(request._params);

                if (r) {
                    responder.write(QJsonDocument(makeResponse(id, *r)));
                    return;
                }
            }

            responder.write(QJsonDocument(
                Schema::toJson(
                    Schema::JSONRPCErrorResponse()
                        .error(
                            Schema::Error()
                                .code(InvalidParams)
                                .message(
                                    QString("Resource \"%1\" not found").arg(request._params._uri)))
                        .id(id))));
            return;
        }

        Result<Schema::ReadResourceResult> r = it->callback(request._params);
        if (!r) {
            responder.write(QJsonDocument(
                Schema::toJson(
                    Schema::JSONRPCErrorResponse()
                        .error(Schema::Error().code(InternalError).message(r.error()))
                        .id(id))));
            return;
        }

        responder.write(QJsonDocument(makeResponse(id, *r)));
    }

    void onListResourceTemplates(
        Schema::RequestId id,
        const Schema::ListResourceTemplatesRequest &request,
        const Responder &responder)
    {
        auto it = m_resourceTemplates.begin();
        if (request._params && request._params->_cursor)
            it = m_resourceTemplates.find(*request._params->_cursor);

        static const int pageSize = 20;
        Schema::ListResourceTemplatesResult result;
        int count = 0;
        for (; it != m_resourceTemplates.end() && count < pageSize; ++it, ++count) {
            result._resourceTemplates.append(it.value());
        }
        if (it != m_resourceTemplates.end())
            result._nextCursor = it.key();

        responder.write(QJsonDocument(makeResponse(id, result)));
    }

    void onGetPrompt(
        Schema::RequestId id, const Schema::GetPromptRequest &request, const Responder &responder)
    {
        auto it = m_prompts.find(request._params._name);
        if (it == m_prompts.end()) {
            responder.write(QJsonDocument(
                Schema::toJson(
                    Schema::JSONRPCErrorResponse()
                        .error(
                            Schema::Error()
                                .code(InvalidParams)
                                .message(
                                    QString("Prompt \"%1\" not found").arg(request._params._name)))
                        .id(id))));
            return;
        }

        QList<Schema::PromptMessage> messages = it->callback(
            request._params._arguments.value_or(QMap<QString, QString>{}));

        Schema::GetPromptResult result{
            ._description = it->prompt._description,
            ._messages = messages,
        };
        responder.write(QJsonDocument(makeResponse(id, result)));
    }

    void onData(const QByteArray &data, const Responder &responder)
    {
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
            responder.writeData(
                "Invalid JSON body", "text/plain", QHttpServerResponse::StatusCode::BadRequest);
            return;
        }

        const auto request = Schema::fromJson<Schema::JSONRPCRequest>(jsonDoc.object());
        const auto clientRequest = Schema::fromJson<Schema::ClientRequest>(jsonDoc.object());
        if (request && clientRequest) {
            onRequest(request->_id, *clientRequest, responder);
            return;
        }

        const auto clientNotification = Schema::fromJson<Schema::ClientNotification>(jsonDoc.object());
        if (clientNotification) {
            qCDebug(mcpServerLog) << "Received JSONRPCNotification:"
                                  << Schema::dispatchValue(clientNotification.value());
            responder.writeStatus(QHttpServerResponse::StatusCode::NoContent);
            return;
        }

        responder.writeStatus(QHttpServerResponse::StatusCode::BadRequest);
        return;
    }

    struct ToolAndCallback
    {
        Schema::Tool tool;
        std::variant<Server::ToolCallback, Server::AsyncToolCallback> callback;
    };
    QMap<QString, ToolAndCallback> m_tools;

    struct PromptAndCallback
    {
        Schema::Prompt prompt;
        Server::PromptCallback callback;
    };
    QMap<QString, PromptAndCallback> m_prompts;

    struct ResourceAndCallback
    {
        Schema::Resource resource;
        Server::ResourceCallback callback;
    };
    QMap<QString, ResourceAndCallback> m_resources;
    Server::ResourceCallback m_resourceFallbackCallback;
    QMap<QString, Schema::ResourceTemplate> m_resourceTemplates;

    QHttpServer m_server;
    std::vector<std::unique_ptr<SseStream>> m_sseStreams;
    std::function<void(QByteArray)> m_ioOutputHandler;

    Server::CompletionCallback m_completionCallback;
};

Server::Server(Schema::Implementation serverInfo, bool enableSSETestRoute)
    : d(std::make_unique<ServerPrivate>(serverInfo))
{
    d->m_server.setMissingHandler(
        new QObject(), [](const QHttpServerRequest &request, QHttpServerResponder &responder) {
            qCDebug(mcpServerLog) << request.url() << request.method() << "not found";
            qCDebug(mcpServerLog) << request.headers();
            responder.write(QHttpServerResponse::StatusCode::NotFound);
        });

    d->m_server.route(
        "/",
        QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest &req, QHttpServerResponder &responder) {
            qCDebug(mcpServerLog) << "Received request with Accept header:"
                                  << req.headers().value("Accept");
            if (req.headers().value("accept") == "text/event-stream") {
                d->m_sseStreams.emplace_back(std::make_unique<SseStream>(std::move(responder)));
                return;
            }

            responder.write(QHttpServerResponse::StatusCode::NotFound);
        });

    if (enableSSETestRoute) {
        d->m_server.route("/ssetest", QHttpServerRequest::Method::Get, []() {
            auto html = R"(
                <html>
                    <body>
                        <h1>SSE Test</h1>
                        <div id="events"></div>
                        <script>
                            const eventSource = new EventSource("/");
                            eventSource.onmessage = function(event) {
                                const newElement = document.createElement("div");
                                newElement.textContent = "Received event: " + event.data;
                                document.getElementById("events").appendChild(newElement);
                            };
                            eventSource.onerror = function() {
                                const newElement = document.createElement("div");
                                newElement.textContent = "Error occurred, connection closed.";
                                document.getElementById("events").appendChild(newElement);
                                eventSource.close();
                            };
                        </script>
                    </body>
                </html>
            )";
            return QHttpServerResponse(html, QHttpServerResponse::StatusCode::Ok);
        });
    }

    d->m_server.route(
        "/",
        QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest &req, QHttpServerResponder &responder) -> void {
            // We do not allow any origin header, so check that it is not present
            if (req.headers().contains("Origin")) {
                responder.write(
                    "Origin header not allowed",
                    "text/plain",
                    QHttpServerResponse::StatusCode::BadRequest);
                return;
            }

            // Check header contains "Accept" with only "application/json" and "text/event-stream"
            if (!req.headers().contains("Accept")) {
                responder.write(
                    "Missing Accept header",
                    "text/plain",
                    QHttpServerResponse::StatusCode::BadRequest);
                return;
            }

            if (req.headers().contains("mcp-protocol-version")
                && req.headers().value("mcp-protocol-version") != "2025-11-25") {
                responder.write(
                    "Unsupported Mcp protocol version",
                    "text/plain",
                    QHttpServerResponse::StatusCode::BadRequest);
                return;
            }

            qCDebug(mcpServerLog).noquote() << "Received request with headers:\n"
                                            << req.headers() << "\nand body:\n"
                                            << req.body() << "\nEnd of body";

            QStringList acceptValues = QString::fromUtf8(req.headers().value("Accept")).split(",");
            for (QString &value : acceptValues)
                value = value.trimmed();

            if (acceptValues.size() != 2 || !acceptValues.contains("application/json")
                || !acceptValues.contains("text/event-stream")) {
                responder.write(
                    "Invalid Accept header",
                    "text/plain",
                    QHttpServerResponse::StatusCode::BadRequest);
                return;
            }

            ServerPrivate::Responder r;
            r.httpResponder = std::make_shared<QHttpServerResponder>(std::move(responder));
            r.write = [r = r.httpResponder](QJsonDocument json) {
                const QByteArray jsonData = json.toJson(QJsonDocument::Compact);
                qCDebug(mcpServerLog).noquote() << "Writing response:" << jsonData;
                r->write(jsonData, "application/json", QHttpServerResponse::StatusCode::Ok);
            };
            r.writeStatus = [r = r.httpResponder](QHttpServerResponder::StatusCode status) {
                r->write(status);
            };
            r.writeData = [r = r.httpResponder](
                              const QByteArray &data,
                              const char *contentType,
                              QHttpServerResponse::StatusCode status) {
                r->write(data, contentType, status);
            };

            d->onData(req.body(), r);
        });
}

Server::~Server() = default;

bool Server::bind(QTcpServer *server)
{
    return d->bind(server);
}

QList<QTcpServer *> Server::boundTcpServers() const
{
    return d->m_server.servers();
}

void Server::addTool(const Schema::Tool &tool, const ToolCallback &callback)
{
    d->m_tools.insert(tool._name, ServerPrivate::ToolAndCallback{tool, callback});
    sendNotification(Schema::ToolListChangedNotification{});
}

void Server::addTool(const Schema::Tool &tool, const AsyncToolCallback &callback)
{
    d->m_tools.insert(tool._name, ServerPrivate::ToolAndCallback{tool, callback});
    sendNotification(Schema::ToolListChangedNotification{});
}

void Server::sendNotification(const Schema::ServerNotification &notification)
{
    auto data = QJsonDocument(toJson(notification)).toJson(QJsonDocument::Compact);

    if (d->m_ioOutputHandler) {
        d->m_ioOutputHandler(data);
    }

    for (auto it = d->m_sseStreams.begin(); it != d->m_sseStreams.end();) {
        if (!(*it)->notify(data))
            it = d->m_sseStreams.erase(it);
        else
            ++it;
    }
}

Result<std::function<void(QByteArray)>> Server::bindIO(std::function<void(QByteArray)> outputHandler)
{
    if (d->m_ioOutputHandler)
        return ResultError("IO already bound");
    if (!outputHandler)
        return ResultError("Output handler cannot be null");
    d->m_ioOutputHandler = std::move(outputHandler);

    return [this](QByteArray data) {
        ServerPrivate::Responder
            r{.write =
                  [this](QJsonDocument json) {
                      if (d->m_ioOutputHandler)
                          d->m_ioOutputHandler(json.toJson(QJsonDocument::Compact));
                  },
              .writeStatus =
                  [](QHttpServerResponder::StatusCode status) {
                      Q_UNUSED(status);
                      // We do not use HTTP status codes in IO mode, so ignore this
                  },
              .writeData =
                  [this](
                      const QByteArray &data,
                      const char *contentType,
                      QHttpServerResponse::StatusCode status) {
                      Q_UNUSED(contentType);
                      Q_UNUSED(status);
                      Q_ASSERT(
                          data.contains('\n')
                          == false); // We use newlines to separate messages, so data cannot contain newlines
                      if (d->m_ioOutputHandler)
                          d->m_ioOutputHandler(data);
                  }};
        d->onData(data, std::move(r));
    };
}

void Server::removeTool(const QString &toolName)
{
    if (d->m_tools.remove(toolName) > 0)
        sendNotification(Schema::ToolListChangedNotification{});
}

void Server::addPrompt(const Schema::Prompt &prompt, const PromptCallback &callback)
{
    d->m_prompts.insert(prompt._name, {prompt, callback});
    sendNotification(Schema::PromptListChangedNotification{});
}

void Server::removePrompt(const QString &promptName)
{
    if (d->m_prompts.remove(promptName) > 0)
        sendNotification(Schema::PromptListChangedNotification{});
}

void Server::addResource(const Schema::Resource &resource, const ResourceCallback &callback)
{
    d->m_resources.insert(resource._uri, {resource, callback});
    sendNotification(Schema::ResourceListChangedNotification{});
}

void Server::removeResource(const QString &uri)
{
    if (d->m_resources.remove(uri) > 0)
        sendNotification(Schema::ResourceListChangedNotification{});
}

void Server::addResourceTemplate(const Schema::ResourceTemplate &resourceTemplate)
{
    d->m_resourceTemplates.insert(resourceTemplate._name, resourceTemplate);
    sendNotification(Schema::ResourceListChangedNotification{});
}

void Server::removeResourceTemplate(const QString &name)
{
    if (d->m_resourceTemplates.remove(name) > 0)
        sendNotification(Schema::ResourceListChangedNotification{});
}

void Server::setCompletionCallback(const CompletionCallback &callback)
{
    d->m_completionCallback = callback;
}

void Server::setResourceFallbackCallback(const ResourceCallback &callback)
{
    d->m_resourceFallbackCallback = callback;
}

} // namespace Mcp
