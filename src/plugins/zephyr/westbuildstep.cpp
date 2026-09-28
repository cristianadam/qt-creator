// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "westbuildstep.h"

#include "zephyrconstants.h"
#include "zephyrsettings.h"
#include "zephyrtr.h"

#include <coreplugin/actionmanager/actionmanager.h>

#include <projectexplorer/abstractprocessstep.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/processparameters.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>
#include <projectexplorer/ioutputparser.h>
#include <projectexplorer/task.h>

#include <qtsupport/qtkitaspect.h>
#include <qtsupport/baseqtversion.h>

#include <utils/aspects.h>
#include <utils/layoutbuilder.h>
#include <utils/outputformatter.h>
#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>
#include <utils/terminalhooks.h>

#include <QDesktopServices>
#include <QDir>
#include <QSettings>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace Zephyr::Internal {

static const char SDK_INSTALL_SCHEME[] = "zephyr-sdk-install";

static FilePath westWorkingDirectory(const Project *project)
{
    const FilePath ws = settings().workspaceDir();
    return ws.isEmpty() && project ? project->projectDirectory() : ws;
}

static void runWestInTerminal(const QStringList &westArgs, const FilePath &workingDir)
{
    const OsType osType = workingDir.osType();
    QString command = ProcessArgs::quoteArg(settings().westFilePath().nativePath(), osType);
    for (const QString &arg : westArgs)
        command += ' ' + ProcessArgs::quoteArg(arg, osType);

    CommandLine cmd;
    if (osType == OsTypeWindows)
        cmd = {workingDir.findCmdExe(), {"/k", command}};
    else
        cmd = {workingDir.withNewPath("/bin/bash"), {"-c", command + "; exec $SHELL"}};
    Terminal::Hooks::instance().openTerminal({cmd, workingDir, std::nullopt});
}

class WestSdkInstaller : public QObject
{
    Q_OBJECT

public:
    WestSdkInstaller()
    {
        QDesktopServices::setUrlHandler(SDK_INSTALL_SCHEME, this, "install");
    }

    ~WestSdkInstaller()
    {
        QDesktopServices::unsetUrlHandler(SDK_INSTALL_SCHEME);
    }

public slots:
    void install(const QUrl &)
    {
        runWestInTerminal({"sdk", "install"}, settings().workspaceDir());
    }
};

class ZephyrActions final : public QObject
{
public:
    ZephyrActions()
    {
        namespace PEC = ProjectExplorer::Constants;

        auto addAction = [this](const char *id, const QString &text, QAction **action,
                                const std::function<void(BuildConfiguration *)> &handler) {
            ActionBuilder(this, Id("Zephyr.").withSuffix(id))
                .setText(text)
                .bindContextAction(action)
                .setCommandAttribute(Command::CA_Hide)
                .addToContainer(PEC::M_BUILD_TOOL, PEC::G_BUILD_TOOL)
                .addOnTriggered(this, [handler] {
                    if (BuildConfiguration *bc = activeBuildConfigForActiveProject())
                        handler(bc);
                });
        };

        addAction("Menuconfig", Tr::tr("Run Zephyr menuconfig"), &m_menuconfigAction,
                  [](BuildConfiguration *bc) { runBuildTarget(bc, "menuconfig"); });
        addAction("Guiconfig", Tr::tr("Run Zephyr guiconfig"), &m_guiconfigAction,
                  [](BuildConfiguration *bc) {
                      Process::startDetached({settings().westFilePath(),
                                              buildTargetArgs(bc, "guiconfig")},
                                             westWorkingDirectory(bc->project()));
                  });
        addAction("RamReport", Tr::tr("Show Zephyr RAM Report"), &m_ramReportAction,
                  [](BuildConfiguration *bc) { runBuildTarget(bc, "ram_report"); });
        addAction("RomReport", Tr::tr("Show Zephyr ROM Report"), &m_romReportAction,
                  [](BuildConfiguration *bc) { runBuildTarget(bc, "rom_report"); });
        addAction("WestUpdate", Tr::tr("Run west update"), &m_westUpdateAction,
                  [](BuildConfiguration *bc) {
                      runWestInTerminal({"update"}, westWorkingDirectory(bc->project()));
                  });

        connect(ProjectManager::instance(), &ProjectManager::startupProjectChanged,
                this, &ZephyrActions::updateActions);
        connect(BuildManager::instance(), &BuildManager::buildStateChanged,
                this, &ZephyrActions::updateActions);
        updateActions();
    }

private:
    static QStringList buildTargetArgs(const BuildConfiguration *bc, const QString &target)
    {
        return {"build", "-d", bc->buildDirectory().nativePath(), "-t", target};
    }

