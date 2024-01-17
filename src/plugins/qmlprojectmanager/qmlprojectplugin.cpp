// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlprojectplugin.h"

#include "qdslandingpage.h"
#include "qmlproject.h"
#include "qmlprojectconstants.h"
#include "qmlprojectmanagertr.h"
#include "qmlprojectrunconfiguration.h"
#include "projectfilecontenttools.h"
#include "cmakegen/cmakeprojectconverter.h"
#include "cmakegen/generatecmakelists.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/designmode.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagebox.h>
#include <coreplugin/modemanager.h>

#include <debugger/debuggerruncontrol.h>

#include <extensionsystem/iplugin.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>

#include <qmljs/qmljsmodelmanagerinterface.h>

#include <qmljseditor/qmljseditor.h>
#include <qmljseditor/qmljseditorconstants.h>

#include <qmljstools/qmljstoolsconstants.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <utils/fileutils.h>
#include <utils/fsengine/fileiconprovider.h>
#include <utils/mimeconstants.h>
#include <utils/process.h>
#include <utils/qtcsettings.h>

#include <QAction>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

using namespace Core;
using namespace Debugger;
using namespace ProjectExplorer;
using namespace Utils;

namespace QmlProjectManager::Internal {

static bool isQmlDesigner(const ExtensionSystem::PluginSpec *spec)
{
    if (!spec)
        return false;

    return spec->name().contains("QmlDesigner");
}

static bool qmlDesignerEnabled()
{
    const auto plugins = ExtensionSystem::PluginManager::plugins();
    const auto it = std::find_if(plugins.begin(), plugins.end(), &isQmlDesigner);
    return it != plugins.end() && (*it)->plugin();
}

static QString alwaysOpenWithMode()
{
    return ICore::settings()
        ->value(QmlProjectManager::Constants::ALWAYS_OPEN_UI_MODE, "")
        .toString();
}

static void setAlwaysOpenWithMode(const QString &mode)
{
    ICore::settings()->setValue(QmlProjectManager::Constants::ALWAYS_OPEN_UI_MODE, mode);
}

static void clearAlwaysOpenWithMode()
{
    ICore::settings()->remove(QmlProjectManager::Constants::ALWAYS_OPEN_UI_MODE);
}

class QmlProjectPluginPrivate
{
public:
    QmlProjectRunConfigurationFactory runConfigFactory;
    SimpleTargetRunnerFactory runWorkerFactory{{runConfigFactory.runConfigurationId()}};
    SimpleDebugRunnerFactory debugRunWorkerFactory{{runConfigFactory.runConfigurationId()}};
    QPointer<QMessageBox> lastMessageBox;
    QdsLandingPage *landingPage = nullptr;
    QdsLandingPageWidget *landingPageWidget = nullptr;
};

void openQDS(const FilePath &fileName)
{
    const FilePath qdsPath = qdsInstallationEntry();
    bool qdsStarted = false;
    qputenv(Constants::enviromentLaunchedQDS, "true");
    //-a and -client arguments help to append project to open design studio application
    if (HostOsInfo::isMacHost())
        qdsStarted = Process::startDetached(
            {"/usr/bin/open", {"-a", qdsPath.path(), fileName.toString()}});
    else
        qdsStarted = Process::startDetached({qdsPath, {"-client", fileName.toString()}});

    if (!qdsStarted) {
        QMessageBox::warning(ICore::dialogParent(),
                             fileName.fileName(),
                             Tr::tr("Failed to start Qt Design Studio."));
        if (alwaysOpenWithMode() == Core::Constants::MODE_DESIGN)
            clearAlwaysOpenWithMode();
    }
}

FilePath qdsInstallationEntry()
{
    QtcSettings *settings = ICore::settings();
    const Key qdsInstallationEntry = "QML/Designer/DesignStudioInstallation"; //set in installer

    return FilePath::fromUserInput(settings->value(qdsInstallationEntry).toString());
}

bool qdsInstallationExists()
{
    return qdsInstallationEntry().exists();
}

bool checkIfEditorIsuiQml(IEditor *editor)
{
    if (editor
        && (editor->document()->id() == QmlJSEditor::Constants::C_QMLJSEDITOR_ID
            || editor->document()->id() == QmlJSEditor::Constants::C_QTQUICKDESIGNEREDITOR_ID)) {
        QmlJS::ModelManagerInterface *modelManager = QmlJS::ModelManagerInterface::instance();
        QmlJS::Document::Ptr document = modelManager->ensuredGetDocumentForPath(
            editor->document()->filePath());
        if (!document.isNull())
            return document->language() == QmlJS::Dialect::QmlQtQuick2Ui;
    }
    return false;
}

const FilePath findQmlProject(const FilePath &folder)
{
    const FilePaths files = folder.dirEntries({QStringList("*.qmlproject"), QDir::Files});
    if (files.isEmpty())
        return {};

    return files.constFirst();
}

const FilePath findQmlProjectUpwards(const FilePath &folder)
{
    FilePath ret = findQmlProject(folder);
    if (ret.exists())
        return ret;

    if (folder.parentDir().isDir())
        return findQmlProjectUpwards(folder.parentDir());

    return {};
}

static bool findAndOpenProject(const FilePath &filePath)
{
    if (Project *project = ProjectManager::projectForFile(filePath)) {
        if (project->projectFilePath().suffix() == "qmlproject") {
            openQDS(project->projectFilePath());
            return true;
        }
        FilePath projectFolder = project->rootProjectDirectory();
        FilePath qmlProjectFile = findQmlProject(projectFolder);
        if (qmlProjectFile.exists()) {
            openQDS(qmlProjectFile);
            return true;
        }
    }

    FilePath qmlProjectFile = findQmlProjectUpwards(filePath);
    if (qmlProjectFile.exists()) {
        openQDS(qmlProjectFile);
        return true;
    }
    return false;
}

void openInQDSWithProject(const FilePath &filePath)
{
    if (findAndOpenProject(filePath)) {
        openQDS(filePath);
        //The first one might be ignored when QDS is starting up
        QTimer::singleShot(4000, [filePath] { openQDS(filePath); });
    } else {
        AsynchronousMessageBox::warning(
            Tr::tr("Qt Design Studio"),
            Tr::tr("No project file (*.qmlproject) found for Qt Design "
               "Studio.\nQt Design Studio requires a .qmlproject "
               "based project to open the .ui.qml file."));
    }
}

static QmlBuildSystem *qmlBuildSystemforFileNode(const FileNode *fileNode)
{
    if (!fileNode)
        return nullptr;

    if (QmlProject *qmlProject = qobject_cast<QmlProject*>(fileNode->getProject())) {
        Target *target = qmlProject->activeTarget();
        if (!target)
            return nullptr;

        return qobject_cast<QmlProjectManager::QmlBuildSystem *>(target->buildSystem());

    }

    return nullptr;
}

class QmlProjectPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "QmlProjectManager.json")

