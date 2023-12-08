// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "idevice.h"

#include <projectexplorer/projectexplorer_export.h>

#include <QObject>

#include <memory>

namespace Utils { class FilePath; }

namespace ProjectExplorer {
class ProjectExplorerPlugin;

namespace Internal {
class DeviceManagerPrivate;
class DeviceSettingsWidget;
} // namespace Internal

class PROJECTEXPLORER_EXPORT DeviceManagerBase : public QObject
{
    Q_OBJECT

public:
    ~DeviceManagerBase();

    int deviceCount() const;
    IDevice::ConstPtr deviceAt(int index) const;
    IDevice::ConstPtr find(Utils::Id id) const;
    IDevice::ConstPtr defaultDevice(Utils::Id deviceType) const;

signals:
    void deviceAdded(Utils::Id id);
    void deviceRemoved(Utils::Id id);
    void deviceUpdated(Utils::Id id);
    void deviceListReplaced(); // For bulk changes via the settings dialog.
    void updated(); // Emitted for all of the above.

protected:
    DeviceManagerBase();

    const std::unique_ptr<Internal::DeviceManagerPrivate> d;
};

class PROJECTEXPLORER_EXPORT DeviceManager final : public DeviceManagerBase
{
    Q_OBJECT
    friend class Internal::DeviceSettingsWidget;
    friend class IDevice;

public:
    ~DeviceManager() override;

    static DeviceManager *instance();
    static DeviceManager *clonedInstance();

    void forEachDevice(const std::function<void(const IDeviceConstPtr &)> &) const;

    bool hasDevice(const QString &name) const;

    void addDevice(const IDevice::ConstPtr &device);
    void removeDevice(Utils::Id id);
    void setDeviceState(Utils::Id deviceId, IDevice::DeviceState deviceState);

    bool isLoaded() const;

    static IDevice::ConstPtr deviceForPath(const Utils::FilePath &path);
    static IDevice::ConstPtr defaultDesktopDevice();

signals:
    void devicesLoaded(); // Emitted once load() is done

private:
    void save();

    DeviceManager(bool isInstance = true);

    void load();
    QList<IDevice::Ptr> fromMap(const Utils::Store &map, QHash<Utils::Id, Utils::Id> *defaultDevices);
    Utils::Store toMap() const;

    // For SettingsWidget.
    IDevice::Ptr mutableDevice(Utils::Id id) const;
    void setDefaultDevice(Utils::Id id);
    static DeviceManager *cloneInstance();
    static void replaceInstance();
    static void removeClonedInstance();

    static void copy(const DeviceManager *source, DeviceManager *target, bool deep);

    friend class Internal::DeviceManagerPrivate;
    friend class ProjectExplorerPlugin;
    friend class ProjectExplorerPluginPrivate;
};

} // namespace ProjectExplorer
