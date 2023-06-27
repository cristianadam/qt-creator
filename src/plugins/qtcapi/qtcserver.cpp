// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcserver.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(qtcServer, "qtcapi.server", QtDebugMsg)

namespace QtcApi {

class QtcServerPrivate
{
public:
    QtcServerPrivate()
    {
        if (!localServer.listen(
                QString("qtcapi-%1")
                    .arg(QUuid::createUuid().toString(QUuid::StringFormat::WithoutBraces)))) {
            qCWarning(qtcServer) << "Failed to listen on:" << localServer.fullServerName();
            return;
        }
        server.bind(&localServer);
        qCDebug(qtcServer) << "Listening on:" << localServer.fullServerName();
    }

    QHttpServer server;
    QLocalServer localServer;
};

QtcServer &QtcServer::instance()
{
    static QtcServer server;
    return server;
}

QtcServer::QtcServer()
    : d(std::make_unique<QtcServerPrivate>())
{}

QtcServer::~QtcServer() {}

QUrl QtcServer::url()
{
    return QUrl(QStringLiteral("unix:///"));
}

QString QtcServer::socket()
{
    return instance().d->localServer.fullServerName();
}

QHttpServer &QtcServer::server() const
{
    return d->server;
}

} // namespace QtcApi
