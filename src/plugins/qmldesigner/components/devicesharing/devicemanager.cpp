// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "devicemanager.h"

#include "devicemanagerwidget.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QNetworkInterface>

#include <coreplugin/icore.h>

namespace QmlDesigner::DeviceShare {

DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent)
{
    QFileInfo fileInfo(Core::ICore::settings()->fileName());
    m_settingsPath = fileInfo.absolutePath() + "/device_manager.json";
    readSettings();
    if (m_uuid.isEmpty()) {
        m_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        writeSettings();
    }
    initUdpDiscovery();
}

DeviceManager::~DeviceManager() = default;

void DeviceManager::initUdpDiscovery()
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        if (interface.flags().testFlag(QNetworkInterface::IsUp)
            && interface.flags().testFlag(QNetworkInterface::IsRunning)) {
            for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    QSharedPointer<QUdpSocket> udpSocket = QSharedPointer<QUdpSocket>::create();
                    connect(udpSocket.data(),
                            &QUdpSocket::readyRead,
                            this,
                            &DeviceManager::incomingDatagram);

                    bool retVal = udpSocket->bind(entry.ip(), 53452, QUdpSocket::ShareAddress);

                    if (!retVal) {
                        qCWarning(deviceSharePluginLog)
                            << "UDP:: Failed to bind to UDP port 53452 on" << entry.ip().toString()
                            << "on interface" << interface.name()
                            << ". Error:" << udpSocket->errorString();
                        continue;
                    }

                    qCDebug(deviceSharePluginLog) << "UDP:: Listening on" << entry.ip().toString()
                                                  << "port" << udpSocket->localPort();
                    m_udpSockets.append(udpSocket);
                }
            }
        }
    }
}

void DeviceManager::incomingDatagram()
{
    const auto udpSocket = qobject_cast<QUdpSocket *>(sender());
    if (!udpSocket)
        return;

    while (udpSocket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = udpSocket->receiveDatagram();
        const QJsonDocument doc = QJsonDocument::fromJson(datagram.data());
        const QJsonObject message = doc.object();

        if (message["name"].toString() != "__qtuiviewer__") {
            continue;
        }

        const QString id = message["id"].toString();
        const QString ip = datagram.senderAddress().toString();
        qCDebug(deviceSharePluginLog) << "Qt UI VIewer found at" << ip << "with id" << id;

        for (const auto &device : m_devices) {
            if (device->deviceInfo().selfId() == id) {
                if (device->deviceSettings().ipAddress() != ip) {
                    qCDebug(deviceSharePluginLog) << "Updating IP address for device" << id;
                    setDeviceIP(id, ip);
                }
            }
        }
    }
}

void DeviceManager::writeSettings()
{
    QJsonObject root;
    QJsonArray devices;
    for (const auto &device : m_devices) {
        QJsonObject deviceInfo;
        deviceInfo.insert("deviceInfo", device->deviceInfo().jsonObject());
        deviceInfo.insert("deviceSettings", device->deviceSettings().jsonObject());
        devices.append(deviceInfo);
    }

    root.insert("devices", devices);
    root.insert("uuid", m_uuid);

    QJsonDocument doc(root);
    QFile file(m_settingsPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(deviceSharePluginLog) << "Failed to open settings file" << file.fileName();
        return;
    }

    file.write(doc.toJson());
}

void DeviceManager::readSettings()
{
    QFile file(m_settingsPath);
    qCDebug(deviceSharePluginLog) << "Reading settings from" << file.fileName();
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(deviceSharePluginLog) << "Failed to open settings file" << file.fileName();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    m_uuid = doc.object()["uuid"].toString();
    QJsonArray devices = doc.object()["devices"].toArray();
    for (const QJsonValue &deviceInfoJson : devices) {
        DeviceInfo deviceInfo;
        DeviceSettings deviceSettings;
        deviceInfo.setJsonObject(deviceInfoJson.toObject()["deviceInfo"].toObject());
        deviceSettings.setJsonObject(deviceInfoJson.toObject()["deviceSettings"].toObject());
        auto device = initDevice(deviceInfo, deviceSettings);
        m_devices.append(device);
    }
}

QList<QSharedPointer<Device>> DeviceManager::devices() const
{
    return m_devices;
}

QSharedPointer<Device> DeviceManager::findDevice(const QString &deviceId) const
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(), [deviceId](const auto &device) {
        return device->deviceSettings().deviceId() == deviceId;
    });

    return it != m_devices.end() ? *it : nullptr;
}

std::optional<DeviceInfo> DeviceManager::deviceInfo(const QString &deviceId) const
{
    auto device = findDevice(deviceId);
    if (!device)
        return {};

    return device->deviceInfo();
}

std::optional<DeviceSettings> DeviceManager::deviceSettings(const QString &deviceId) const
{
    auto device = findDevice(deviceId);
    if (!device)
        return {};

    return device->deviceSettings();
}

std::optional<bool> DeviceManager::deviceIsConnected(const QString &deviceId) const
{
    auto device = findDevice(deviceId);
    if (!device)
        return {};

    return device->isConnected();
}

void DeviceManager::setDeviceAlias(const QString &deviceId, const QString &alias)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    auto deviceSettings = device->deviceSettings();

    if (deviceSettings.alias() == alias)
        return;

    deviceSettings.setAlias(alias);
    device->setDeviceSettings(deviceSettings);
    writeSettings();

    emit deviceAliasChanged(device->deviceInfo());
}

void DeviceManager::setDeviceActive(const QString &deviceId, const bool active)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    auto deviceSettings = device->deviceSettings();
    deviceSettings.setActive(active);
    device->setDeviceSettings(deviceSettings);
    writeSettings();

    if (active)
        emit deviceActivated(deviceId);
    else
        emit deviceDeactivated(deviceId);
}

