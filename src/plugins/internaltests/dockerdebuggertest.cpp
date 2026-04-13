// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dockerdebuggertest.h"

#include <debugger/debuggerrunconfigurationaspect.h>

#include <docker/dockerdevice.h>

#include <projectexplorer/kit.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>

#include <utils/aspects.h>

#include <QTest>

using namespace Docker;
using namespace ProjectExplorer;
using namespace Utils;

namespace InternalTests::Internal {

// Reproduces the scenario from QTCREATORBUG-34093: automatic QML debugging blocks
// indefinitely when running on a Docker device.  The root cause was that
// DebuggerRunWorkerFactory did not call requestQmlChannel() for Docker devices,
// so no QML channel port was ever allocated and the application waited forever.
class DockerDebuggerTest : public QObject
{
    Q_OBJECT

private:
    // Build AspectContainerData with QML debugging enabled and C++/Python disabled.
    static AspectContainerData makeQmlOnlyAspectData()
    {
        AspectContainerData data;
        data.append(Debugger::DebuggerRunConfigurationAspect::Data::makeForTest(true));
        return data;
    }

private slots:
    void cleanupTestCase()
    {
        for (Kit *k : m_kits)
            KitManager::deregisterKit(k);
        m_kits.clear();
    }

    // Test 1: Docker device + QML debugging -> requestQmlChannel() must be called.
    void testDockerQmlDebuggingRequestsQmlChannel()
    {
        auto device = DockerDevice::create();

        Kit *kit = KitManager::registerKit([](Kit *k) {
            k->setUnexpandedDisplayName("InternalTests_DockerDebuggerTest");
        });
        QVERIFY(kit);
        m_kits.append(kit);

        auto *rc = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
        // setKit() sets d->data.kit and calls setDevice(RunDeviceKitAspect::device(kit)).
        // The kit has no device, so setDevice(nullptr) is called first, then
        // setDeviceForTest() overrides it with the Docker device.
        rc->setKit(kit);
        rc->setDeviceForTest(device);
        rc->setRunConfigIdForTest(ProjectExplorer::Constants::CMAKE_RUNCONFIG_ID);
        rc->setAspectDataForTest(makeQmlOnlyAspectData());

        // createRecipe() finds DebuggerRunWorkerFactory and calls its recipe producer.
        // The producer calls requestQmlChannel() as a side effect when the device
        // is DockerDeviceType and isQmlDebugging() is true.
        rc->createRecipe(ProjectExplorer::Constants::DEBUG_RUN_MODE);

        QVERIFY(rc->usesQmlChannel());
        delete rc;
    }

    // Test 2: Non-Docker device (no device) + QML debugging -> no requestQmlChannel().
    void testNonDockerQmlDebuggingDoesNotRequestQmlChannel()
    {
        Kit *kit = KitManager::registerKit([](Kit *k) {
            k->setUnexpandedDisplayName("InternalTests_NonDockerDebuggerTest");
        });
        QVERIFY(kit);
        m_kits.append(kit);

        auto *rc = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
        rc->setKit(kit);
        // No Docker device -> condition "device->type() == DockerDeviceType" is false.
        rc->setRunConfigIdForTest(ProjectExplorer::Constants::CMAKE_RUNCONFIG_ID);
        rc->setAspectDataForTest(makeQmlOnlyAspectData());
        rc->createRecipe(ProjectExplorer::Constants::DEBUG_RUN_MODE);

        QVERIFY(!rc->usesQmlChannel());
        delete rc;
    }

private:
    QList<Kit *> m_kits;
};

QObject *createDockerDebuggerTest()
{
    return new DockerDebuggerTest;
}

} // namespace InternalTests::Internal

#include "dockerdebuggertest.moc"
