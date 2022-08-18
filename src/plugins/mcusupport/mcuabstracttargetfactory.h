// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "mcusupport_global.h"

#include <QHash>
#include <QPair>

#include <memory>

namespace Utils {
class FilePath;
} // namespace Utils

namespace McuSupport::Internal {

struct McuTargetDescription;

class McuAbstractTargetFactory
{
public:
    using Ptr = std::unique_ptr<McuAbstractTargetFactory>;
    virtual ~McuAbstractTargetFactory() = default;

    virtual QPair<Targets, Packages> createTargets(const McuTargetDescription &,
                                                   const Utils::FilePath &qtForMcuPath)
        = 0;
    using AdditionalPackages
        = QPair<QHash<QString, McuToolChainPackagePtr>, QHash<QString, McuPackagePtr>>;
    virtual AdditionalPackages getAdditionalPackages() const { return {}; }
}; // struct McuAbstractTargetFactory
} // namespace McuSupport::Internal
