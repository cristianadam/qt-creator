// Copyright (C) 2019 Luxoft Sweden AB
// Copyright (C) 2018 Pelagicore AG
// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "appmanagerruncontrol.h"

#include "appmanagerconstants.h"
#include "appmanagertargetinformation.h"
#include "appmanagertr.h"
#include "appmanagerutilities.h"

#include <debugger/debuggerengine.h>
#include <debugger/debuggerrunconfigurationaspect.h>
#include <debugger/debuggerruncontrol.h>
#include <debugger/debuggerkitaspect.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildsystem.h>
#include <projectexplorer/buildtargetinfo.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/target.h>
#include <projectexplorer/toolchain.h>

#include <qmldebug/qmldebugcommandlinearguments.h>

#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtkitaspect.h>

#include <utils/process.h>
#include <utils/qtcassert.h>

using namespace ProjectExplorer;
using namespace Utils;

namespace AppManager::Internal {

// AppManagerRunner

class AppManagerRunner final : public SimpleTargetRunner
{
public:
    AppManagerRunner(RunControl *runControl)
        : SimpleTargetRunner(runControl)
    {
        setId("ApplicationManagerPlugin.Run.TargetRunner");
        connect(this, &RunWorker::stopped, this, [this, runControl] {
            appendMessage(Tr::tr("%1 exited").arg(runControl->runnable().command.toUserOutput()),
                          OutputFormat::NormalMessageFormat);
        });

        setStartModifier([this, runControl] {
            const auto targetInformation = TargetInformation(runControl->target());
            if (!targetInformation.isValid())
                return;

            // Prevent the write channel to be closed, otherwise the appman-controller will exit
            setProcessMode(ProcessMode::Writer);
            setWorkingDirectory(targetInformation.workingDirectory());
            setCommandLine({FilePath::fromString(getToolFilePath(Constants::APPMAN_CONTROLLER, runControl->kit(),
                                                                 targetInformation.device)),
                            {"start-application", "-eio", targetInformation.manifest.id}});
        });
    }
};


// AppManInferiorRunner

class AppManInferiorRunner : public SimpleTargetRunner
{
public:
    AppManInferiorRunner(RunControl *runControl,
                         bool usePerf, bool useGdbServer, bool useQmlServer,
                         QmlDebug::QmlDebugServicesPreset qmlServices)
        : SimpleTargetRunner(runControl),
        m_usePerf(usePerf), m_useGdbServer(useGdbServer), m_useQmlServer(useQmlServer),
        m_qmlServices(qmlServices)
    {
        setId(AppManager::Constants::DEBUG_LAUNCHER_ID);

        connect(&m_launcher, &Process::started, this, &RunWorker::reportStarted);
        connect(&m_launcher, &Process::done, this, &RunWorker::reportStopped);

        connect(&m_launcher, &Process::readyReadStandardOutput, this, [this] {
            appendMessage(m_launcher.readAllStandardOutput(), StdOutFormat);
        });
        connect(&m_launcher, &Process::readyReadStandardError, this, [this] {
            appendMessage(m_launcher.readAllStandardError(), StdErrFormat);
        });

        m_portsGatherer = new Debugger::DebugServerPortsGatherer(runControl);
        m_portsGatherer->setUseGdbServer(useGdbServer || usePerf);
        m_portsGatherer->setUseQmlServer(useQmlServer);
        addStartDependency(m_portsGatherer);

        setStartModifier([this, runControl] {

            const auto targetInformation = TargetInformation(runControl->target());
            if (!targetInformation.isValid()) {
                reportFailure();
                return;
            }

//            const int perfPort = m_portsGatherer->gdbServer().port();
            const int gdbServerPort = m_portsGatherer->gdbServer().port();
            const int qmlServerPort = m_portsGatherer->qmlServer().port();

            CommandLine cmd{FilePath::fromString(getToolFilePath(Constants::APPMAN_CONTROLLER,
                                                                 runControl->kit(),
                                                                 targetInformation.device))};
            cmd.addArg("debug-application");

            if (m_useGdbServer || m_useQmlServer) {
                QStringList debugArgs;
                if (m_useGdbServer) {
                    debugArgs.append(QString("gdbserver :%1").arg(gdbServerPort));
                }
                if (m_useQmlServer) {
                    debugArgs.append(QString("%program% %1 %arguments%")
                                         .arg(qmlDebugCommandLineArguments(m_qmlServices,
                                                                            QString("port:%1").arg(qmlServerPort),
                                                                            true)));
                }
                cmd.addArg(debugArgs.join(' '));
            }
            //FIXME UNTESTED CODE
            if (m_usePerf) {
                Store settingsData = runControl->settingsData("Analyzer.Perf.Settings");
                QVariant perfRecordArgs = settingsData.value("Analyzer.Perf.RecordArguments");
                QString args =  Utils::transform(perfRecordArgs.toStringList(), [](QString arg) {
                                   return arg.replace(',', ",,");
                               }).join(',');
                cmd.addArg(QString("perf record %1 -o - --").arg(args));
            }

            cmd.addArg("-eio");
            cmd.addArg(targetInformation.manifest.id);

            // Prevent the write channel to be closed, otherwise the appman-controller will exit
            setProcessMode(ProcessMode::Writer);
            setCommandLine(cmd);
            setWorkingDirectory(targetInformation.workingDirectory());

            appendMessage(Tr::tr("Starting AppMan Debugging..."), NormalMessageFormat);
            appendMessage(Tr::tr("Using: %1").arg(cmd.toUserOutput()), NormalMessageFormat);
        });
    }

