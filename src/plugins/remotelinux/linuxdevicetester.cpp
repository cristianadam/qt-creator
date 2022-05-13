/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "linuxdevicetester.h"

#include "remotelinux_constants.h"
#include "rsyncdeploystep.h"

#include <projectexplorer/devicesupport/deviceusedportsgatherer.h>
#include <ssh/sftptransfer.h>
#include <ssh/sshconnection.h>
#include <ssh/sshconnectionmanager.h>
#include <utils/port.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

using namespace ProjectExplorer;
using namespace QSsh;
using namespace Utils;

namespace RemoteLinux {
namespace Internal {
namespace {

enum State { Inactive, Connecting, RunningUname, TestingPorts, TestingSftp, TestingRsync };

} // anonymous namespace

class GenericLinuxDeviceTesterPrivate
{
public:
    IDevice::Ptr device;
    SshConnection *connection = nullptr;
    QtcProcess unameProcess;
    DeviceUsedPortsGatherer portsGatherer;
    SftpTransferPtr sftpTransfer;
    QtcProcess rsyncProcess;
    State state = Inactive;
    bool sftpWorks = false;
};

} // namespace Internal

using namespace Internal;

GenericLinuxDeviceTester::GenericLinuxDeviceTester(QObject *parent)
    : DeviceTester(parent), d(new GenericLinuxDeviceTesterPrivate)
{
    connect(&d->unameProcess, &QtcProcess::done, this,
            &GenericLinuxDeviceTester::handleUnameDone);
    connect(&d->portsGatherer, &DeviceUsedPortsGatherer::error,
            this, &GenericLinuxDeviceTester::handlePortsGathererError);
    connect(&d->portsGatherer, &DeviceUsedPortsGatherer::portListReady,
            this, &GenericLinuxDeviceTester::handlePortsGathererDone);
    connect(&d->rsyncProcess, &QtcProcess::done, this,
            &GenericLinuxDeviceTester::handleRsyncDone);
    SshConnectionParameters::setupSshEnvironment(&d->rsyncProcess);
}

GenericLinuxDeviceTester::~GenericLinuxDeviceTester()
{
    if (d->connection)
        SshConnectionManager::releaseConnection(d->connection);
}

void GenericLinuxDeviceTester::testDevice(const IDevice::Ptr &deviceConfiguration)
{
    QTC_ASSERT(d->state == Inactive, return);

    d->device = deviceConfiguration;
    SshConnectionManager::forceNewConnection(deviceConfiguration->sshParameters());
    d->connection = SshConnectionManager::acquireConnection(deviceConfiguration->sshParameters());
    connect(d->connection, &SshConnection::connected,
            this, &GenericLinuxDeviceTester::handleConnected);
    connect(d->connection, &SshConnection::errorOccurred,
            this, &GenericLinuxDeviceTester::handleConnectionFailure);

    emit progressMessage(tr("Connecting to device..."));
    d->state = Connecting;
    d->connection->connectToHost();
}

void GenericLinuxDeviceTester::stopTest()
{
    QTC_ASSERT(d->state != Inactive, return);

    switch (d->state) {
    case Connecting:
        d->connection->disconnectFromHost();
        break;
    case TestingPorts:
        d->portsGatherer.stop();
        break;
    case RunningUname:
        d->unameProcess.close();
        break;
    case TestingSftp:
        d->sftpTransfer->stop();
        break;
    case TestingRsync:
        d->rsyncProcess.close();
        break;
    case Inactive:
        break;
    }

    setFinished(TestFailure);
}

void GenericLinuxDeviceTester::handleConnectionFailure()
{
    QTC_ASSERT(d->state != Inactive, return);

    emit errorMessage(d->connection->errorString() + QLatin1Char('\n'));

    setFinished(TestFailure);
}

void GenericLinuxDeviceTester::handleConnected()
{
    QTC_ASSERT(d->state == Connecting, return);
    emit progressMessage(tr("Connection to device established.") + QLatin1Char('\n'));

    testUname();
}

void GenericLinuxDeviceTester::testUname()
{
    d->state = RunningUname;
    emit progressMessage(tr("Checking kernel version..."));

    d->unameProcess.setCommand({d->device->filePath("uname"), {"-rsm"}});
    d->unameProcess.start();
}

void GenericLinuxDeviceTester::handleUnameDone()
{
    QTC_ASSERT(d->state == RunningUname, return);

    if (!d->unameProcess.errorString().isEmpty() || d->unameProcess.exitCode() != 0) {
        const QByteArray stderrOutput = d->unameProcess.readAllStandardError();
        if (!stderrOutput.isEmpty())
            emit errorMessage(tr("uname failed: %1").arg(QString::fromUtf8(stderrOutput)) + QLatin1Char('\n'));
        else
            emit errorMessage(tr("uname failed.") + QLatin1Char('\n'));
    } else {
        emit progressMessage(QString::fromUtf8(d->unameProcess.readAllStandardOutput()));
    }

    testPortsGatherer();
}

void GenericLinuxDeviceTester::testPortsGatherer()
{
    d->state = TestingPorts;
    emit progressMessage(tr("Checking if specified ports are available..."));

    d->portsGatherer.start(d->device);
}

void GenericLinuxDeviceTester::handlePortsGathererError(const QString &message)
{
    QTC_ASSERT(d->state == TestingPorts, return);

    emit errorMessage(tr("Error gathering ports: %1").arg(message) + QLatin1Char('\n'));
    setFinished(TestFailure);
}

void GenericLinuxDeviceTester::handlePortsGathererDone()
{
    QTC_ASSERT(d->state == TestingPorts, return);

    if (d->portsGatherer.usedPorts().isEmpty()) {
        emit progressMessage(tr("All specified ports are available.") + QLatin1Char('\n'));
    } else {
        const QString portList = transform(d->portsGatherer.usedPorts(), [](const Port &port) {
            return QString::number(port.number());
        }).join(", ");
        emit errorMessage(tr("The following specified ports are currently in use: %1")
            .arg(portList) + QLatin1Char('\n'));
    }

    testSftp();
}

void GenericLinuxDeviceTester::testSftp()
{
    d->state = TestingSftp;
    emit progressMessage(tr("Checking whether an SFTP connection can be set up..."));

    d->sftpTransfer = d->connection->createDownload(FilesToTransfer());
    connect(d->sftpTransfer.get(), &SftpTransfer::done,
            this, &GenericLinuxDeviceTester::handleSftpDone);
    d->sftpTransfer->start();
}

void GenericLinuxDeviceTester::handleSftpDone(const QString &error)
{
    QTC_ASSERT(d->state == TestingSftp, return);

    if (error.isEmpty()) {
        d->sftpWorks = true;
        emit progressMessage(tr("SFTP service available.\n"));
    } else {
        d->sftpWorks = false;
        emit errorMessage(tr("Error setting up SFTP connection: %1\n").arg(error));
    }
    disconnect(d->sftpTransfer.get(), nullptr, this, nullptr);

    testRsync();
}

void GenericLinuxDeviceTester::testRsync()
{
    d->state = TestingRsync;
    emit progressMessage(tr("Checking whether rsync works..."));

    const RsyncCommandLine cmdLine = RsyncDeployStep::rsyncCommand(*d->connection,
                                                                   RsyncDeployStep::defaultFlags());
    const QStringList args = QStringList(cmdLine.options)
            << "-n" << "--exclude=*" << (cmdLine.remoteHostSpec + ":/tmp");
    d->rsyncProcess.setCommand(CommandLine("rsync", args));
    d->rsyncProcess.start();
}

void GenericLinuxDeviceTester::handleRsyncDone()
{
    QTC_ASSERT(d->state == TestingRsync, return);

    QString error;
    if (d->rsyncProcess.error() == QProcess::FailedToStart) {
        error = tr("Failed to start rsync: %1\n").arg(d->rsyncProcess.errorString());
    } else if (d->rsyncProcess.exitStatus() == QProcess::CrashExit) {
        error = tr("rsync crashed.\n");
    } else if (d->rsyncProcess.exitCode() != 0) {
        error = tr("rsync failed with exit code %1: %2\n")
                .arg(d->rsyncProcess.exitCode())
                .arg(QString::fromLocal8Bit(d->rsyncProcess.readAllStandardError()));
    }
    TestResult result = TestSuccess;
    if (!error.isEmpty()) {
        emit errorMessage(error);
        if (d->sftpWorks) {
            emit progressMessage(tr("SFTP will be used for deployment, because rsync "
                                    "is not available.\n"));
        } else {
            emit errorMessage(tr("Deployment to this device will not work out of the box.\n"));
            result = TestFailure;
        }
    } else {
        emit progressMessage(tr("rsync is functional.\n"));
    }

    d->device->setExtraData(Constants::SupportsRSync, error.isEmpty());
    setFinished(result);
}

void GenericLinuxDeviceTester::setFinished(TestResult result)
{
    d->state = Inactive;
    if (d->sftpTransfer) {
        disconnect(d->sftpTransfer.get(), nullptr, this, nullptr);
        d->sftpTransfer.release()->deleteLater();
    }
    if (d->connection) {
        disconnect(d->connection, nullptr, this, nullptr);
        SshConnectionManager::releaseConnection(d->connection);
        d->connection = nullptr;
    }
    emit finished(result);
}

} // namespace RemoteLinux
