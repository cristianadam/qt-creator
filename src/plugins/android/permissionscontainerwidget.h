// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "androidmanifestutils.h"

#include <utils/filepath.h>
#include <utils/result.h>

#include <QWidget>

#include <optional>

class QCheckBox;
class QComboBox;
class QPushButton;
class QTreeView;

namespace ProjectExplorer { class Project; }
namespace TextEditor { class TextEditorWidget; }
namespace Utils { class InfoLabel; }
namespace CMakeProjectManager { class CMakeListFile; }

namespace Android::Internal {
class PermissionsModel;

class PermissionsContainerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PermissionsContainerWidget(QWidget *parent = nullptr);
    bool initialize(TextEditor::TextEditorWidget *textEditorWidget);
    void refresh();

signals:
    void permissionsModified();

private:
    void addPermission();
    void removePermission();
    void editAttributes();
    void updateAddRemovePermissionButtons();
    void updateCMakePermissionsCheckBoxState(
        const std::optional<Utils::Result<CMakeProjectManager::CMakeListFile>> &cmakeFile,
        const Utils::Result<AndroidManifestParser::ManifestData> &manifestData);
    void defaultPermissionOrFeatureCheckBoxClicked();
    Utils::Result<> updateManifestPermissions();
    Utils::Result<> updateManifestDefaultComments();
    void loadPermissionsFromManifest();
    void loadPermissionsFromManifest(
        const Utils::Result<AndroidManifestParser::ManifestData> &manifestData);
    void showCMakePermissionsConsentDialog();
    void onCMakePermissionsCheckBoxChanged();
    bool isCMakePermissionsSupported() const;
    Utils::FilePath manifestPath() const;
    ProjectExplorer::Project *currentProject() const;
    bool ensureCMakeInfo();

    bool resolveCMakeProjectInfo();
    void loadPermissionsFromCMake();
    void loadPermissionsFromCMake(
        const std::optional<Utils::Result<CMakeProjectManager::CMakeListFile>> &cmakeFile,
        const Utils::Result<AndroidManifestParser::ManifestData> &manifestData);
    Utils::Result<> addCMakePermission(const QString &permission,
                                       const PermissionAttributes &attributes = {});
    Utils::Result<> removeCMakePermission(const QString &permission);
    Utils::Result<> updateCMakePermission(const QString &permission,
                                          const PermissionAttributes &attributes);
    Utils::Result<> writeCMakeFile(const QString &content);

    TextEditor::TextEditorWidget *m_textEditorWidget = nullptr;
    Utils::FilePath m_CMakeFilePath;
    QString m_CMakeTargetName;

    QCheckBox *m_defaultPermissonsCheckBox = nullptr;
    QCheckBox *m_defaultFeaturesCheckBox = nullptr;
    QComboBox *m_permissionsComboBox = nullptr;
    QCheckBox *m_CMakePermissionsCheckBox = nullptr;
    QPushButton *m_addPermissionButton = nullptr;
    QPushButton *m_removePermissionButton = nullptr;
    QPushButton *m_editAttributesButton = nullptr;
    QTreeView *m_permissionsListView = nullptr;
    PermissionsModel *m_permissionsModel = nullptr;
    bool m_checkBoxStateInitialized = false;
    Utils::InfoLabel *m_CMakeErrorLabel = nullptr;
    bool m_CMakeFileBroken = false;
};

} // namespace Android::Internal
