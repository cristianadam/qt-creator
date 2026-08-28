// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alienmcp.h"

#include "extensionhost.h"

#include <mcp/server/mcpserver.h>
#include <mcp/server/toolregistry.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace Alien::Internal {

void registerMcpTools()
{
    using namespace Mcp::Schema;
    namespace Schema = Mcp::Schema;
    using Mcp::ToolInterface;
    using Mcp::ToolRegistry;

    ToolRegistry::registerTool(
        Schema::Tool()
            .name("list_webviews")
            .title("List extension webview panels")
            .description(
                "Lists the webview panels the running VS Code extensions have open: "
                "{id, viewType, title, scripts}. The id is what webview_eval takes. "
                "\"scripts\" says whether the extension asked for a scripted panel, "
                "which is what decides whether its page can be driven.")
            .annotations(ToolAnnotations{}.readOnlyHint(true)),
        [](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            ExtensionHost *host = runningExtensionHost();
            if (!host)
                return CallToolResult{}.isError(true).addContent(
                    Schema::TextContent{}.text("VS Code extension support is not running."));
            return CallToolResult{}
                .isError(false)
                .structuredContent(QJsonObject{{"webviews", host->webviewPanels()}});
        });

    ToolRegistry::registerTool(
        Schema::Tool()
            .name("run_extension_command")
            .title("Run a command an extension registered")
            .description(
                "Runs one of the commands the running extensions registered, by id - the "
                "same ids the locator offers. Extensions do their work through commands, "
                "so this is how one of them is asked to do something that has no other "
                "way in. "
                "\n\n"
                "The command is dispatched and the call returns: what it goes on to do "
                "shows up wherever the extension puts it, which may be an output channel, "
                "a webview panel or a debug session.")
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "id",
                        QJsonObject{{"description",
                                     "Command id, as the locator lists it, for example "
                                     "\"zephyr-ide.debug\"."},
                                    {"type", "string"}})
                    .addRequired("id")),
        [](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
            const QString id = params.argumentsAsObject().value("id").toString();
            ExtensionHost *host = runningExtensionHost();
            if (!host)
                return CallToolResult{}.isError(true).addContent(
                    Schema::TextContent{}.text("VS Code extension support is not running."));
            // Saying so beats dispatching into nothing: a command that no
            // extension claimed would otherwise look like one that did nothing.
            if (!host->registeredCommands().contains(id)) {
                return CallToolResult{}.isError(true).addContent(Schema::TextContent{}.text(
                    QString("No extension registered the command \"%1\".").arg(id)));
            }
            host->executeCommand(id);
            return CallToolResult{}.isError(false).structuredContent(
                QJsonObject{{"dispatched", id}});
        });

    ToolRegistry::registerTool(
        Schema::Tool()
            .name("webview_eval")
            .title("Evaluate JavaScript in an extension webview")
            .description(
                "Runs JavaScript in the page of an extension's webview panel and returns "
                "what the last expression evaluated to. This is how a panel's own "
                "interface is driven and read: the widget tools see widgets, not page "
                "contents. "
                "\n\n"
                "Examples: \"document.body.innerText\" to read the panel; "
                "\"!!document.querySelector('#install')\" to check a control is there; "
                "\"document.querySelector('#install').click(), true\" to press it. "
                "\n\n"
                "Only a panel the extension asked to be scripted can run anything; "
                "one that cannot says so rather than failing silently.")
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "id",
                        QJsonObject{{"description",
                                     "Panel id, as reported by list_webviews."},
                                    {"type", "string"}})
                    .addProperty(
                        "script",
                        QJsonObject{{"description",
                                     "JavaScript to evaluate in the panel's page."},
                                    {"type", "string"}})
                    .addRequired("id")
                    .addRequired("script")),
        [](const CallToolRequestParams &params,
           const ToolInterface &toolInterface) -> Utils::Result<> {
            const QJsonObject arguments = params.argumentsAsObject();
            const QString id = arguments.value("id").toString();
            const QString script = arguments.value("script").toString();
            ExtensionHost *host = runningExtensionHost();
            if (!host) {
                toolInterface.finish(CallToolResult{}.isError(true).addContent(
                    Schema::TextContent{}.text("VS Code extension support is not running.")));
                return Utils::ResultOk;
            }
            host->runInWebview(
                id, script, [toolInterface](const QJsonValue &result, const QString &error) {
                    if (!error.isEmpty()) {
                        toolInterface.finish(CallToolResult{}.isError(true).addContent(
                            Schema::TextContent{}.text(error)));
                        return;
                    }
                    toolInterface.finish(
                        CallToolResult{}.isError(false).structuredContent(
                            QJsonObject{{"result", result}}));
                });
            return Utils::ResultOk;
        });
}

} // namespace Alien::Internal
