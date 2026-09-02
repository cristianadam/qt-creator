// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dapdataproviders.h"

using namespace Utils;

namespace Debugger::Internal {

ProcessDataProvider::ProcessDataProvider(const ProcessRunData &runData,
                                         const CommandLine &cmd,
                                         QObject *parent)
    : IDataProvider(parent)
    , m_runData(runData)
    , m_cmd(cmd)
{
    connect(&m_process, &Process::started,
            this, &IDataProvider::started);
    connect(&m_process, &Process::done,
            this, &IDataProvider::done);
    connect(&m_process, &Process::readyReadStandardOutput,
            this, &IDataProvider::readyReadStandardOutput);
    connect(&m_process, &Process::readyReadStandardError,
            this, &IDataProvider::readyReadStandardError);
}

ProcessDataProvider::~ProcessDataProvider()
{
    m_process.kill();
    m_process.waitForFinished();
}

ProcessResultData ProcessDataProvider::resultData() const
{
    return m_process.resultData();
}

void ProcessDataProvider::start()
{
    m_process.setProcessMode(ProcessMode::Writer);
    if (m_runData.workingDirectory.isDir())
        m_process.setWorkingDirectory(m_runData.workingDirectory);
    m_process.setEnvironment(m_runData.environment);
    m_process.setCommand(m_cmd);
    m_process.start();
}

bool ProcessDataProvider::isRunning() const
{
    return m_process.isRunning();
}

void ProcessDataProvider::writeRaw(const QByteArray &data)
{
    if (m_process.state() == ProcessState::Running)
        m_process.writeRaw(data);
}

void ProcessDataProvider::kill()
{
    m_process.kill();
}

void ProcessDataProvider::interrupt()
{
    m_process.interrupt();
}

QByteArray ProcessDataProvider::readAllStandardOutput()
{
    return m_process.readAllStandardOutput().toUtf8();
}

QString ProcessDataProvider::readAllStandardError()
{
    return m_process.readAllStandardError();
}

int ProcessDataProvider::exitCode() const
{
    return m_process.exitCode();
}

QString ProcessDataProvider::executable() const
{
    return m_process.commandLine().executable().toUserOutput();
}

QProcess::ExitStatus ProcessDataProvider::exitStatus() const
{
    return Utils::toQProcess(m_process.exitStatus());
}

QProcess::ProcessError ProcessDataProvider::error() const
{
    return Utils::toQProcess(m_process.error());
}

ProcessResult ProcessDataProvider::result() const
{
    return m_process.result();
}

QString ProcessDataProvider::exitMessage() const
{
    return m_process.exitMessage();
}

LocalSocketDataProvider::LocalSocketDataProvider(const QString &socketName, QObject *parent)
    : IDataProvider(parent)
    , m_socketName(socketName)
{
    connect(&m_socket, &QLocalSocket::connected,
            this, &IDataProvider::started);
    connect(&m_socket, &QLocalSocket::disconnected,
            this, &IDataProvider::done);
    connect(&m_socket, &QLocalSocket::readyRead,
            this, &IDataProvider::readyReadStandardOutput);
    connect(&m_socket, &QLocalSocket::errorOccurred,
            this, &IDataProvider::readyReadStandardError);
}

LocalSocketDataProvider::~LocalSocketDataProvider()
{
    m_socket.disconnectFromServer();
}

void LocalSocketDataProvider::start()
{
    m_socket.connectToServer(m_socketName, QIODevice::ReadWrite);
}

bool LocalSocketDataProvider::isRunning() const
{
    return m_socket.isOpen();
}

void LocalSocketDataProvider::writeRaw(const QByteArray &data)
{
    if (m_socket.isOpen())
        m_socket.write(data);
}

void LocalSocketDataProvider::kill()
{
    if (m_socket.isOpen()) {
        m_socket.disconnectFromServer();
    } else {
        m_socket.abort();
        emit done();
    }
}

QByteArray LocalSocketDataProvider::readAllStandardOutput()
{
    return m_socket.readAll();
}

QString LocalSocketDataProvider::readAllStandardError()
{
    return {};
}

int LocalSocketDataProvider::exitCode() const
{
    return 0;
}

QString LocalSocketDataProvider::executable() const
{
    return m_socket.serverName();
}

QProcess::ExitStatus LocalSocketDataProvider::exitStatus() const
{
    return QProcess::NormalExit;
}

QProcess::ProcessError LocalSocketDataProvider::error() const
{
    return QProcess::UnknownError;
}

ProcessResult LocalSocketDataProvider::result() const
{
    return ProcessResult::FinishedWithSuccess;
}

QString LocalSocketDataProvider::exitMessage() const
{
    return {};
}

TcpDataProvider::TcpDataProvider(const QString &host, quint16 port, QObject *parent)
    : IDataProvider(parent)
    , m_host(host)
    , m_port(port)
{
    connect(&m_socket, &QTcpSocket::connected,
            this, &IDataProvider::started);
    connect(&m_socket, &QTcpSocket::disconnected,
            this, &IDataProvider::done);
    connect(&m_socket, &QTcpSocket::readyRead,
            this, &IDataProvider::readyReadStandardOutput);
    connect(&m_socket, &QTcpSocket::errorOccurred,
            this, &IDataProvider::readyReadStandardError);
}

TcpDataProvider::~TcpDataProvider()
{
    m_socket.abort();
}

void TcpDataProvider::start()
{
    m_socket.connectToHost(m_host, m_port);
}

bool TcpDataProvider::isRunning() const
{
    return m_socket.isOpen();
}

void TcpDataProvider::writeRaw(const QByteArray &data)
{
    if (m_socket.isOpen())
        m_socket.write(data);
}

void TcpDataProvider::kill()
{
    m_socket.abort();
    emit done();
}

QByteArray TcpDataProvider::readAllStandardOutput()
{
    return m_socket.readAll();
}

QString TcpDataProvider::readAllStandardError()
{
    return m_socket.errorString();
}

int TcpDataProvider::exitCode() const
{
    return 0;
}

QString TcpDataProvider::executable() const
{
    return m_host + ':' + QString::number(m_port);
}

QProcess::ExitStatus TcpDataProvider::exitStatus() const
{
    return QProcess::NormalExit;
}

QProcess::ProcessError TcpDataProvider::error() const
{
    return QProcess::UnknownError;
}

ProcessResult TcpDataProvider::result() const
{
    return ProcessResult::FinishedWithSuccess;
}

QString TcpDataProvider::exitMessage() const
{
    return {};
}

} // namespace Debugger::Internal