    QUrl perfServer() const { return m_portsGatherer->gdbServer(); }
    QUrl gdbServer() const { return m_portsGatherer->gdbServer(); }
    QUrl qmlServer() const { return m_portsGatherer->qmlServer(); }

private:
    Debugger::DebugServerPortsGatherer *m_portsGatherer = nullptr;
    bool m_usePerf;
    bool m_useGdbServer;
    bool m_useQmlServer;
    QmlDebug::QmlDebugServicesPreset m_qmlServices;
    Process m_launcher;
};


// AppManagerDebugSupport

class AppManagerDebugSupport final : public Debugger::DebuggerRunTool
{
private:
    QString m_symbolFile;
    AppManInferiorRunner *m_debuggee = nullptr;

public:
    AppManagerDebugSupport(RunControl *runControl)
        : DebuggerRunTool(runControl)
    {
        setId("ApplicationManagerPlugin.Debug.Support");

        //setUsePortsGatherer(isCppDebugging(), isQmlDebugging());

        m_debuggee = new AppManInferiorRunner(runControl, false, isCppDebugging(), isQmlDebugging(),
                                              QmlDebug::QmlDebuggerServices);

        addStartDependency(m_debuggee);
        addStopDependency(m_debuggee);

        Target *target = runControl->target();

        const Internal::TargetInformation targetInformation(target);
        if (!targetInformation.isValid())
            return;

        if (targetInformation.manifest.isQmlRuntime()) {
            const Utils::FilePath dir = SysRootKitAspect::sysRoot(target->kit());
            // TODO: get real aspect from deploy configuration
            QString amfolder = Constants::REMOTE_DEFAULT_BIN_PATH;
            m_symbolFile = dir.toString() + amfolder + QDir::separator() + Constants::APPMAN_LAUNCHER_QML;
        } else if (targetInformation.manifest.isNativeRuntime()) {
            m_symbolFile = Utils::findOrDefault(target->buildSystem()->applicationTargets(), [&](const BuildTargetInfo &ti) {
                               return ti.buildKey == targetInformation.manifest.code || ti.projectFilePath.toString() == targetInformation.manifest.code;
                           }).targetFilePath.toString();
        } else {
            reportFailure(Tr::tr("Cannot debug: Only QML and native applications are supported."));
        }
    }

private:
    void start() override
    {
        if (m_symbolFile.isEmpty()) {
            reportFailure(tr("Cannot debug: Local executable is not set."));
            return;
        }

        setStartMode(Debugger::AttachToRemoteServer);
        setCloseMode(Debugger::KillAndExitMonitorAtClose);

        if (isQmlDebugging())
            setQmlServer(m_debuggee->qmlServer());

        if (isCppDebugging()) {
            setUseExtendedRemote(false);
            setUseContinueInsteadOfRun(true);
            setContinueAfterAttach(true);
            setRemoteChannel(m_debuggee->gdbServer());
            setSymbolFile(FilePath::fromString(m_symbolFile));

            QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(runControl()->kit());
            if (version) {
                setSolibSearchPath(version->qtSoPaths());
                addSearchDirectory(version->qmlPath());
            }

            auto sysroot = SysRootKitAspect().sysRoot(runControl()->kit());
            if (sysroot.isEmpty())
                setSysRoot("/");
            else
                setSysRoot(sysroot);
        }

        DebuggerRunTool::start();
    }
};

// AppManagerQmlToolingSupport

class AppManagerQmlToolingSupport final : public RunWorker
{
public:
    explicit AppManagerQmlToolingSupport(RunControl *runControl)
        : RunWorker(runControl)
    {
        setId("AppManagerQmlToolingSupport");

        QmlDebug::QmlDebugServicesPreset services = QmlDebug::servicesForRunMode(runControl->runMode());
        m_runner = new AppManInferiorRunner(runControl, false, false, true, services);
        addStartDependency(m_runner);
        addStopDependency(m_runner);

        m_worker = runControl->createWorker(QmlDebug::runnerIdForRunMode(runControl->runMode()));
        m_worker->addStartDependency(this);
        addStopDependency(m_worker);
    }

private:
    void start() final
    {
        m_worker->recordData("QmlServerUrl", m_runner->qmlServer());
        reportStarted();
    }

