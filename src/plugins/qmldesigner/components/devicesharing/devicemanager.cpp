// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "devicemanager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QNetworkInterface>

namespace QmlDesigner::DeviceShare {

DeviceManager::DeviceManager(QObject *parent, const QString &settingsPath)
    : QObject(parent)
    , m_settingsPath(settingsPath)
{
    readSettings();
    if (m_uuid.isEmpty()) {
        m_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        writeSettings();
    }
    initUdpDiscovery();
}

void DeviceManager::initUdpDiscovery()
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        if (interface.flags().testFlag(QNetworkInterface::IsUp)
            && interface.flags().testFlag(QNetworkInterface::IsRunning)
            && !interface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
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
            if (device->deviceInfo().deviceId() == id) {
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
        return device->deviceInfo().deviceId() == deviceId;
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

void DeviceManager::setDeviceAlias(const QString &deviceId, const QString &alias)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    auto deviceSettings = device->deviceSettings();
    deviceSettings.setAlias(alias);
    device->setDeviceSettings(deviceSettings);
    writeSettings();
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

void DeviceManager::addDevice(const QString &ip)
{
    if (ip.isEmpty())
        return;

    const auto trimmedIp = ip.trimmed();

    // check regex for xxx.xxx.xxx.xxx
    QRegularExpression ipRegex(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$)");
    if (!ipRegex.match(trimmedIp).hasMatch()) {
        qCWarning(deviceSharePluginLog) << "Invalid IP address" << ip;
        return;
    }

    for (const auto &device : m_devices) {
        if (device->deviceSettings().ipAddress() == trimmedIp) {
            qCWarning(deviceSharePluginLog) << "Device" << trimmedIp << "already exists";
            return;
        }
    }

    DeviceSettings deviceSettings;
    deviceSettings.setIpAddress(trimmedIp);
    auto device = initDevice({}, deviceSettings);
    m_devices.append(device);
    writeSettings();
    emit deviceAdded(device->deviceInfo());
}

QSharedPointer<Device> DeviceManager::initDevice(const DeviceInfo &deviceInfo,
                                                 const DeviceSettings &deviceSettings)
{
    QSharedPointer<Device> device = QSharedPointer<Device>(new Device{deviceInfo, deviceSettings},
                                                           &QObject::deleteLater);
    connect(device.data(), &Device::deviceInfoReady, this, &DeviceManager::deviceInfoReceived);
    connect(device.data(), &Device::disconnected, this, &DeviceManager::deviceDisconnected);
    connect(device.data(), &Device::projectStarted, this, [this](const QString deviceId) {
        auto device = findDevice(deviceId);
        qCDebug(deviceSharePluginLog) << "Project started on device" << deviceId;
        emit projectStarted(device->deviceInfo());
    });
    connect(device.data(), &Device::projectStopped, this, [this](const QString deviceId) {
        auto device = findDevice(deviceId);
        qCDebug(deviceSharePluginLog) << "Project stopped on device" << deviceId;
        emit projectStopped(device->deviceInfo());
    });
    connect(device.data(),
            &Device::projectLogsReceived,
            this,
            [this](const QString deviceId, const QString &logs) {
                auto device = findDevice(deviceId);
                qCDebug(deviceSharePluginLog) << "Log:" << deviceId << logs;
                emit projectLogsReceived(device->deviceInfo(), logs);
            });

    return device;
}

void DeviceManager::deviceInfoReceived(const QString &deviceId, const DeviceInfo &deviceInfo)
{
    writeSettings();
    qCDebug(deviceSharePluginLog) << "Device" << deviceId << "is online";
    emit deviceOnline(deviceInfo);
}

void DeviceManager::deviceDisconnected(const QString &deviceId)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    qCDebug(deviceSharePluginLog) << "Device" << deviceId << "disconnected";
    emit deviceOffline(device->deviceInfo());
}

void DeviceManager::removeDevice(const QString &deviceId)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    const auto deviceInfo = device->deviceInfo();
    m_devices.removeOne(device);
    writeSettings();
    emit deviceRemoved(deviceInfo);
}

void DeviceManager::removeDeviceAt(int index)
{
    if (index < 0 || index >= m_devices.size())
        return;

    auto deviceInfo = m_devices[index]->deviceInfo();
    m_devices.removeAt(index);
    writeSettings();
    emit deviceRemoved(deviceInfo);
}

void DeviceManager::sendProjectFile(const QString &deviceId, const QString &projectFile)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    device->sendProjectNotification();

    QFile file(projectFile);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(deviceSharePluginLog) << "Failed to open project file" << projectFile;
        return;
    }

    qCDebug(deviceSharePluginLog) << "Sending project file to device" << deviceId;

    QByteArray projectData = file.readAll();
    device->sendProjectData(projectData);

    qCDebug(deviceSharePluginLog) << "Project file sent to device" << deviceId;
}

void DeviceManager::stopRunningProject(const QString &deviceId)
{
    auto device = findDevice(deviceId);
    if (!device)
        return;

    device->sendProjectStopped();
}

} // namespace QmlDesigner::DeviceShare