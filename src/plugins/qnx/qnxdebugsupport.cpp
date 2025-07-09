// Copyright (C) 2016 BlackBerry Limited. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qnxdebugsupport.h"

#include "qnxconstants.h"
#include "qnxqtversion.h"
#include "qnxtr.h"
#include "slog2inforunner.h"

#include <coreplugin/icore.h>

#include <debugger/debuggerkitaspect.h>
#include <debugger/debuggerruncontrol.h>
#include <debugger/debuggertr.h>

#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/devicesupport/deviceprocessesdialog.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/kitchooser.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/qmldebugcommandlinearguments.h>
#include <projectexplorer/runconfigurationaspects.h>
#include <projectexplorer/target.h>
#include <projectexplorer/toolchain.h>

#include <solutions/tasking/barrier.h>

#include <qtsupport/qtkitaspect.h>

#include <utils/fileutils.h>
#include <utils/pathchooser.h>
#include <utils/portlist.h>
#include <utils/qtcprocess.h>
#include <utils/processinfo.h>
#include <utils/qtcassert.h>

#include <QDir>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

using namespace Debugger;
using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

namespace Qnx::Internal {

const char QNX_DEBUG_EXECUTABLE[] = "pdebug";

static FilePaths searchPaths(Kit *kit)
{
    auto qtVersion = dynamic_cast<QnxQtVersion *>(QtSupport::QtKitAspect::qtVersion(kit));
    if (!qtVersion)
        return {};

    FilePaths searchPaths = qtVersion->pluginPath().dirEntries(QDir::Dirs | QDir::NoDotAndDotDot);

    searchPaths << qtVersion->libraryPath();
    searchPaths << qtVersion->qnxTarget().pathAppended(qtVersion->cpuDir() + "/lib");
    searchPaths << qtVersion->qnxTarget().pathAppended(qtVersion->cpuDir() + "/usr/lib");

    return searchPaths;
}

// QnxAttachDebugDialog

class QnxAttachDebugDialog : public DeviceProcessesDialog
{
public:
    QnxAttachDebugDialog(KitChooser *kitChooser)
        : DeviceProcessesDialog(kitChooser, Core::ICore::dialogParent())
    {
        auto sourceLabel = new QLabel(Tr::tr("Project source directory:"), this);
        m_projectSource = new PathChooser(this);
        m_projectSource->setExpectedKind(PathChooser::ExistingDirectory);

        auto binaryLabel = new QLabel(Tr::tr("Local executable:"), this);
        m_localExecutable = new PathChooser(this);
        m_localExecutable->setExpectedKind(PathChooser::File);

        auto formLayout = new QFormLayout;
        formLayout->addRow(sourceLabel, m_projectSource);
        formLayout->addRow(binaryLabel, m_localExecutable);

        auto mainLayout = dynamic_cast<QVBoxLayout*>(layout());
        QTC_ASSERT(mainLayout, return);
        mainLayout->insertLayout(mainLayout->count() - 2, formLayout);
    }

