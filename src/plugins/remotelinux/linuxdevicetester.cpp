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

#include <projectexplorer/devicesupport/deviceusedportsgatherer.h>
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
    TestingEcho,
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
    IDevice::Ptr m_device;
    QtcProcess m_echoProcess;
    QtcProcess m_unameProcess;
    DeviceUsedPortsGatherer m_portsGatherer;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QByteArray m_tempFileContents;
    FileToTransfer m_uploadTransfer;
    FileToTransfer m_downloadTransfer;
    FileTransfer m_fileTransfer;
    State m_state = Inactive;
    bool m_sftpWorks = false;
    bool m_rsyncWorks = false;
};

} // namespace Internal

using namespace Internal;

GenericLinuxDeviceTester::GenericLinuxDeviceTester(QObject *parent)
    : DeviceTester(parent), d(new GenericLinuxDeviceTesterPrivate)
{
    connect(&d->m_echoProcess, &QtcProcess::done,
            this, &GenericLinuxDeviceTester::handleEchoDone);
    connect(&d->m_unameProcess, &QtcProcess::done,
            this, &GenericLinuxDeviceTester::handleUnameDone);
    connect(&d->m_portsGatherer, &DeviceUsedPortsGatherer::error,
            this, &GenericLinuxDeviceTester::handlePortsGathererError);
    connect(&d->m_portsGatherer, &DeviceUsedPortsGatherer::portListReady,
            this, &GenericLinuxDeviceTester::handlePortsGathererDone);
    connect(&d->m_fileTransfer, &FileTransfer::done,
            this, &GenericLinuxDeviceTester::handleTransferDone);
    connect(&d->m_fileTransfer, &FileTransfer::progress,
            this, &GenericLinuxDeviceTester::handleStdOut);
}

GenericLinuxDeviceTester::~GenericLinuxDeviceTester() = default;

void GenericLinuxDeviceTester::testDevice(const IDevice::Ptr &deviceConfiguration)
{
    QTC_ASSERT(d->m_state == Inactive, return);

    d->m_device = deviceConfiguration;
    d->m_fileTransfer.setDevice(d->m_device);

    testNext();
}

