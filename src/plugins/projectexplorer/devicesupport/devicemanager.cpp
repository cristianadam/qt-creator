// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "devicemanager.h"

#include "../projectexplorertr.h"
#include "idevicefactory.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <utils/algorithm.h>
#include <utils/devicefileaccess.h>
#include <utils/environment.h>
#include <utils/fileutils.h>
#include <utils/fsengine/fsengine.h>
#include <utils/persistentsettings.h>
#include <utils/portlist.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/stringutils.h>

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QVariantList>

#include <limits>
#include <memory>

using namespace Utils;

namespace ProjectExplorer {
namespace Internal {

const char DeviceManagerKey[] = "DeviceManager";
const char DeviceListKey[] = "DeviceList";
const char DefaultDevicesKey[] = "DefaultDevices";

class DeviceManagerPrivate
{
public:
    DeviceManagerPrivate(DeviceManager *p)
        : q(p){};

    QList<IDevice::Ptr> deviceList() const
    {
        QMutexLocker lk(&mutex);
        return devices;
    }

    QHash<Utils::Id, Utils::Id> defaultDeviceList() const
    {
        QMutexLocker lk(&mutex);
        return defaultDevices;
    }

    void setDefaultDevice(Utils::Id deviceType, Utils::Id deviceId)
    {
        QMutexLocker lk(&mutex);
        setDefaultDevice(deviceType, deviceId, lk);
    }

    void copyFrom(const DeviceManagerPrivate &other, bool deep)
    {
        QMutexLocker lk(&mutex);
        // We need to keep the old devices around, as they might try to access the mutex during
        // destruction.
        QList<IDevice::Ptr> oldDevices = devices;

        if (deep) {
            for (const IDevice::Ptr &device : std::as_const(other.devices))
                devices << device->clone();
        } else {
            devices = other.devices;
        }
        defaultDevices = other.defaultDevices;

        // Unlock before destroying the old devices.
        lk.unlock();
        oldDevices.clear();
    }

    IDevice::Ptr deviceById(Utils::Id id) const
    {
        QMutexLocker lk(&mutex);
        return deviceById(id, lk);
    }

    IDevice::Ptr defaultDeviceById(Utils::Id id) const
    {
        QMutexLocker lk(&mutex);
        return defaultDeviceById(id, lk);
    }

    qsizetype deviceCount() const
    {
        QMutexLocker lk(&mutex);
        return devices.count();
    }

    int addDevice(IDevice::Ptr device)
    {
        QMutexLocker lk(&mutex);

        QStringList names;
        for (const IDevice::Ptr &tmp : devices) {
            if (tmp->id() != device->id())
                names << tmp->displayName();
        }

        device->setDisplayName(Utils::makeUniquelyNumbered(device->displayName(), names));

        const int pos = indexForId(device->id());

        if (!defaultDeviceById(device->type(), lk))
            setDefaultDevice(device->type(), device->id(), lk);

        if (q == DeviceManager::instance() && clonedInstance)
            clonedInstance->addDevice(device->clone());

        if (pos >= 0) {
            devices.replace(pos, device);
            return pos;
        }

        devices.append(device);
        return pos;
    }

    struct RemoveResult
    {
        IDevice::Ptr removedDevice;
        IDevice::Ptr newDefaultDevice;
    };

    expected_str<RemoveResult> removeDevice(Utils::Id id)
    {
        QMutexLocker lk(&mutex);
        RemoveResult result;

        const IDevice::Ptr deviceToRemove = deviceById(id, lk);
        if (!deviceToRemove) {
            return make_unexpected(
                QString("Could not remove Device \"%1\", not found.").arg(id.toString()));
        }
        if (q == DeviceManager::instance() && !deviceToRemove->isAutoDetected()) {
            return make_unexpected(QString(
                "removeDevice called on main singleton instance for manually added device."));
        }

        const bool wasDefault = defaultDevices.value(deviceToRemove->type())
                                == deviceToRemove->id();
        const Utils::Id deviceType = deviceToRemove->type();

        devices.removeOne(deviceToRemove);
        result.removedDevice = deviceToRemove;

        if (wasDefault) {
            for (const IDevice::Ptr &newDefaultDevice : std::as_const(devices)) {
                if (newDefaultDevice->type() == deviceType) {
                    setDefaultDevice(newDefaultDevice->type(), newDefaultDevice->id(), lk);
                    result.newDefaultDevice = newDefaultDevice;
                    break;
                }
            }
        }

        if (q == DeviceManager::instance() && clonedInstance)
            clonedInstance->removeDevice(id);

        return result;
    }

private:
    int indexForId(Utils::Id id) const
    {
        for (int i = 0; i < devices.count(); ++i) {
            if (devices.at(i)->id() == id)
                return i;
        }
        return -1;
    }