    static void runBuildTarget(const BuildConfiguration *bc, const QString &target)
    {
        runWestInTerminal(buildTargetArgs(bc, target), westWorkingDirectory(bc->project()));
    }

    void updateActions()
    {
        Project *project = ProjectManager::startupProject();
        const bool isZephyr = project && project->type() == Constants::ZEPHYR_PROJECT_ID;
        const bool enabled = isZephyr && !BuildManager::isBuilding(project);
        for (QAction *action : {m_menuconfigAction, m_guiconfigAction, m_ramReportAction,
                                m_romReportAction, m_westUpdateAction}) {
            action->setVisible(isZephyr);
            action->setEnabled(enabled);
        }
    }

    QAction *m_menuconfigAction = nullptr;
    QAction *m_guiconfigAction = nullptr;
    QAction *m_ramReportAction = nullptr;
    QAction *m_romReportAction = nullptr;
    QAction *m_westUpdateAction = nullptr;
};

class WestOutputParser final : public OutputTaskParser
{
public:
    explicit WestOutputParser(const FilePath &workspaceDir)
        : m_workspaceDir(workspaceDir) {}

private:
    Result handleLine(const QString &line, OutputFormat format) final
    {
        if (format != StdErrFormat)
            return Status::NotHandled;
        if (line.contains("Zephyr-sdk") && line.contains("Could not find")) {
            BuildSystemTask task(Task::Error,
                Tr::tr("Zephyr SDK (toolchain) not installed."));
            const QString url = QString(SDK_INSTALL_SCHEME) + ":install";
            task.addLinkDetail(url,
                Tr::tr("Run \"west sdk install\" in \"%1\"")
                    .arg(m_workspaceDir.toUserOutput()));
            scheduleTask(task, 1, 0);
            return Status::Done;
        }
        return Status::NotHandled;
    }

    FilePath m_workspaceDir;
};

static QString boardFromWestConfig(const FilePath &workspaceDir)
{
    if (workspaceDir.isEmpty())
        return {};

    const FilePath configFile = workspaceDir.pathAppended(".west/config");
    if (!configFile.isReadableFile())
        return {};

    QSettings cfg(configFile.toFSPathString(), QSettings::IniFormat);
    QVariant board = cfg.value("build/board");
    return board.toString();
}

class WestBuildStep final : public AbstractProcessStep
{
public:
    WestBuildStep(BuildStepList *bsl, Id id)
        : AbstractProcessStep(bsl, id)
    {
        m_board.setSettingsKey("Zephyr.WestBuildStep.Board");
        m_board.setLabelText(Tr::tr("Board:"));
        m_board.setDisplayStyle(StringAspect::LineEditDisplay);
        m_board.setPlaceHolderText(Tr::tr("For example, qemu_x86"));
        m_board.setDefaultValue(boardFromWestConfig(settings().workspaceDir()));

        m_sysbuild.setSettingsKey("Zephyr.WestBuildStep.Sysbuild");
        m_sysbuild.setLabelText(Tr::tr("Sysbuild:"));
        m_sysbuild.setLabelPlacement(BoolAspect::LabelPlacement::InExtraLabel);
        m_sysbuild.setToolTip(Tr::tr("Build the application together with its bootloader "
                                     "and other images."));

        m_optimization.setSettingsKey("Zephyr.WestBuildStep.Optimization");
        m_optimization.setLabelText(Tr::tr("Optimization:"));
        m_optimization.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        m_optimization.setUseDataAsSavedValue();
        m_optimization.addOption({Tr::tr("Project Default"), {}, QString()});
        m_optimization.addOption({Tr::tr("Debug"), {}, QString("CONFIG_DEBUG_OPTIMIZATIONS")});
        m_optimization.addOption({Tr::tr("Size"), {}, QString("CONFIG_SIZE_OPTIMIZATIONS")});
        m_optimization.addOption({Tr::tr("Speed"), {}, QString("CONFIG_SPEED_OPTIMIZATIONS")});
        m_optimization.addOption({Tr::tr("None"), {}, QString("CONFIG_NO_OPTIMIZATIONS")});

        m_extraConfFiles.setSettingsKey("Zephyr.WestBuildStep.ExtraConfFiles");
        m_extraConfFiles.setLabelText(Tr::tr("Extra Kconfig fragments:"));

        m_extraOverlays.setSettingsKey("Zephyr.WestBuildStep.ExtraOverlays");
        m_extraOverlays.setLabelText(Tr::tr("Extra devicetree overlays:"));

        m_extraArgs.setSettingsKey("Zephyr.WestBuildStep.ExtraArgs");
        m_extraArgs.setLabelText(Tr::tr("Extra arguments:"));
        m_extraArgs.setDisplayStyle(StringAspect::LineEditDisplay);

        setCommandLineProvider([this] { return westCommand(); });
        setWorkingDirectoryProvider([this]() -> FilePath {
            const FilePath ws = settings().workspaceDir();
            return ws.isEmpty() ? project()->projectDirectory() : ws;
        });
        setUseEnglishOutput();
    }