public:
    ~QmlProjectPlugin()
    {
        if (d->lastMessageBox)
            d->lastMessageBox->deleteLater();
        if (d->landingPage)
            d->landingPage->deleteLater();
        if (d->landingPageWidget)
            d->landingPageWidget->deleteLater();
        delete d;
    }

public slots:
    void editorModeChanged(Utils::Id newMode, Utils::Id oldMode);
    void openQtc(bool permanent = false);
    void openQds(bool permanent = false);

private:
    void initialize() final;
    void displayQmlLandingPage();
    void hideQmlLandingPage();
    void updateQmlLandingPageProjectInfo(const Utils::FilePath &projectFile);

    class QmlProjectPluginPrivate *d = nullptr;
};

void QmlProjectPlugin::initialize()
{
    d = new QmlProjectPluginPrivate;

    if (!qmlDesignerEnabled()) {
        d->landingPage = new QdsLandingPage();
        qmlRegisterSingletonInstance<QdsLandingPage>("LandingPageApi",
                                                     1,
                                                     0,
                                                     "LandingPageApi",
                                                     d->landingPage);

        d->landingPageWidget = new QdsLandingPageWidget();

        const QStringList mimeTypes = {Utils::Constants::QMLUI_MIMETYPE};
        auto context = new Internal::DesignModeContext(d->landingPageWidget);
        ICore::addContextObject(context);

        DesignMode::registerDesignWidget(d->landingPageWidget, mimeTypes, context->context());

        connect(ModeManager::instance(), &ModeManager::currentModeChanged,
                this, &QmlProjectPlugin::editorModeChanged);
    }

    ProjectManager::registerProjectType<QmlProject>(Utils::Constants::QMLPROJECT_MIMETYPE);
    FileIconProvider::registerIconOverlayForSuffix(":/qmlproject/images/qmlproject.png",
                                                   "qmlproject");

    if (ICore::isQtDesignStudio()) {
        ActionContainer *menu = ActionManager::actionContainer(
            ProjectExplorer::Constants::M_FILECONTEXT);
        QAction *mainfileAction = new QAction(Tr::tr("Set as Main .qml File"), this);
        mainfileAction->setEnabled(false);

        connect(mainfileAction, &QAction::triggered, this, []() {
            const Node *currentNode = ProjectTree::currentNode();
            if (!currentNode || !currentNode->asFileNode()
                || currentNode->asFileNode()->fileType() != FileType::QML)
                return;

            const FilePath file = currentNode->filePath();

            QmlBuildSystem *buildSystem = qmlBuildSystemforFileNode(currentNode->asFileNode());
            if (buildSystem)
                buildSystem->setMainFileInProjectFile(file);
        });

        menu->addAction(ActionManager::registerAction(
                            mainfileAction,
                            "QmlProject.setMainFile",
                            Context(ProjectExplorer::Constants::C_PROJECT_TREE)),
                        ProjectExplorer::Constants::G_FILE_OTHER);
        mainfileAction->setVisible(false);
        connect(ProjectTree::instance(),
                &ProjectTree::currentNodeChanged,
                mainfileAction,
                [mainfileAction](Node *node) {
                    const FileNode *fileNode = node ? node->asFileNode() : nullptr;

                    const bool isVisible = fileNode && fileNode->fileType() == FileType::QML
                                           && fileNode->filePath().completeSuffix() == "qml";

                    mainfileAction->setVisible(isVisible);

                    if (!isVisible)
                        return;

                    QmlBuildSystem *buildSystem = qmlBuildSystemforFileNode(fileNode);

                    if (buildSystem)
                        mainfileAction->setEnabled(buildSystem->mainFilePath()
                                                   != fileNode->filePath());
                });

        QAction *mainUifileAction = new QAction(Tr::tr("Set as Main .ui.qml File"), this);
        mainUifileAction->setEnabled(false);

        connect(mainUifileAction, &QAction::triggered, this, []() {
            const Node *currentNode = ProjectTree::currentNode();
            if (!currentNode || !currentNode->asFileNode()
                || currentNode->asFileNode()->fileType() != FileType::QML)
                return;

            const FilePath file = currentNode->filePath();

            QmlBuildSystem *buildSystem = qmlBuildSystemforFileNode(currentNode->asFileNode());
            if (buildSystem)
                buildSystem->setMainUiFileInProjectFile(file);
        });

        menu->addAction(ActionManager::registerAction(
                            mainUifileAction,
                            "QmlProject.setMainUIFile",
                            Core::Context(ProjectExplorer::Constants::C_PROJECT_TREE)),
                        ProjectExplorer::Constants::G_FILE_OTHER);
        mainUifileAction->setVisible(false);
        connect(ProjectTree::instance(),
                &ProjectTree::currentNodeChanged,
                mainUifileAction,
                [mainUifileAction](Node *node) {
                    const FileNode *fileNode = node ? node->asFileNode() : nullptr;
                    const bool isVisible = fileNode && fileNode->fileType() == FileType::QML
                                           && fileNode->filePath().completeSuffix() == "ui.qml";

                    mainUifileAction->setVisible(isVisible);

                    if (!isVisible)
                        return;

                    QmlBuildSystem *buildSystem = qmlBuildSystemforFileNode(fileNode);
                    if (buildSystem)
                        mainUifileAction->setEnabled(buildSystem->mainUiFilePath()
                                                     != fileNode->filePath());
                });
    }

    GenerateCmake::generateMenuEntry(this);
    if (ICore::isQtDesignStudio())
        GenerateCmake::CmakeProjectConverter::generateMenuEntry(this);
}