    AppManInferiorRunner *m_runner = nullptr;
    RunWorker *m_worker = nullptr;
};


// Factories

class AppManagerRunWorkerFactory final : public RunWorkerFactory
{
public:
    AppManagerRunWorkerFactory()
    {
        setProduct<AppManagerRunner>();
        addSupportedRunMode(ProjectExplorer::Constants::NORMAL_RUN_MODE);
        addSupportedRunConfig(Constants::RUNCONFIGURATION_ID);
        addSupportedRunConfig(Constants::RUNANDDEBUGCONFIGURATION_ID);
    }
};

class AppManagerDebugWorkerFactory final : public RunWorkerFactory
{
public:
    AppManagerDebugWorkerFactory()
    {
        setProduct<AppManagerDebugSupport>();
        addSupportedRunMode(ProjectExplorer::Constants::DEBUG_RUN_MODE);
        addSupportedRunConfig(Constants::RUNANDDEBUGCONFIGURATION_ID);
    }
};

class AppManagerQmlToolingWorkerFactory final : public RunWorkerFactory
{
public:
    AppManagerQmlToolingWorkerFactory()
    {
        setProduct<AppManagerQmlToolingSupport>();
        addSupportedRunMode(ProjectExplorer::Constants::QML_PROFILER_RUN_MODE);
        addSupportedRunMode(ProjectExplorer::Constants::QML_PREVIEW_RUN_MODE);
        addSupportedRunConfig(Constants::RUNANDDEBUGCONFIGURATION_ID);
    }
};

void setupAppManagerRunWorker()
{
    static AppManagerRunWorkerFactory theAppManagerRunWorkerFactory;
}

void setupAppManagerDebugWorker()
{
    static AppManagerDebugWorkerFactory theAppManagerDebugWorkerFactory;
}

void setupAppManagerQmlToolingWorker()
{
    static AppManagerQmlToolingWorkerFactory theAppManagerQmlToolingWorkerFactory;
}

} // AppManager::Internal
