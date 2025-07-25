// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangtool.h"

#include "clangselectablefilesdialog.h"
#include "clangtoolrunner.h"
#include "clangtoolscompilationdb.h"
#include "clangtoolsconstants.h"
#include "clangtoolsdiagnosticview.h"
#include "clangtoolsprojectsettings.h"
#include "clangtoolssettings.h"
#include "clangtoolstr.h"
#include "clangtoolsutils.h"
#include "executableinfo.h"
#include "filterdialog.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagebox.h>
#include <coreplugin/progressmanager/taskprogress.h>

#include <cppeditor/cppmodelmanager.h>

#include <debugger/analyzer/analyzerutils.h>
#include <debugger/debuggerconstants.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectexplorericons.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/target.h>
#include <projectexplorer/taskhub.h>
#include <projectexplorer/toolchainkitaspect.h>

#include <solutions/tasking/tasktree.h>

#include <texteditor/textdocument.h>

#include <utils/algorithm.h>
#include <utils/checkablemessagebox.h>
#include <utils/fileutils.h>
#include <utils/infolabel.h>
#include <utils/progressindicator.h>
#include <utils/proxyaction.h>
#include <utils/stringutils.h>
#include <utils/temporarydirectory.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QCheckBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QToolButton>

using namespace Core;
using namespace CppEditor;
using namespace Debugger;
using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

static Q_LOGGING_CATEGORY(LOG, "qtc.clangtools.runcontrol", QtWarningMsg)

namespace ClangTools::Internal {

class ProjectBuilderTaskAdapter final
{
public:
    void operator()(QPointer<RunControl> *task, TaskInterface *iface)
    {
        RunControl *runControl = *task;
        QTC_ASSERT(runControl, iface->reportDone(DoneResult::Error); return);
        BuildConfiguration *bc = runControl->buildConfiguration();
        QTC_ASSERT(bc, iface->reportDone(DoneResult::Error); return);
        QObject::connect(BuildManager::instance(), &BuildManager::buildQueueFinished, iface,
                         [iface](bool success) {
            iface->reportDone(toDoneResult(success));
        }, Qt::SingleShotConnection);
        if (!BuildManager::isBuilding(bc->target())) {
            BuildManager::buildProjectWithDependencies(runControl->project(), ConfigSelection::Active,
                                                       runControl);
        }
    }
};

using ProjectBuilderTask = CustomTask<QPointer<RunControl>, ProjectBuilderTaskAdapter>;

static QDebug operator<<(QDebug debug, const Environment &environment)
{
    for (const QString &entry : environment.toStringList())
        debug << "\n  " << entry;
    return debug;
}

static QDebug operator<<(QDebug debug, const AnalyzeUnits &analyzeUnits)
{
    for (const AnalyzeUnit &unit : analyzeUnits)
        debug << "\n  " << unit.file;
    return debug;
}

static QString makeLink(const QString &text)
{
    return QString("<a href=t>%1</a>").arg(text);
}

class InfoBarWidget : public QFrame
{
    Q_OBJECT

public:
    InfoBarWidget()
        : m_progressIndicator(new ProgressIndicator(ProgressIndicatorSize::Small))
        , m_info(new InfoLabel({}, InfoLabel::Information))
        , m_error(new InfoLabel({}, InfoLabel::Warning))
        , m_diagStats(new QLabel)
    {
        m_info->setElideMode(Qt::ElideNone);
        m_error->setElideMode(Qt::ElideNone);

        m_diagStats->setTextInteractionFlags(Qt::TextBrowserInteraction);

        QHBoxLayout *layout = new QHBoxLayout;
        layout->setContentsMargins(5, 5, 5, 5);
        layout->addWidget(m_progressIndicator);
        layout->addWidget(m_info);
        layout->addWidget(m_error);
        layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum));
        layout->addWidget(m_diagStats);
        setLayout(layout);

        QPalette pal;
        pal.setColor(QPalette::Window, Utils::creatorColor(Theme::InfoBarBackground));
        pal.setColor(QPalette::WindowText, Utils::creatorColor(Theme::InfoBarText));
        setPalette(pal);

        setAutoFillBackground(true);
    }

    // Info
    enum InfoIconType { ProgressIcon, InfoIcon };
    void setInfoIcon(InfoIconType type)
    {
        const bool showProgress = type == ProgressIcon;
        m_progressIndicator->setVisible(showProgress);
        m_info->setType(showProgress ? InfoLabel::None : InfoLabel::Information);
    }
    QString infoText() const { return m_info->text(); }
    void setInfoText(const QString &text)
    {
        m_info->setVisible(!text.isEmpty());
        m_info->setText(text);
        evaluateVisibility();
    }

    // Error
    using OnLinkActivated = std::function<void()>;
    enum IssueType { Warning, Error };

    QString errorText() const { return m_error->text(); }
    void setError(IssueType type,
                  const QString &text,
                  const OnLinkActivated &linkAction = OnLinkActivated())
    {
        m_error->setVisible(!text.isEmpty());
        m_error->setText(text);
        m_error->setType(type == Warning ? InfoLabel::Warning : InfoLabel::Error);
        disconnect(m_error, &QLabel::linkActivated, this, nullptr);
        if (linkAction)
            connect(m_error, &QLabel::linkActivated, this, linkAction);
        evaluateVisibility();
    }

    // Diag stats
    void setDiagText(const QString &text) { m_diagStats->setText(text); }

    void reset()
    {
        setInfoIcon(InfoIcon);
        setInfoText({});
        setError(Warning, {}, {});
        setDiagText({});
    }

    void evaluateVisibility()
    {
        setVisible(!infoText().isEmpty() || !errorText().isEmpty());
    }

private:
    ProgressIndicator *m_progressIndicator;
    InfoLabel *m_info;
    InfoLabel *m_error;
    QLabel *m_diagStats;
};

class SelectFixitsCheckBox : public QCheckBox
{
    Q_OBJECT

private:
    void nextCheckState() final
    {
        setCheckState(checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    }
};

class ApplyFixIts
{
public:
    class RefactoringFileInfo
    {
    public:
        TextEditor::RefactoringFilePtr file;
        QList<DiagnosticItem *> diagnosticItems;
        bool hasScheduledFixits = false;
    };

    ApplyFixIts(const QList<DiagnosticItem *> &diagnosticItems)
    {
        for (DiagnosticItem *diagnosticItem : diagnosticItems) {
            const FilePath &filePath = diagnosticItem->diagnostic().location.targetFilePath;
            QTC_ASSERT(!filePath.isEmpty(), continue);

            // Get or create refactoring file
            RefactoringFileInfo &fileInfo = m_refactoringFileInfos[filePath];
            if (!fileInfo.file)
                fileInfo.file = m_changes.file(filePath);

            // Append item
            fileInfo.diagnosticItems += diagnosticItem;
            if (diagnosticItem->fixItStatus() == FixitStatus::Scheduled)
                fileInfo.hasScheduledFixits = true;
        }
    }

