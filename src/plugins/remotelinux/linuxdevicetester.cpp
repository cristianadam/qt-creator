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

#include "linuxdevice.h"
#include "remotelinux_constants.h"
#include "rsyncdeploystep.h"

#include <projectexplorer/devicesupport/deviceusedportsgatherer.h>
#include <ssh/sshconnection.h>
#include <ssh/sshconnectionmanager.h>
#include <utils/port.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QDateTime>
#include <QTemporaryDir>

using namespace ProjectExplorer;
using namespace QSsh;
using namespace Utils;

namespace RemoteLinux {
namespace Internal {
namespace {

enum State {
    Inactive,
    Connecting,
    TestingUname,
    TestingPorts,
    TestingSftpUpload,
    TestingSftpDownload,
    TestingRsync
};

} // anonymous namespace

class GenericLinuxDeviceTesterPrivate
{
public:
    GenericLinuxDeviceTesterPrivate(GenericLinuxDeviceTester *tester) : q(tester) { }

    void testSftpTransfer(const FilesToTransfer &files, State newState,
                          std::function<void(const ProcessResultData &)> doneHandler,
                          const QString &progressMessage);

    GenericLinuxDeviceTester *q = nullptr;
    IDevice::Ptr device;
    SshConnection *connection = nullptr;
    QtcProcess unameProcess;
    DeviceUsedPortsGatherer portsGatherer;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QByteArray m_tempFileContents;
    FileToTransfer m_uploadTransfer;
    FileToTransfer m_downloadTransfer;
    std::unique_ptr<FileTransfer> m_fileTransfer;
    QtcProcess rsyncProcess;
    State state = Inactive;
    bool sftpWorks = false;
};

} // namespace Internal

using namespace Internal;

GenericLinuxDeviceTester::GenericLinuxDeviceTester(QObject *parent)
    : DeviceTester(parent), d(new GenericLinuxDeviceTesterPrivate(this))
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
    case TestingUname:
        d->unameProcess.close();
        break;
    case TestingPorts:
        d->portsGatherer.stop();
        break;
    case TestingSftpUpload:
        d->m_fileTransfer->stop();
        d->m_tempDir.reset();
        break;
    case TestingSftpDownload:
        d->m_fileTransfer->stop();
        d->m_tempDir.reset();
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

    emit errorMessage(d->connection->errorString() + '\n');

    setFinished(TestFailure);
}

void GenericLinuxDeviceTester::handleConnected()
{
    QTC_ASSERT(d->state == Connecting, return);
    emit progressMessage(tr("Connection to device established.") + '\n');

    testUname();
}

void GenericLinuxDeviceTester::testUname()
{
    d->state = TestingUname;
    emit progressMessage(tr("Checking kernel version..."));

    d->unameProcess.setCommand({d->device->filePath("uname"), {"-rsm"}});
    d->unameProcess.start();
}

void GenericLinuxDeviceTester::handleUnameDone()
{
    QTC_ASSERT(d->state == TestingUname, return);

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

    emit errorMessage(tr("Error gathering ports: %1").arg(message) + '\n');
    setFinished(TestFailure);
}

void GenericLinuxDeviceTester::handlePortsGathererDone()
{
    QTC_ASSERT(d->state == TestingPorts, return);

    if (d->portsGatherer.usedPorts().isEmpty()) {
        emit progressMessage(tr("All specified ports are available.") + '\n');
    } else {
        const QString portList = transform(d->portsGatherer.usedPorts(), [](const Port &port) {
            return QString::number(port.number());
        }).join(", ");
        emit errorMessage(tr("The following specified ports are currently in use: %1")
            .arg(portList) + QLatin1Char('\n'));
    }

    testSftpInit();
}

void GenericLinuxDeviceTester::testSftpInit()
{
    emit progressMessage(tr("Creating a temporary file for SFTP transfer..."));

    auto tempFileFailed = [this] {
        emit errorMessage(tr("Failed to create a temporary file for SFTP transfer.") + '\n');
        testSftpCleanup();
    };

    d->m_tempDir.reset(new QTemporaryDir);
    if (!d->m_tempDir->isValid()) {
        tempFileFailed();
        return;
    }
    const QString localUploadPath = d->m_tempDir->filePath("forUpload.txt");
    const QString localDownloadPath = d->m_tempDir->filePath("fromDownload.txt");
    const FilePath remotePath = d->device->filePath("/tmp/test_remote.txt");
    QFile tempFile(localUploadPath);
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        tempFileFailed();
        return;
    }
    d->m_tempFileContents.setNum(QDateTime::currentMSecsSinceEpoch());
    tempFile.write(d->m_tempFileContents);
    tempFile.close();
    d->m_uploadTransfer = {FilePath::fromString(localUploadPath), remotePath};
    d->m_downloadTransfer = {remotePath, FilePath::fromString(localDownloadPath)};

    emit progressMessage(tr("Created a temporary file for SFTP transfer.") + '\n');

    testSftpUpload();
}

