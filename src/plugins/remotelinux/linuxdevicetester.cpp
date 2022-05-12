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

#include "filetransfer.h"
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
    TestingRsyncUpload,
    TestingRsyncDownload
};

} // anonymous namespace

class GenericLinuxDeviceTesterPrivate
{
public:
    IDevice::Ptr device;
    SshConnection *connection = nullptr;
    QtcProcess unameProcess;
    DeviceUsedPortsGatherer portsGatherer;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QByteArray m_tempFileContents;
    FileToTransfer m_uploadTransfer;
    FileToTransfer m_downloadTransfer;
    FileTransfer m_fileTransfer;
    State state = Inactive;
    bool sftpWorks = false;
    bool rsyncWorks = false;
};

} // namespace Internal

using namespace Internal;

GenericLinuxDeviceTester::GenericLinuxDeviceTester(QObject *parent)
    : DeviceTester(parent), d(new GenericLinuxDeviceTesterPrivate)
{
    connect(&d->unameProcess, &QtcProcess::done,
            this, &GenericLinuxDeviceTester::handleUnameDone);
    connect(&d->portsGatherer, &DeviceUsedPortsGatherer::error,
            this, &GenericLinuxDeviceTester::handlePortsGathererError);
    connect(&d->portsGatherer, &DeviceUsedPortsGatherer::portListReady,
            this, &GenericLinuxDeviceTester::handlePortsGathererDone);
    connect(&d->m_fileTransfer, &FileTransfer::done,
            this, &GenericLinuxDeviceTester::handleTransferDone);
    connect(&d->m_fileTransfer, &FileTransfer::progress, this, [this] (const QString &message) {
        const QStringList messageList = message.split('\n', Qt::SkipEmptyParts);
        for (const QString &msg : messageList)
            emit stdOutMessage(msg);
    });
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
    d->m_fileTransfer.setDevice(d->device);
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
    case TestingSftpDownload:
    case TestingRsyncUpload:
    case TestingRsyncDownload:
        d->m_fileTransfer.stop();
        d->m_tempDir.reset();
        break;
    case Inactive:
        break;
    }

    setFinished(TestFailure);
}

void GenericLinuxDeviceTester::testNext()
{
    switch (d->state) {
    case Connecting:
        testUname();
        break;
    case TestingUname:
        testPortsGatherer();
        break;
    case TestingPorts:
        testTransferInit();
        break;
    case TestingSftpUpload:
        testSftpDownload();
        break;
    case TestingSftpDownload:
        testTransferredFiles();
        break;
    case TestingRsyncUpload:
        testRsyncDownload();
        break;
    case TestingRsyncDownload:
        testTransferredFiles();
        break;
    case Inactive:
        break;
    }
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

    testNext();
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

    testNext();
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

    testNext();
}

