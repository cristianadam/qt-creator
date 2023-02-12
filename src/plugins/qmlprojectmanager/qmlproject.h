// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "buildsystem/qmlbuildsystem.h" // IWYU pragma: keep
#include <projectexplorer/project.h>

namespace QmlProjectManager {

class QmlProject;

class Q_DECL_EXPORT QmlProject : public ProjectExplorer::Project
{
    Q_OBJECT
public:
    explicit QmlProject(const Utils::FilePath &filename);

    static bool isQtDesignStudio();
    static bool isQtDesignStudioStartedFromQtC();
    bool isEditModePreferred() const override;

    ProjectExplorer::Tasks projectIssues(const ProjectExplorer::Kit *k) const final;

protected:
    RestoreResult fromMap(const QVariantMap &map, QString *errorMessage) override;

private:
    ProjectExplorer::DeploymentKnowledge deploymentKnowledge() const override;
    Utils::FilePaths getUiQmlFilesForFolder(const Utils::FilePath &folder);

    bool setKitWithVersion(const int qtMajorVersion, const QList<ProjectExplorer::Kit *> kits);

    bool allowOnlySingleProject();
    int preferedQtTarget(ProjectExplorer::Target *target);

private slots:
    void parsingFinished(const ProjectExplorer::Target *target, bool success);
};
} // namespace QmlProjectManager
