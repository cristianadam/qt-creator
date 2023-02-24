// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "qmlproject.h"

#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtkitinformation.h>
#include <qtsupport/qtsupportconstants.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/session.h>
#include <projectexplorer/target.h>

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>

#include "projectexplorer/devicesupport/idevice.h"
#include "qmlprojectconstants.h"
#include "qmlprojectmanagerconstants.h"
#include "utils/algorithm.h"

using namespace Core;
using namespace ProjectExplorer;

namespace QmlProjectManager {
QmlProject::QmlProject(const Utils::FilePath &fileName)
    : Project(QString::fromLatin1(Constants::QMLPROJECT_MIMETYPE), fileName)
{
    setId(QmlProjectManager::Constants::QML_PROJECT_ID);
    setProjectLanguages(Core::Context(ProjectExplorer::Constants::QMLJS_LANGUAGE_ID));
    setDisplayName(fileName.completeBaseName());

    setNeedsBuildConfigurations(false);
    setBuildSystemCreator([](Target *t) { return new QmlBuildSystem(t); });

    // FIXME: hancerli: why checking this?
    // this should not even be the case. if that's possible, then what?
    // what are the follow-up actions?
    if (!QmlProject::isQtDesignStudio())
        return;

    if (allowOnlySingleProject()) {
        Core::EditorManager::closeAllDocuments();
        SessionManager::closeAllProjects();
    }

    connect(this, &QmlProject::anyParsingFinished, this, &QmlProject::parsingFinished);
}

void QmlProject::parsingFinished(const Target *target, bool success)
{
    // trigger only once
    disconnect(this, &QmlProject::anyParsingFinished, this, &QmlProject::parsingFinished);

    // FIXME: hancerli: what to do in this case?
    if (!target || !success || !activeTarget())
        return;

    auto targetActive = activeTarget();
    auto qmlBuildSystem = qobject_cast<QmlProjectManager::QmlBuildSystem *>(
        targetActive->buildSystem());

    const Utils::FilePath mainUiFile = qmlBuildSystem->mainUiFilePath();

    if (mainUiFile.completeSuffix() == "ui.qml" && mainUiFile.exists()) {
        QTimer::singleShot(1000, [mainUiFile]() {
            Core::EditorManager::openEditor(mainUiFile, Utils::Id());
        });
        return;
    }

    Utils::FilePaths uiFiles = getUiQmlFilesForFolder(projectDirectory() + "/content");
    if (uiFiles.isEmpty()) {
        uiFiles = getUiQmlFilesForFolder(projectDirectory());
        if (uiFiles.isEmpty())
            return;
    }

    Utils::FilePath currentFile;
    if (auto cd = Core::EditorManager::currentDocument())
        currentFile = cd->filePath();

    if (currentFile.isEmpty() || !isKnownFile(currentFile)) {
        QTimer::singleShot(1000, [uiFiles]() {
            Core::EditorManager::openEditor(uiFiles.first(), Utils::Id());
        });
    }
}

Project::RestoreResult QmlProject::fromMap(const QVariantMap &map, QString *errorMessage)
{
    RestoreResult result = Project::fromMap(map, errorMessage);
    if (result != RestoreResult::Ok)
        return result;

    if (activeTarget())
        return RestoreResult::Ok;

    // find a kit that matches prerequisites (prefer default one)
    const QList<Kit *> kits = Utils::filtered(KitManager::kits(), [this](const Kit *k) {
        return !containsType(projectIssues(k), Task::TaskType::Error)
               && DeviceTypeKitAspect::deviceTypeId(k)
                      == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE;
    });

    if (!kits.isEmpty()) {
        if (kits.contains(KitManager::defaultKit()))
            addTargetForDefaultKit();
        else
            addTargetForKit(kits.first());
    }

    // FIXME: hancerli: are there any other way?
    // What if it's not a Design Studio project? What should we do then?
    if (QmlProject::isQtDesignStudio()) {
        int preferedVersion = preferedQtTarget(activeTarget());

        if (activeTarget())
            removeTarget(activeTarget());
        setKitWithVersion(preferedVersion, kits);
    }

    return RestoreResult::Ok;
}

bool QmlProject::setKitWithVersion(const int qtMajorVersion, const QList<Kit *> kits)
{
    const QList<Kit *> qtVersionkits = Utils::filtered(kits, [qtMajorVersion](const Kit *k) {
        if (!k->isAutoDetected())
            return false;

        if (k->isReplacementKit())
            return false;

        QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(k);
        return (version && version->qtVersion().majorVersion() == qtMajorVersion);
    });

    if (!qtVersionkits.isEmpty()) {
        if (qtVersionkits.contains(KitManager::defaultKit()))
            addTargetForDefaultKit();
        else
            addTargetForKit(qtVersionkits.first());
    }

    return true;
}


Utils::FilePaths QmlProject::getUiQmlFilesForFolder(const Utils::FilePath &folder)
{
    const Utils::FilePaths uiFiles = files([&](const Node *node) {
        return node->filePath().completeSuffix() == "ui.qml"
               && node->filePath().parentDir() == folder;
    });
    return uiFiles;
}

Tasks QmlProject::projectIssues(const Kit *k) const
{
    Tasks result = Project::projectIssues(k);

    const QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(k);
    if (!version)
        result.append(createProjectTask(Task::TaskType::Warning, tr("No Qt version set in kit.")));

    IDevice::ConstPtr dev = DeviceKitAspect::device(k);
    if (dev.isNull())
        result.append(createProjectTask(Task::TaskType::Error, tr("Kit has no device.")));

    if (version && version->qtVersion() < QVersionNumber(5, 0, 0))
        result.append(createProjectTask(Task::TaskType::Error, tr("Qt version is too old.")));

    if (dev.isNull() || !version)
        return result; // No need to check deeper than this

    if (dev->type() == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE) {
        if (version->type() == QtSupport::Constants::DESKTOPQT) {
            if (version->qmlRuntimeFilePath().isEmpty()) {
                result.append(
                    createProjectTask(Task::TaskType::Error, tr("Qt version has no QML utility.")));
            }
        } else {
            // Non-desktop Qt on a desktop device? We don't support that.
            result.append(createProjectTask(Task::TaskType::Error,
                                            tr("Non-desktop Qt is used with a desktop device.")));
        }
    } else {
        // If not a desktop device, don't check the Qt version for qml runtime binary.
        // The device is responsible for providing it and we assume qml runtime can be found
        // in $PATH if it's not explicitly given.
    }

    return result;
}

bool QmlProject::isQtDesignStudio()
{
    QSettings *settings = Core::ICore::settings();
    const QString qdsStandaloneEntry = "QML/Designer/StandAloneMode";
    return settings->value(qdsStandaloneEntry, false).toBool();
}

bool QmlProject::isQtDesignStudioStartedFromQtC()
{
    return qEnvironmentVariableIsSet(Constants::enviromentLaunchedQDS);
}

DeploymentKnowledge QmlProject::deploymentKnowledge() const
{
    return DeploymentKnowledge::Perfect;
}

bool QmlProject::isEditModePreferred() const
{
    return !isQtDesignStudio();
}

int QmlProject::preferedQtTarget(Target *target)
{
    if (!target)
        return -1;

    auto buildSystem = qobject_cast<QmlBuildSystem *>(target->buildSystem());
    return (buildSystem && buildSystem->qt6Project()) ? 6 : 5;
}

bool QmlProject::allowOnlySingleProject()
{
    QSettings *settings = Core::ICore::settings();
    auto key = "QML/Designer/AllowMultipleProjects";
    return !settings->value(key, false).toBool();
}

} // namespace QmlProjectManager