    QWidget *createConfigWidget() final
    {
        auto updateDetails = [this] {
            ProcessParameters param;
            setupProcessParameters(&param);
            setSummaryText(param.summary(displayName()));
        };

        setDisplayName(Tr::tr("West Build"));

        updateDetails();

        m_board.addOnChanged(this, updateDetails);
        m_sysbuild.addOnChanged(this, updateDetails);
        m_optimization.addOnChanged(this, updateDetails);
        m_extraConfFiles.addOnChanged(this, updateDetails);
        m_extraOverlays.addOnChanged(this, updateDetails);
        m_extraArgs.addOnChanged(this, updateDetails);

        using namespace Layouting;
        return Form {
            noMargin,
            m_board, br,
            m_sysbuild, br,
            m_optimization, br,
            m_extraConfFiles, br,
            m_extraOverlays, br,
            m_extraArgs
        }.emerge();
    }

    void setupOutputFormatter(OutputFormatter *formatter) final
    {
        formatter->addLineParser(new WestOutputParser(settings().workspaceDir()));
        AbstractProcessStep::setupOutputFormatter(formatter);
    }

private:
    CommandLine westCommand() const
    {
        const FilePath projectDir = project()->projectDirectory();
        const FilePath ws = settings().workspaceDir();

        CommandLine cmd{settings().westFilePath()};
        cmd.addArg("build");
        if (!m_board().isEmpty()) {
            cmd.addArg("-b");
            cmd.addArg(m_board());
        }
        cmd.addArg("-d");
        cmd.addArg(buildConfiguration()->buildDirectory().nativePath());
        if (!ws.isEmpty() && ws != projectDir) {
            cmd.addArg("--source-dir");
            cmd.addArg(projectDir.nativePath());
        }
        if (m_sysbuild())
            cmd.addArg("--sysbuild");
        for (const FilePath &file : m_extraConfFiles())
            cmd.addArg("--extra-conf=" + file.nativePath());
        for (const FilePath &file : m_extraOverlays())
            cmd.addArg("--extra-dtc-overlay=" + file.nativePath());
        const QString optimization = m_optimization.itemValue().toString();
        if (!optimization.isEmpty())
            cmd.addArg("--cmake-opt=-D" + optimization + "=y");
        cmd.addArgs(m_extraArgs(), CommandLine::Raw);
        return cmd;
    }

    StringAspect m_board{this};
    BoolAspect m_sysbuild{this};
    SelectionAspect m_optimization{this};
    FilePathListAspect m_extraConfFiles{this};
    FilePathListAspect m_extraOverlays{this};
    StringAspect m_extraArgs{this};
};

class WestBuildStepFactory final : public BuildStepFactory
{
public:
    WestBuildStepFactory()
    {
        registerStep<WestBuildStep>(Constants::WEST_BUILD_STEP_ID);
        setDisplayName(Tr::tr("West Build"));
        setSupportedStepLists({ProjectExplorer::Constants::BUILDSTEPS_BUILD});
    }
};

static FilePath qmlProjectExporterFromKit(const Kit *kit)
{
    const QtSupport::QtVersion *qt = QtSupport::QtKitAspect::qtVersion(kit);
    if (!qt)
        return {};
    const FilePath candidate = qt->hostBinPath()
                                   .pathAppended("qmlprojectexporter")
                                   .withExecutableSuffix();
    return candidate.isExecutableFile() ? candidate : FilePath{};
}

