// Copyright  (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "webassemblyconstants.h"
#include "webassemblydevice.h"

#include <projectexplorer/runcontrol.h>

using namespace ProjectExplorer;
using namespace Utils;

namespace WebAssembly {
namespace Internal {

WebAssemblyDevice::WebAssemblyDevice()
{
    setupId(IDevice::AutoDetected, Constants::WEBASSEMBLY_DEVICE_DEVICE_ID);
    setType(Constants::WEBASSEMBLY_DEVICE_TYPE);
    const QString displayNameAndType = tr("Web Browser");
    setDefaultDisplayName(displayNameAndType);
    setDisplayType(displayNameAndType);
    setDeviceState(IDevice::DeviceStateUnknown);
    setMachineType(IDevice::Hardware);
    setOsType(OsTypeOther);
}

IDevice::Ptr WebAssemblyDevice::create()
{
    return IDevice::Ptr(new WebAssemblyDevice);
}

WebAssemblyDeviceFactory::WebAssemblyDeviceFactory()
    : ProjectExplorer::IDeviceFactory(Constants::WEBASSEMBLY_DEVICE_TYPE)
{
    setDisplayName(WebAssemblyDevice::tr("WebAssembly Runtime"));
    setCombinedIcon(":/webassembly/images/webassemblydevicesmall.png",
                    ":/webassembly/images/webassemblydevice.png");
    setConstructionFunction(&WebAssemblyDevice::create);
    setCreator(&WebAssemblyDevice::create);
}

} // namespace Internal
} // namespace WebAssembly