    static bool addChanges(
        const TextEditor::RefactoringFilePtr &file,
        const Diagnostic &diagnostic,
        ChangeSet &changeSet)
    {
        for (const ExplainingStep &step : diagnostic.explainingSteps) {
            if (!step.isFixIt)
                continue;

            const Link start = step.ranges.first();
            const Link end = step.ranges.last();
            const bool operationAdded = changeSet.replace(
                file->position(start.target.line, start.target.column + 1),
                file->position(end.target.line, end.target.column + 1),
                step.message);
            if (!operationAdded)
                return false;
        }

        return true;
    }

    void apply()
    {
        for (auto i = m_refactoringFileInfos.begin(); i != m_refactoringFileInfos.end(); ++i) {
            const RefactoringFileInfo &fileInfo = i.value();

            QVector<DiagnosticItem *> itemsSucceeded;
            QVector<DiagnosticItem *> itemsFailed;
            QVector<DiagnosticItem *> itemsInvalidated;

            ChangeSet changeSet = fileInfo.file->changeSet();
            changeSet.operationList().reserve(fileInfo.diagnosticItems.size());

            // Construct change set
            for (DiagnosticItem *diagnosticItem : fileInfo.diagnosticItems) {
                const FixitStatus fixItStatus = diagnosticItem->fixItStatus();
                if (fixItStatus == FixitStatus::Scheduled) {
                    if (addChanges(fileInfo.file, diagnosticItem->diagnostic(), changeSet))
                        itemsSucceeded += diagnosticItem;
                    else // Ops, some fixits might have overlapping ranges.
                        itemsFailed += diagnosticItem;
                } else if (fileInfo.hasScheduledFixits && fixItStatus == FixitStatus::NotScheduled) {
                    itemsInvalidated += diagnosticItem;
                }
            }
            fileInfo.file->setChangeSet(changeSet);

            // Apply file
            if (!fileInfo.file->apply()) {
                itemsSucceeded.clear();
                itemsFailed = fileInfo.diagnosticItems;
            }

            // Update DiagnosticItem state
            for (DiagnosticItem *diagnosticItem : std::as_const(itemsSucceeded))
                diagnosticItem->setFixItStatus(FixitStatus::Applied, false);
            for (DiagnosticItem *diagnosticItem : std::as_const(itemsFailed))
                diagnosticItem->setFixItStatus(FixitStatus::FailedToApply, false);
            for (DiagnosticItem *diagnosticItem : std::as_const(itemsInvalidated))
                diagnosticItem->setFixItStatus(FixitStatus::Invalidated, false);
        }
    }

private:
    TextEditor::PlainRefactoringFileFactory m_changes;
    QMap<FilePath, RefactoringFileInfo> m_refactoringFileInfos;
};

static FileInfos sortedFileInfos(const CppCodeModelSettings &settings,
                                 const QList<ProjectPart::ConstPtr> &projectParts)
{
    FileInfos fileInfos;

    for (const ProjectPart::ConstPtr &projectPart : projectParts) {
        QTC_ASSERT(projectPart, continue);
        if (!projectPart->selectedForBuilding)
            continue;

        for (const ProjectFile &file : std::as_const(projectPart->files)) {
            QTC_ASSERT(file.kind != ProjectFile::Unclassified, continue);
            QTC_ASSERT(file.kind != ProjectFile::Unsupported, continue);
            if (file.path == CppModelManager::configurationFileName())
                continue;

            if (file.active && (ProjectFile::isSource(file.kind)
                                || ProjectFile::isHeader(file.kind))) {
                ProjectFile::Kind sourceKind = ProjectFile::sourceKind(file.kind);
                fileInfos.emplace_back(file.path, sourceKind, settings, projectPart);
            }
        }
    }

    Utils::sort(fileInfos, [](const FileInfo &fi1, const FileInfo &fi2) {
        if (fi1.file == fi2.file) {
            // If the same file appears more than once, prefer contexts where the file is
            // built as part of an application or library to those where it may not be,
            // e.g. because it is just listed as some sort of resource.
            return fi1.projectPart->buildTargetType != BuildTargetType::Unknown
                    && fi2.projectPart->buildTargetType == BuildTargetType::Unknown;
        }
        return fi1.file < fi2.file;
    });
    fileInfos.erase(std::unique(fileInfos.begin(), fileInfos.end()), fileInfos.end());

    return fileInfos;
}

static RunSettings runSettings()
{
    if (Project *project = ProjectManager::startupProject()) {
        const auto projectSettings = ClangToolsProjectSettings::getSettings(project);
        if (!projectSettings->useGlobalSettings())
            return projectSettings->runSettings();
    }
    return ClangToolsSettings::instance()->runSettings();
}

ClangTool::ClangTool(const QString &name, Id id, ClangToolType type)
    : m_name(name), m_perspective{id.toString(), name}, m_type(type)
{
    setObjectName(name);
    m_diagnosticModel = new ClangToolsDiagnosticModel(type, this);

    auto action = new QAction(Tr::tr("Analyze Project with %1...").arg(name), this);
    action->setIcon(Utils::Icons::RUN_SELECTED_TOOLBAR.icon());
    m_startAction = action;

    action = new QAction(Tr::tr("Analyze Current File with %1").arg(name), this);
    action->setIcon(Utils::Icons::RUN_FILE.icon());
    m_startOnCurrentFileAction = action;

    m_stopAction = Debugger::createStopAction();

    m_diagnosticFilterModel = new DiagnosticFilterModel(this);
    m_diagnosticFilterModel->setSourceModel(m_diagnosticModel);
    m_diagnosticFilterModel->setDynamicSortFilter(true);

    m_infoBarWidget = new InfoBarWidget;

    m_diagnosticView = new DiagnosticView;
    initDiagnosticView();
    m_diagnosticView->setModel(m_diagnosticFilterModel);
    m_diagnosticView->setSortingEnabled(true);
    m_diagnosticView->sortByColumn(DetailedErrorView::DiagnosticColumn, Qt::AscendingOrder);
    connect(m_diagnosticView, &DiagnosticView::showHelp,
            this, &ClangTool::help);
    connect(m_diagnosticView, &DiagnosticView::showFilter,
            this, &ClangTool::filter);
    connect(m_diagnosticView, &DiagnosticView::clearFilter,
            this, &ClangTool::clearFilter);
    connect(m_diagnosticView, &DiagnosticView::filterForCurrentKind,
            this, &ClangTool::filterForCurrentKind);
    connect(m_diagnosticView, &DiagnosticView::filterOutCurrentKind,
            this, &ClangTool::filterOutCurrentKind);

    for (QAbstractItemModel *const model :
         QList<QAbstractItemModel *>({m_diagnosticModel, m_diagnosticFilterModel})) {
        connect(model, &QAbstractItemModel::rowsInserted,
                this, &ClangTool::updateForCurrentState);
        connect(model, &QAbstractItemModel::rowsRemoved,
                this, &ClangTool::updateForCurrentState);
        connect(model, &QAbstractItemModel::modelReset,
                this, &ClangTool::updateForCurrentState);
        connect(model, &QAbstractItemModel::layoutChanged, // For QSortFilterProxyModel::invalidate()
                this, &ClangTool::updateForCurrentState);
    }

    // Go to previous diagnostic
    action = new QAction(this);
    action->setDisabled(true);
    action->setIcon(Utils::Icons::PREV_TOOLBAR.icon());
    action->setToolTip(Tr::tr("Go to previous diagnostic."));
    connect(action, &QAction::triggered, m_diagnosticView, &DetailedErrorView::goBack);
    m_goBack = action;

    // Go to next diagnostic
    action = new QAction(this);
    action->setDisabled(true);
    action->setIcon(Utils::Icons::NEXT_TOOLBAR.icon());
    action->setToolTip(Tr::tr("Go to next diagnostic."));
    connect(action, &QAction::triggered, m_diagnosticView, &DetailedErrorView::goNext);
    m_goNext = action;

    // Load diagnostics from file
    action = new QAction(this);
    action->setIcon(Utils::Icons::OPENFILE_TOOLBAR.icon());
    action->setToolTip(Tr::tr("Load diagnostics from YAML files exported with \"-export-fixes\"."));
    connect(action, &QAction::triggered, this, &ClangTool::loadDiagnosticsFromFiles);
    m_loadExported = action;

    // Clear data
    action = new QAction(this);
    action->setDisabled(true);
    action->setIcon(Utils::Icons::CLEAN_TOOLBAR.icon());
    action->setToolTip(Tr::tr("Clear"));
    connect(action, &QAction::triggered, this, [this] {
        reset();
        update();
    });
    m_clear = action;

    // Expand/Collapse
    action = new QAction(this);
    action->setCheckable(true);
    action->setIcon(Utils::Icons::EXPAND_ALL_TOOLBAR.icon());
    m_expandCollapse = action;
    const auto handleCollapseExpandToggled = [this](bool checked){
        if (checked) {
            m_expandCollapse->setToolTip(Tr::tr("Collapse All"));
            m_diagnosticView->expandAll();
        } else {
            m_expandCollapse->setToolTip(Tr::tr("Expand All"));
            m_diagnosticView->collapseAll();
        }
    };
    connect(action, &QAction::toggled, this, handleCollapseExpandToggled);
    handleCollapseExpandToggled(action->isChecked());

    // Filter button
    action = m_showFilter = new QAction(this);
    action->setIcon(Utils::Icon({{":/utils/images/filtericon.png", Theme::IconsBaseColor}}).icon());
    action->setToolTip(Tr::tr("Filter Diagnostics"));
    action->setCheckable(true);
    connect(action, &QAction::triggered, this, &ClangTool::filter);

    // Schedule/Unschedule all fixits
    m_selectFixitsCheckBox = new SelectFixitsCheckBox;
    m_selectFixitsCheckBox->setText("Select Fixits");
    m_selectFixitsCheckBox->setEnabled(false);
    m_selectFixitsCheckBox->setTristate(true);
    connect(m_selectFixitsCheckBox, &QCheckBox::clicked, this, [this] {
        m_diagnosticView->scheduleAllFixits(m_selectFixitsCheckBox->isChecked());
    });

    // Apply fixits button
    m_applyFixitsButton = new QToolButton;
    m_applyFixitsButton->setText(Tr::tr("Apply Fixits"));
    m_applyFixitsButton->setEnabled(false);

    connect(m_diagnosticModel, &ClangToolsDiagnosticModel::fixitStatusChanged,
            m_diagnosticFilterModel, &DiagnosticFilterModel::onFixitStatusChanged);
    connect(m_diagnosticFilterModel, &DiagnosticFilterModel::fixitCountersChanged, this, [this] {
        const int scheduled = m_diagnosticFilterModel->fixitsScheduled();
        const int schedulable = m_diagnosticFilterModel->fixitsSchedulable();
        m_selectFixitsCheckBox->setEnabled(schedulable > 0);
        m_applyFixitsButton->setEnabled(scheduled > 0);

        if (scheduled == 0)
            m_selectFixitsCheckBox->setCheckState(Qt::Unchecked);
        else if (scheduled == schedulable)
            m_selectFixitsCheckBox->setCheckState(Qt::Checked);
        else
            m_selectFixitsCheckBox->setCheckState(Qt::PartiallyChecked);

        updateForCurrentState();
    });
    connect(m_applyFixitsButton, &QToolButton::clicked, this, [this] {
        QList<DiagnosticItem *> diagnosticItems;
        m_diagnosticModel->forItemsAtLevel<2>([&](DiagnosticItem *item){
            diagnosticItems += item;
        });

        ApplyFixIts(diagnosticItems).apply();
        emit m_diagnosticFilterModel->fixitCountersChanged();
    });

    // Open Project Settings
    action = new QAction(this);
    action->setIcon(Utils::Icons::SETTINGS_TOOLBAR.icon());
    //action->setToolTip(Tr::tr("Open Project Settings")); // TODO: Uncomment in master.
    connect(action, &QAction::triggered, [] {
        ProjectExplorerPlugin::activateProjectPanel(Constants::PROJECT_PANEL_ID);
    });
    m_openProjectSettings = action;

    ActionContainer *menu = ActionManager::actionContainer(Debugger::Constants::M_DEBUG_ANALYZER);
    const QString toolTip = Tr::tr("Clang-Tidy and Clazy use a customized Clang executable from the "
                                   "Clang project to search for diagnostics.");

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(1);
    mainLayout->addWidget(m_infoBarWidget);
    mainLayout->addWidget(m_diagnosticView);
    auto mainWidget = new QWidget;
    mainWidget->setObjectName(id.toString() + "IssuesView");
    mainWidget->setWindowTitle(Tr::tr("Diagnostics"));
    mainWidget->setLayout(mainLayout);

    m_perspective.addWindow(mainWidget, Perspective::SplitVertical, nullptr);

    action = new QAction(name, this);
    action->setToolTip(toolTip);
    menu->addAction(ActionManager::registerAction(action, id),
                    Debugger::Constants::G_ANALYZER_TOOLS);
    QObject::connect(action, &QAction::triggered, this, [this] {
        startTool(FileSelectionType::AskUser);
    });
    QObject::connect(m_startAction, &QAction::triggered, action, &QAction::triggered);
    QObject::connect(m_startAction, &QAction::changed, action, [action, this] {
        action->setEnabled(m_startAction->isEnabled());
    });

    QObject::connect(m_startOnCurrentFileAction, &QAction::triggered, this, [this] {
        startTool(FileSelectionType::CurrentFile);
    });

    m_perspective.addToolBarAction(m_startAction);
    m_perspective.addToolBarAction(ProxyAction::proxyActionWithIcon(
                                       m_startOnCurrentFileAction,
                                       Utils::Icons::RUN_FILE_TOOLBAR.icon()));
    m_perspective.addToolBarAction(m_stopAction);
    m_perspective.addToolBarAction(m_openProjectSettings);
    m_perspective.addToolbarSeparator();
    m_perspective.addToolBarAction(m_loadExported);
    m_perspective.addToolBarAction(m_clear);
    m_perspective.addToolbarSeparator();
    m_perspective.addToolBarAction(m_expandCollapse);
    m_perspective.addToolBarAction(m_goBack);
    m_perspective.addToolBarAction(m_goNext);
    m_perspective.addToolbarSeparator();
    m_perspective.addToolBarAction(m_showFilter);
    m_perspective.addToolBarWidget(m_selectFixitsCheckBox);
    m_perspective.addToolBarWidget(m_applyFixitsButton);
    m_perspective.registerNextPrevShortcuts(m_goNext, m_goBack);

    update();

    connect(ProjectExplorerPlugin::instance(), &ProjectExplorerPlugin::runActionsUpdated,
            this, &ClangTool::update);
    connect(CppModelManager::instance(), &CppModelManager::projectPartsUpdated,
            this, &ClangTool::update);
    connect(ClangToolsSettings::instance(), &ClangToolsSettings::changed,
            this, &ClangTool::update);
}

void ClangTool::selectPerspective()
{
    m_perspective.select();
}

void ClangTool::startTool(FileSelection fileSelection)
{
    const RunSettings theRunSettings = runSettings();
    startTool(fileSelection, theRunSettings, diagnosticConfig(theRunSettings.diagnosticConfigId()));
}

static bool continueDespiteReleaseBuild(const QString &toolName)
{
    const QString wrongMode = Tr::tr("Release");
    const QString title = Tr::tr("Run %1 in %2 Mode?").arg(toolName, wrongMode);
    const QString problem
        = Tr::tr(
              "You are trying to run the tool \"%1\" on an application in %2 mode. The tool is "
              "designed to be used in Debug mode since enabled assertions can reduce the number of "
              "false positives.")
              .arg(toolName, wrongMode);
    const QString question = Tr::tr(
                                 "Do you want to continue and run the tool in %1 mode?")
                                 .arg(wrongMode);
    const QString message = QString("<html><head/><body>"
                                    "<p>%1</p>"
                                    "<p>%2</p>"
                                    "</body></html>")
                                .arg(problem, question);
    return CheckableMessageBox::question(title,
                                         message,
                                         Key("ClangToolsCorrectModeWarning"))
           == QMessageBox::Yes;
}

Group ClangTool::runRecipe(const RunSettings &runSettings,
                           const ClangDiagnosticConfig &diagnosticConfig,
                           const FileInfos &fileInfos,
                           bool buildBeforeAnalysis)
{
    m_filesCount = int(fileInfos.size());

    struct ClangStorage {
        ClangStorage() { m_timer.start(); }
        ~ClangStorage() {
            if (m_elapsedHandler)
                m_elapsedHandler(m_timer.elapsed());
        }
        QElapsedTimer m_timer;
        std::function<void(qint64 elapsedTime)> m_elapsedHandler = {};
    };
    const Storage<ClangStorage> storage;

    std::shared_ptr<TemporaryDirectory> tempDir(new TemporaryDirectory("clangtools-XXXXXX"));
    tempDir->setAutoRemove(qtcEnvironmentVariable("QTC_CLANG_DONT_DELETE_OUTPUT_FILES") != "1");

    BuildConfiguration *buildConfiguration = m_runControl->buildConfiguration();
    QTC_ASSERT(buildConfiguration, return {});
    const Environment environment = buildConfiguration->environment();

    const auto onTopSetup = [this, tempDir] {
        m_runControl->reportStarted();
        if (tempDir->isValid())
            return SetupResult::Continue;
        m_infoBarWidget->setError(InfoBarWidget::Error,
                                  makeLink(Tr::tr("Failed to start the analyzer.")),
                                  [this] { showOutputPane(); });
        m_runControl->postMessage(Tr::tr("Failed to create temporary directory: %1.")
                                      .arg(tempDir->errorString()), ErrorMessageFormat);
        setState(State::PreparationFailed);
        return SetupResult::StopWithError;
    };

    GroupItems topTasks { onGroupSetup(onTopSetup) };

    if (buildBeforeAnalysis) {
        QPointer<RunControl> runControl(m_runControl);
        const auto onSetup = [this, runControl](QPointer<RunControl> &buildRunControl) {
            m_infoBarWidget->setInfoText("Waiting for build to finish...");
            buildRunControl = runControl;
        };
        const auto onError = [this] {
            const QString message(Tr::tr("Failed to build the project."));
            m_infoBarWidget->setError(InfoBarWidget::Error, message, [this] { showOutputPane(); });
            m_runControl->postMessage(message, ErrorMessageFormat);
            setState(State::PreparationFailed);
        };
        topTasks.append(ProjectBuilderTask(onSetup, onError, CallDone::OnError));
    }

    const ProjectInfo::ConstPtr projectInfoBeforeBuild
        = CppModelManager::projectInfo(buildConfiguration->project());

    const auto onTreeSetup = [this, storage, runSettings, diagnosticConfig, fileInfos, tempDir,
                              environment, projectInfoBeforeBuild](TaskTree &taskTree) {
        storage->m_elapsedHandler = [this](qint64 elapsedTime) {
            m_runControl->postMessage(Utils::formatElapsedTime(elapsedTime), NormalMessageFormat);
        };
        auto progress = new TaskProgress(&taskTree);
        progress->setDisplayName(Tr::tr("Analyzing"));

        const QString failedMessage = makeLink(Tr::tr("Failed to start the analyzer."));

        ProjectExplorerPlugin::saveModifiedFiles();

        Project *project = m_runControl->project();
        const ProjectInfo::ConstPtr projectInfo = CppModelManager::projectInfo(project);
        if (!projectInfo) {
            m_infoBarWidget->setError(InfoBarWidget::Error, failedMessage,
                                      [this] { showOutputPane(); });
            m_runControl->postMessage(Tr::tr("No code model data available for project."),
                                      ErrorMessageFormat);
            setState(State::PreparationFailed);
            return SetupResult::StopWithError;
        }

        // Project changed in the mean time?
        if (projectInfo->configurationOrFilesChanged(*projectInfoBeforeBuild)) {
            // If it's more than a release/debug build configuration change, e.g.
            // a version control checkout, files might be not valid C++ anymore
            // or even gone, so better stop here.
            m_infoBarWidget->setError(InfoBarWidget::Error, failedMessage,
                                      [this] { showOutputPane(); });
            m_runControl->postMessage(Tr::tr("The project configuration changed since the start of "
                                             "the %1. Please re-run with current configuration.")
                                          .arg(name()), ErrorMessageFormat);
            setState(State::PreparationFailed);
            return SetupResult::StopWithError;
        }

        setState(State::AnalyzerRunning);

        const FilePath projectFile = projectInfo->projectFilePath();
        m_runControl->postMessage(Tr::tr("Running %1 on %2 with configuration \"%3\".")
            .arg(name(), projectFile.toUserOutput(), diagnosticConfig.displayName()),
            NormalMessageFormat);

        const ClangToolType tool = this == ClangTidyTool::instance() ? ClangToolType::Tidy
                                                                     : ClangToolType::Clazy;
        const FilePath executable = toolExecutable(tool);
        const auto [includeDir, clangVersion] = getClangIncludeDirAndVersion(executable);
        // Collect files
        AnalyzeUnits unitsToProcess;
        for (const FileInfo &fileInfo : fileInfos) {
            if (diagnosticConfig.isEnabled(tool)
                || runSettings.hasConfigFileForSourceFile(fileInfo.file)) {
                unitsToProcess.append({fileInfo, tool});
            }
        }
        qCDebug(LOG) << Q_FUNC_INFO << executable << includeDir << clangVersion;
        qCDebug(LOG) << "Files to process:" << unitsToProcess;
        qCDebug(LOG) << "Environment:" << environment;

        const auto setupHandler = [this, tool](const AnalyzeUnit &unit) {
            const QString filePath = unit.file.toUserOutput();
            m_runControl->postMessage(Tr::tr("Analyzing \"%1\" [%2].")
                                          .arg(filePath, clangToolName(tool)), StdOutFormat);
            return true;
        };
        const auto outputHandler = [this, runSettings](const AnalyzeOutputData &output) {
            if (!output.success) {
                qCDebug(LOG).noquote() << "Clang tool task finished with an error:"
                                       << output.errorMessage << '\n' << output.errorDetails;
                ++m_filesFailed;

                const QString message = Tr::tr("Failed to analyze \"%1\": %2")
                    .arg(output.fileToAnalyze.toUserOutput(), output.errorMessage);
                // TODO: postMessage() instead
                m_runControl->postMessage(message, StdErrFormat);
                m_runControl->postMessage(output.errorDetails, StdErrFormat);
            } else if (!output.errorMessage.isEmpty()) {
                m_runControl->postMessage(output.errorMessage, ErrorMessageFormat);
                m_runControl->postMessage(output.errorDetails, StdErrFormat);
            } else {
                qCDebug(LOG) << "Clang tool task finished with success:"
                             << output.outputFilePath;
                ++m_filesSucceeded;

                const Diagnostics diagnostics = output.diagnostics;
                if (!diagnostics.isEmpty()) {
                    // do not generate marks when we always analyze open files since marks from that
                    // analysis should be more up to date
                    const bool generateMarks = !runSettings.analyzeOpenFiles();
                    onNewDiagnosticsAvailable(diagnostics, generateMarks, RootItemUse::Existing);
                }
            }
            updateForCurrentState();
        };
        const AnalyzeInputData input{tool, runSettings, diagnosticConfig, tempDir->path(),
                                     environment};

        ClangToolsCompilationDb &db
            = ClangToolsCompilationDb::getDb(tool, project->activeBuildConfiguration());
        taskTree.setRecipe(
            {clangToolTask(unitsToProcess, input, setupHandler, outputHandler, db.parentDir())});
        return SetupResult::Continue;
    };

    const auto onTreeDone = [this, buildConfiguration, runSettings] {
        if (m_filesFailed != 0) {
            m_runControl->postMessage(Tr::tr("Error: Failed to analyze %n files.", nullptr,
                                             m_filesFailed), ErrorMessageFormat);
            if (buildConfiguration
                && !buildConfiguration->buildDirectory().exists()
                && !runSettings.buildBeforeAnalysis()) {
                m_runControl->postMessage(
                    Tr::tr("Note: You might need to build the project to generate or update "
                           "source files. To build automatically, enable "
                           "\"Build the project before analysis\"."), NormalMessageFormat);
            }
        }
        m_runControl->postMessage(Tr::tr("%1 finished: Processed %2 files successfully, %3 failed.")
                                      .arg(name()).arg(m_filesSucceeded).arg(m_filesFailed),
                                  NormalMessageFormat);
    };

    topTasks.append(Group {
        storage,
        TaskTreeTask(onTreeSetup, onTreeDone, CallDone::OnSuccess)
    });
    return {topTasks};
}

void ClangTool::startTool(FileSelection fileSelection, const RunSettings &runSettings,
                          const ClangDiagnosticConfig &diagnosticConfig)
{
    Project *project = ProjectManager::startupProject();
    QTC_ASSERT(project, return);
    QTC_ASSERT(project->activeBuildConfiguration(), return);

    ClangToolsCompilationDb &db
        = ClangToolsCompilationDb::getDb(m_type, project->activeBuildConfiguration());
    db.disconnect(this);

    // Continue despite release mode?
    if (BuildConfiguration *bc = project->activeBuildConfiguration()) {
        if (bc->buildType() == BuildConfiguration::Release)
            if (!continueDespiteReleaseBuild(m_name))
                return;
    }

    if (db.generateIfNecessary()) {
        connect(&db, &ClangToolsCompilationDb::generated, this, [=, this](bool success) {
            if (success)
                startTool(fileSelection, runSettings, diagnosticConfig);
        }, Qt::SingleShotConnection);
        return;
    }

    TaskHub::clearTasks(taskCategory());

    // Collect files to analyze
    const FileInfos fileInfos = collectFileInfos(project, fileSelection);
    if (fileInfos.empty())
        return;

    // Run control
    m_runControl = new RunControl(Constants::CLANGTIDYCLAZY_RUN_MODE);
    m_runControl->setDisplayName(m_name);
    m_runControl->setIcon(ProjectExplorer::Icons::ANALYZER_START_SMALL_TOOLBAR);
    m_runControl->setBuildConfiguration(project->activeBuildConfiguration());
    m_stopAction->disconnect();
    connect(m_stopAction, &QAction::triggered, m_runControl, [this] {
        m_runControl->postMessage(Tr::tr("%1 tool stopped by user.").arg(m_name),
                                  NormalMessageFormat);
        m_runControl->initiateStop();
        setState(State::StoppedByUser);
    });
    connect(m_runControl, &RunControl::stopped, this, [this] {
        if (m_state != State::StoppedByUser && m_state != State::PreparationFailed)
            setState(State::AnalyzerFinished);
        emit finished(m_infoBarWidget->errorText());
    });
    connect(m_runControl, &RunControl::aboutToStart, this, [this, project] {
        TaskHub::clearTasks(taskCategory());
        reset();
        m_diagnosticFilterModel->setProject(project);
        m_perspective.select();
        setState(State::PreparationStarted);
    });

    const bool preventBuild = std::holds_alternative<FilePath>(fileSelection)
                              || std::get<FileSelectionType>(fileSelection)
                                     == FileSelectionType::CurrentFile;
    const bool buildBeforeAnalysis = !preventBuild && runSettings.buildBeforeAnalysis();
    m_runControl->setRunRecipe(runRecipe(runSettings, diagnosticConfig, fileInfos,
                                         buildBeforeAnalysis).withCancel(m_runControl->canceler()));
    m_runControl->start();
}

FileInfos ClangTool::collectFileInfos(Project *project, FileSelection fileSelection)
{
    FileSelectionType *selectionType = std::get_if<FileSelectionType>(&fileSelection);
    // early bailout
    if (selectionType && *selectionType == FileSelectionType::CurrentFile
        && !EditorManager::currentDocument()) {
        TaskHub::addTask(
            Task::DisruptingError,
            Tr::tr("Cannot analyze current file: No files open."),
            taskCategory());
        return {};
    }

    const auto projectInfo = CppModelManager::projectInfo(project);
    QTC_ASSERT(projectInfo, return FileInfos());

    const FileInfos allFileInfos = sortedFileInfos(projectInfo->settings(),
                                                   projectInfo->projectParts());

    if (selectionType && *selectionType == FileSelectionType::AllFiles)
        return allFileInfos;

    if (selectionType && *selectionType == FileSelectionType::AskUser) {
        static int initialProviderIndex = 0;
        SelectableFilesDialog dialog(project,
                                     fileInfoProviders(project, allFileInfos),
                                     initialProviderIndex);
        if (dialog.exec() == QDialog::Rejected)
            return FileInfos();
        initialProviderIndex = dialog.currentProviderIndex();
        return dialog.fileInfos();
    }

    const FilePath filePath = std::holds_alternative<FilePath>(fileSelection)
                                  ? std::get<FilePath>(fileSelection)
                                  : EditorManager::currentDocument()->filePath(); // see early bailout
    if (!filePath.isEmpty()) {
        const FileInfo fileInfo = Utils::findOrDefault(allFileInfos, [&](const FileInfo &fi) {
            return fi.file == filePath;
        });
        if (!fileInfo.file.isEmpty())
            return {fileInfo};
        TaskHub::addTask(
            Task::DisruptingError,
            Tr::tr("Cannot analyze current file: \"%1\" is not a known source file.")
                .arg(filePath.toUserOutput()),
            taskCategory());
    }

    return {};
}

const QString &ClangTool::name() const
{
    return m_name;
}

void ClangTool::initDiagnosticView()
{
    m_diagnosticView->setFrameStyle(QFrame::NoFrame);
    m_diagnosticView->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_diagnosticView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_diagnosticView->setAutoScroll(false);
}

void ClangTool::loadDiagnosticsFromFiles()
{
    // Ask user for files
    const FilePaths filePaths = FileUtils::getOpenFilePaths(
        Tr::tr("Select YAML Files with Diagnostics"),
        FileUtils::homePath(),
        Tr::tr("YAML Files (*.yml *.yaml)") + ";;" + DocumentManager::allFilesFilterString());
    if (filePaths.isEmpty())
        return;

    // Load files
    Diagnostics diagnostics;
    QStringList errors;
    for (const FilePath &filePath : filePaths) {
        if (Result<Diagnostics> expectedDiagnostics = readExportedDiagnostics(filePath))
            diagnostics << *expectedDiagnostics;
        else
            errors.append(expectedDiagnostics.error());
    }

    // Show errors
    if (!errors.isEmpty()) {
        AsynchronousMessageBox::critical(Tr::tr("Error Loading Diagnostics"), errors.join('\n'));
        return;
    }

    // Show imported
    reset();

    onNewDiagnosticsAvailable(diagnostics, /*generateMarks =*/ true, RootItemUse::New);
    setState(State::ImportFinished);
}

DiagnosticItem *ClangTool::diagnosticItem(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    TreeItem *item = m_diagnosticModel->itemForIndex(m_diagnosticFilterModel->mapToSource(index));
    if (item->level() == 3)
        item = item->parent();
    if (item->level() == 2)
        return static_cast<DiagnosticItem *>(item);

    return {};
}

void ClangTool::showOutputPane()
{
    m_runControl->showOutputPane();
}

void ClangTool::reset()
{
    m_clear->setEnabled(false);
    m_showFilter->setEnabled(false);
    m_showFilter->setChecked(false);
    m_selectFixitsCheckBox->setEnabled(false);
    m_applyFixitsButton->setEnabled(false);

    m_diagnosticModel->clear();
    m_diagnosticFilterModel->reset();

    m_infoBarWidget->reset();

    m_state = State::Initial;

    m_filesSucceeded = 0;
    m_filesFailed = 0;
}

static bool canAnalyzeProject(Project *project)
{
    const Id c = ProjectExplorer::Constants::C_LANGUAGE_ID;
    const Id cxx = ProjectExplorer::Constants::CXX_LANGUAGE_ID;
    const bool projectSupportsLanguage = project->projectLanguages().contains(c)
                                         || project->projectLanguages().contains(cxx);
    return projectSupportsLanguage
           && CppModelManager::projectInfo(project)
           && ToolchainKitAspect::cxxToolchain(project->activeKit());
}

struct CheckResult {
    enum {
        InvalidExecutable,
        ProjectNotOpen,
        ProjectNotSuitable,
        ReadyToAnalyze,
    } kind;
    QString errorText;
};

static CheckResult canAnalyze(ClangToolType type, const QString &name)
{
    const ClangDiagnosticConfig config = diagnosticConfig(runSettings().diagnosticConfigId());

    if (toolEnabled(type, config, runSettings())
        && !toolExecutable(type).isExecutableFile()) {
        return {CheckResult::InvalidExecutable, Tr::tr("Set a valid %1 executable.").arg(name)};
    }

    if (Project *project = ProjectManager::startupProject()) {
        if (!canAnalyzeProject(project)) {
            return {CheckResult::ProjectNotSuitable,
                    Tr::tr("Project \"%1\" is not a C/C++ project.")
                        .arg(project->displayName())};
        }
    } else {
        return {CheckResult::ProjectNotOpen,
                Tr::tr("Open a C/C++ project to start analyzing.")};
    }

    return {CheckResult::ReadyToAnalyze, {}};
}

void ClangTool::updateForInitialState()
{
    if (m_state != State::Initial)
        return;

    m_infoBarWidget->reset();

    const CheckResult result = canAnalyze(m_type, m_name);
    switch (result.kind)
    case CheckResult::InvalidExecutable: {
        m_infoBarWidget->setError(InfoBarWidget::Warning, makeLink(result.errorText),
                                  [] { ICore::showOptionsDialog(Constants::SETTINGS_PAGE_ID); });
        break;
    case CheckResult::ProjectNotSuitable:
    case CheckResult::ProjectNotOpen:
    case CheckResult::ReadyToAnalyze:
        break;
        }
}

void ClangTool::help()
{
    if (DiagnosticItem *item = diagnosticItem(m_diagnosticView->currentIndex())) {
        const QString url = documentationUrl(item->diagnostic().name);
        if (!url.isEmpty())
            QDesktopServices::openUrl(url);
    }
}

void ClangTool::setFilterOptions(const OptionalFilterOptions &filterOptions)
{
    m_diagnosticFilterModel->setFilterOptions(filterOptions);
    const bool isFilterActive = filterOptions
                                    ? (filterOptions->checks != m_diagnosticModel->allChecks())
                                    : false;
    m_showFilter->setChecked(isFilterActive);
}

void ClangTool::filter()
{
    const OptionalFilterOptions filterOptions = m_diagnosticFilterModel->filterOptions();

    // Collect available and currently shown checks
    QHash<QString, Check> checks;
    m_diagnosticModel->forItemsAtLevel<2>([&](DiagnosticItem *item) {
        const QString checkName = item->diagnostic().name;
        Check &check = checks[checkName];
        if (check.name.isEmpty()) {
            check.name = checkName;
            check.displayName = checkName;
            check.count = 1;
            check.isShown = filterOptions ? filterOptions->checks.contains(checkName) : true;
            check.hasFixit = check.hasFixit || item->diagnostic().hasFixits;
            checks.insert(check.name, check);
        } else {
            ++check.count;
        }
    });

    // Show dialog
    FilterDialog dialog(checks.values());
    if (dialog.exec() == QDialog::Rejected)
        return;

    // Apply filter
    setFilterOptions(FilterOptions{dialog.selectedChecks()});
}

void ClangTool::clearFilter()
{
    m_diagnosticFilterModel->setFilterOptions({});
    m_showFilter->setChecked(false);
}

void ClangTool::filterForCurrentKind()
{
    if (DiagnosticItem *item = diagnosticItem(m_diagnosticView->currentIndex()))
        setFilterOptions(FilterOptions{{item->diagnostic().name}});
}

void ClangTool::filterOutCurrentKind()
{
    if (DiagnosticItem *item = diagnosticItem(m_diagnosticView->currentIndex())) {
        const OptionalFilterOptions filterOpts = m_diagnosticFilterModel->filterOptions();
        QSet<QString> checks = filterOpts ? filterOpts->checks : m_diagnosticModel->allChecks();
        checks.remove(item->diagnostic().name);

        setFilterOptions(FilterOptions{checks});
    }
}

void ClangTool::update()
{
    updateForInitialState();
    updateForCurrentState();
}

using DocumentPredicate = std::function<bool(IDocument *)>;

static FileInfos fileInfosMatchingDocuments(const FileInfos &fileInfos,
                                            const DocumentPredicate &predicate)
{
    QSet<FilePath> documentPaths;
    for (const DocumentModel::Entry *e : DocumentModel::entries()) {
        if (predicate(e->document))
            documentPaths.insert(e->filePath());
    }

    return Utils::filtered(fileInfos, [documentPaths](const FileInfo &fileInfo) {
        return documentPaths.contains(fileInfo.file);
    });
}

static FileInfos fileInfosMatchingOpenedDocuments(const FileInfos &fileInfos)
{
    // Note that (initially) suspended text documents are still IDocuments, not yet TextDocuments.
    return fileInfosMatchingDocuments(fileInfos, [](IDocument *) { return true; });
}

static FileInfos fileInfosMatchingEditedDocuments(const FileInfos &fileInfos)
{
    return fileInfosMatchingDocuments(fileInfos, [](IDocument *document) {
        if (auto textDocument = qobject_cast<TextEditor::TextDocument*>(document))
            return textDocument->document()->revision() > 1;
        return false;
    });
}

FileInfoProviders ClangTool::fileInfoProviders(Project *project, const FileInfos &allFileInfos)
{
    const std::shared_ptr<ClangToolsProjectSettings> s = ClangToolsProjectSettings::getSettings(project);
    static FileInfoSelection openedFilesSelection;
    static FileInfoSelection editeddFilesSelection;

    return {
        {Tr::tr("All Files"),
         allFileInfos,
         FileInfoSelection{s->selectedDirs(), s->selectedFiles()},
         FileInfoProvider::Limited,
         [s](const FileInfoSelection &selection) {
             s->setSelectedDirs(selection.dirs);
             s->setSelectedFiles(selection.files);
         }},

        {Tr::tr("Opened Files"),
         fileInfosMatchingOpenedDocuments(allFileInfos),
         openedFilesSelection,
         FileInfoProvider::All,
         [](const FileInfoSelection &selection) { openedFilesSelection = selection; }},

        {Tr::tr("Edited Files"),
         fileInfosMatchingEditedDocuments(allFileInfos),
         editeddFilesSelection,
         FileInfoProvider::All,
         [](const FileInfoSelection &selection) { editeddFilesSelection = selection; }},
    };
}

void ClangTool::setState(State state)
{
    m_state = state;
    updateForCurrentState();
}

QSet<Diagnostic> ClangTool::diagnostics() const
{
    return Utils::filtered(m_diagnosticModel->diagnostics(), [](const Diagnostic &diagnostic) {
        return ProjectFile::isSource(ProjectFile::classify(diagnostic.location.targetFilePath));
    });
}

void ClangTool::onNewDiagnosticsAvailable(
    const Diagnostics &diagnostics, bool generateMarks, RootItemUse rootItemUse)
{
    const int oldLevel1RowCount = m_diagnosticModel->rowCount();
    const auto getOldLastLevel1Index = [&] {
        return m_diagnosticModel->index(oldLevel1RowCount - 1, 0);
    };
    const auto getLevel2RowCountForOldLastLevel1Index = [&] {
        return oldLevel1RowCount == 0 ? -1 : m_diagnosticModel->rowCount(getOldLastLevel1Index());
    };
    const int oldLevel2RowCount = getLevel2RowCountForOldLastLevel1Index();
    TreeItem * const rootItem = rootItemUse == RootItemUse::New
                                   ? m_diagnosticModel->createRootItem()
                                   : m_diagnosticModel->rootItem();
    m_diagnosticModel->addDiagnostics(diagnostics, generateMarks, rootItem);
    if (rootItemUse == RootItemUse::New)
        m_diagnosticModel->resetRootItem(rootItem);

    if (!m_expandCollapse->isChecked())
        return;

    // Now expand newly added items, both in existing file nodes and in newly added ones.
    // We assume diagnostics arrive "in order", i.e. things are only ever added at the end
    // (in the source model).
    const int newLevel2RowCount = getLevel2RowCountForOldLastLevel1Index();
    for (int i = oldLevel2RowCount; i < newLevel2RowCount; ++i) {
        m_diagnosticView->expand(m_diagnosticFilterModel->mapFromSource(
            m_diagnosticModel
                ->index(i, 0, m_diagnosticFilterModel->mapFromSource(getOldLastLevel1Index()))));
    }
    const int newLevel1RowCount = m_diagnosticFilterModel->rowCount();
    for (int i = oldLevel1RowCount; i < newLevel1RowCount; ++i) {
        m_diagnosticView->expandRecursively(
            m_diagnosticFilterModel->mapFromSource(m_diagnosticModel->index(i, 0)));
    }
}

void ClangTool::updateForCurrentState()
{
    // Actions
    bool canStart = false;
    const bool isPreparing = m_state == State::PreparationStarted;
    const bool isRunning = m_state == State::AnalyzerRunning;
    QString startActionToolTip = m_startAction->text();
    QString startOnCurrentToolTip = m_startOnCurrentFileAction->text();
    if (!isRunning) {
        const CheckResult result = canAnalyze(m_type, m_name);
        canStart = result.kind == CheckResult::ReadyToAnalyze;
        if (!canStart) {
            startActionToolTip = result.errorText;
            startOnCurrentToolTip = result.errorText;
        }
    }
    m_startAction->setEnabled(canStart);
    m_startAction->setToolTip(startActionToolTip);
    m_startOnCurrentFileAction->setEnabled(canStart);
    m_startOnCurrentFileAction->setToolTip(startOnCurrentToolTip);
    m_stopAction->setEnabled(isPreparing || isRunning);

    const int issuesFound = m_diagnosticModel->diagnostics().count();
    const int issuesVisible = m_diagnosticFilterModel->diagnostics();
    m_goBack->setEnabled(issuesVisible > 0);
    m_goNext->setEnabled(issuesVisible > 0);
    m_clear->setEnabled(!isRunning);
    m_loadExported->setEnabled(!isRunning);
    m_showFilter->setEnabled(issuesFound > 1);

    // Diagnostic view
    m_diagnosticView->setCursor(isRunning ? Qt::BusyCursor : Qt::ArrowCursor);

    // Info bar: errors
    if (m_filesFailed > 0) {
        const QString currentErrorText = m_infoBarWidget->errorText();
        const QString newErrorText = makeLink(Tr::tr("Failed to analyze %n file(s).", nullptr,
                                                 m_filesFailed));
        if (newErrorText != currentErrorText)
            m_infoBarWidget->setError(InfoBarWidget::Warning, newErrorText,
                                      [this] { showOutputPane(); });
    }

    // Info bar: info
    bool showProgressIcon = false;
    QString infoText;
    switch (m_state) {
    case State::Initial:
        infoText = m_infoBarWidget->infoText();
        break;
    case State::AnalyzerRunning:
        showProgressIcon = true;
        if (m_filesSucceeded + m_filesFailed == 0) {
            infoText = Tr::tr("Analyzing..."); // Not yet fully started/initialized
        } else {
            infoText = Tr::tr("Analyzing... %1 of %n file(s) processed.", nullptr, m_filesCount)
                           .arg(m_filesSucceeded + m_filesFailed);
        }
        break;
    case State::PreparationStarted:
        showProgressIcon = true;
        infoText = m_infoBarWidget->infoText();
        break;
    case State::PreparationFailed:
        break; // OK, we just show an error.
    case State::StoppedByUser:
        infoText = Tr::tr("Analysis stopped by user.");
        break;
    case State::AnalyzerFinished:
        infoText = Tr::tr("Finished processing %n file(s).", nullptr, m_filesCount);
        break;
    case State::ImportFinished:
        infoText = Tr::tr("Diagnostics imported.");
        break;
    }
    m_infoBarWidget->setInfoText(infoText);
    m_infoBarWidget->setInfoIcon(showProgressIcon ? InfoBarWidget::ProgressIcon
                                                  : InfoBarWidget::InfoIcon);

    // Info bar: diagnostic stats
    QString diagText;
    if (issuesFound) {
        diagText = Tr::tr("%1 diagnostics. %2 fixits, %3 selected.")
                   .arg(issuesVisible)
                   .arg(m_diagnosticFilterModel->fixitsSchedulable())
                   .arg(m_diagnosticFilterModel->fixitsScheduled());
    } else if (m_state != State::AnalyzerRunning
               && m_state != State::Initial
               && m_state != State::PreparationStarted
               && m_state != State::PreparationFailed) {
        diagText = Tr::tr("No diagnostics.");
    }
    m_infoBarWidget->setDiagText(diagText);
}

ClangTidyTool::ClangTidyTool() : ClangTool(Tr::tr("Clang-Tidy"), "ClangTidy.Perspective",
                                           ClangToolType::Tidy)
{
    m_instance = this;
}

ClazyTool::ClazyTool() : ClangTool(Tr::tr("Clazy"), "Clazy.Perspective", ClangToolType::Clazy)
{
    m_instance = this;
}

} // namespace ClangTools::Internal

#include "clangtool.moc"