    void setDefaultDevice(Utils::Id deviceType, Utils::Id deviceId, const QMutexLocker<QMutex> &lk)
    {
        Q_UNUSED(lk);
        defaultDevices.insert(deviceType, deviceId);
    }

    inline IDevice::Ptr deviceById(Utils::Id id, const QMutexLocker<QMutex> &lk) const
    {
        Q_UNUSED(lk);
        for (const IDevice::Ptr &device : std::as_const(devices)) {
            if (device->id() == id)
                return device;
        }
        return IDevice::Ptr();
    }

    IDevice::Ptr defaultDeviceById(Utils::Id id, const QMutexLocker<QMutex> &lk) const
    {
        Q_UNUSED(lk);
        return deviceById(defaultDevices.value(id), lk);
    }

public:
    Utils::PersistentSettingsWriter *writer = nullptr;
    static DeviceManager *clonedInstance;

private:
    mutable QMutex mutex;
    QList<IDevice::Ptr> devices;
    QHash<Utils::Id, Utils::Id> defaultDevices;

    DeviceManager *q;
};

DeviceManager *DeviceManagerPrivate::clonedInstance = nullptr;

} // namespace Internal

using namespace Internal;

DeviceManager *DeviceManager::m_instance = nullptr;

DeviceManager *DeviceManager::instance()
{
    return m_instance;
}

int DeviceManager::deviceCount() const
{
    return d->deviceCount();
}

void DeviceManager::replaceInstance()
{
    const QList<Id> newIds = Utils::transform(DeviceManagerPrivate::clonedInstance->d->deviceList(),
                                              &IDevice::id);

    for (const IDevice::Ptr &dev : m_instance->d->deviceList()) {
        if (!newIds.contains(dev->id()))
            dev->aboutToBeRemoved();
    }

    copy(DeviceManagerPrivate::clonedInstance, instance(), false);

    emit instance()->deviceListReplaced();
    emit instance()->updated();
}

void DeviceManager::removeClonedInstance()
{
    delete DeviceManagerPrivate::clonedInstance;
    DeviceManagerPrivate::clonedInstance = nullptr;
}

DeviceManager *DeviceManager::cloneInstance()
{
    QTC_ASSERT(!DeviceManagerPrivate::clonedInstance, return nullptr);

    DeviceManagerPrivate::clonedInstance = new DeviceManager(false);
    copy(instance(), DeviceManagerPrivate::clonedInstance, true);
    return DeviceManagerPrivate::clonedInstance;
}

void DeviceManager::copy(const DeviceManager *source, DeviceManager *target, bool deep)
{
    target->d->copyFrom(*source->d, deep);
}

void DeviceManager::save()
{
    if (d->clonedInstance == this || !d->writer)
        return;
    QVariantMap data;
    data.insert(QLatin1String(DeviceManagerKey), toMap());
    d->writer->save(data, Core::ICore::dialogParent());
}

static FilePath settingsFilePath(const QString &extension)
{
    return Core::ICore::userResourcePath(extension);
}

static FilePath systemSettingsFilePath(const QString &deviceFileRelativePath)
{
    return Core::ICore::installerResourcePath(deviceFileRelativePath);
}

void DeviceManager::load()
{
    QTC_ASSERT(!d->writer, return);

    // Only create writer now: We do not want to save before the settings were read!
    d->writer = new PersistentSettingsWriter(settingsFilePath("devices.xml"), "QtCreatorDevices");

    Utils::PersistentSettingsReader reader;
    // read devices file from global settings path
    QHash<Utils::Id, Utils::Id> defaultDevices;
    QList<IDevice::Ptr> sdkDevices;
    if (reader.load(systemSettingsFilePath("devices.xml"))) {
        sdkDevices = fromMap(reader.restoreValues().value(DeviceManagerKey).toMap(),
                             &defaultDevices);
    }
    // read devices file from user settings path
    QList<IDevice::Ptr> userDevices;
    if (reader.load(settingsFilePath("devices.xml"))) {
        userDevices = fromMap(reader.restoreValues().value(DeviceManagerKey).toMap(),
                              &defaultDevices);
    }
    // Insert devices into the model. Prefer the higher device version when there are multiple
    // devices with the same id.
    for (IDevice::ConstPtr device : std::as_const(userDevices)) {
        for (const IDevice::Ptr &sdkDevice : std::as_const(sdkDevices)) {
            if (device->id() == sdkDevice->id() || device->rootPath() == sdkDevice->rootPath()) {
                if (device->version() < sdkDevice->version())
                    device = sdkDevice;
                sdkDevices.removeOne(sdkDevice);
                break;
            }
        }
        addDevice(device);
    }
    // Append the new SDK devices to the model.
    for (const IDevice::Ptr &sdkDevice : std::as_const(sdkDevices))
        addDevice(sdkDevice);

    // Overwrite with the saved default devices.
    for (auto itr = defaultDevices.constBegin(); itr != defaultDevices.constEnd(); ++itr) {
        IDevice::ConstPtr device = find(itr.value());
        if (device)
            d->setDefaultDevice(device->type(), device->id());
    }

    emit devicesLoaded();
}

static const IDeviceFactory *restoreFactory(const QVariantMap &map)
{
    const Utils::Id deviceType = IDevice::typeFromMap(map);
    IDeviceFactory *factory = Utils::findOrDefault(IDeviceFactory::allDeviceFactories(),
                                                   [&map, deviceType](IDeviceFactory *factory) {
                                                       return factory->canRestore(map)
                                                              && factory->deviceType()
                                                                     == deviceType;
                                                   });

    if (!factory)
        qWarning("Warning: No factory found for device '%s' of type '%s'.",
                 qPrintable(IDevice::idFromMap(map).toString()),
                 qPrintable(IDevice::typeFromMap(map).toString()));
    return factory;
}

QList<IDevice::Ptr> DeviceManager::fromMap(const QVariantMap &map,
                                           QHash<Utils::Id, Utils::Id> *defaultDevices)
{
    QList<IDevice::Ptr> devices;

    if (defaultDevices) {
        const QVariantMap defaultDevsMap = map.value(DefaultDevicesKey).toMap();
        for (auto it = defaultDevsMap.constBegin(); it != defaultDevsMap.constEnd(); ++it)
            defaultDevices->insert(Utils::Id::fromString(it.key()),
                                   Utils::Id::fromSetting(it.value()));
    }
    const QVariantList deviceList = map.value(QLatin1String(DeviceListKey)).toList();
    for (const QVariant &v : deviceList) {
        const QVariantMap map = v.toMap();
        const IDeviceFactory *const factory = restoreFactory(map);
        if (!factory)
            continue;
        const IDevice::Ptr device = factory->construct();
        QTC_ASSERT(device, continue);
        device->fromMap(map);
        devices << device;
    }
    return devices;
}

QVariantMap DeviceManager::toMap() const
{
    QVariantMap map;
    QVariantMap defaultDeviceMap;

    for (const auto &[type, id] : d->defaultDeviceList().asKeyValueRange())
        defaultDeviceMap.insert(type.toString(), id.toSetting());

    map.insert(QLatin1String(DefaultDevicesKey), defaultDeviceMap);
    QVariantList deviceList;
    for (const IDevice::Ptr &device : d->deviceList())
        deviceList << device->toMap();
    map.insert(QLatin1String(DeviceListKey), deviceList);
    return map;
}

void DeviceManager::addDevice(const IDevice::ConstPtr &device)
{
    int pos = d->addDevice(device->clone());

    if (pos >= 0) {
        emit deviceUpdated(device->id());
    } else {
        emit deviceAdded(device->id());

        if (FSEngine::isAvailable())
            Utils::FSEngine::addDevice(device->rootPath());
    }

    emit updated();
}

void DeviceManager::removeDevice(Utils::Id id)
{
    const expected_str<DeviceManagerPrivate::RemoveResult> result = d->removeDevice(id);
    QTC_ASSERT_EXPECTED(result, return);

    emit deviceRemoved(result->removedDevice->id());

    if (FSEngine::isAvailable())
        Utils::FSEngine::removeDevice(result->removedDevice->rootPath());

    if (result->newDefaultDevice)
        emit deviceUpdated(result->newDefaultDevice->id());

    emit updated();
}

void DeviceManager::setDeviceState(Utils::Id deviceId, IDevice::DeviceState deviceState)
{
    // To see the state change in the DeviceSettingsWidget. This has to happen before
    // calling d->deviceById(...), in case the device is only present in the cloned instance.
    if (this == instance() && d->clonedInstance)
        d->clonedInstance->setDeviceState(deviceId, deviceState);

    const IDevice::Ptr device = d->deviceById(deviceId);
    if (!device)
        return;

    if (device->deviceState() == deviceState)
        return;

    // TODO: make it thread safe?
    device->setDeviceState(deviceState);
    emit deviceUpdated(deviceId);
    emit updated();
}

bool DeviceManager::isLoaded() const
{
    return d->writer;
}

// Thread safe
IDevice::ConstPtr DeviceManager::deviceForPath(const FilePath &path)
{
    const QList<IDevice::Ptr> devices = instance()->d->deviceList();

    if (path.scheme() == u"device") {
        for (const IDevice::Ptr &dev : devices) {
            if (path.host() == dev->id().toString())
                return dev;
        }
        return {};
    }

    for (const IDevice::Ptr &dev : devices) {
        // TODO: ensure handlesFile is thread safe
        if (dev->handlesFile(path))
            return dev;
    }
    return {};
}

IDevice::ConstPtr DeviceManager::defaultDesktopDevice()
{
    return m_instance->defaultDevice(Constants::DESKTOP_DEVICE_TYPE);
}

void DeviceManager::setDefaultDevice(Utils::Id id)
{
    QTC_ASSERT(this != instance(), return);

    const IDevice::ConstPtr &device = find(id);
    QTC_ASSERT(device, return);
    const IDevice::ConstPtr &oldDefaultDevice = defaultDevice(device->type());
    if (device == oldDefaultDevice)
        return;
    d->setDefaultDevice(device->type(), device->id());
    emit deviceUpdated(device->id());
    emit deviceUpdated(oldDefaultDevice->id());

    emit updated();
}

DeviceManager::DeviceManager(bool isInstance)
    : d(std::make_unique<DeviceManagerPrivate>(this))
{
    QTC_ASSERT(isInstance == !m_instance, return);

    if (!isInstance)
        return;

    m_instance = this;
    connect(Core::ICore::instance(),
            &Core::ICore::saveSettingsRequested,
            this,
            &DeviceManager::save);

    DeviceFileHooks &deviceHooks = DeviceFileHooks::instance();

    deviceHooks.isSameDevice = [](const FilePath &left, const FilePath &right) {
        auto leftDevice = DeviceManager::deviceForPath(left);
        auto rightDevice = DeviceManager::deviceForPath(right);

        return leftDevice == rightDevice;
    };

    deviceHooks.localSource = [](const FilePath &file) -> expected_str<FilePath> {
        auto device = DeviceManager::deviceForPath(file);
        if (!device)
            return make_unexpected(Tr::tr("No device for path \"%1\"").arg(file.toUserOutput()));
        return device->localSource(file);
    };

    deviceHooks.fileAccess = [](const FilePath &filePath) -> expected_str<DeviceFileAccess *> {
        if (!filePath.needsDevice())
            return DesktopDeviceFileAccess::instance();
        auto device = DeviceManager::deviceForPath(filePath);
        if (!device) {
            return make_unexpected(
                QString("No device found for path \"%1\"").arg(filePath.toUserOutput()));
        }
        return device->fileAccess();
    };

    deviceHooks.environment = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, qDebug() << filePath.toString(); return Environment{});
        return device->systemEnvironment();
    };

    deviceHooks.deviceDisplayName = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return filePath.toUserOutput());
        return device->displayName();
    };

    deviceHooks.ensureReachable = [](const FilePath &filePath, const FilePath &other) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->ensureReachable(other);
    };

    DeviceProcessHooks processHooks;

    processHooks.processImplHook = [](const FilePath &filePath) -> ProcessInterface * {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return nullptr);
        return device->createProcessInterface();
    };

    processHooks.systemEnvironmentForBinary = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return Environment());
        return device->systemEnvironment();
    };

    QtcProcess::setRemoteProcessHooks(processHooks);
}

