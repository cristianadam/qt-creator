// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "qdbmakedefaultappstep.h"

#include "qdbconstants.h"

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runconfigurationaspects.h>
#include <projectexplorer/target.h>

#include <remotelinux/abstractremotelinuxdeployservice.h>
#include <remotelinux/abstractremotelinuxdeploystep.h>

#include <utils/commandline.h>
#include <utils/qtcprocess.h>

using namespace ProjectExplorer;
using namespace Utils;

namespace Qdb {
namespace Internal {

// QdbMakeDefaultAppService

class QdbMakeDefaultAppService : public RemoteLinux::AbstractRemoteLinuxDeployService
{
    Q_DECLARE_TR_FUNCTIONS(Qdb::Internal::QdbMakeDefaultAppService)

public:
    QdbMakeDefaultAppService()
    {
        connect(&m_process, &QtcProcess::done, this, [this] {
            if (m_process.error() != QProcess::UnknownError)
                emit errorMessage(tr("Remote process failed: %1").arg(m_process.errorString()));
            else if (m_makeDefault)
                emit progressMessage(tr("Application set as the default one."));
            else
                emit progressMessage(tr("Reset the default application."));

            stopDeployment();
        });
        connect(&m_process, &QtcProcess::readyReadStandardError, this, [this] {
            emit stdErrData(QString::fromUtf8(m_process.readAllStandardError()));
        });
    }

    void setMakeDefault(bool makeDefault)
    {
        m_makeDefault = makeDefault;
    }

private:
    bool isDeploymentNecessary() const final { return true; }

    void doDeploy() final
    {
        QString remoteExe;

        if (RunConfiguration *rc = target()->activeRunConfiguration()) {
            if (auto exeAspect = rc->aspect<ExecutableAspect>())
                remoteExe = exeAspect->executable().toString();
        }

        const QString args = m_makeDefault && !remoteExe.isEmpty()
                ? QStringLiteral("--make-default ") + remoteExe
                : QStringLiteral("--remove-default");
        m_process.setCommand(
                    {deviceConfiguration()->filePath(Constants::AppcontrollerFilepath), {args}});
        m_process.start();
    }

    void stopDeployment() final
    {
        m_process.close();
        handleDeploymentDone();
    }

    bool m_makeDefault = true;
    QtcProcess m_process;
};


// QdbMakeDefaultAppStep

class QdbMakeDefaultAppStep final : public RemoteLinux::AbstractRemoteLinuxDeployStep
{
    Q_DECLARE_TR_FUNCTIONS(Qdb::Internal::QdbMakeDefaultAppStep)

public:
    QdbMakeDefaultAppStep(BuildStepList *bsl, Id id)
        : AbstractRemoteLinuxDeployStep(bsl, id)
    {
        auto service = new QdbMakeDefaultAppService;
        setDeployService(service);

        auto selection = addAspect<SelectionAspect>();
        selection->setSettingsKey("QdbMakeDefaultDeployStep.MakeDefault");
        selection->addOption(tr("Set this application to start by default"));
        selection->addOption(tr("Reset default application"));

        setInternalInitializer([service, selection] {
            service->setMakeDefault(selection->value() == 0);
            return service->isDeploymentPossible();
        });
    }
};


// QdbMakeDefaultAppStepFactory

QdbMakeDefaultAppStepFactory::QdbMakeDefaultAppStepFactory()
{
    registerStep<QdbMakeDefaultAppStep>(Constants::QdbMakeDefaultAppStepId);
    setDisplayName(QdbMakeDefaultAppStep::tr("Change default application"));
    setSupportedDeviceType(Qdb::Constants::QdbLinuxOsType);
    setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_DEPLOY);
}

} // namespace Internal
} // namespace Qdb
