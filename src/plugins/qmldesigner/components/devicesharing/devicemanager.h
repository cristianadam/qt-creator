// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QUdpSocket>
#include <QWebSocketServer>

#include "device.h"

namespace QmlDesigner::DeviceShare {

class DeviceManager : public QObject
{
    Q_OBJECT
public:
    explicit DeviceManager(QObject *parent = nullptr, const QString &settingsPath = "settings.json");

    // internal init functions
    void addDevice(const QString &ip);

    // Getters
    QList<QSharedPointer<Device>> devices() const;
    std::optional<DeviceInfo> deviceInfo(const QString &deviceId) const;

    // Device management functions
    void setDeviceAlias(const QString &deviceId, const QString &alias);
    void setDeviceActive(const QString &deviceId, const bool active);
    void setDeviceIP(const QString &deviceId, const QString &ip);

    void removeDevice(const QString &deviceId);
    void removeDeviceAt(int index);
    void sendProjectFile(const QString &deviceId, const QString &projectFile);
    void stopRunningProject(const QString &deviceId);

private:
    // Devices management
    QList<QSharedPointer<Device>> m_devices;
    QList<QSharedPointer<QUdpSocket>> m_udpSockets;

    // settings
    const QString m_settingsPath;
    QString m_uuid;

private:
    // internal slots
    void initUdpDiscovery();
    void incomingDatagram();
    void incomingConnection();
    void readSettings();
    void writeSettings();
    QSharedPointer<Device> initDevice(const DeviceInfo &deviceInfo = DeviceInfo(),
                                      const DeviceSettings &deviceSettings = DeviceSettings());

    // device signals
    void deviceInfoReceived(const QString &deviceId, const DeviceInfo &deviceInfo);
    void deviceDisconnected(const QString &deviceId);

    QSharedPointer<Device> findDevice(const QString &deviceId) const;

signals:
    void deviceAdded(const DeviceInfo &deviceInfo);
    void deviceRemoved(const DeviceInfo &deviceInfo);
    void deviceOnline(const DeviceInfo &deviceInfo);
    void deviceOffline(const DeviceInfo &deviceInfo);
    void projectStarted(const DeviceInfo &deviceInfo);
    void projectStopped(const DeviceInfo &deviceInfo);
    void projectLogsReceived(const DeviceInfo &deviceInfo, const QString &logs);
};

} // namespace QmlDesigner::DeviceShare