DeviceManager::~DeviceManager()
{
    if (d->clonedInstance != this)
        delete d->writer;
    if (m_instance == this)
        m_instance = nullptr;
}

IDevice::ConstPtr DeviceManager::deviceAt(int idx) const
{
    const QList<IDevice::Ptr> devices = d->deviceList();
    QTC_ASSERT(idx >= 0 && idx < devices.size(), return IDevice::ConstPtr());
    return devices.at(idx);
}

IDevice::Ptr DeviceManager::mutableDevice(Utils::Id id) const
{
    return d->deviceById(id);
}

bool DeviceManager::hasDevice(const QString &name) const
{
    return Utils::anyOf(d->deviceList(), [&name](const IDevice::Ptr &device) {
        return device->displayName() == name;
    });
}

IDevice::ConstPtr DeviceManager::find(Utils::Id id) const
{
    return d->deviceById(id);
}

IDevice::ConstPtr DeviceManager::defaultDevice(Utils::Id deviceType) const
{
    return d->defaultDeviceById(deviceType);
}

} // namespace ProjectExplorer

#ifdef WITH_TESTS
#include <projectexplorer/projectexplorer.h>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

namespace ProjectExplorer {

class TestDevice : public IDevice
{
public:
    TestDevice()
    {
        setupId(AutoDetected, Utils::Id::fromString(QUuid::createUuid().toString()));
        setType(testTypeId());
        setMachineType(Hardware);
        setOsType(HostOsInfo::hostOs());
        setDisplayType("blubb");
    }

