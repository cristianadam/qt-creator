// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtversions.h"

#include "baseqtversion.h"
#include "qtsupportconstants.h"
#include "qtsupporttr.h"
#include "qtversionfactory.h"

#include <projectexplorer/abi.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <remote/remotelinux_constants.h>

#include <coreplugin/featureprovider.h>

#include <utils/algorithm.h>
#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>

namespace QtSupport::Internal {

class DesktopQtVersion final : public QtVersion
{
public:
    QString description() const final
    {
        // A Qt whose ABI a device plugin claims (e.g. WebAssembly) is described by that plugin,
        // even though it is stored as a plain desktop Qt version.
        const QString abiDescription = deviceDescriptionForQtAbis(qtAbis());
        if (!abiDescription.isEmpty())
            return abiDescription;
        return Tr::tr("Desktop", "Qt Version is meant for the desktop");
    }

    QSet<Utils::Id> availableFeatures() const final
    {
        QSet<Utils::Id> features = QtVersion::availableFeatures();
        features.insert(Constants::FEATURE_DESKTOP);
        features.insert(Constants::FEATURE_QMLPROJECT);
        return features;
    }

    QSet<Utils::Id> targetDeviceTypes() const final
    {
        QSet<Utils::Id> result = {ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE};
        if (Utils::contains(qtAbis(), [](const ProjectExplorer::Abi a) {
                return a.os() == ProjectExplorer::Abi::LinuxOS;
            }))
            result.insert(Remote::Constants::GenericLinuxOsType);
        if (Utils::contains(qtAbis(), [](const ProjectExplorer::Abi a) {
                return a.os() == ProjectExplorer::Abi::DarwinOS;
            }))
            result.insert(Remote::Constants::GenericMacOsType);
        result.unite(deviceTypesForQtAbis(qtAbis()));
        return result;
    }
};

// Factory

class DesktopQtVersionFactory : public QtVersionFactory
{
public:
    DesktopQtVersionFactory()
    {
        setQtVersionCreator([] { return new DesktopQtVersion; });
        setSupportedType(QtSupport::Constants::DESKTOPQT);
        setPriority(0); // Lowest of all, we want to be the fallback
        // No further restrictions. We are the fallback :) so we don't care what kind of qt it is.

        // Migrate the retired WebAssembly Qt version type: it is now a plain desktop Qt whose
        // WebAssembly target device type is derived from its Emscripten ABI. Restoring it here
        // (instead of dropping it when the WebAssembly plugin is not loaded) rewrites it as a
        // desktop Qt on the next save.
        // The type was written by Qt Creator up to and including 20; retired in 21. This legacy
        // entry can be removed once settings from Qt Creator 20 or earlier are no longer expected.
        addLegacyRestoreType("Qt4ProjectManager.QtVersion.WebAssembly");
    }
};

void setupDesktopQtVersion()
{
    static DesktopQtVersionFactory theDesktopQtVersionFactory;
}

// EmbeddedLinuxQtVersion

const char EMBEDDED_LINUX_QT[] = "RemoteLinux.EmbeddedLinuxQt";

class EmbeddedLinuxQtVersion final : public QtVersion
{
public:
    EmbeddedLinuxQtVersion() = default;

    QString description() const final
    {
        return Tr::tr("Embedded Linux", "Qt Version is used for embedded Linux development");
    }

    QSet<Utils::Id> targetDeviceTypes() const final
    {
        return {Remote::Constants::GenericLinuxOsType};
    }
};

class EmbeddedLinuxQtVersionFactory : public QtSupport::QtVersionFactory
{
public:
    EmbeddedLinuxQtVersionFactory()
    {
        setQtVersionCreator([] { return new EmbeddedLinuxQtVersion; });
        setSupportedType(EMBEDDED_LINUX_QT);
        setPriority(10);

        setRestrictionChecker([](const SetupData &) { return false; });
    }
};

void setupEmbeddedLinuxQtVersion()
{
    static EmbeddedLinuxQtVersionFactory theEmbeddedLinuxQtVersionFactory;
}

} // QtSupport::Internal
