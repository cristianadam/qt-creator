// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "mcuabstracttargetfactory.h"
#include "mcutargetdescription.h"
#include "settingshandler.h"

namespace McuSupport::Internal {

struct PackageDescription;

class McuPackageVersionDetector;

class McuTargetFactory : public McuAbstractTargetFactory
{
public:
    explicit McuTargetFactory(const SettingsHandler::Ptr &);
    QPair<Targets, Packages> createTargets(const McuTargetDescription &,
                                           const McuPackagePtr &qtForMCUsPackage) override;
    Packages createPackages(const McuTargetDescription &);
    McuToolChainPackage *createToolchain(const McuTargetDescription::Toolchain &);
    McuPackagePtr createPackage(const PackageDescription &);
    static void expandVariables(Packages &packages);

private:
    SettingsHandler::Ptr settingsHandler;
}; // struct McuTargetFactory

} // namespace McuSupport::Internal
