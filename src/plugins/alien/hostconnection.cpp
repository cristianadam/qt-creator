// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "hostconnection.h"

#include <utils/environment.h>
#include <utils/qtcprocess.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

using namespace Utils;

static Q_LOGGING_CATEGORY(logHost, "qtc.alien.host", QtWarningMsg)

namespace Alien::Internal {

HostConnection::HostConnection(QObject *parent)
    : QObject(parent)
{}

HostConnection::~HostConnection()
{
    stop();
}

void HostConnection::setCommand(const CommandLine &cmd)
{
    m_cmd = cmd;
}

void HostConnection::setWorkingDirectory(const FilePath &dir)
{
    m_workingDirectory = dir;
}

bool HostConnection::isRunning() const
{
    return m_process && m_process->isRunning();
}

void HostConnection::start()
{
    if (m_process)
        stop();

    m_buffer.clear();
    m_process = new Process(this);
    m_process->setProcessMode(ProcessMode::Writer);

    connect(m_process, &Process::readyReadStandardOutput, this, &HostConnection::readOutput);
    connect(m_process, &Process::readyReadStandardError, this, [this] {
        const QString err = m_process->readAllStandardError();
        if (!err.isEmpty())
            qCDebug(logHost).noquote() << err.trimmed();
    });
    connect(m_process, &Process::started, this, &HostConnection::started);
    connect(m_process, &Process::done, this, [this] {
        if (m_process->result() != ProcessResult::FinishedWithSuccess)
            emit errorOccurred(m_process->exitMessage());
        emit finished();
    });

    m_process->setCommand(m_cmd);
    if (!m_workingDirectory.isEmpty())
        m_process->setWorkingDirectory(m_workingDirectory);
    m_process->setEnvironment(m_cmd.executable().deviceEnvironment());

    qCDebug(logHost) << "Starting host:" << m_cmd.toUserOutput();
    m_process->start();
}

void HostConnection::stop()
{
    if (m_process && m_process->isRunning()) {
        m_process->kill();
        m_process->waitForFinished(QDeadlineTimer(3000));
    }
    delete m_process;
    m_process = nullptr;
    m_pending.clear();
}

void HostConnection::readOutput()
{
    m_buffer += m_process->readAllRawStandardOutput();

    int index;
    while ((index = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(index).trimmed();
        m_buffer.remove(0, index + 1);
        if (line.isEmpty())
            continue;

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (document.isNull() || !document.isObject()) {
            qCWarning(logHost) << "Cannot parse host message:" << error.errorString() << line;
            continue;
        }
        handleMessage(document.object());
    }
}

void HostConnection::handleMessage(const QJsonObject &message)
{
    const bool hasMethod = message.contains("method");
    const bool hasId = message.contains("id");

    if (hasMethod && hasId) {
        // Inbound request.
        const QString method = message.value("method").toString();
        const int id = message.value("id").toInt();
        const QJsonValue params = message.value("params");

        const auto handler = m_requestHandlers.constFind(method);
        Responder respond = [this, id](const QJsonValue &result, const QString &errorMessage) {
            QJsonObject response{{"id", id}};
            if (errorMessage.isEmpty())
                response.insert("result", result);
            else
                response.insert("error", QJsonObject{{"code", -32000}, {"message", errorMessage}});
            writeMessage(response);
        };
        if (handler == m_requestHandlers.constEnd())
            respond({}, QString("Method not found: %1").arg(method));
        else
            (*handler)(params, respond);
        return;
    }

    if (hasMethod) {
        // Inbound notification.
        const QString method = message.value("method").toString();
        const auto handler = m_notificationHandlers.constFind(method);
        if (handler != m_notificationHandlers.constEnd())
            (*handler)(message.value("params"));
        return;
    }

    if (hasId) {
        // Response to one of our requests.
        const int id = message.value("id").toInt();
        const ResponseCallback callback = m_pending.take(id);
        if (!callback)
            return;
        if (message.contains("error"))
            callback({}, message.value("error").toObject().value("message").toString());
        else
            callback(message.value("result"), {});
    }
}

void HostConnection::writeMessage(QJsonObject message)
{
    if (!m_process)
        return;
    message.insert("jsonrpc", "2.0");
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    m_process->writeRaw(data);
}

void HostConnection::sendRequest(const QString &method, const QJsonValue &params,
                                 const ResponseCallback &cb)
{
    const int id = m_nextId++;
    if (cb)
        m_pending.insert(id, cb);
    writeMessage({{"id", id}, {"method", method}, {"params", params}});
}

void HostConnection::sendNotification(const QString &method, const QJsonValue &params)
{
    writeMessage({{"method", method}, {"params", params}});
}

void HostConnection::setRequestHandler(const QString &method, const RequestHandler &handler)
{
    m_requestHandlers.insert(method, handler);
}

void HostConnection::setNotificationHandler(const QString &method, const NotificationHandler &handler)
{
    m_notificationHandlers.insert(method, handler);
}

} // namespace Alien::Internal