    QString projectSource() const { return m_projectSource->filePath().toUrlishString(); }
    FilePath localExecutable() const { return m_localExecutable->filePath(); }

private:
    PathChooser *m_projectSource;
    PathChooser *m_localExecutable;
};

static Group attachToProcessRecipe(RunControl *runControl, const DebuggerRunParameters &rp)
{
    if (!rp.isCppDebugging())
        return debuggerRecipe(runControl, rp);

    const auto modifier = [runControl](Process &process) {
        process.setCommand({QNX_DEBUG_EXECUTABLE, {QString::number(runControl->debugChannel().port())}});
    };
    return When (runControl->processTaskWithModifier(modifier), &Process::started) >> Do {
        debuggerRecipe(runControl, rp)
    };
}

void showAttachToProcessDialog()
{
    auto kitChooser = new KitChooser;
    kitChooser->setKitPredicate([](const Kit *k) {
        return k->isValid() && RunDeviceTypeKitAspect::deviceTypeId(k) == Constants::QNX_QNX_OS_TYPE;
    });

    QnxAttachDebugDialog dlg(kitChooser);
    dlg.addAcceptButton(::Debugger::Tr::tr("&Attach to Process"));
    dlg.showAllDevices();
    if (dlg.exec() == QDialog::Rejected)
        return;

    Kit *kit = kitChooser->currentKit();
    if (!kit)
        return;

    // FIXME: That should be somehow related to the selected kit.
    auto runConfig = activeRunConfigForActiveProject();

    const int pid = dlg.currentProcess().processId;
//    QString projectSourceDirectory = dlg.projectSource();
    FilePath localExecutable = dlg.localExecutable();
    if (localExecutable.isEmpty()) {
        if (auto aspect = runConfig->aspect<SymbolFileAspect>())
            localExecutable = aspect->expandedValue();
        QTC_ASSERT(!localExecutable.isEmpty(), return);
    }

    auto runControl = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
    runControl->copyDataFromRunConfiguration(runConfig);
    runControl->setAttachPid(ProcessHandle(pid));
    DebuggerRunParameters rp = DebuggerRunParameters::fromRunControl(runControl);
    rp.setupPortsGatherer(runControl);
    rp.setUseCtrlCStub(true);

    rp.setStartMode(AttachToRemoteServer);
    rp.setCloseMode(DetachAtClose);
    rp.setSymbolFile(localExecutable);
//    setRunControlName(Tr::tr("Remote: \"%1\" - Process %2").arg(remoteChannel).arg(m_process.pid));
    rp.setDisplayName(Tr::tr("Remote QNX process %1").arg(pid));
    rp.setSolibSearchPath(searchPaths(kit));
    if (auto qtVersion = dynamic_cast<QnxQtVersion *>(QtSupport::QtKitAspect::qtVersion(kit)))
        rp.setSysRoot(qtVersion->qnxTarget());
    rp.setUseContinueInsteadOfRun(true);

    runControl->setRunRecipe(attachToProcessRecipe(runControl, rp));
    runControl->start();
}

// QnxDebugWorkerFactory

class QnxDebugWorkerFactory final : public RunWorkerFactory
{
public:
    QnxDebugWorkerFactory()
    {
        setId("QnxDebugWorkerFactory");
        setRecipeProducer([](RunControl *runControl) {
            runControl->postMessage(Tr::tr("Preparing remote side..."), LogMessageFormat);

            Kit *k = runControl->kit();
            DebuggerRunParameters rp = DebuggerRunParameters::fromRunControl(runControl);
            rp.setupPortsGatherer(runControl);
            rp.setStartMode(AttachToRemoteServer);
            rp.setCloseMode(KillAtClose);
            rp.setUseCtrlCStub(true);
            rp.setSolibSearchPath(searchPaths(k));
            rp.setSkipDebugServer(true);
            if (auto qtVersion = dynamic_cast<QnxQtVersion *>(QtSupport::QtKitAspect::qtVersion(k))) {
                rp.setSysRoot(qtVersion->qnxTarget());
                rp.modifyDebuggerEnvironment(qtVersion->environment());
            }

            const auto modifier = [runControl](Process &process) {
                CommandLine cmd = runControl->commandLine();
                QStringList arguments;
                if (runControl->usesDebugChannel()) {
                    const int pdebugPort = runControl->debugChannel().port();
                    cmd.setExecutable(runControl->device()->filePath(QNX_DEBUG_EXECUTABLE));
                    arguments.append(QString::number(pdebugPort));
                }
                if (runControl->usesQmlChannel()) {
                    arguments.append(qmlDebugTcpArguments(QmlDebuggerServices, runControl->qmlChannel()));
                }
                cmd.setArguments(ProcessArgs::joinArgs(arguments));
                process.setCommand(cmd);
            };

            return Group {
                parallel,
                slog2InfoRecipe(runControl),
                When (runControl->processTaskWithModifier(modifier), &Process::started) >> Do {
                    debuggerRecipe(runControl, rp)
                }
            };
        });
        addSupportedRunMode(ProjectExplorer::Constants::DEBUG_RUN_MODE);
        addSupportedRunConfig(Constants::QNX_RUNCONFIG_ID);
    }
};

void setupQnxDebugging()
{
    static QnxDebugWorkerFactory theQnxDebugWorkerFactory;
}

} // Qnx::Internal
