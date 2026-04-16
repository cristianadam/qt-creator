// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "kitdata.h"

#include "devicesupport/idevicefactory.h"
#include "projectexplorerconstants.h"

#include <utils/algorithm.h>

using namespace Utils;

namespace ProjectExplorer {

QIcon KitData::iconForDeviceType(Id deviceType)
{
    const IDeviceFactory *factory = findOrDefault(IDeviceFactory::allDeviceFactories(),
        [&deviceType](const IDeviceFactory *f) { return f->deviceType() == deviceType; });
    return factory ? factory->icon() : QIcon();
}

QString KitData::unexpandedDisplayName() const
{
    return m_unexpandedDisplayName.value();
}

bool KitData::operator==(const KitData &other) const
{
    return m_detectionSource == other.m_detectionSource
        && m_data == other.m_data
        && m_iconPath == other.m_iconPath
        && m_deviceTypeForIcon == other.m_deviceTypeForIcon
        && m_unexpandedDisplayName == other.m_unexpandedDisplayName
        && m_fileSystemFriendlyName == other.m_fileSystemFriendlyName
        && m_relevantAspects == other.m_relevantAspects
        && m_irrelevantAspects == other.m_irrelevantAspects
        && m_mutable == other.m_mutable;
    // Note: This leaves out m_sticky, this is intentional for now:
    // Kit::setSticky() calls kitUpdated() directly and never goes through the working
    // copy, so dirty-detection doesn't need to track it
}

QIcon KitData::icon() const
{
    if (!m_deviceTypeForIcon.isValid() && !m_iconPath.isEmpty() && m_iconPath.exists())
        return QIcon(m_iconPath.toFSPathString());
    if (m_deviceTypeForIcon.isValid()) {
        const QIcon deviceTypeIcon = iconForDeviceType(m_deviceTypeForIcon);
        if (!deviceTypeIcon.isNull())
            return deviceTypeIcon;
    }
    // FIXME: Kit::icon() uses RunDeviceTypeKitAspect::deviceTypeId() as a fallback here,
    // so working copies of kits with a non-desktop run device show the wrong icon.
    return iconForDeviceType(Constants::DESKTOP_DEVICE_TYPE);
}

} // namespace ProjectExplorer