void QmlProjectPlugin::displayQmlLandingPage()
{
    if (!d->landingPage)
        return;

    d->landingPage->setWidget(d->landingPageWidget->widget());

    updateQmlLandingPageProjectInfo(projectFilePath());
    d->landingPage->setQdsInstalled(qdsInstallationExists());
    d->landingPage->setCmakeResources(ProjectFileContentTools::rootCmakeFiles());
    d->landingPage->show();
}

void QmlProjectPlugin::hideQmlLandingPage()
{
    if (d->landingPage)
        d->landingPage->hide();
}

static bool isDesignerMode(Id mode)
{
    return mode == Core::Constants::MODE_DESIGN;
}

void QmlProjectPlugin::editorModeChanged(Id newMode, Id oldMode)
{
    IEditor *currentEditor = EditorManager::currentEditor();
    if (checkIfEditorIsuiQml(currentEditor)) {
        if (isDesignerMode(newMode)) {
            if (alwaysOpenWithMode() == Core::Constants::MODE_DESIGN)
                openQds();
            else if (alwaysOpenWithMode() == Core::Constants::MODE_EDIT)
                openQtc();
            else
                displayQmlLandingPage();
        } else if (isDesignerMode(oldMode)) {
            hideQmlLandingPage();
        }
    }
}

