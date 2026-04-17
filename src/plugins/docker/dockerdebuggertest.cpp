// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dockerdebuggertest.h"

#include <debugger/debuggerrunconfigurationaspect.h>

#include "dockerapi.h"
#include "dockerdevice.h"
#include "dockersettings.h"

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>

#include <utils/aspects.h>
#include <utils/portlist.h>
#include <utils/qtcprocess.h>

#include <QtTaskTree/QTaskTree>

#include <QLocalServer>
#include <QLoggingCategory>
#include <QSignalSpy>
#include <QTest>

using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace Docker::Internal {

// Verifies the design for QTCREATORBUG-34093:
// Docker devices forward QML debug connections via a CmdBridge Unix socket.
// fixupParameters() calls device->prepareQmlDebugging() (which initialises the
// bridge) and then reads the socket URL from toolControlChannel(), so no TCP
// port is ever allocated.  The TCP channel mechanism (requestQmlChannel()) is
// therefore NOT used for Docker devices.

class DockerQmlSocketTest : public QObject
{
    Q_OBJECT

private:
    static AspectContainerData makeQmlOnlyAspectData()
    {
        AspectContainerData data;
        data.append(Debugger::DebuggerRunConfigurationAspect::Data::createQmlTestData());
        return data;
    }

    static AspectContainerData makeCombinedAspectData()
    {
        AspectContainerData data;
        data.append(Debugger::DebuggerRunConfigurationAspect::Data::createCombinedTestData());
        return data;
    }

private slots:
    void cleanupTestCase()
    {
        for (Kit *k : m_kits)
            KitManager::deregisterKit(k);
        m_kits.clear();
    }

    // Docker device + QML-only debugging: socket forwarding flag is set; no TCP
    // channel is requested (the bridge is set up lazily in fixupParameters()).
    void testDockerQmlDebuggingUsesSocketForwarding()
    {
        auto device = DockerDevice::create();
        QVERIFY(device->forwardsQmlDebugSocket());

        Kit *kit = KitManager::registerKit([](Kit *k) {
            k->setUnexpandedDisplayName("Docker_DockerQmlSocketTest");
        });
        QVERIFY(kit);
        m_kits.append(kit);

        auto *rc = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
        rc->setKit(kit);
        rc->setDeviceForTest(device);
        rc->setRunConfigIdForTest(ProjectExplorer::Constants::CMAKE_RUNCONFIG_ID);
        rc->setAspectDataForTest(makeQmlOnlyAspectData());
        rc->createRecipe(ProjectExplorer::Constants::DEBUG_RUN_MODE);

        QVERIFY(!rc->usesQmlChannel());
        delete rc;
    }

    // Docker device + combined C++/QML debugging: same — socket forwarding, no
    // TCP channel.
    void testDockerCombinedDebuggingUsesSocketForwarding()
    {
        auto device = DockerDevice::create();
        QVERIFY(device->forwardsQmlDebugSocket());

        Kit *kit = KitManager::registerKit([](Kit *k) {
            k->setUnexpandedDisplayName("Docker_CombinedQmlSocketTest");
        });
        QVERIFY(kit);
        m_kits.append(kit);

        auto *rc = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
        rc->setKit(kit);
        rc->setDeviceForTest(device);
        rc->setRunConfigIdForTest(ProjectExplorer::Constants::CMAKE_RUNCONFIG_ID);
        rc->setAspectDataForTest(makeCombinedAspectData());
        rc->createRecipe(ProjectExplorer::Constants::DEBUG_RUN_MODE);

        QVERIFY(!rc->usesQmlChannel());
        delete rc;
    }

    // Non-Docker device: no socket forwarding, no TCP channel (the TCP channel
    // is allocated later by the desktop-device path in fixupParameters()).
    void testNonDockerQmlDebuggingDoesNotForwardSocket()
    {
        Kit *kit = KitManager::registerKit([](Kit *k) {
            k->setUnexpandedDisplayName("Docker_NonDockerQmlSocketTest");
        });
        QVERIFY(kit);
        m_kits.append(kit);

        auto *rc = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
        rc->setKit(kit);
        rc->setRunConfigIdForTest(ProjectExplorer::Constants::CMAKE_RUNCONFIG_ID);
        rc->setAspectDataForTest(makeQmlOnlyAspectData());
        rc->createRecipe(ProjectExplorer::Constants::DEBUG_RUN_MODE);

        QVERIFY(!rc->usesQmlChannel());
        delete rc;
    }

private:
    QList<Kit *> m_kits;
};

QObject *createDockerQmlSocketTest()
{
    return new DockerQmlSocketTest;
}

// DockerPortsGatheringTest
//
// QTCREATORBUG-34093, part 1:
// portsGatheringRecipe() reads /proc/net/tcp and /proc/net/tcp6 directly via
// docker exec, bypassing the isReadableDir("/proc/net") probe that returned
// false through the Docker file-access bridge.
//
// Requires a running Docker daemon and alpine:latest; skipped automatically
// when either is unavailable.

class DockerPortsGatheringTest : public QObject
{
    Q_OBJECT

private:
    DockerDevice::Ptr m_device;

private slots:
    void initTestCase()
    {
        if (!DockerApi::instance()->canConnect())
            QSKIP("Docker daemon is not reachable");

        const FilePath dockerBin = settings().dockerBinaryPath.effectiveBinary();
        Process imageCheck;
        imageCheck.setCommand({dockerBin, {"image", "inspect", "alpine:latest",
                                           "--format", "{{.Id}}"}});
        imageCheck.runBlocking();
        if (imageCheck.result() != ProcessResult::FinishedWithSuccess)
            QSKIP("alpine:latest not available; run: docker pull alpine:latest");

        m_device = DockerDevice::create();
        m_device->repo.setValue("alpine");
        m_device->tag.setValue("latest");
        m_device->imageId.setValue(imageCheck.stdOut().trimmed());

        DeviceManager::addDevice(m_device);

        const Result<> r = m_device->updateContainerAccess();
        if (!r)
            QSKIP(qPrintable("Failed to start Docker container: " + r.error()));
    }

    void cleanupTestCase()
    {
        // When the container is killed, the bridge process exits with code -1.
        // The CmdBridgeClient logs expected errors during this forced teardown.
        // Suppress the category for cleanup so they do not pollute test output.
        QLoggingCategory::setFilterRules("qtc.cmdbridge.client=false");
        if (m_device) {
            DeviceManager::removeDevice(m_device->id());
            m_device->shutdown();
        }
        m_device.reset();
        QLoggingCategory::setFilterRules("");
    }

    // Verifies that portsGatheringRecipe() succeeds inside a Docker container.
    void testPortsGatheringRecipeSucceeds()
    {
        m_device->setFreePorts(PortList::fromString("10000-10099"));

        // Capture the result inside the tree: storage data is freed when the tree
        // is destroyed on return from runBlocking(), so it must not be accessed after.
        bool portsValid = false;
        QString portsError;
        const Storage<PortsOutputData> output;
        const auto onDone = [&portsValid, &portsError, output] {
            portsValid = bool(*output);
            if (!portsValid)
                portsError = (*output).error();
        };

        const DoneWith doneWith = QTaskTree::runBlocking(
            Group{output, m_device->portsGatheringRecipe(output), onGroupDone(onDone)});

        QCOMPARE(doneWith, DoneWith::Success);
        QVERIFY2(portsValid, qPrintable(portsError));
    }

    // Verifies that a port actively listened on inside the container is detected.
    void testPortsGatheringRecipeFindsListeningPort()
    {
        constexpr quint16 testPort = 10003;
        m_device->setFreePorts(PortList::fromString("10000-10099"));

        // Start nc in the background and poll /proc/net/tcp until the port
        // appears. The shell returns only once the socket is confirmed bound,
        // so no fixed sleep is needed on the host side.
        const QString portHex = QString("%1").arg(testPort, 4, 16, QChar('0')).toUpper();
        Process plant;
        plant.setCommand(CommandLine{
            m_device->filePath("/bin/sh"),
            {"-c", QString("nc -l -p %1 & "
                           "until grep -q ':%2 ' /proc/net/tcp /proc/net/tcp6 2>/dev/null; "
                           "do :; done").arg(testPort).arg(portHex)}});
        plant.runBlocking();

        QList<Port> foundPorts;
        QString portsError;
        const Storage<PortsOutputData> output;
        const auto onDone = [&foundPorts, &portsError, output] {
            if (*output)
                foundPorts = **output;
            else
                portsError = (*output).error();
        };

        const DoneWith doneWith = QTaskTree::runBlocking(
            Group{output, m_device->portsGatheringRecipe(output), onGroupDone(onDone)});

        QCOMPARE(doneWith, DoneWith::Success);
        QVERIFY2(portsError.isEmpty(), qPrintable(portsError));
        QVERIFY2(foundPorts.contains(Port(testPort)),
                 qPrintable(QString("Port %1 not found in /proc/net/tcp output").arg(testPort)));
    }
};

QObject *createDockerPortsGatheringTest()
{
    return new DockerPortsGatheringTest;
}

// DockerQmlForwardingTest
//
// Verifies end-to-end QML debug socket forwarding: a process inside the
// container connects to the CmdBridge-managed remote Unix socket and the
// connection is forwarded to a QLocalServer on the host.
//
// Requires a running Docker daemon and qt-6-ubuntu-25.10-build (which has
// Python 3); skipped automatically when unavailable.

class DockerQmlForwardingTest : public QObject
{
    Q_OBJECT

private:
    DockerDevice::Ptr m_device;

private slots:
    void initTestCase()
    {
        if (!DockerApi::instance()->canConnect())
            QSKIP("Docker daemon is not reachable");

        const FilePath dockerBin = settings().dockerBinaryPath.effectiveBinary();
        Process imageCheck;
        imageCheck.setCommand({dockerBin, {"image", "inspect", "qt-6-ubuntu-25.10-build:latest",
                                           "--format", "{{.Id}}"}});
        imageCheck.runBlocking();
        if (imageCheck.result() != ProcessResult::FinishedWithSuccess)
            QSKIP("qt-6-ubuntu-25.10-build not available locally");

        m_device = DockerDevice::create();
        m_device->repo.setValue("qt-6-ubuntu-25.10-build");
        m_device->tag.setValue("latest");
        m_device->imageId.setValue(imageCheck.stdOut().trimmed());

        DeviceManager::addDevice(m_device);

        const Result<> r = m_device->updateContainerAccess();
        if (!r)
            QSKIP(qPrintable("Failed to start Docker container: " + r.error()));
    }

    void cleanupTestCase()
    {
        QLoggingCategory::setFilterRules("qtc.cmdbridge.client=false");
        if (m_device) {
            DeviceManager::removeDevice(m_device->id());
            m_device->shutdown();
        }
        m_device.reset();
        QLoggingCategory::setFilterRules("");
    }

    void testQmlSocketForwardingWorks()
    {
        // Trigger createBridgeFileAccess(), which sets m_qmlDebuggerAccess
        // (local socket URL) and m_qmlDebuggerForward (remote socket path).
        const DeviceFileAccessPtr access = m_device->fileAccess();
        QVERIFY2(access, "Failed to obtain file access (CmdBridge not initialized)");

        const QUrl localUrl = m_device->toolControlChannel(IDevice::QmlControlChannel);
        QVERIFY2(!localUrl.path().isEmpty(),
                 "toolControlChannel returned empty path; "
                 "createBridgeFileAccess may not have run or socket forward failed");

        const QString remotePath = m_device->qmlDebugRemoteSocketPath();
        QVERIFY2(!remotePath.isEmpty(),
                 "qmlDebugRemoteSocketPath is empty; socket forward was not established");

        // Start the local server before launching socat so it is ready when
        // LocalSocketForwardImpl forwards the connection.
        QLocalServer server;
        QVERIFY2(server.listen(localUrl.path()),
                 qPrintable("QLocalServer failed to listen at " + localUrl.path()
                            + ": " + server.errorString()));

        QSignalSpy newConnectionSpy(&server, &QLocalServer::newConnection);

        // Connect from inside Docker to the remote socket path. The Python
        // one-liner connects and immediately closes, which is enough to
        // trigger newConnection on the host side.
        Process connectProcess;
        connectProcess.setCommand(
            {m_device->filePath("/usr/bin/python3"),
             {"-c",
              "import socket; s=socket.socket(socket.AF_UNIX);"
              " s.connect('" + remotePath + "'); s.close()"}});
        connectProcess.start();

        QVERIFY2(newConnectionSpy.wait(30000),
                 qPrintable("QLocalServer did not receive a forwarded connection "
                            "within 30 s.\n"
                            "  local socket:  " + localUrl.path() + "\n"
                            "  remote socket: " + remotePath));
    }
};

QObject *createDockerQmlForwardingTest()
{
    return new DockerQmlForwardingTest;
}

} // namespace Docker::Internal

#include "dockerdebuggertest.moc"
