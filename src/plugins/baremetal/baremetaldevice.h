// Copyright  (C) 2016 Tim Sander <tim@krieglstein.org>
// Copyright  (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/idevicefactory.h>

namespace BareMetal {
namespace Internal {

class IDebugServerProvider;

// BareMetalDevice

class BareMetalDevice final : public ProjectExplorer::IDevice
{
    Q_DECLARE_TR_FUNCTIONS(BareMetal::Internal::BareMetalDevice)

public:
    using Ptr = QSharedPointer<BareMetalDevice>;
    using ConstPtr = QSharedPointer<const BareMetalDevice>;

    static Ptr create() { return Ptr(new BareMetalDevice); }
    ~BareMetalDevice() final;

    static QString defaultDisplayName();

    ProjectExplorer::IDeviceWidget *createWidget() final;

    ProjectExplorer::DeviceProcessSignalOperation::Ptr signalOperation() const final;

    QString debugServerProviderId() const;
    void setDebugServerProviderId(const QString &id);
    void unregisterDebugServerProvider(IDebugServerProvider *provider);

protected:
    void fromMap(const QVariantMap &map) final;
    QVariantMap toMap() const final;

private:
    BareMetalDevice();
    QString m_debugServerProviderId;
};

// BareMetalDeviceFactory

class BareMetalDeviceFactory final : public ProjectExplorer::IDeviceFactory
{
public:
    BareMetalDeviceFactory();
};

} //namespace Internal
} //namespace BareMetal
