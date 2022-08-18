// Copyright  (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "workspace.h"

namespace LanguageServerProtocol {

constexpr const char WorkSpaceFolderRequest::methodName[];
constexpr const char DidChangeWorkspaceFoldersNotification::methodName[];
constexpr const char DidChangeConfigurationNotification::methodName[];
constexpr const char ConfigurationRequest::methodName[];
constexpr const char WorkspaceSymbolRequest::methodName[];
constexpr const char ExecuteCommandRequest::methodName[];
constexpr const char ApplyWorkspaceEditRequest::methodName[];
constexpr const char DidChangeWatchedFilesNotification::methodName[];

WorkSpaceFolderRequest::WorkSpaceFolderRequest()
    : Request(methodName, nullptr)
{ }

DidChangeWorkspaceFoldersNotification::DidChangeWorkspaceFoldersNotification(
        const DidChangeWorkspaceFoldersParams &params)
    : Notification(methodName, params)
{ }

DidChangeConfigurationNotification::DidChangeConfigurationNotification(
        const DidChangeConfigurationParams &params)
    : Notification(methodName, params)
{ }

ConfigurationRequest::ConfigurationRequest(const ConfigurationParams &params)
    : Request(methodName, params)
{ }

WorkspaceSymbolRequest::WorkspaceSymbolRequest(const WorkspaceSymbolParams &params)
    : Request(methodName, params)
{ }

ExecuteCommandRequest::ExecuteCommandRequest(const ExecuteCommandParams &params)
    : Request(methodName, params)
{ }

ApplyWorkspaceEditRequest::ApplyWorkspaceEditRequest(const ApplyWorkspaceEditParams &params)
    : Request(methodName, params)
{ }

WorkspaceFoldersChangeEvent::WorkspaceFoldersChangeEvent()
{
    insert(addedKey, QJsonArray());
    insert(removedKey, QJsonArray());
}

DidChangeWatchedFilesNotification::DidChangeWatchedFilesNotification(
        const DidChangeWatchedFilesParams &params)
    : Notification(methodName, params)
{ }

ExecuteCommandParams::ExecuteCommandParams(const Command &command)
{
    setCommand(command.command());
    if (command.arguments().has_value())
        setArguments(*command.arguments());
}

LanguageServerProtocol::WorkSpaceFolderResult::operator const QJsonValue() const
{
    if (!Utils::holds_alternative<QList<WorkSpaceFolder>>(*this))
        return QJsonValue::Null;
    QJsonArray array;
    for (const auto &folder : Utils::get<QList<WorkSpaceFolder>>(*this))
        array.append(QJsonValue(folder));
    return array;
}

} // namespace LanguageServerProtocol
