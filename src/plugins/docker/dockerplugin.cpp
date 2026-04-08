// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dockerapi.h"
#include "dockerconstants.h"
#include "dockerdevice.h"

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/qmldebugcommandlinearguments.h>
#include <projectexplorer/runcontrol.h>

#include <extensionsystem/iplugin.h>

#include <utils/fsengine/fsengine.h>

#include <QtTaskTree/QBarrier>

using namespace Core;
using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace Docker::Internal {

class DockerQmlToolingWorkerFactory final : public RunWorkerFactory
{
public:
    DockerQmlToolingWorkerFactory()
    {
        setId("DockerQmlToolingWorkerFactory");
        setPriority(10); // More important then the default 0.
        setRecipeProducer([](RunControl *runControl) {
            // Docker allocates the QML debug port when the container first starts.
            // Call updateContainerAccess() explicitly so that toolControlChannel()
            // returns a valid URL before we build the command line arguments below.
            const auto dockerDevice =
                std::static_pointer_cast<const DockerDevice>(runControl->device());
            if (const Result<> result = dockerDevice->updateContainerAccess(); !result) {
                runControl->postMessage(result.error(), Utils::ErrorMessageFormat);
                return runControl->noRecipeTask();
            }
            // Docker maps the debug port 1:1 (host:PORT -> container:PORT). Read it
            // directly rather than going through requestQmlChannel() /
            // portsGatheringRecipe(), which would pick an unmapped port and trigger a
            // QTC_CHECK in toolControlChannel().
            const QUrl channel =
                dockerDevice->toolControlChannel(IDevice::QmlControlChannel);
            runControl->setQmlChannel(channel);

            const auto modifier = [runControl](Process &process) {
                const QmlDebugServicesPreset services =
                    servicesForRunMode(runControl->runMode());
                CommandLine cmd = runControl->commandLine();
                cmd.addArg(qmlDebugTcpArguments(services, runControl->qmlChannel()));
                process.setCommand(cmd);
            };
            const ProcessTask processTask(runControl->processTaskWithModifier(modifier));
            return Group {
                When (processTask, &Process::started, WorkflowPolicy::StopOnSuccessOrError) >> Do {
                    runControl->createRecipe(runnerIdForRunMode(runControl->runMode()))
                }
            };
        });
        addSupportedRunMode(ProjectExplorer::Constants::QML_PROFILER_RUN_MODE);
        addSupportedRunMode(ProjectExplorer::Constants::QML_PREVIEW_RUN_MODE);
        addSupportedDeviceType(Constants::DOCKER_DEVICE_TYPE);
    }
};

void setupDockerRunAndDebugSupport()
{
    static DockerQmlToolingWorkerFactory qmlToolingWorkerFactory;
}

class DockerPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Docker.json")

public:
    DockerPlugin()
    {
        FSEngine::registerDeviceScheme(Constants::DOCKER_DEVICE_SCHEME);
    }

private:
    ~DockerPlugin() final
    {
        FSEngine::unregisterDeviceScheme(Constants::DOCKER_DEVICE_SCHEME);
        m_deviceFactory->shutdownExistingDevices();
    }

    void initialize() final
    {
        m_deviceFactory = std::make_unique<DockerDeviceFactory>();
        m_dockerApi = std::make_unique<DockerApi>();
        setupDockerRunAndDebugSupport();
    }

    std::unique_ptr<DockerDeviceFactory> m_deviceFactory;
    std::unique_ptr<DockerApi> m_dockerApi;
};

} // Docker::Internal

#include "dockerplugin.moc"
