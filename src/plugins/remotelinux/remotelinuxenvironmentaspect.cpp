// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "remotelinuxenvironmentaspect.h"

#include "linuxdevice.h"
#include "remotelinuxtr.h"

#include <coreplugin/icore.h>

#include <projectexplorer/environmentaspectwidget.h>
#include <projectexplorer/environmentwidget.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/target.h>

#include <utils/algorithm.h>
#include <utils/devicefileaccess.h>
#include <utils/qtcassert.h>

#include <QMessageBox>

using namespace ProjectExplorer;
using namespace Utils;

namespace RemoteLinux {

const char DISPLAY_KEY[] = "DISPLAY";
const char VERSION_KEY[] = "RemoteLinux.EnvironmentAspect.Version";
const int ENVIRONMENTASPECT_VERSION = 1; // Version was introduced in 4.3 with the value 1

static bool displayAlreadySet(const Utils::EnvironmentItems &changes)
{
    return Utils::contains(changes, [](const Utils::EnvironmentItem &item) {
        return item.name == DISPLAY_KEY;
    });
}

static Environment fetchEnvironment(const Target *target)
{
    const IDevice::ConstPtr &device = DeviceKitAspect::device(target->kit());
    QTC_ASSERT(device, return {});
    DeviceFileAccess *access = device->fileAccess();
    QTC_ASSERT(device, return {});
    return access->deviceEnvironment();
}

RemoteLinuxEnvironmentAspect::RemoteLinuxEnvironmentAspect(Target *target)
{
    addSupportedBaseEnvironment(Tr::tr("Clean Environment"), {});
    addPreferredBaseEnvironment(Tr::tr("System Environment"), [target] {
        return fetchEnvironment(target);
    });

    setConfigWidgetCreator([this, target]() -> QWidget * {
        auto widget = new EnvironmentAspectWidget(this);

        connect(target, &Target::kitChanged, this, &EnvironmentAspect::environmentChanged);

        widget->envWidget()->setOpenTerminalFunc([target](const Environment &env) {
            if (IDevice::ConstPtr device = DeviceKitAspect::device(target->kit())) {
                device->openTerminal(env, FilePath());
            } else {
                QMessageBox::critical(Core::ICore::dialogParent(),
                                      Tr::tr("Cannot Open Terminal"),
                                      Tr::tr("Cannot open remote terminal: Current kit has no device."));
            }
        });

        return widget;
    });
}

void RemoteLinuxEnvironmentAspect::fromMap(const QVariantMap &map)
{
    ProjectExplorer::EnvironmentAspect::fromMap(map);

    const auto version = map.value(QLatin1String(VERSION_KEY), 0).toInt();
    if (version == 0) {
        // In Qt Creator versions prior to 4.3 RemoteLinux included DISPLAY=:0.0 in the base
        // environment, if DISPLAY was not set. In order to keep existing projects expecting
        // that working, add the DISPLAY setting to user changes in them. New projects will
        // have version 1 and will not get DISPLAY set.
        auto changes = userEnvironmentChanges();
        if (!displayAlreadySet(changes)) {
            changes.append(Utils::EnvironmentItem(QLatin1String(DISPLAY_KEY), QLatin1String(":0.0")));
            setUserEnvironmentChanges(changes);
        }
    }
}

void RemoteLinuxEnvironmentAspect::toMap(QVariantMap &map) const
{
    ProjectExplorer::EnvironmentAspect::toMap(map);
    map.insert(QLatin1String(VERSION_KEY), ENVIRONMENTASPECT_VERSION);
}

} // namespace RemoteLinux

