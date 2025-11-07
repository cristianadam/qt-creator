// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "remotelinux_export.h"

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/idevicefactory.h>

#include <utils/synchronizedvalue.h>

namespace Utils { class ProcessResultData; }

namespace RemoteLinux {
namespace Internal {

class SshConnectionHandle : public QObject
{
    Q_OBJECT

public:
    SshConnectionHandle(const ProjectExplorer::DeviceConstRef &device) : m_device(device) {}
    ~SshConnectionHandle() override { emit detachFromSharedConnection(); }

signals:
    void connected(const QString &socketFilePath);
    void disconnected(const Utils::ProcessResultData &result);

    void detachFromSharedConnection();

private:
    ProjectExplorer::DeviceConstRef m_device;
};

} // Internal

class REMOTELINUX_EXPORT SshDevice : public ProjectExplorer::IDevice
{
public:
    using Ptr = std::shared_ptr<SshDevice>;
    using ConstPtr = std::shared_ptr<const SshDevice>;

    ~SshDevice();

    static Ptr create() { return Ptr(new SshDevice); }

    ProjectExplorer::IDeviceWidget *createWidget() override;

    bool canCreateProcessModel() const override { return true; }
    bool hasDeviceTester() const override { return true; }
    ProjectExplorer::DeviceTester *createDeviceTester() override;
    ProjectExplorer::DeviceProcessSignalOperation::Ptr signalOperation() const override;

    QString userAtHost() const;
    QString userAtHostAndPort() const;

    Utils::FilePath rootPath() const override;

    Utils::Result<> handlesFile(const Utils::FilePath &filePath) const override;

    Utils::ProcessInterface *createProcessInterface() const override;
    ProjectExplorer::FileTransferInterface *createFileTransferInterface(
            const ProjectExplorer::FileTransferSetupData &setup) const override;

    void checkOsType() override;

    QString deviceStateToString() const override;

    bool isDisconnected() const;
    void tryToConnect(const Utils::Continuation<> &cont) const override;
    void closeConnection(bool announce) const;

    void attachToSharedConnection(Internal::SshConnectionHandle *sshConnectionHandle,
                                  const ProjectExplorer::SshParameters &sshParams) const;

    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;
    void postLoad() override;

public:
    Utils::BoolAspect sourceProfile{this};
    Utils::BoolAspect autoConnectOnStartup{this};

protected:
    SshDevice();

    class SshDevicePrivate *d;
    friend class SshDevicePrivate;
};

namespace Internal {

class SshDeviceFactory final : public ProjectExplorer::IDeviceFactory
{
public:
    SshDeviceFactory();
    ~SshDeviceFactory() override;

private:
    Utils::SynchronizedValue<std::vector<std::weak_ptr<SshDevice>>> m_existingDevices;
    void shutdownExistingDevices();
};

} // namespace Internal

} // namespace RemoteLinux
