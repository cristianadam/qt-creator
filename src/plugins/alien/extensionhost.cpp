// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionhost.h"

#include "alientr.h"
#include "hostconnection.h"

#include <coreplugin/messagemanager.h>

#include <utils/commandline.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>

using namespace Core;
using namespace Utils;

static Q_LOGGING_CATEGORY(logHost, "qtc.alien.host", QtWarningMsg)

namespace Alien::Internal {

static Result<> extractResource(const QString &resourcePath, const FilePath &dest)
{
    QFile resource(resourcePath);
    if (!resource.open(QIODevice::ReadOnly))
        return make_unexpected(Tr::tr("Cannot read bundled resource \"%1\".").arg(resourcePath));

    if (const Result<> dir = dest.parentDir().ensureWritableDir(); !dir)
        return dir;

    const Result<qint64> written = dest.writeFileContents(resource.readAll());
    if (!written)
        return make_unexpected(written.error());
    return {};
}

ExtensionHost::ExtensionHost(const FilePath &nodePath, QObject *parent)
    : QObject(parent)
    , m_nodePath(nodePath)
{}

ExtensionHost::~ExtensionHost() = default;

bool ExtensionHost::isRunning() const
{
    return m_connection && m_connection->isRunning();
}

Result<> ExtensionHost::ensureStarted()
{
    if (m_connection)
        return {};

    if (!m_nodePath.isExecutableFile())
        return make_unexpected(Tr::tr("Node.js was not found. Set its path in the settings."));

    if (!m_runtimeDir.isValid())
        return make_unexpected(Tr::tr("Cannot create a temporary directory for the host."));

    const FilePath runtime = FilePath::fromString(m_runtimeDir.path());
    const FilePath hostJs = runtime / "host.js";
    if (const Result<> extracted = extractResource(":/alien/host/host.js", hostJs); !extracted)
        return extracted;

    m_connection = new HostConnection(this);
    installHandlers();
    m_connection->setCommand({m_nodePath, {hostJs.toFSPathString()}});
    m_connection->setWorkingDirectory(runtime);

    connect(m_connection, &HostConnection::started, this, [this] {
        const QList<std::function<void()>> deferred = std::exchange(m_deferred, {});
        for (const std::function<void()> &action : deferred)
            action();
    });
    connect(m_connection, &HostConnection::errorOccurred, this, [](const QString &message) {
        MessageManager::writeFlashing(Tr::tr("Alien host error: %1").arg(message));
    });

    m_connection->start();
    return {};
}

void ExtensionHost::installHandlers()
{
    m_connection->setRequestHandler(
        "window/showMessage",
        [](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString level = object.value("level").toString();
            const QString message = object.value("message").toString();
            const QString prefix = level == "error" ? Tr::tr("Error")
                                   : level == "warn" ? Tr::tr("Warning")
                                                     : Tr::tr("Info");
            MessageManager::writeFlashing(QString("Alien [%1]: %2").arg(prefix, message));
            // No message-box UI yet: report that no item was selected.
            respond(QJsonValue(QJsonValue::Null), {});
        });

    m_connection->setNotificationHandler("output/append", [](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        MessageManager::writeSilently(
            QString("Alien <%1>: %2")
                .arg(object.value("channel").toString(), object.value("value").toString()));
    });

    m_connection->setNotificationHandler("commands/register", [this](const QJsonValue &params) {
        const QString command = params.toObject().value("command").toString();
        if (!command.isEmpty() && !m_commands.contains(command)) {
            m_commands.append(command);
            emit commandsChanged();
        }
    });

    m_connection->setNotificationHandler("commands/unregister", [this](const QJsonValue &params) {
        const QString command = params.toObject().value("command").toString();
        if (m_commands.removeAll(command) > 0)
            emit commandsChanged();
    });

    m_connection->setNotificationHandler("log", [](const QJsonValue &params) {
        qCDebug(logHost).noquote() << params.toObject().value("message").toString();
    });

    m_connection->setNotificationHandler("host/ready", [](const QJsonValue &params) {
        qCDebug(logHost) << "Host ready, node" << params.toObject().value("node").toString();
    });
}

void ExtensionHost::whenReady(const std::function<void()> &action)
{
    if (isRunning())
        action();
    else
        m_deferred.append(action);
}

void ExtensionHost::activate(const VscodeManifest &manifest)
{
    if (const Result<> started = ensureStarted(); !started) {
        emit activationFailed(manifest.qualifiedId(), started.error());
        MessageManager::writeFlashing(
            Tr::tr("Cannot activate \"%1\": %2").arg(manifest.qualifiedId(), started.error()));
        return;
    }

    const QString id = manifest.qualifiedId();
    const QJsonObject params{
        {"id", id},
        {"path", manifest.rootDir.toFSPathString()},
        {"main", manifest.mainPath().toFSPathString()},
    };
    whenReady([this, id, params] {
        m_connection->sendRequest(
            "activate", params, [this, id](const QJsonValue &, const QString &error) {
                if (!error.isEmpty()) {
                    emit activationFailed(id, error);
                    MessageManager::writeFlashing(
                        Tr::tr("Activation of \"%1\" failed: %2").arg(id, error));
                }
            });
    });
}

Result<> ExtensionHost::activateBundledTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath dir = FilePath::fromString(m_runtimeDir.path()) / "testextension";
    const FilePath packageJson = dir / "package.json";
    const FilePath extensionJs = dir / "extension.js";

    if (const Result<> r = extractResource(":/alien/host/testextension/package.json", packageJson); !r)
        return r;
    if (const Result<> r = extractResource(":/alien/host/testextension/extension.js", extensionJs); !r)
        return r;

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

void ExtensionHost::executeCommand(const QString &command)
{
    if (!m_connection)
        return;
    whenReady([this, command] {
        m_connection->sendRequest(
            "executeCommand",
            QJsonObject{{"command", command}, {"args", QJsonArray{}}},
            [command](const QJsonValue &, const QString &error) {
                if (!error.isEmpty()) {
                    MessageManager::writeFlashing(
                        Tr::tr("Command \"%1\" failed: %2").arg(command, error));
                }
            });
    });
}

} // namespace Alien::Internal