class WestExportStep final : public AbstractProcessStep
{
public:
    WestExportStep(BuildStepList *bsl, Id id)
        : AbstractProcessStep(bsl, id)
    {
        m_qmlProjectFile.setSettingsKey("Zephyr.WestExportStep.QmlProjectFile");
        m_qmlProjectFile.setLabelText(Tr::tr("QML project file:"));
        m_qmlProjectFile.setExpectedKind(PathChooserKind::File);
        m_qmlProjectFile.setPromptDialogFilter("*.qmlproject");

        // Auto-detect a single .qmlproject at project root on first creation.
        if (m_qmlProjectFile().isEmpty()) {
            const FilePaths found = project()->projectDirectory()
                                        .dirEntries({{"*.qmlproject"}, DirFilterFlag::Files});
            if (found.size() == 1)
                m_qmlProjectFile.setValue(found.first());
        }

        setCommandLineProvider([this] { return exporterCommand(); });
        setWorkingDirectoryProvider([this]() -> FilePath {
            return project()->projectDirectory();
        });
        setUseEnglishOutput();
    }

    bool init() override
    {
        setEnabled(!exporterFilePath().isEmpty() && !m_qmlProjectFile().isEmpty());
        return AbstractProcessStep::init();
    }

    QWidget *createConfigWidget() final
    {
        auto updateDetails = [this] {
            ProcessParameters param;
            setupProcessParameters(&param);
            setSummaryText(param.summary(displayName()));
        };

        setDisplayName(Tr::tr("QML Project Export"));
        updateDetails();
        m_qmlProjectFile.addOnChanged(this, updateDetails);

        using namespace Layouting;
        return Form { noMargin, m_qmlProjectFile }.emerge();
    }

private:
    FilePath exporterFilePath() const
    {
        const FilePath fromSettings = settings().qmlProjectExporterFilePath();
        return fromSettings.isEmpty()
                   ? qmlProjectExporterFromKit(buildConfiguration()->kit())
                   : fromSettings;
    }

    CommandLine exporterCommand() const
    {
        CommandLine cmd{exporterFilePath()};
        cmd.addArg("--project-type=zephyr");
        cmd.addArg("--outdir");
        cmd.addArg(project()->projectDirectory().nativePath());
        cmd.addArg(m_qmlProjectFile().nativePath());
        return cmd;
    }

    FilePathAspect m_qmlProjectFile{this};
};

class WestExportStepFactory final : public BuildStepFactory
{
public:
    WestExportStepFactory()
    {
        registerStep<WestExportStep>(Constants::WEST_EXPORT_STEP_ID);
        setDisplayName(Tr::tr("QML Project Export"));
        setSupportedStepLists({ProjectExplorer::Constants::BUILDSTEPS_BUILD});
    }
};

class WestCleanStep final : public AbstractProcessStep
{
public:
    WestCleanStep(BuildStepList *bsl, Id id)
        : AbstractProcessStep(bsl, id)
    {
        m_pristine.setSettingsKey("Zephyr.WestCleanStep.Pristine");
        m_pristine.setLabelText(Tr::tr("Pristine:"));
        m_pristine.setLabelPlacement(BoolAspect::LabelPlacement::InExtraLabel);
        m_pristine.setToolTip(Tr::tr("Remove the whole build directory contents, including "
                                     "the CMake cache, instead of only the build products."));

        setCommandLineProvider([this] {
            CommandLine cmd{settings().westFilePath()};
            cmd.addArg("build");
            cmd.addArg("-d");
            cmd.addArg(buildConfiguration()->buildDirectory().nativePath());
            cmd.addArg("-t");
            cmd.addArg(m_pristine() ? QString("pristine") : QString("clean"));
            return cmd;
        });
        setWorkingDirectoryProvider([this]() -> FilePath {
            const FilePath ws = settings().workspaceDir();
            return ws.isEmpty() ? project()->projectDirectory() : ws;
        });
        setUseEnglishOutput();
    }

    QWidget *createConfigWidget() final
    {
        auto updateDetails = [this] {
            ProcessParameters param;
            setupProcessParameters(&param);
            setSummaryText(param.summary(displayName()));
        };

        setDisplayName(Tr::tr("West Clean"));
        updateDetails();
        m_pristine.addOnChanged(this, updateDetails);

        using namespace Layouting;
        return Form { noMargin, m_pristine }.emerge();
    }

private:
    BoolAspect m_pristine{this};
};