void GenericLinuxDeviceTesterPrivate::testSftpTransfer(const FilesToTransfer &files,
         State newState, std::function<void(const ProcessResultData &)> doneHandler,
         const QString &progressMessage)
{
    state = newState;
    emit q->progressMessage(progressMessage);

    LinuxDevice::ConstPtr linuxDevice = device.dynamicCast<const LinuxDevice>();
    QTC_ASSERT(linuxDevice, return);
    if (m_fileTransfer)
        m_fileTransfer.release()->deleteLater();
    m_fileTransfer.reset(linuxDevice->createFileTransfer(files));
    QObject::connect(m_fileTransfer.get(), &FileTransfer::done, q, doneHandler);
    QObject::connect(m_fileTransfer.get(), &FileTransfer::progress,
                     q, [this] (const QString &message) {
        QString withoutNewLines = message;
        while (!withoutNewLines.isEmpty() && withoutNewLines.back() == '\n')
            withoutNewLines.chop(1);
        if (!withoutNewLines.isEmpty())
            emit q->progressMessage(withoutNewLines);
    });
    m_fileTransfer->start();
}

void GenericLinuxDeviceTester::testSftpUpload()
{
    d->testSftpTransfer({d->m_uploadTransfer}, TestingSftpUpload,
        std::bind(&GenericLinuxDeviceTester::handleSftpUploadDone, this, std::placeholders::_1),
        tr("Checking whether an SFTP upload works..."));
}

void GenericLinuxDeviceTester::handleSftpUploadDone(const ProcessResultData &resultData)
{
    QTC_ASSERT(d->state == TestingSftpUpload, return);

    if (resultData.m_error == QProcess::UnknownError) {
        emit progressMessage(tr("SFTP upload is functional.") + '\n');
        testSftpDownload();
    } else {
        emit errorMessage(tr("SFTP upload error: %1").arg(resultData.m_errorString) + '\n');
        testSftpCleanup();
    }
}

void GenericLinuxDeviceTester::testSftpDownload()
{
    d->testSftpTransfer({d->m_downloadTransfer}, TestingSftpDownload,
        std::bind(&GenericLinuxDeviceTester::handleSftpDownloadDone, this, std::placeholders::_1),
        tr("Checking whether an SFTP download works..."));
}

void GenericLinuxDeviceTester::handleSftpDownloadDone(const ProcessResultData &resultData)
{
    QTC_ASSERT(d->state == TestingSftpDownload, return);

    if (resultData.m_error == QProcess::UnknownError)
        emit progressMessage(tr("SFTP download is functional.") + '\n');
    else
        emit errorMessage(tr("SFTP download error: %1").arg(resultData.m_errorString) + '\n');

    testSftpCleanup();
}

void GenericLinuxDeviceTester::testSftpTransfer()
{
    emit progressMessage(tr("Checking files transferred via SFTP..."));

    QFile tempFile(d->m_downloadTransfer.m_target.path());
    if (!tempFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorMessage(tr("Cannot open file downloaded via SFTP.") + '\n');
        testSftpCleanup();
        return;
    }
    if (tempFile.readAll() != d->m_tempFileContents) {
        emit errorMessage(tr("The file downloaded via SFTP has unexpected contents.") + '\n');
        testSftpCleanup();
        return;
    }
    if (!d->m_uploadTransfer.m_target.removeFile()) {
        emit errorMessage(tr("Failed to remove the file uploaded via SFTP.") + '\n');
        testSftpCleanup();
        return;
    }

    emit progressMessage(tr("Files successfully transferred via SFTP.") + '\n');
    d->sftpWorks = true;

    testSftpCleanup();
}

void GenericLinuxDeviceTester::testSftpCleanup()
{
    d->m_tempDir.reset();
    if (d->m_fileTransfer)
        d->m_fileTransfer.release()->deleteLater();
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
        error = tr("Failed to start rsync: %1").arg(d->rsyncProcess.errorString()) + '\n';
    } else if (d->rsyncProcess.exitStatus() == QProcess::CrashExit) {
        error = tr("rsync crashed.") + '\n';
    } else if (d->rsyncProcess.exitCode() != 0) {
        error = tr("rsync failed with exit code %1: %2")
                .arg(d->rsyncProcess.exitCode())
                .arg(QString::fromLocal8Bit(d->rsyncProcess.readAllStandardError())) + '\n';
    }
    TestResult result = TestSuccess;
    if (!error.isEmpty()) {
        emit errorMessage(error);
        if (d->sftpWorks) {
            emit progressMessage(tr("SFTP will be used for deployment, because rsync "
                                    "is not available.") + '\n');
        } else {
            emit errorMessage(tr("Deployment to this device will not work out of the box.") + '\n');
            result = TestFailure;
        }
    } else {
        emit progressMessage(tr("rsync is functional.") + '\n');
    }

    d->device->setExtraData(Constants::SupportsRSync, error.isEmpty());
    setFinished(result);
}

void GenericLinuxDeviceTester::setFinished(TestResult result)
{
    d->state = Inactive;
    if (d->connection) {
        disconnect(d->connection, nullptr, this, nullptr);
        SshConnectionManager::releaseConnection(d->connection);
        d->connection = nullptr;
    }
    emit finished(result);
}

} // namespace RemoteLinux