void DeviceManager::setDeviceIP(const QString &deviceId, const QString &ip)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    auto deviceSettings = device->deviceSettings();
    deviceSettings.setIpAddress(ip);
    device->setDeviceSettings(deviceSettings);
    writeSettings();
}

QString DeviceManager::generateDeviceAlias() const
{
    QStringList m_currentAliases;
    for (const auto &device : m_devices)
        m_currentAliases.append(device->deviceSettings().alias());

    QString alias = "Device 0";
    int index = 0;
    while (m_currentAliases.contains(alias))
        alias = QString("Device %1").arg(++index);

    return alias;
}

bool DeviceManager::addDevice(const QString &ip)
{
    if (ip.isEmpty())
        return false;

    const auto trimmedIp = ip.trimmed();

    // check regex for xxx.xxx.xxx.xxx
    QRegularExpression ipRegex(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$)");
    if (!ipRegex.match(trimmedIp).hasMatch()) {
        qCWarning(deviceSharePluginLog) << "Invalid IP address" << ip;
        return false;
    }

    for (const auto &device : m_devices) {
        if (device->deviceSettings().ipAddress() == trimmedIp) {
            qCWarning(deviceSharePluginLog) << "Device" << trimmedIp << "already exists";
            return false;
        }
    }

    DeviceSettings deviceSettings;
    deviceSettings.setIpAddress(trimmedIp);
    deviceSettings.setAlias(generateDeviceAlias());
    deviceSettings.setDeviceId(QUuid::createUuid().toString(QUuid::WithoutBraces));

    auto device = initDevice({}, deviceSettings);
    m_devices.append(device);
    writeSettings();
    emit deviceAdded(deviceSettings.deviceId());

    return true;
}

QSharedPointer<Device> DeviceManager::initDevice(const DeviceInfo &deviceInfo,
                                                 const DeviceSettings &deviceSettings)
{
    QSharedPointer<Device> device = QSharedPointer<Device>(new Device{deviceInfo, deviceSettings},
                                                           &QObject::deleteLater);
    connect(device.data(), &Device::deviceInfoReady, this, &DeviceManager::deviceInfoReceived);
    connect(device.data(), &Device::disconnected, this, &DeviceManager::deviceDisconnected);
    connect(device.data(), &Device::projectSendingProgress, this, &DeviceManager::projectSendingProgress);
    connect(device.data(), &Device::projectStarted, this, &DeviceManager::projectStarted);
    connect(device.data(), &Device::projectStopped, this, &DeviceManager::projectStopped);

    connect(device.data(),
            &Device::projectLogsReceived,
            this,
            [this](const QString deviceId, const QString &logs) {
                qCDebug(deviceSharePluginLog) << "Log:" << deviceId << logs;
                emit projectLogsReceived(deviceId, logs);
            });

    return device;
}

void DeviceManager::deviceInfoReceived(const QString &deviceIp, const QString &deviceId)
{
    auto newDevIt = std::find_if(m_devices.begin(),
                                 m_devices.end(),
                                 [deviceId, deviceIp](const auto &device) {
                                     return device->deviceSettings().deviceId() == deviceId
                                            && device->deviceSettings().ipAddress() == deviceIp;
                                 });
    auto oldDevIt = std::find_if(m_devices.begin(),
                                 m_devices.end(),
                                 [deviceId, deviceIp](const auto &device) {
                                     return device->deviceSettings().deviceId() == deviceId
                                            && device->deviceSettings().ipAddress() != deviceIp;
                                 });

    // if there are 2 devices with the same ID but different IPs, remove the old one
    // aka: merge devices with the same ID
    if (oldDevIt != m_devices.end()) {
        QSharedPointer<Device> oldDevice = *oldDevIt;
        QSharedPointer<Device> newDevice = *newDevIt;
        DeviceSettings deviceSettings = oldDevice->deviceSettings();
        deviceSettings.setIpAddress(newDevice->deviceSettings().ipAddress());
        newDevice->setDeviceSettings(deviceSettings);
        m_devices.removeOne(oldDevice);
    }

    writeSettings();
    qCDebug(deviceSharePluginLog) << "Device" << deviceId << "is online";
    emit deviceOnline(deviceId);
}

void DeviceManager::deviceDisconnected(const QString &deviceId)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    qCDebug(deviceSharePluginLog) << "Device" << deviceId << "disconnected";
    emit deviceOffline(deviceId);
}

void DeviceManager::removeDevice(const QString &deviceId)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    m_devices.removeOne(device);
    writeSettings();
    emit deviceRemoved(deviceId);
}

void DeviceManager::removeDeviceAt(int index)
{
    if (index < 0 || index >= m_devices.size())
        return;

    QString deviceId = m_devices[index]->deviceSettings().deviceId();
    m_devices.removeAt(index);
    writeSettings();
    emit deviceRemoved(deviceId);
}

bool DeviceManager::sendProjectFile(const QString &deviceId, const QString &projectFile)
{
    auto device = findDevice(deviceId);
    if (!device)
        return false;

    QFile file(projectFile);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(deviceSharePluginLog) << "Failed to open project file" << projectFile;
        return false;
    }

    qCDebug(deviceSharePluginLog) << "Sending project file to device" << deviceId;
    return device->sendProjectData(file.readAll());
}

bool DeviceManager::stopRunningProject(const QString &deviceId)
{
    auto device = findDevice(deviceId);
    if (!device)
        return false;

    return device->sendProjectStopped();
}

DeviceManagerWidget *DeviceManager::widget()
{
    if (!m_widget)
        m_widget = new DeviceManagerWidget(*this, Core::ICore::instance()->dialogParent());

    return m_widget.get();
}

} // namespace QmlDesigner::DeviceShare