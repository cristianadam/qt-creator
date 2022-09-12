// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/idevicefactory.h>

#include "dockerdevicesetupwizard.h"
#include "dockertr.h"

namespace Docker::Internal {

class DockerDeviceFactory final : public ProjectExplorer::IDeviceFactory
{
public:
    DockerDeviceFactory(DockerSettings *settings);

    void shutdownExistingDevices();

private:
    QMutex m_deviceListMutex;
    std::vector<QWeakPointer<DockerDevice>> m_existingDevices;
};

DockerDeviceFactory::DockerDeviceFactory(DockerSettings *settings)
    : ProjectExplorer::IDeviceFactory(Constants::DOCKER_DEVICE_TYPE)
{
    setDisplayName(Tr::tr("Docker Device"));
    setIcon(QIcon());
    setCreator([settings] {
        DockerDeviceSetupWizard wizard(settings);
        if (wizard.exec() != QDialog::Accepted)
            return ProjectExplorer::IDevice::Ptr();
        return wizard.device();
    });
    setConstructionFunction([settings, this] {
        auto device = DockerDevice::create(settings, {});
        QMutexLocker lk(&m_deviceListMutex);
        m_existingDevices.push_back(device);
        return device;
    });
}

void DockerDeviceFactory::shutdownExistingDevices()
{
    QMutexLocker lk(&m_deviceListMutex);
    for (const auto &weakDevice : m_existingDevices) {
        if (QSharedPointer<DockerDevice> device = weakDevice.lock())
            device->shutdown();
    }
}

} // namespace Docker::Internal
