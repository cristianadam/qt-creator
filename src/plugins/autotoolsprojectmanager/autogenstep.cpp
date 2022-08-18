// Copyright  (C) 2016 Openismus GmbH.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "autogenstep.h"

#include "autotoolsprojectconstants.h"

#include <projectexplorer/abstractprocessstep.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/processparameters.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>

#include <utils/aspects.h>

#include <QDateTime>

using namespace ProjectExplorer;
using namespace Utils;

namespace AutotoolsProjectManager {
namespace Internal {

// AutogenStep

/**
 * @brief Implementation of the ProjectExplorer::AbstractProcessStep interface.
 *
 * A autogen step can be configured by selecting the "Projects" button of Qt Creator
 * (in the left hand side menu) and under "Build Settings".
 *
 * It is possible for the user to specify custom arguments.
 */

class AutogenStep final : public AbstractProcessStep
{
    Q_DECLARE_TR_FUNCTIONS(AutotoolsProjectManager::Internal::AutogenStep)

public:
    AutogenStep(BuildStepList *bsl, Id id);

private:
    void doRun() final;

    bool m_runAutogen = false;
};

AutogenStep::AutogenStep(BuildStepList *bsl, Id id) : AbstractProcessStep(bsl, id)
{
    auto arguments = addAspect<StringAspect>();
    arguments->setSettingsKey("AutotoolsProjectManager.AutogenStep.AdditionalArguments");
    arguments->setLabelText(tr("Arguments:"));
    arguments->setDisplayStyle(StringAspect::LineEditDisplay);
    arguments->setHistoryCompleter("AutotoolsPM.History.AutogenStepArgs");

    connect(arguments, &BaseAspect::changed, this, [this] {
        m_runAutogen = true;
    });

    setCommandLineProvider([arguments] {
        return CommandLine(FilePath("./autogen.sh"),
                           arguments->value(),
                           CommandLine::Raw);
    });

    setSummaryUpdater([this] {
        ProcessParameters param;
        setupProcessParameters(&param);
        return param.summary(displayName());
    });
}

void AutogenStep::doRun()
{
    // Check whether we need to run autogen.sh
    const QString projectDir = project()->projectDirectory().toString();
    const QFileInfo configureInfo(projectDir + "/configure");
    const QFileInfo configureAcInfo(projectDir + "/configure.ac");
    const QFileInfo makefileAmInfo(projectDir + "/Makefile.am");

    if (!configureInfo.exists()
        || configureInfo.lastModified() < configureAcInfo.lastModified()
        || configureInfo.lastModified() < makefileAmInfo.lastModified()) {
        m_runAutogen = true;
    }

    if (!m_runAutogen) {
        emit addOutput(tr("Configuration unchanged, skipping autogen step."), BuildStep::OutputFormat::NormalMessage);
        emit finished(true);
        return;
    }

    m_runAutogen = false;
    AbstractProcessStep::doRun();
}

// AutogenStepFactory

/**
 * @brief Implementation of the ProjectExplorer::BuildStepFactory interface.
 *
 * This factory is used to create instances of AutogenStep.
 */

AutogenStepFactory::AutogenStepFactory()
{
    registerStep<AutogenStep>(Constants::AUTOGEN_STEP_ID);
    setDisplayName(AutogenStep::tr("Autogen", "Display name for AutotoolsProjectManager::AutogenStep id."));
    setSupportedProjectType(Constants::AUTOTOOLS_PROJECT_ID);
    setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD);
}

} // Internal
} // AutotoolsProjectManager
