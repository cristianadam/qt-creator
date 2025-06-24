// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "devcontainerplugin_constants.h"
#include "devcontainerplugintr.h"

#include <extensionsystem/iplugin.h>

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/idevicefactory.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/project.h>

using namespace ProjectExplorer;

namespace DevContainer::Internal {

class DevContainerDeviceFactory final : public ProjectExplorer::IDeviceFactory
{
public:
    DevContainerDeviceFactory()
        : IDeviceFactory(DEVCONTAINER_DEVICE_TYPE)
    {
        setDisplayName(Tr::tr("Dev Container"));
        setIcon(QIcon());
        setCreator([this] {
            return nullptr;
            /*            DockerDeviceSetupWizard wizard;
            if (wizard.exec() != QDialog::Accepted)
                return IDevice::Ptr();
            DockerDevice::Ptr device = wizard.createDevice();
            m_existingDevices.writeLocked()->push_back(device);
            return std::static_pointer_cast<IDevice>(device);
*/
        });
        setConstructionFunction([this] {
            return nullptr;
            //auto device = DockerDevice::create();
            //m_existingDevices.writeLocked()->push_back(device);
            //return device;
        });
    }
};

class DevContainerPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "DevContainerPlugin.json")

public:
    DevContainerPlugin() {}

    void initialize() final
    {
        connect(
            ProjectManager::instance(),
            &ProjectManager::projectAdded,
            this,
            [=](ProjectExplorer::Project *project) {
                if ((project->projectDirectory() / ".devcontainer" / "devcontainer.json").exists()) {
                    
                };
            });
    }
    void extensionsInitialized() final {}
};

} // namespace DevContainer::Internal

#include <devcontainerplugin.moc>
