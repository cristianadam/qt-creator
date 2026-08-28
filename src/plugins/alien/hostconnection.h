// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/commandline.h>
#include <utils/filepath.h>

#include <QHash>
#include <QJsonValue>
#include <QObject>

#include <functional>

namespace Utils { class Process; }

namespace Alien::Internal {

// A newline-delimited JSON-RPC 2.0 peer over a spawned process's stdio.
// Both request/response and notification directions work in both ways: the
// main side and the extension host each act as client and server.
class HostConnection final : public QObject
{
    Q_OBJECT

public:
    using ResponseCallback
        = std::function<void(const QJsonValue &result, const QString &error)>;
    using Responder = std::function<void(const QJsonValue &result, const QString &error)>;
    using RequestHandler
        = std::function<void(const QJsonValue &params, const Responder &respond)>;
    using NotificationHandler = std::function<void(const QJsonValue &params)>;

    explicit HostConnection(QObject *parent = nullptr);
    ~HostConnection() override;

    void setCommand(const Utils::CommandLine &cmd);
    void setWorkingDirectory(const Utils::FilePath &dir);

    void start();
    void stop();
    bool isRunning() const;

    void sendRequest(const QString &method, const QJsonValue &params, const ResponseCallback &cb);
    void sendNotification(const QString &method, const QJsonValue &params);

    void setRequestHandler(const QString &method, const RequestHandler &handler);
    void setNotificationHandler(const QString &method, const NotificationHandler &handler);

signals:
    void started();
    void finished();
    void errorOccurred(const QString &message);

private:
    void readOutput();
    void handleMessage(const QJsonObject &message);
    void writeMessage(QJsonObject message);

    Utils::Process *m_process = nullptr;
    Utils::CommandLine m_cmd;
    Utils::FilePath m_workingDirectory;
    QByteArray m_buffer;
    int m_nextId = 1;
    QHash<int, ResponseCallback> m_pending;
    QHash<QString, RequestHandler> m_requestHandlers;
    QHash<QString, NotificationHandler> m_notificationHandlers;
};

} // namespace Alien::Internal