void GenericLinuxDeviceTester::stopTest()
{
    QTC_ASSERT(d->m_state != Inactive, return);

    switch (d->m_state) {
    case TestingEcho:
        d->m_echoProcess.close();
        break;
    case TestingUname:
        d->m_unameProcess.close();
        break;
    case TestingPorts:
        d->m_portsGatherer.stop();
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
    switch (d->m_state) {
    case Inactive:
        testEcho();
        break;
    case Internal::TestingEcho:
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
    }
}

void GenericLinuxDeviceTester::handleStdOut(const QString &message)
{
    const QStringList messageList = message.split('\n', Qt::SkipEmptyParts);
    for (const QString &msg : messageList)
        emit stdOutMessage(msg);
}

static const char s_echoContents[] = "Hello Remote World!";

void GenericLinuxDeviceTester::testEcho()
{
    d->m_state = TestingEcho;
    emit progressMessage(tr("Sending echo to device..."));

    d->m_echoProcess.setCommand({d->m_device->filePath("echo"), {s_echoContents}});
    d->m_echoProcess.start();
}

void GenericLinuxDeviceTester::handleEchoDone()
{
    QTC_ASSERT(d->m_state == TestingEcho, return);
    if (d->m_echoProcess.result() != ProcessResult::FinishedWithSuccess) {
        const QByteArray stdErrOutput = d->m_echoProcess.readAllStandardError();
        if (!stdErrOutput.isEmpty())
            emit errorMessage(tr("echo failed: %1").arg(QString::fromUtf8(stdErrOutput)) + '\n');
        else
            emit errorMessage(tr("echo failed.") + '\n');
    } else {
        const QString reply = d->m_echoProcess.stdOut().chopped(1); // Remove trailing \n
        handleStdOut(reply);

        if (reply != s_echoContents)
            emit errorMessage(tr("Device replied to echo with unexpected contents.") + '\n');
        else
            emit progressMessage(tr("Device replied to echo with expected contents.") + '\n');
    }

    testNext();
}

void GenericLinuxDeviceTester::testUname()
{
    d->m_state = TestingUname;
    emit progressMessage(tr("Checking kernel version..."));

    d->m_unameProcess.setCommand({d->m_device->filePath("uname"), {"-rsm"}});
    d->m_unameProcess.start();
}

void GenericLinuxDeviceTester::handleUnameDone()
{
    QTC_ASSERT(d->m_state == TestingUname, return);

    if (d->m_unameProcess.result() != ProcessResult::FinishedWithSuccess) {
        const QByteArray stdErrOutput = d->m_unameProcess.readAllStandardError();
        if (!stdErrOutput.isEmpty())
            emit errorMessage(tr("uname failed: %1").arg(QString::fromUtf8(stdErrOutput)) + '\n');
        else
            emit errorMessage(tr("uname failed.") + '\n');
    } else {
        handleStdOut(d->m_unameProcess.stdOut());
        emit progressMessage(tr("Checking kernel version succeeded.") + '\n');
    }

    testNext();
}

void GenericLinuxDeviceTester::testPortsGatherer()
{
    d->m_state = TestingPorts;
    emit progressMessage(tr("Checking if specified ports are available..."));

    d->m_portsGatherer.start(d->m_device);
}

void GenericLinuxDeviceTester::handlePortsGathererError(const QString &message)
{
    QTC_ASSERT(d->m_state == TestingPorts, return);

    emit errorMessage(tr("Error gathering ports: %1").arg(message) + '\n');
    setFinished(TestFailure);
}

void GenericLinuxDeviceTester::handlePortsGathererDone()
{
    QTC_ASSERT(d->m_state == TestingPorts, return);

    if (d->m_portsGatherer.usedPorts().isEmpty()) {
        emit progressMessage(tr("All specified ports are available.") + '\n');
    } else {
        const QString portList = transform(d->m_portsGatherer.usedPorts(), [](const Port &port) {
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
    const FilePath remotePath = d->m_device->filePath("/tmp/test_remote.txt");
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
    switch (d->m_state) {
    case TestingSftpUpload:
    case TestingSftpDownload:
    case TestingRsyncUpload:
    case TestingRsyncDownload:
        break;
    default:
        QTC_ASSERT(false, return);
    }

    const QString method = FileTransfer::methodName(d->m_fileTransfer.transferMethod());
    const QString direction = (d->m_state == TestingSftpUpload || d->m_state == TestingRsyncUpload)
            ? tr("upload") : tr("download");

    if (resultData.m_error == QProcess::UnknownError) {
        emit progressMessage(tr("%1 %2 is functional.").arg(method, direction) + '\n');
        testNext();
    } else {
        emit errorMessage(tr("%1 %2 error: %3").arg(method, direction, resultData.m_errorString)
                          + '\n');
        if (d->m_state == TestingSftpUpload || d->m_state == TestingSftpDownload)
            testRsyncUpload();
        else
            testTransferCleanup();
    }
}

void GenericLinuxDeviceTester::testTransferredFiles()
{
    switch (d->m_state) {
    case TestingSftpDownload:
    case TestingRsyncDownload:
        break;
    default:
        QTC_ASSERT(false, return);
    }

    const QString method = FileTransfer::methodName(d->m_fileTransfer.transferMethod());
    emit progressMessage(tr("Checking transferred files via %1...").arg(method));

    auto nextTest = [this] {
        if (d->m_state == TestingSftpDownload)
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
    if (d->m_state == TestingSftpDownload)
        d->m_sftpWorks = true;
    else
        d->m_rsyncWorks = true;
    nextTest();
}

void GenericLinuxDeviceTester::testTransferCleanup()
{
    d->m_tempDir.reset();
    d->m_fileTransfer.stop();

    if (!d->m_rsyncWorks) {
        if (d->m_sftpWorks) {
            emit progressMessage(tr("SFTP will be used for deployment, because rsync "
                                    "is not available.") + '\n');
        } else {
            emit errorMessage(tr("Deployment to this device will not work out of the box.") + '\n');
        }
    }

    d->m_device->setExtraData(Constants::SupportsRSync, d->m_rsyncWorks);
    setFinished(d->m_rsyncWorks || d->m_sftpWorks ? TestSuccess : TestFailure);
}

void GenericLinuxDeviceTester::testSftpUpload()
{
    d->m_state = TestingSftpUpload;
    emit progressMessage(tr("Checking whether an Sftp upload works..."));

    d->m_fileTransfer.stop();
    d->m_fileTransfer.setFilesToTransfer({d->m_uploadTransfer});
    d->m_fileTransfer.setTransferMethod(FileTransferMethod::Sftp);
    d->m_fileTransfer.start();
}

void GenericLinuxDeviceTester::testSftpDownload()
{
    d->m_state = TestingSftpDownload;
    emit progressMessage(tr("Checking whether an Sftp download works..."));

    d->m_fileTransfer.stop();
    d->m_fileTransfer.setFilesToTransfer({d->m_downloadTransfer});
    d->m_fileTransfer.setTransferMethod(FileTransferMethod::Sftp);
    d->m_fileTransfer.start();
}

void GenericLinuxDeviceTester::testRsyncUpload()
{
    d->m_state = TestingRsyncUpload;
    emit progressMessage(tr("Checking whether an Rsync upload works..."));

    d->m_fileTransfer.stop();
    d->m_fileTransfer.setFilesToTransfer({d->m_uploadTransfer});
    d->m_fileTransfer.setTransferMethod(FileTransferMethod::Rsync);
    d->m_fileTransfer.start();
}

void GenericLinuxDeviceTester::testRsyncDownload()
{
    d->m_state = TestingRsyncDownload;
    emit progressMessage(tr("Checking whether an Rsync download works..."));

    d->m_fileTransfer.stop();
    d->m_fileTransfer.setFilesToTransfer({d->m_downloadTransfer});
    d->m_fileTransfer.setTransferMethod(FileTransferMethod::Rsync);
    d->m_fileTransfer.start();
}

void GenericLinuxDeviceTester::setFinished(TestResult result)
{
    d->m_state = Inactive;
    emit finished(result);
}

} // namespace RemoteLinux