    static Utils::Id testTypeId() { return "TestType"; }

private:
    IDeviceWidget *createWidget() override { return nullptr; }
};

class TestDeviceFactory final : public IDeviceFactory
{
public:
    TestDeviceFactory()
        : IDeviceFactory(TestDevice::testTypeId())
    {
        setConstructionFunction([] { return IDevice::Ptr(new TestDevice); });
    }
};

void ProjectExplorerPlugin::testDeviceManager()
{
    TestDeviceFactory factory;

    TestDevice::Ptr dev = IDevice::Ptr(new TestDevice);
    dev->setDisplayName(QLatin1String("blubbdiblubbfurz!"));
    QVERIFY(dev->isAutoDetected());
    QCOMPARE(dev->deviceState(), IDevice::DeviceStateUnknown);
    QCOMPARE(dev->type(), TestDevice::testTypeId());

    TestDevice::Ptr dev2 = dev->clone();
    QCOMPARE(dev->id(), dev2->id());

    DeviceManager *const mgr = DeviceManager::instance();
    QVERIFY(!mgr->find(dev->id()));
    const int oldDeviceCount = mgr->deviceCount();

    QSignalSpy deviceAddedSpy(mgr, &DeviceManager::deviceAdded);
    QSignalSpy deviceRemovedSpy(mgr, &DeviceManager::deviceRemoved);
    QSignalSpy deviceUpdatedSpy(mgr, &DeviceManager::deviceUpdated);
    QSignalSpy deviceListReplacedSpy(mgr, &DeviceManager::deviceListReplaced);
    QSignalSpy updatedSpy(mgr, &DeviceManager::updated);

    mgr->addDevice(dev);
    QCOMPARE(mgr->deviceCount(), oldDeviceCount + 1);
    QVERIFY(mgr->find(dev->id()));
    QVERIFY(mgr->hasDevice(dev->displayName()));
    QCOMPARE(deviceAddedSpy.count(), 1);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 0);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 1);
    deviceAddedSpy.clear();
    updatedSpy.clear();

    mgr->setDeviceState(dev->id(), IDevice::DeviceStateUnknown);
    QCOMPARE(deviceAddedSpy.count(), 0);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 0);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 0);

    mgr->setDeviceState(dev->id(), IDevice::DeviceReadyToUse);
    QCOMPARE(mgr->find(dev->id())->deviceState(), IDevice::DeviceReadyToUse);
    QCOMPARE(deviceAddedSpy.count(), 0);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 1);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 1);
    deviceUpdatedSpy.clear();
    updatedSpy.clear();

    mgr->addDevice(dev2);
    QCOMPARE(mgr->deviceCount(), oldDeviceCount + 1);
    QVERIFY(mgr->find(dev->id()));
    QCOMPARE(deviceAddedSpy.count(), 0);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 1);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 1);
    deviceUpdatedSpy.clear();
    updatedSpy.clear();

    TestDevice::Ptr dev3 = IDevice::Ptr(new TestDevice);
    QVERIFY(dev->id() != dev3->id());

    dev3->setDisplayName(dev->displayName());
    mgr->addDevice(dev3);
    QCOMPARE(mgr->deviceAt(mgr->deviceCount() - 1)->displayName(),
             QString(dev3->displayName() + QLatin1Char('2')));
    QCOMPARE(deviceAddedSpy.count(), 1);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 0);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 1);
    deviceAddedSpy.clear();
    updatedSpy.clear();

    mgr->removeDevice(dev->id());
    mgr->removeDevice(dev3->id());
    QCOMPARE(mgr->deviceCount(), oldDeviceCount);
    QVERIFY(!mgr->find(dev->id()));
    QVERIFY(!mgr->find(dev3->id()));
    QCOMPARE(deviceAddedSpy.count(), 0);
    QCOMPARE(deviceRemovedSpy.count(), 2);
    //    QCOMPARE(deviceUpdatedSpy.count(), 0); Uncomment once the "default" stuff is gone.
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 2);
}

} // namespace ProjectExplorer

#endif // WITH_TESTS
