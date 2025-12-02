// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcreatorapplication.h"

#include <utils/algorithm.h>
#include <utils/processinfo.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QFileOpenEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QThread>
#include <QTime>

using namespace Utils;

static const char s_ack[] = "ack";

QtCreatorApplication::QtCreatorApplication(int &argc, char **argv)
    : QApplication(argc, argv)
{
    setupFreezeDetection();
}

void QtCreatorApplication::setupFreezeDetection()
{
    static const char s_freezeDetector[] = "QTC_FREEZE_DETECTOR";

    const auto isUsingFreezeDetector = []() -> std::optional<std::chrono::milliseconds> {
        if (!qEnvironmentVariableIsSet(s_freezeDetector))
            return {};

        bool ok = false;
        const int threshold = qEnvironmentVariableIntValue(s_freezeDetector, &ok);
        return ok ? std::chrono::milliseconds(threshold) : std::chrono::milliseconds(100);
    };

    m_freezeDetector = isUsingFreezeDetector();

    if (m_freezeDetector) {
        qDebug() << s_freezeDetector << "evn var is set. The freezes of main thread, above"
                 << *m_freezeDetector << "ms, will be reported.";
        qDebug() << "Change the freeze detection threshold by setting the" << s_freezeDetector
                 << "env var to a different numeric value (in ms).";
    }
}

bool QtCreatorApplication::notify(QObject *receiver, QEvent *event)
{
    if (m_inNotify || !m_freezeDetector)
        return QApplication::notify(receiver, event);

    const auto start = std::chrono::system_clock::now();
    const QPointer<QObject> p(receiver);
    const QString className = QLatin1String(receiver->metaObject()->className());
    const QString name = receiver->objectName();

    m_inNotify = true;
    const bool ret = QApplication::notify(receiver, event);
    m_inNotify = false;

    const auto end = std::chrono::system_clock::now();
    const auto freeze = duration_cast<std::chrono::milliseconds>(end - start);
    if (freeze > *m_freezeDetector) {
        m_total += freeze;
        const QString time = QTime::currentTime().toString(Qt::ISODateWithMs);
        qDebug().noquote() << QString("FREEZE [%1]").arg(time) << "of" << freeze.count()
                           << "ms, total" << m_total.count() << "ms, on:" << event;
        const QString receiverMessage
            = name.isEmpty() ? QString("receiver class: %1").arg(className)
                             : QString("receiver class: %1, object name: %2").arg(className, name);
        qDebug().noquote() << m_align << receiverMessage;
        if (!p)
            qDebug().noquote() << m_align << "THE RECEIVER GOT DELETED inside the event filter!";
    }

    return ret;
}

bool QtCreatorApplication::event(QEvent *event)
{
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *foe = static_cast<QFileOpenEvent *>(event);
        emit fileOpenRequest(foe->file());
        return true;
    }
    return QApplication::event(event);
}

QString QtCreatorApplication::socketName(qint64 pid)
{
    return QString("%1-%2").arg(QCoreApplication::applicationName()).arg(pid);
}

Result<QList<qint64>> QtCreatorApplication::pidsOfRunningInstances()
{
    const Result<QList<ProcessInfo>> processInfos = Utils::ProcessInfo::processInfoList();
    if (!processInfos)
        return ResultError(processInfos.error());

    const auto isSelf = [](const ProcessInfo &info) {
        return info.processId == QCoreApplication::applicationPid();
    };

    const auto isCreator = [](const ProcessInfo &info) {
        return info.executable == QCoreApplication::applicationFilePath();
    };

    QList<qint64> pids;

    for (const ProcessInfo &info : *processInfos) {
        if (isSelf(info))
            continue;
        if (!isCreator(info))
            continue;

        pids.append(info.processId);
    }

    if (pids.isEmpty()) {
        return ResultError(
            QCoreApplication::translate(
                "QtCreatorApplication", "No running Qt Creator instances found."));
    }
    return pids;
}

Utils::Result<> QtCreatorApplication::sendMessage(
    const QString &message, std::chrono::milliseconds timeout, qint64 pid, bool block)
{
    QLocalSocket socket;
    bool connOk = false;
    for (int i = 0; i < 2; i++) {
        // Try twice, in case the other instance is just starting up
        socket.connectToServer(socketName(pid));
        connOk = socket.waitForConnected(timeout.count() / 2);
        if (connOk || i)
            break;
        QThread::msleep(250);
    }
    if (!connOk) {
        return ResultError(
            QCoreApplication::translate(
                "QtCreatorApplication", "Could not connect to Qt Creator instance."));
    }

    QByteArray uMsg(message.toUtf8());
    QDataStream ds(&socket);
    ds.writeBytes(uMsg.constData(), uMsg.size());
    if (!socket.waitForBytesWritten(timeout.count())) {
        return ResultError(
            QCoreApplication::translate(
                "QtCreatorApplication", "Could not write message to Qt Creator instance."));
    }
    if (!socket.waitForReadyRead(timeout.count())) {
        return ResultError(
            QCoreApplication::translate(
                "QtCreatorApplication", "No response from Qt Creator instance."));
    }
    if (socket.read(qstrlen(s_ack)) != s_ack) {
        return ResultError(
            QCoreApplication::translate(
                "QtCreatorApplication", "Invalid response from Qt Creator instance."));
    }

    // block until peer disconnects
    if (block && socket.waitForReadyRead(-1)) {
        const QByteArray response = socket.readAll();
        std::fwrite(response.data(), response.size(), 1, stdout);
    }

    return ResultOk;
}

static QLocalServer localServer;

Result<> QtCreatorApplication::listenForMessages()
{
    if (!QLocalServer::removeServer(socketName())) {
        return ResultError(
            QCoreApplication::translate("QtCreatorApplication", "Could not cleanup socket"));
    }

    bool res = localServer.listen(socketName());
    if (!res) {
        ResultError(
            QCoreApplication::translate(
                "QtCreatorApplication",
                "Listen on local socket failed, %s",
                qPrintable(localServer.errorString())));
    }

    QObject::connect(
        &localServer, &QLocalServer::newConnection, this, &QtCreatorApplication::onNewConnection);

    return ResultOk;
}

void QtCreatorApplication::onNewConnection()
{
    QLocalSocket *socket = localServer.nextPendingConnection();
    if (!socket)
        return;

    // Why doesn't Qt have a blocking stream that takes care of this shait???
    while (socket->bytesAvailable() < static_cast<int>(sizeof(quint32))) {
        if (!socket->isValid()) // stale request
            return;
        socket->waitForReadyRead(1000);
    }
    QDataStream ds(socket);
    QByteArray uMsg;
    quint32 remaining;
    ds >> remaining;
    uMsg.resize(remaining);
    int got = 0;
    char *uMsgBuf = uMsg.data();
    //qDebug() << "RCV: remaining" << remaining;
    do {
        got = ds.readRawData(uMsgBuf, remaining);
        remaining -= got;
        uMsgBuf += got;
        //qDebug() << "RCV: got" << got << "remaining" << remaining;
    } while (remaining && got >= 0 && socket->waitForReadyRead(2000));
    //### error check: got<0
    if (got < 0) {
        qWarning() << "QtLocalPeer: Message reception failed" << socket->errorString();
        delete socket;
        return;
    }
    // ### async this
    QString message = QString::fromUtf8(uMsg.constData(), uMsg.size());
    socket->write(s_ack, qstrlen(s_ack));
    socket->waitForBytesWritten(1000);
    emit messageReceived(message, socket); // ##(might take a long time to return)
}
