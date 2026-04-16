// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "kitaspect.h"
#include "projectexplorer_export.h"

#include <utils/displayname.h>
#include <utils/filepath.h>
#include <utils/id.h>

#include <QHash>
#include <QIcon>
#include <QSet>
#include <QVariant>

#include <optional>

namespace ProjectExplorer {

// Copiable value type holding all user-editable kit data.
// We need copies to allow dirty-detection.
class PROJECTEXPLORER_EXPORT KitData
{
public:
    KitData() = default;
    KitData(const KitData &) = default;
    KitData &operator=(const KitData &) = default;

    bool operator==(const KitData &other) const;

    QString unexpandedDisplayName() const;
    QIcon icon() const;
    static QIcon iconForDeviceType(Utils::Id deviceType);

    DetectionSource m_detectionSource{DetectionSource::Manual};
    Utils::DisplayName m_unexpandedDisplayName;
    QString m_fileSystemFriendlyName;
    Utils::FilePath m_iconPath;
    Utils::Id m_deviceTypeForIcon;
    QHash<Utils::Id, QVariant> m_data;
    QSet<Utils::Id> m_sticky;
    QSet<Utils::Id> m_mutable;
    std::optional<QSet<Utils::Id>> m_irrelevantAspects;
    std::optional<QSet<Utils::Id>> m_relevantAspects;
};

} // namespace ProjectExplorer
