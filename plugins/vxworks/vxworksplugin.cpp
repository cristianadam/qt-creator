// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial

#include "vxworksconstants.h"
#include "vxworksdebuggerruncontrol.h"
#include "vxworksdeploysupport.h"
#include "vxworksdevice.h"
#include "vxworksgcctoolchain.h"
#include "vxworksqtversion.h"
#include "vxworksrunconfiguration.h"
#include "vxworksruncontrol.h"
#include "vxworkssettingspage.h"

#include <extensionsystem/iplugin.h>

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/runconfiguration.h>

#include <qtsupport/qtversionfactory.h>

using namespace ProjectExplorer;

namespace VxWorks::Internal {

class VxWorksQtVersionFactory : public QtSupport::QtVersionFactory
{
public:
    VxWorksQtVersionFactory()
    {
        setQtVersionCreator([] { return new VxWorksQtVersion; });
        setSupportedType(Constants::VXWORKSQT);
        setPriority(50);
        setRestrictionChecker([](const SetupData &setup) {
            return setup.mkspec.contains("vxworks");
        });
    }
};

class VxWorksPluginPrivate
{
public:
    VxWorksQtVersionFactory qtVersionFactory;
    VxWorksToolchainFactory toolChainFactory;
    VxWorksDeviceFactory deviceFactory;
    VxWorksRunConfigurationFactory runConfigFactory;
    VxWorksRunWorkerFactory runWorkerFactory;
};

class VxWorksPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "VxWorks.json")

public:
    ~VxWorksPlugin() final { delete d; }

private:
    void initialize() final
    {
        d = new VxWorksPluginPrivate;

        setupVxWorksSettingsPage();
        setupVxWorksDeploySupport();
        setupVxWorksDebugSupport();
    }

    VxWorksPluginPrivate *d = nullptr;
};

} // VxWorks::Internal

#include "vxworksplugin.moc"
