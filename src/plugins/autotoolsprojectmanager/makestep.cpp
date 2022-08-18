// Copyright  (C) 2016 Openismus GmbH.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "makestep.h"
#include "autotoolsprojectconstants.h"

#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/projectexplorerconstants.h>

using namespace AutotoolsProjectManager::Constants;

namespace AutotoolsProjectManager {
namespace Internal {

// MakeStep

class MakeStep : public ProjectExplorer::MakeStep
{
public:
    MakeStep(ProjectExplorer::BuildStepList *bsl, Utils::Id id);
};

MakeStep::MakeStep(ProjectExplorer::BuildStepList *bsl, Utils::Id id)
    : ProjectExplorer::MakeStep(bsl, id)
{
    setAvailableBuildTargets({"all", "clean"});
    if (bsl->id() == ProjectExplorer::Constants::BUILDSTEPS_CLEAN) {
        setSelectedBuildTarget("clean");
        setIgnoreReturnValue(true);
    } else {
        setSelectedBuildTarget("all");
    }
}

// MakeStepFactory

MakeStepFactory::MakeStepFactory()
{
    registerStep<MakeStep>(MAKE_STEP_ID);
    setDisplayName(ProjectExplorer::MakeStep::defaultDisplayName());
    setSupportedProjectType(AUTOTOOLS_PROJECT_ID);
}

} // Internal
} // AutotoolsProjectManager