void GenericLinuxDeviceTester::testTransferInit()
{
    emit progressMessage(tr("Creating a temporary file for file transfer..."));

    auto tempFileFailed = [this] {
        emit errorMessage(tr("Failed to create a temporary file for file transfer.") + '\n');
        testTransferCleanup();
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

    emit progressMessage(tr("Created a temporary file for file transfer.") + '\n');

    testSftpUpload();
}

void GenericLinuxDeviceTester::handleTransferDone(const ProcessResultData &resultData)
{
    switch (d->state) {
    case TestingSftpUpload:
    case TestingSftpDownload:
    case TestingRsyncUpload:
    case TestingRsyncDownload:
        break;
    default:
        QTC_ASSERT(false, return);
    }

    const QString method = FileTransfer::methodName(d->m_fileTransfer.transferMethod());
    const QString direction = (d->state == TestingSftpUpload || d->state == TestingRsyncUpload)
            ? tr("upload") : tr("download");

    if (resultData.m_error == QProcess::UnknownError) {
        emit progressMessage(tr("%1 %2 is functional.").arg(method, direction) + '\n');
        testNext();
    } else {
        emit errorMessage(tr("%1 %2 error: %3").arg(method, direction, resultData.m_errorString)
                          + '\n');
        if (d->state == TestingSftpUpload || d->state == TestingSftpDownload)
            testRsyncUpload();
        else
            testTransferCleanup();
    }
}

void GenericLinuxDeviceTester::testTransferredFiles()
{
    switch (d->state) {
    case TestingSftpDownload:
    case TestingRsyncDownload:
        break;
    default:
        QTC_ASSERT(false, return);
    }

    const QString method = FileTransfer::methodName(d->m_fileTransfer.transferMethod());
    emit progressMessage(tr("Checking transferred files via %1...").arg(method));

    auto nextTest = [this] {
        if (d->state == TestingSftpDownload)
            testRsyncUpload();
        else
            testTransferCleanup();
    };
    auto testFailed = [this, nextTest] (const QString &message) {
        emit errorMessage(message);
        nextTest();
    };
    QFile tempFile(d->m_downloadTransfer.m_target.path());
    if (!tempFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return testFailed(tr("Cannot open file downloaded via %1.").arg(method) + '\n');
    if (tempFile.readAll() != d->m_tempFileContents)
        return testFailed(tr("The file downloaded via %1 has wrong contents.").arg(method) + '\n');
    if (!d->m_uploadTransfer.m_target.removeFile())
        return testFailed(tr("Failed to remove the file uploaded via %1.").arg(method) + '\n');

    emit progressMessage(tr("Files successfully transferred via %1.").arg(method) + '\n');
    if (d->state == TestingSftpDownload)
        d->sftpWorks = true;
    else
        d->rsyncWorks = true;
    nextTest();
}

void GenericLinuxDeviceTester::testTransferCleanup()
{
    d->m_tempDir.reset();
    d->m_fileTransfer.stop();

    if (!d->rsyncWorks) {
        if (d->sftpWorks) {
            emit progressMessage(tr("SFTP will be used for deployment, because rsync "
                                    "is not available.") + '\n');
        } else {
            emit errorMessage(tr("Deployment to this device will not work out of the box.") + '\n');
        }
    }

    d->device->setExtraData(Constants::SupportsRSync, d->rsyncWorks);
    setFinished(d->rsyncWorks || d->sftpWorks ? TestSuccess : TestFailure);
}

void GenericLinuxDeviceTester::testSftpUpload()
{
    d->state = TestingSftpUpload;
    emit progressMessage(tr("Checking whether an Sftp upload works..."));

    d->m_fileTransfer.stop();
    d->m_fileTransfer.setFilesToTransfer({d->m_uploadTransfer});
    d->m_fileTransfer.setTransferMethod(FileTransferMethod::Sftp);
    d->m_fileTransfer.start();
}

void GenericLinuxDeviceTester::testSftpDownload()
{
    d->state = TestingSftpDownload;
    emit progressMessage(tr("Checking whether an Sftp download works..."));

    d->m_fileTransfer.stop();
    d->m_fileTransfer.setFilesToTransfer({d->m_downloadTransfer});
    d->m_fileTransfer.setTransferMethod(FileTransferMethod::Sftp);
    d->m_fileTransfer.start();
}

void GenericLinuxDeviceTester::testRsyncUpload()
{
    d->state = TestingRsyncUpload;
    emit progressMessage(tr("Checking whether an Rsync upload works..."));

    d->m_fileTransfer.stop();
    d->m_fileTransfer.setFilesToTransfer({d->m_uploadTransfer});
    d->m_fileTransfer.setTransferMethod(FileTransferMethod::Rsync);
    d->m_fileTransfer.start();
}

void GenericLinuxDeviceTester::testRsyncDownload()
{
    d->state = TestingRsyncDownload;
    emit progressMessage(tr("Checking whether an Rsync download works..."));

    d->m_fileTransfer.stop();
    d->m_fileTransfer.setFilesToTransfer({d->m_downloadTransfer});
    d->m_fileTransfer.setTransferMethod(FileTransferMethod::Rsync);
    d->m_fileTransfer.start();
}

void GenericLinuxDeviceTester::setFinished(TestResult result)
{
    d->state = Inactive;
    d->m_fileTransfer.stop();
    if (d->connection) {
        disconnect(d->connection, nullptr, this, nullptr);
        SshConnectionManager::releaseConnection(d->connection);
        d->connection = nullptr;
    }
    emit finished(result);
}

} // namespace RemoteLinux
