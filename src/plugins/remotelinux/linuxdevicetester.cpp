// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "linuxdevicetester.h"

#include "remotelinux_constants.h"
#include "remotelinuxtr.h"

#include <projectexplorer/devicesupport/deviceusedportsgatherer.h>
#include <projectexplorer/devicesupport/filetransfer.h>

#include <utils/algorithm.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

using namespace ProjectExplorer;
using namespace Utils;
using namespace Utils::Tasking;

namespace RemoteLinux {
namespace Internal {

class GenericLinuxDeviceTesterPrivate
{
public:
    GenericLinuxDeviceTesterPrivate(GenericLinuxDeviceTester *tester) : q(tester) {}

    TaskBase echoTask() const;
    TaskBase unameTask() const;
    TaskBase gathererTask() const;
    TaskBase transferTask(FileTransferMethod method);
    TaskBase transferTasks();
    TaskBase commandTask(const QString &commandName) const;
    TaskBase commandTasks() const;

    GenericLinuxDeviceTester *q = nullptr;
    IDevice::Ptr m_device;
    std::unique_ptr<TaskTree> m_taskTree;
    bool m_sftpWorks = false;
};

const QStringList s_commandsToTest = {"base64",
                                      "cat",
                                      "chmod",
                                      "cp",
                                      "cut",
                                      "dd",
                                      "df",
                                      "echo",
                                      "eval",
                                      "exit",
                                      "kill",
                                      "ls",
                                      "mkdir",
                                      "mkfifo",
                                      "mktemp",
                                      "mv",
                                      "printf",
                                      "read",
                                      "readlink",
                                      "rm",
                                      "sed",
                                      "sh",
                                      "shift",
                                      "stat",
                                      "tail",
                                      "test",
                                      "trap",
                                      "touch",
                                      "which"};
// other possible commands (checked for qnx):
// "awk", "grep", "netstat", "print", "pidin", "sleep", "uname"

static const char s_echoContents[] = "Hello Remote World!";

TaskBase GenericLinuxDeviceTesterPrivate::echoTask() const
{
    const auto setup = [this](QtcProcess &process) {
        emit q->progressMessage(Tr::tr("Sending echo to device..."));
        process.setCommand({m_device->filePath("echo"), {s_echoContents}});
    };
    const auto done = [this](const QtcProcess &process) {
        const QString reply = process.cleanedStdOut().chopped(1); // Remove trailing '\n'
        if (reply != s_echoContents)
            emit q->errorMessage(Tr::tr("Device replied to echo with unexpected contents.") + '\n');
        else
            emit q->progressMessage(Tr::tr("Device replied to echo with expected contents.") + '\n');
    };
    const auto error = [this](const QtcProcess &process) {
        const QString stdErrOutput = process.cleanedStdErr();
        if (!stdErrOutput.isEmpty())
            emit q->errorMessage(Tr::tr("echo failed: %1").arg(stdErrOutput) + '\n');
        else
            emit q->errorMessage(Tr::tr("echo failed.") + '\n');
    };
    return Process(setup, done, error);
}

TaskBase GenericLinuxDeviceTesterPrivate::unameTask() const
{
    const auto setup = [this](QtcProcess &process) {
        emit q->progressMessage(Tr::tr("Checking kernel version..."));
        process.setCommand({m_device->filePath("uname"), {"-rsm"}});
    };
    const auto done = [this](const QtcProcess &process) {
        emit q->progressMessage(process.cleanedStdOut());
    };
    const auto error = [this](const QtcProcess &process) {
        const QString stdErrOutput = process.cleanedStdErr();
        if (!stdErrOutput.isEmpty())
            emit q->errorMessage(Tr::tr("uname failed: %1").arg(stdErrOutput) + '\n');
        else
            emit q->errorMessage(Tr::tr("uname failed.") + '\n');
    };
    return Tasking::Task {
        optional,
        Process(setup, done, error)
    };
}

TaskBase GenericLinuxDeviceTesterPrivate::gathererTask() const
{
    const auto setup = [this](DeviceUsedPortsGatherer &gatherer) {
        emit q->progressMessage(Tr::tr("Checking if specified ports are available..."));
        gatherer.setDevice(m_device);
    };
    const auto done = [this](const DeviceUsedPortsGatherer &gatherer) {
        if (gatherer.usedPorts().isEmpty()) {
            emit q->progressMessage(Tr::tr("All specified ports are available.") + '\n');
        } else {
            const QString portList = transform(gatherer.usedPorts(), [](const Port &port) {
                return QString::number(port.number());
            }).join(", ");
            emit q->errorMessage(Tr::tr("The following specified ports are currently in use: %1")
                .arg(portList) + '\n');
        }
    };
    const auto error = [this](const DeviceUsedPortsGatherer &gatherer) {
        emit q->errorMessage(Tr::tr("Error gathering ports: %1").arg(gatherer.errorString()) + '\n');
    };
    return PortGatherer(setup, done, error);
}

TaskBase GenericLinuxDeviceTesterPrivate::transferTask(FileTransferMethod method)
{
    const auto setup = [this, method](FileTransfer &transfer) {
        emit q->progressMessage(Tr::tr("Checking whether \"%1\" works...")
                                .arg(FileTransfer::transferMethodName(method)));
        transfer.setTransferMethod(method);
        transfer.setTestDevice(m_device);
    };
    const auto done = [this, method](const FileTransfer &) {
        const QString methodName = FileTransfer::transferMethodName(method);
        emit q->progressMessage(Tr::tr("\"%1\" is functional.\n").arg(methodName));
        if (method == FileTransferMethod::Rsync)
            m_device->setExtraData(Constants::SupportsRSync, true);
        else
            m_sftpWorks = true;
    };
    const auto error = [this, method](const FileTransfer &transfer) {
        const QString methodName = FileTransfer::transferMethodName(method);
        const ProcessResultData resultData = transfer.resultData();
        QString error;
        if (resultData.m_error == QProcess::FailedToStart) {
            error = Tr::tr("Failed to start \"%1\": %2\n").arg(methodName, resultData.m_errorString);
        } else if (resultData.m_exitStatus == QProcess::CrashExit) {
            error = Tr::tr("\"%1\" crashed.\n").arg(methodName);
        } else if (resultData.m_exitCode != 0) {
            error = Tr::tr("\"%1\" failed with exit code %2: %3\n")
                    .arg(methodName).arg(resultData.m_exitCode).arg(resultData.m_errorString);
        }
        emit q->errorMessage(error);
        if (method == FileTransferMethod::Rsync) {
            m_device->setExtraData(Constants::SupportsRSync, false);
            if (!m_sftpWorks)
                return;
            const QString sftp = FileTransfer::transferMethodName(FileTransferMethod::Sftp);
            const QString rsync = methodName;
            emit q->progressMessage(Tr::tr("\"%1\" will be used for deployment, because \"%2\" "
                                           "is not available.\n").arg(sftp, rsync));
        }
    };
    return Transfer(setup, done, error);
}

TaskBase GenericLinuxDeviceTesterPrivate::transferTasks()
{
    return Tasking::Task {
        continueOnDone,
        transferTask(FileTransferMethod::Sftp),
        transferTask(FileTransferMethod::Rsync),
        OnSubTreeError([this] { emit q->errorMessage(Tr::tr("Deployment to this device will not "
                                                            "work out of the box.\n"));
        })
    };
}

TaskBase GenericLinuxDeviceTesterPrivate::commandTask(const QString &commandName) const
{
    const auto setup = [this, commandName](QtcProcess &process) {
        emit q->progressMessage(Tr::tr("%1...").arg(commandName));
        CommandLine command{m_device->filePath("/bin/sh"), {"-c"}};
        command.addArgs(QLatin1String("\"command -v %1\"").arg(commandName), CommandLine::Raw);
        process.setCommand(command);
    };
    const auto done = [this, commandName](const QtcProcess &) {
        emit q->progressMessage(Tr::tr("%1 found.").arg(commandName));
    };
    const auto error = [this, commandName](const QtcProcess &process) {
        const QString message = process.result() == ProcessResult::StartFailed
                ? Tr::tr("An error occurred while checking for %1.").arg(commandName)
                  + '\n' + process.errorString()
                : Tr::tr("%1 not found.").arg(commandName);
        emit q->errorMessage(message);
    };
    return Process(setup, done, error);
}

TaskBase GenericLinuxDeviceTesterPrivate::commandTasks() const
{
    QList<TaskBase> tasks {continueOnError};
    tasks.append(OnSubTreeSetup([this] {
        emit q->progressMessage(Tr::tr("Checking if required commands are available..."));
    }));
    for (const QString &commandName : s_commandsToTest)
        tasks.append(commandTask(commandName));
    return Tasking::Task {tasks};
}

} // namespace Internal

using namespace Internal;

GenericLinuxDeviceTester::GenericLinuxDeviceTester(QObject *parent)
    : DeviceTester(parent), d(new GenericLinuxDeviceTesterPrivate(this))
{
}

GenericLinuxDeviceTester::~GenericLinuxDeviceTester() = default;

void GenericLinuxDeviceTester::testDevice(const IDevice::Ptr &deviceConfiguration)
{
    QTC_ASSERT(!d->m_taskTree, return);

    d->m_sftpWorks = false;
    d->m_device = deviceConfiguration;

    auto allFinished = [this](DeviceTester::TestResult testResult) {
        emit finished(testResult);
        d->m_taskTree.release()->deleteLater();
    };

    const Tasking::Task root {
        d->echoTask(),
        d->unameTask(),
        d->gathererTask(),
        d->transferTasks(),
        d->commandTasks(),
        OnSubTreeDone(std::bind(allFinished, TestSuccess)),
        OnSubTreeError(std::bind(allFinished, TestFailure))
    };
    d->m_taskTree.reset(new TaskTree(root));
    d->m_taskTree->start();
}

void GenericLinuxDeviceTester::stopTest()
{
    QTC_ASSERT(d->m_taskTree, return);
    d->m_taskTree.reset();
    emit finished(TestFailure);
}

} // namespace RemoteLinux
