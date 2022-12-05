// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "squishqueryrunner.h"

#include "squishserverprocess.h"
#include "squishtr.h"

#include <utils/environment.h>
#include <utils/qtcassert.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(queryLog, "qtc.squish.queryrunner", QtWarningMsg)

namespace Squish::Internal {

SquishQueryRunner::SquishQueryRunner(QObject *parent)
    : SquishRunnerProcessBase{parent}
{
}

void SquishQueryRunner::executeQuery()
{
    QTC_ASSERT(m_process.state() == QProcess::NotRunning, return);
    QTC_ASSERT(server(), return);
    QTC_ASSERT(server()->isRunning(), return);
    const Utils::FilePath runnerFilePath = m_process.commandLine().executable();
    QTC_ASSERT(runnerFilePath.isExecutableFile(), return);
    int port = server()->port();
    QTC_ASSERT(port != -1, return);

    // avoid crashes on fast re-use
    m_process.close();

    QStringList arguments = { "--port", QString::number(port) };
    Utils::CommandLine cmdLine = {runnerFilePath, arguments};
    switch (m_query) {
    case ServerInfo:
        cmdLine.addArg("--info");
        cmdLine.addArg("all");
        break;
    case GetGlobalScriptDirs:
        cmdLine.addArg("--config");
        cmdLine.addArg("getGlobalScriptDirs");
        break;
    case SetGlobalScriptDirs:
        cmdLine.addArg("--config");
        cmdLine.addArg("setGlobalScriptDirs");
        cmdLine.addArgs(m_queryParameter, Utils::CommandLine::Raw);
        break;
    default:
        QTC_ASSERT(false, return);
    }

    m_process.setCommand(cmdLine);
    m_licenseIssues = false;

    qCInfo(queryLog).noquote() << "Starting" << cmdLine.toUserOutput();
    m_process.start();
    if (!m_process.waitForStarted()) {
        qCDebug(queryLog) << "RunnerState > StartFailed";
        m_process.close();
        return;
    }
    qCDebug(queryLog) << "RunnerState > Started";
}

void SquishQueryRunner::onDone()
{
    const QString error = m_licenseIssues ? Tr::tr("Could not get Squish license from server.")
                                          : QString();
    const QString fullOutput = m_process.cleanedStdOut();
    if (m_queryCallback)
        m_queryCallback(fullOutput, error);
    qCDebug(queryLog) << "RunnerState > Stopped";
    m_queryCallback = {};
    m_queryParameter.clear();
    m_process.setEnvironment(Utils::Environment::systemEnvironment());

    if (server()) {
        qCDebug(queryLog) << "Stopping server from query runner";
        server()->stop();
        setServer(nullptr);
    }
}

void SquishQueryRunner::onErrorOutput()
{
    // output that must be send to the Runner/Server Log
    const QByteArray output = m_process.readAllStandardError();
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            emit logOutputReceived("Runner: " + QLatin1String(trimmed));
            if (trimmed.startsWith("Couldn't get license")
                       || trimmed.contains("UNLICENSED version of Squish")) {
                m_licenseIssues = true;
            }
        }
    }
}

void SquishQueryRunner::onServerStateChanged(SquishProcessState toServerState)
{
    switch (toServerState) {
    case Started:
        executeQuery();
        break;
    case Stopped:
        if (m_process.isRunning())
            m_process.close();
        break;
    default: // ignore other states
        break;
    }
}

} // namespace Squish::Internal
