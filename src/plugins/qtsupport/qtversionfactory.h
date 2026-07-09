// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtsupport_global.h"

#include <projectexplorer/kitaspect.h>

#include <utils/store.h>

namespace Utils { class FilePath; }

namespace QtSupport {

class QtVersion;

class QTSUPPORT_EXPORT QtVersionFactory
{
public:
    QtVersionFactory();
    virtual ~QtVersionFactory();

    static const QList<QtVersionFactory *> allQtVersionFactories();

    bool canRestore(const QString &type);
    QtVersion *restore(const QString &type, const Utils::Store &data, const Utils::FilePath &workingDirectory);

    /// factories with higher priority are asked first to identify
    /// a qtversion, the priority of the desktop factory is 0 and
    /// the desktop factory claims to handle all paths
    int priority() const { return m_priority; }

    static QtVersion *createQtVersionFromQMakePath(
        const Utils::FilePath &qmakePath,
        const ProjectExplorer::DetectionSource &detectionSource,
        QString *error = nullptr);

protected:
    struct SetupData
    {
        QStringList platforms;
        QStringList config;
        bool isQnx = false; // eeks
        QString mkspec;
    };

    void setQtVersionCreator(const std::function<QtVersion *()> &creator);
    void setRestrictionChecker(const std::function<bool(const SetupData &)> &checker);
    void setSupportedType(const QString &type);
    void setPriority(int priority);

    // Additional stored type strings this factory also restores, e.g. to migrate a retired
    // QtVersion type to the one this factory creates.
    void addLegacyRestoreType(const QString &type);

private:
    friend class QtVersion;
    QtVersion *create() const;

    std::function<QtVersion *()> m_creator;
    std::function<bool(const SetupData &)> m_restrictionChecker;
    QString m_supportedType;
    QStringList m_legacyTypes;
    int m_priority = 0;
};

} // namespace QtSupport
