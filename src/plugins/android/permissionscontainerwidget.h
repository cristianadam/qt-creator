// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/fileutils.h>

#include <QMap>
#include <QWidget>
#include <QStringList>
#include <QTreeView>

#include <utils/fileutils.h>

class QCheckBox;
class QComboBox;
class QPushButton;
class QListView;

namespace ProjectExplorer { class Project; }
namespace TextEditor { class TextEditorWidget; }

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
    void defaultPermissionOrFeatureCheckBoxClicked();
    void updateManifestPermissions();
    void loadPermissionsFromManifest();
    void showCMakePermissionsConsentDialog();
    void onCMakePermissionsCheckBoxChanged();
    bool isCMakePermissionsSupported();
    bool hasPermissionsInManifest(Utils::FilePath &manifestPath);
    bool resolveCmakeProjectInfo();
    void loadPermissionsFromCmake();
    void addCmakePermission(const QString &permission);
    bool removeCmakePermission(const QString &permission);
    void updateCmakePermission(const QString &permission, const QMap<QString, QString> &attributes);

    TextEditor::TextEditorWidget *m_textEditorWidget = nullptr;
    Utils::FilePath m_cmakeFilePath;
    QString m_cmakeTargetName;
    ProjectExplorer::Project *m_project = nullptr;
    QCheckBox *m_defaultPermissonsCheckBox = nullptr;
    QCheckBox *m_defaultFeaturesCheckBox = nullptr;
    QComboBox *m_permissionsComboBox = nullptr;
    QCheckBox *m_CMakePermissionsCheckBox = nullptr;
    QPushButton *m_addPermissionButton = nullptr;
    QPushButton *m_removePermissionButton = nullptr;
    QPushButton *m_editAttributesButton = nullptr;
    QTreeView *m_permissionsListView = nullptr;
    PermissionsModel *m_permissionsModel = nullptr;
};

} // namespace Android::Internal
