// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0
#pragma once

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/idevice.h>

#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/processinterface.h>
#include <utils/qtcprocess.h>

namespace Docker::Internal {

class DockerDevicePrivate;

const QString s_pidMarker = "__qtc$$qtc__";

class DockerProcessImpl : public Utils::ProcessInterface
{
public:
    DockerProcessImpl(DockerDevicePrivate *device);
    virtual ~DockerProcessImpl();

private:
    void start() override;
    qint64 write(const QByteArray &data) override;
    void sendControlSignal(Utils::ControlSignal controlSignal) override;

private:
    Utils::CommandLine fullLocalCommandLine(bool interactive);

private:
    DockerDevicePrivate *m_devicePrivate = nullptr;
    // Store the IDevice::ConstPtr in order to extend the lifetime of device for as long
    // as this object is alive.
    ProjectExplorer::IDevice::ConstPtr m_device;

    Utils::QtcProcess m_process;
    qint64 m_remotePID = -1;
    bool m_hasReceivedFirstOutput = false;
};

Utils::CommandLine DockerProcessImpl::fullLocalCommandLine(bool interactive)
{
    QStringList args;

    if (!m_setup.m_workingDirectory.isEmpty()) {
        QTC_CHECK(ProjectExplorer::DeviceManager::deviceForPath(m_setup.m_workingDirectory)
                  == m_device);
        args.append({"cd", m_setup.m_workingDirectory.path()});
        args.append("&&");
    }

    args.append({"echo", s_pidMarker, "&&"});

    const Utils::Environment &env = m_setup.m_environment;
    for (auto it = env.constBegin(); it != env.constEnd(); ++it)
        args.append(env.key(it) + "='" + env.expandedValueForKey(env.key(it)) + '\'');

    args.append("exec");
    args.append({m_setup.m_commandLine.executable().path(), m_setup.m_commandLine.arguments()});

    Utils::CommandLine shCmd("/bin/sh", {"-c", args.join(" ")});
    return m_devicePrivate->q->withDockerExecCmd(shCmd, interactive);
}

DockerProcessImpl::DockerProcessImpl(DockerDevicePrivate *device)
    : m_devicePrivate(device)
    , m_device(device->q->sharedFromThis())
    , m_process(this)
{
    connect(&m_process, &Utils::QtcProcess::started, this, [this] {
        qCDebug(dockerDeviceLog) << "Process started:" << m_process.commandLine();
    });

    connect(&m_process, &Utils::QtcProcess::readyReadStandardOutput, this, [this] {
        if (!m_hasReceivedFirstOutput) {
            QByteArray output = m_process.readAllStandardOutput();
            qsizetype idx = output.indexOf('\n');
            QByteArray firstLine = output.left(idx);
            QByteArray rest = output.mid(idx + 1);
            qCDebug(dockerDeviceLog)
                << "Process first line received:" << m_process.commandLine() << firstLine;
            if (firstLine.startsWith("__qtc")) {
                bool ok = false;
                m_remotePID = firstLine.mid(5, firstLine.size() - 5 - 5).toLongLong(&ok);

                if (ok)
                    emit started(m_remotePID);

                if (rest.size() > 0)
                    emit readyRead(rest, {});

                m_hasReceivedFirstOutput = true;
                return;
            }
        }
        emit readyRead(m_process.readAllStandardOutput(), {});
    });

    connect(&m_process, &Utils::QtcProcess::readyReadStandardError, this, [this] {
        emit readyRead({}, m_process.readAllStandardError());
    });

    connect(&m_process, &Utils::QtcProcess::done, this, [this] {
        qCDebug(dockerDeviceLog) << "Process exited:" << m_process.commandLine()
                                 << "with code:" << m_process.resultData().m_exitCode;
        emit done(m_process.resultData());
    });
}

DockerProcessImpl::~DockerProcessImpl()
{
    if (m_process.state() == QProcess::Running)
        sendControlSignal(Utils::ControlSignal::Kill);
}

void DockerProcessImpl::start()
{
    m_process.setProcessImpl(m_setup.m_processImpl);
    m_process.setProcessMode(m_setup.m_processMode);
    m_process.setTerminalMode(m_setup.m_terminalMode);
    m_process.setReaperTimeout(m_setup.m_reaperTimeout);
    m_process.setWriteData(m_setup.m_writeData);
    m_process.setProcessChannelMode(m_setup.m_processChannelMode);
    m_process.setExtraData(m_setup.m_extraData);
    m_process.setStandardInputFile(m_setup.m_standardInputFile);
    m_process.setAbortOnMetaChars(m_setup.m_abortOnMetaChars);
    if (m_setup.m_lowPriority)
        m_process.setLowPriority();

    m_process.setCommand(fullLocalCommandLine(m_setup.m_processMode == Utils::ProcessMode::Writer));
    m_process.start();
}

qint64 DockerProcessImpl::write(const QByteArray &data)
{
    return m_process.writeRaw(data);
}

void DockerProcessImpl::sendControlSignal(Utils::ControlSignal controlSignal)
{
    int signal = controlSignalToInt(controlSignal);
    m_devicePrivate->runInShell(
        {"kill", {QString("-%1").arg(signal), QString("%2").arg(m_remotePID)}});
}

} // namespace Docker::Internal
