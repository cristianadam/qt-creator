// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>
#include <utils/result.h>

#include <QIODevice>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Android::Internal {

using PermissionAttributes = QMap<QString, QString>;
using PermissionMap = QMap<QString, PermissionAttributes>;

void insertPermission(PermissionMap &permissions, const QString &name,
                      const PermissionAttributes &attributes);

class AndroidManifestParser
{
public:
    struct ManifestData {
        QString iconName;
        bool hasIcon = false;
        PermissionMap permissions; // name -> {attrName: value}
        bool hasDefaultPermissionsComment = false;
        bool hasDefaultFeaturesComment = false;
    };
    struct ModifyParams {
        bool shouldModifyApplication = false;
        QStringList applicationKeys;
        QStringList applicationValues;
        QStringList applicationKeysToRemove;

        bool shouldModifyPermissions = false;
        QSet<QString> permissionsToKeep;
        bool shouldModifyDefaultsComments = false;
        bool writeDefaultPermissionsComment = false;
        bool writeDefaultFeaturesComment = false;

        bool shouldModifyActivityMetaData = false;
        QString activityMetaDataName;
        QString activityMetaDataValue;
    };

    static Utils::Result<ManifestData> readManifest(const Utils::FilePath &manifestPath);
    static Utils::Result<> processAndWriteManifest(const Utils::FilePath &manifestPath,
                                                       const ModifyParams &instructions);

};

Utils::Result<> updateManifestApplicationAttribute(const Utils::FilePath &manifestPath,
                                                       const QString &attributeKey,
                                                       const QString &attributeValue);
Utils::Result<> updateManifestPermissions(const Utils::FilePath &manifestPath,
                                              const QStringList &permissions,
                                              bool includeDefaultPermissions,
                                              bool includeDefaultFeatures);
Utils::Result<> updateManifestDefaultComments(const Utils::FilePath &manifestPath,
                                                  bool includeDefaultPermissions,
                                                  bool includeDefaultFeatures);

Utils::Result<> updateManifestActivityMetaData(const Utils::FilePath &manifestPath,
                                                   const QString &metaDataName,
                                                   const QString &metaDataValue);
Utils::Result<QString> readManifestActivityMetaData(const Utils::FilePath &manifestPath,
                                                    const QString &metaDataName);
Utils::Result<> updateManifestPermissionAttributes(const Utils::FilePath &manifestPath,
                                                       const QString &permission,
                                                       const PermissionAttributes &attributes);
Utils::Result<> writeFileWithEditorReload(const Utils::FilePath &filePath,
                                              const QByteArray &content,
                                              QIODevice::OpenMode mode);

} // namespace Android::Internal
