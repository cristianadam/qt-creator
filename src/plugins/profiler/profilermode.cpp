// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "profilermode.h"

#include "profilerrecorder.h"
#include "profilerstarteditor.h"
#include "profilertr.h"
#include "profilertracedocument.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/findplaceholder.h>
#include <coreplugin/icontext.h>
#include <coreplugin/imode.h>
#include <coreplugin/minisplitter.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/navigationwidget.h>
#include <coreplugin/outputpane.h>
#include <coreplugin/rightpane.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/runconfiguration.h>

#include <utils/icon.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>
#include <utils/widgets.h>
#include <utils/theme/theme.h>

#include <QHBoxLayout>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace Profiler::Internal {

const char MODE_PROFILER[]  = "Mode.Profiler";
const char C_PROFILERMODE[] = "Profiler.ProfilerMode";
const int P_MODE_PROFILER   = 84; // Between Debug (85) and Projects (83).

static QPointer<IMode> theProfilerMode;
static ProfilerRecorder *theRecorder = nullptr;

// Laid out like the Edit mode: the traces, and the page that starts one, are
// documents in the shared editor area, so nothing here has to make room for
// them.
class ProfilerModeWidget final : public MiniSplitter
{
public:
    ProfilerModeWidget()
    {
        auto editorArea = new QWidget;
        auto editorLayout = new QVBoxLayout(editorArea);
        editorLayout->setContentsMargins(0, 0, 0, 0);
        editorLayout->setSpacing(0);
        editorLayout->addWidget(createToolBar());
        editorLayout->addWidget(new EditorManagerPlaceHolder);
        editorLayout->addWidget(new FindToolBarPlaceHolder(editorArea));

        auto editorAndRightPane = new MiniSplitter;
        editorAndRightPane->addWidget(editorArea);
        editorAndRightPane->addWidget(new RightPanePlaceHolder(MODE_PROFILER));
        editorAndRightPane->setStretchFactor(0, 1);
        editorAndRightPane->setStretchFactor(1, 0);

        auto outputPane = new OutputPanePlaceHolder(MODE_PROFILER);
        outputPane->setObjectName("ProfilerOutputPanePlaceHolder");
        auto editorAndOutputPane = new MiniSplitter;
        editorAndOutputPane->setOrientation(Qt::Vertical);
        editorAndOutputPane->addWidget(editorAndRightPane);
        editorAndOutputPane->addWidget(outputPane);
        editorAndOutputPane->setStretchFactor(0, 3);
        editorAndOutputPane->setStretchFactor(1, 0);

        addWidget(new NavigationWidgetPlaceHolder(MODE_PROFILER, Side::Left));
        addWidget(editorAndOutputPane);
        setStretchFactor(0, 0);
        setStretchFactor(1, 1);
        setObjectName("ProfilerModeWidget");
        setFocusProxy(editorArea);

        IContext::attach(this, Context(Core::Constants::C_EDITORMANAGER));
    }

private:
    // The mode's own actions, above the documents rather than in one of them.
    // The mode button only reaches the start page from another mode, and the
    // traces' toolbars belong to a trace, so without this there is no way back
    // to the page from here.
    static QWidget *createToolBar()
    {
        QAction *startPage = profilerStartPageAction();
        QTC_ASSERT(startPage, return new StyledBar);
        auto startPageButton = new QToolButton;
        startPageButton->setDefaultAction(startPage);
        startPageButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        StyleHelper::setPanelWidget(startPageButton);

        auto toolBar = new StyledBar;
        auto layout = new QHBoxLayout(toolBar);
        using namespace StyleHelper::SpacingTokens;
        layout->setContentsMargins(PaddingHS, 0, PaddingHS, 0);
        layout->setSpacing(PrimitiveS);
        layout->addWidget(startPageButton);
        layout->addStretch();
        return toolBar;
    }
};

class ProfilerMode final : public IMode
{
public:
    ProfilerMode()
    {
        setObjectName("ProfilerMode");
        setContext(Context(C_PROFILERMODE, Core::Constants::C_NAVIGATION_PANE));
        setDisplayName(Tr::tr("Profile"));
        const Icon flat({{":/profiler/images/mode_profiler_mask.png", Theme::IconsBaseColor}});
        setIcon(Icon::sideBarIcon(flat, flat));
        setPriority(P_MODE_PROFILER);
        setId(MODE_PROFILER);
        setWidgetCreator([] { return new ProfilerModeWidget; });
    }
};

void activateProfilerMode()
{
    ModeManager::activateMode(MODE_PROFILER);
}

ProfilerRecorder *profilerRecorder()
{
    return theRecorder;
}

// Points the backends at what the run button would run, so starting a
// recording needs no configuration in the common case. Values the user has
// edited are left alone (see ProfilerRecorder::seedLaunchTarget).
static void seedLaunchTarget()
{
    RunConfiguration *runConfig = activeRunConfigForActiveProject();
    if (!runConfig)
        return;
    const ProcessRunData runnable = runConfig->runnable();
    theRecorder->seedLaunchTarget(runnable.command, runnable.workingDirectory,
                                  runnable.environment);
}

void setupProfilerMode()
{
    QTC_ASSERT(!theProfilerMode, return);
    theProfilerMode = new ProfilerMode;

    // Owned here rather than by the mode's widget or the start page: a
    // recording has to survive closing either of them.
    theRecorder = new ProfilerRecorder;
    QObject::connect(ProjectManager::instance(), &ProjectManager::startupProjectChanged,
                     theRecorder, &seedLaunchTarget);
    QObject::connect(ProjectExplorerPlugin::instance(),
                     &ProjectExplorerPlugin::runActionsUpdated, theRecorder, &seedLaunchTarget);
    seedLaunchTarget();

    // Entering the mode raises the page that starts a recording, so the mode
    // button is a reliable way back to it however many traces are open. It is
    // only opened when there is no trace to show instead, so recording one does
    // not bring back a page that was deliberately closed.
    QObject::connect(ModeManager::instance(), &ModeManager::currentModeChanged,
                     theProfilerMode, [](Id mode, Id) {
        if (mode == MODE_PROFILER && (isProfilerStartPageOpen() || !hasOpenTrace()))
            openProfilerStartPage();
    });

    // Closing the last trace leaves nothing to look at; offer a recording again.
    // Closing the page itself is left alone, or it could not be closed at all.
    QObject::connect(EditorManager::instance(), &EditorManager::documentClosed,
                     theProfilerMode, [](IDocument *document) {
        if (qobject_cast<ProfilerTraceDocument *>(document)
                && ModeManager::currentModeId() == MODE_PROFILER && !hasOpenTrace()) {
            openProfilerStartPage();
        }
    });
}

void destroyProfilerMode()
{
    delete theRecorder;
    theRecorder = nullptr;
    delete theProfilerMode;
    theProfilerMode = nullptr;
}

} // namespace Profiler::Internal