void QmlProjectPlugin::openQtc(bool permanent)
{
    if (permanent)
        setAlwaysOpenWithMode(Core::Constants::MODE_EDIT);

    if (d->landingPage)
        hideQmlLandingPage();

    ModeManager::activateMode(Core::Constants::MODE_EDIT);
}

void QmlProjectPlugin::openQds(bool permanent)
{
    if (permanent)
        setAlwaysOpenWithMode(Core::Constants::MODE_DESIGN);

    if (d->landingPage)
        hideQmlLandingPage();

    if (IEditor *editor = EditorManager::currentEditor())
        openInQDSWithProject(editor->document()->filePath());
}

void QmlProjectPlugin::updateQmlLandingPageProjectInfo(const FilePath &projectFile)
{
    if (!d->landingPage)
        return;

    const QString qtVersionString = ProjectFileContentTools::qtVersion(projectFile);
    const QString qdsVersionString = ProjectFileContentTools::qdsVersion(projectFile);
    d->landingPage->setProjectFileExists(projectFile.exists());
    d->landingPage->setQtVersion(qtVersionString);
    d->landingPage->setQdsVersion(qdsVersionString);
}

FilePath projectFilePath()
{
    Project *project = ProjectManager::startupProject();

    if (const QmlProject *qmlProject = qobject_cast<const QmlProject*>(project))
        return qmlProject->projectFilePath();

    if (project) {
        FilePath projectFolder = project->rootProjectDirectory();
        FilePath qmlProjectFile = findQmlProject(projectFolder);
        if (qmlProjectFile.exists())
            return qmlProjectFile;
    }

    return {};
}

} // QmlProjectManager::Internal

#include "qmlprojectplugin.moc"
