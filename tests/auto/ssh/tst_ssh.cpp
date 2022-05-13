/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
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

#include <ssh/sshconnection.h>
#include <ssh/sshsettings.h>

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/launcherinterface.h>
#include <utils/qtcprocess.h>
#include <utils/singleton.h>
#include <utils/temporarydirectory.h>

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QRandomGenerator>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <cstdlib>

using namespace QSsh;
using namespace Utils;

class tst_Ssh : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void errorHandling_data();
    void errorHandling();
    void pristineConnectionObject();
    void remoteProcessInput();
    void sftp();

    void cleanupTestCase();
private:
    bool waitForConnection(SshConnection &connection);
};

void tst_Ssh::initTestCase()
{
    const SshConnectionParameters params = SshTest::getParameters();
    if (!SshTest::checkParameters(params))
        SshTest::printSetupHelp();

    LauncherInterface::setPathToLauncher(qApp->applicationDirPath() + '/'
                                         + QLatin1String(TEST_RELATIVE_LIBEXEC_PATH));
    TemporaryDirectory::setMasterTemporaryDirectory(QDir::tempPath()
                                                    + "/qtc-ssh-autotest-XXXXXX");
}

void tst_Ssh::errorHandling_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<quint16>("port");
    QTest::addColumn<SshConnectionParameters::AuthenticationType>("authType");
    QTest::addColumn<QString>("user");
    QTest::addColumn<QString>("keyFile");

    QTest::newRow("no host")
            << QString("hgdfxgfhgxfhxgfchxgcf") << quint16(12345)
            << SshConnectionParameters::AuthenticationTypeAll  << QString("egal") << QString();
    const QString theHost = SshTest::getHostFromEnvironment();
    if (theHost.isEmpty())
        return;
    const quint16 thePort = SshTest::getPortFromEnvironment();
    QTest::newRow("non-existing key file")
            << theHost << thePort << SshConnectionParameters::AuthenticationTypeSpecificKey
            << QString("root") << QString("somefilenamethatwedontexpecttocontainavalidkey");
}

void tst_Ssh::errorHandling()
{
    if (SshSettings::sshFilePath().isEmpty())
        QSKIP("No ssh found in PATH - skipping this test.");

    QFETCH(QString, host);
    QFETCH(quint16, port);
    QFETCH(SshConnectionParameters::AuthenticationType, authType);
    QFETCH(QString, user);
    QFETCH(QString, keyFile);
    SshConnectionParameters params;
    params.setHost(host);
    params.setPort(port);
    params.setUserName(user);
    params.timeout = 3;
    params.authenticationType = authType;
    params.privateKeyFile = FilePath::fromString(keyFile);
    SshConnection connection(params);
    QEventLoop loop;
    bool disconnected = false;
    QObject::connect(&connection, &SshConnection::connected, &loop, &QEventLoop::quit);
    QObject::connect(&connection, &SshConnection::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&connection, &SshConnection::disconnected,
                     [&disconnected] { disconnected = true; });
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.setSingleShot(true);
    timer.start((params.timeout + 15) * 1000);
    connection.connectToHost();
    loop.exec();
    QVERIFY(timer.isActive());
    const bool expectConnected = !SshSettings::connectionSharingEnabled();
    QCOMPARE(connection.state(), expectConnected ? SshConnection::Connected
                                                 : SshConnection::Unconnected);
    QCOMPARE(connection.errorString().isEmpty(), expectConnected);
    QVERIFY(!disconnected);
}

void tst_Ssh::pristineConnectionObject()
{
    QSsh::SshConnection connection((SshConnectionParameters()));
    QCOMPARE(connection.state(), SshConnection::Unconnected);
    QRegularExpression assertToIgnore(
              "SOFT ASSERT: \"state\\(\\) == Connected\" in file .*[/\\\\]sshconnection.cpp, line \\d*");
    QTest::ignoreMessage(QtDebugMsg, assertToIgnore);
}

void tst_Ssh::remoteProcessInput()
{
    const SshConnectionParameters params = SshTest::getParameters();
    if (!SshTest::checkParameters(params))
        QSKIP("Insufficient setup - set QTC_SSH_TEST_* variables.");
    SshConnection connection(params);
    QVERIFY(waitForConnection(connection));
}

void tst_Ssh::sftp()
{
    // Connect to server
    const SshConnectionParameters params = SshTest::getParameters();
    if (!SshTest::checkParameters(params))
        QSKIP("Insufficient setup - set QTC_SSH_TEST_* variables.");
    SshConnection connection(params);
    QVERIFY(waitForConnection(connection));

    // Create and upload 1000 small files and one big file
    QTemporaryDir dirForFilesToUpload;
    QTemporaryDir dirForFilesToDownload;
    QTemporaryDir dir2ForFilesToDownload;
    QVERIFY2(dirForFilesToUpload.isValid(), qPrintable(dirForFilesToUpload.errorString()));
    QVERIFY2(dirForFilesToDownload.isValid(), qPrintable(dirForFilesToDownload.errorString()));
    QVERIFY2(dir2ForFilesToDownload.isValid(), qPrintable(dirForFilesToDownload.errorString()));
    const QString bigFileName("sftpbigfile");
    QFile bigFile(dirForFilesToUpload.path() + '/' + bigFileName);
    QVERIFY2(bigFile.open(QIODevice::WriteOnly), qPrintable(bigFile.errorString()));
    const int bigFileSize = 100 * 1024 * 1024;
    const int blockSize = 8192;
    const int blockCount = bigFileSize / blockSize;
    for (int block = 0; block < blockCount; ++block) {
        int content[blockSize / sizeof(int)];
        for (size_t j = 0; j < sizeof content / sizeof content[0]; ++j)
            content[j] = QRandomGenerator::global()->generate();
        bigFile.write(reinterpret_cast<char *>(content), sizeof content);
    }
    bigFile.close();
    QVERIFY2(bigFile.error() == QFile::NoError, qPrintable(bigFile.errorString()));
}

void tst_Ssh::cleanupTestCase()
{
    Singleton::deleteAll();
}

bool tst_Ssh::waitForConnection(SshConnection &connection)
{
    QEventLoop loop;
    QObject::connect(&connection, &SshConnection::connected, &loop, &QEventLoop::quit);
    QObject::connect(&connection, &SshConnection::errorOccurred, &loop, &QEventLoop::quit);
    connection.connectToHost();
    loop.exec();
    if (!connection.errorString().isEmpty())
        qDebug() << connection.errorString();
    return connection.state() == SshConnection::Connected && connection.errorString().isEmpty();
}

QTEST_MAIN(tst_Ssh)

#include <tst_ssh.moc>