class WestCleanStepFactory final : public BuildStepFactory
{
public:
    WestCleanStepFactory()
    {
        registerStep<WestCleanStep>(Constants::WEST_CLEAN_STEP_ID);
        setDisplayName(Tr::tr("West Clean"));
        setSupportedStepLists({ProjectExplorer::Constants::BUILDSTEPS_CLEAN});
    }
};

static bool isZephyrProject(Project *project)
{
    return project->projectDirectory().pathAppended("west.yml").isFile();
}

static void connectTarget(Target *target)
{
    auto addStepsIfMissing = [](BuildConfiguration *bc) {
        BuildStepList *bsl = bc->buildSteps();

        if (!bsl->contains(Constants::WEST_BUILD_STEP_ID))
            bsl->appendStep(Constants::WEST_BUILD_STEP_ID);

        BuildStepList *cleanBsl = bc->cleanSteps();
        if (!cleanBsl->contains(Constants::WEST_CLEAN_STEP_ID))
            cleanBsl->appendStep(Constants::WEST_CLEAN_STEP_ID);
    };

    for (BuildConfiguration *bc : target->buildConfigurations())
        addStepsIfMissing(bc);

    QObject::connect(target, &Target::addedBuildConfiguration,
                     target, addStepsIfMissing);
}

static void connectProject(Project *project)
{
    if (!isZephyrProject(project))
        return;

    for (Target *target : project->targets())
        connectTarget(target);

    QObject::connect(project, &Project::addedTarget, project, connectTarget);
}

class WestFlashStep final : public AbstractProcessStep
{
public:
    WestFlashStep(BuildStepList *bsl, Id id)
        : AbstractProcessStep(bsl, id)
    {
        m_extraArgs.setSettingsKey("Zephyr.WestFlashStep.ExtraArgs");
        m_extraArgs.setLabelText(Tr::tr("Extra arguments:"));
        m_extraArgs.setDisplayStyle(StringAspect::LineEditDisplay);

        setCommandLineProvider([this] {
            CommandLine cmd{settings().westFilePath()};
            cmd.addArg("flash");
            cmd.addArg("-d");
            cmd.addArg(buildConfiguration()->buildDirectory().nativePath());
            cmd.addArgs(m_extraArgs(), CommandLine::Raw);
            return cmd;
        });
        setWorkingDirectoryProvider([this]() -> FilePath {
            const FilePath ws = settings().workspaceDir();
            return ws.isEmpty() ? project()->projectDirectory() : ws;
        });
        setUseEnglishOutput();
    }

    QWidget *createConfigWidget() final
    {
        auto updateDetails = [this] {
            ProcessParameters param;
            setupProcessParameters(&param);
            setSummaryText(param.summary(displayName()));
        };

        setDisplayName(Tr::tr("West Flash"));
        updateDetails();
        m_extraArgs.addOnChanged(this, updateDetails);

        using namespace Layouting;
        return Form { noMargin, m_extraArgs }.emerge();
    }

    StringAspect m_extraArgs{this};
};

class WestFlashStepFactory final : public BuildStepFactory
{
public:
    WestFlashStepFactory()
    {
        registerStep<WestFlashStep>(Constants::WEST_FLASH_STEP_ID);
        setDisplayName(Tr::tr("West Flash"));
        setSupportedStepLists({ProjectExplorer::Constants::BUILDSTEPS_DEPLOY});
    }
};

class ZephyrDeployConfigurationFactory final : public DeployConfigurationFactory
{
public:
    ZephyrDeployConfigurationFactory()
    {
        setConfigBaseId(Constants::ZEPHYR_DEPLOY_CONFIG_ID);
        setSupportedProjectType(Constants::ZEPHYR_PROJECT_ID);
        setDefaultDisplayName(Tr::tr("Flash with West"));
        addInitialStep(Constants::WEST_FLASH_STEP_ID);
    }
};

void setupWestBuildSteps()
{
    static WestExportStepFactory theExportStepFactory;
    static WestBuildStepFactory theBuildStepFactory;
    static WestCleanStepFactory theCleanStepFactory;
    static WestFlashStepFactory theFlashStepFactory;
    static ZephyrDeployConfigurationFactory theDeployConfigFactory;
    static WestSdkInstaller theSdkInstaller;
    static ZephyrActions theActions;

    for (Project *project : ProjectManager::projects())
        connectProject(project);

    QObject::connect(ProjectManager::instance(), &ProjectManager::projectAdded,
                     ProjectManager::instance(), connectProject);
}

} // namespace Zephyr::Internal

#include "westbuildstep.moc"
