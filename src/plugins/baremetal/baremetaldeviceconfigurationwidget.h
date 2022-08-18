// Copyright  (C) 2016 Tim Sander <tim@krieglstein.org>
// Copyright  (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/devicesupport/idevicewidget.h>

namespace BareMetal {
namespace Internal {

class DebugServerProviderChooser;

// BareMetalDeviceConfigurationWidget

class BareMetalDeviceConfigurationWidget final
      : public ProjectExplorer::IDeviceWidget
{
    Q_OBJECT

public:
    explicit BareMetalDeviceConfigurationWidget(const ProjectExplorer::IDevicePtr &deviceConfig);

private:
    void debugServerProviderChanged();
    void updateDeviceFromUi() final;

    DebugServerProviderChooser *m_debugServerProviderChooser = nullptr;
};

} // namespace Internal
} // namespace BareMetal
