// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpstdiotransport.h"

#include <utils/qtcprocess.h>

#include <QLoggingCategory>

static Q_LOGGING_CATEGORY(logStdio, "qtc.acpclient.stdio", QtWarningMsg);

using namespace Utils;

namespace AcpClient::Internal {

AcpStdioTransport::AcpStdioTransport(QObject *parent)
    : AcpTransport(parent)
{}

AcpStdioTransport::~AcpStdioTransport()
{
    stop();
}

void AcpStdioTransport::setCommandLine(const CommandLine &cmd)
{
    m_cmd = cmd;
}

void AcpStdioTransport::setWorkingDirectory(const FilePath &workingDirectory)
{
    m_workingDirectory = workingDirectory;
}

void AcpStdioTransport::setEnvironment(const Environment &environment)
{
    m_env = environment;
}

void AcpStdioTransport::start()
{
    if (m_process) {
        if (m_process->isRunning())
            m_process->kill();
        delete m_process;
    }

    m_process = new Process(this);
    m_process->setProcessMode(ProcessMode::Writer);

    connect(m_process, &Process::readyReadStandardOutput, this, &AcpStdioTransport::readOutput);
    connect(m_process, &Process::readyReadStandardError, this, &AcpStdioTransport::readError);
    connect(m_process, &Process::started, this, &AcpTransport::started);
    connect(m_process, &Process::done, this, [this] {
        if (m_process->result() != ProcessResult::FinishedWithSuccess)
            emit errorOccurred(m_process->exitMessage());
        emit finished();
    });

    m_process->setCommand(m_cmd);
    if (!m_workingDirectory.isEmpty())
        m_process->setWorkingDirectory(m_workingDirectory);
    if (m_env)
        m_process->setEnvironment(*m_env);
    else
        m_process->setEnvironment(m_cmd.executable().deviceEnvironment());

    qCDebug(logStdio) << "Starting:" << m_cmd.toUserOutput();
    m_process->start();
}

void AcpStdioTransport::stop()
{
    if (m_process && m_process->isRunning()) {
        m_process->kill();
        m_process->waitForFinished(QDeadlineTimer(3000));
    }
    delete m_process;
    m_process = nullptr;
}

void AcpStdioTransport::sendData(const QByteArray &data)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        emit errorOccurred(tr("Cannot send data to unstarted process %1")
                               .arg(m_cmd.toUserOutput()));
        return;
    }
    m_process->writeRaw(data);
}

void AcpStdioTransport::readOutput()
{
    if (!m_process)
        return;
    parseData(m_process->readAllRawStandardOutput());
}

void AcpStdioTransport::readError()
{
    if (!m_process)
        return;
    const QByteArray stdErr = m_process->readAllRawStandardError();
    qCDebug(logStdio) << "stderr:" << stdErr;
}

} // namespace AcpClient::Internal
