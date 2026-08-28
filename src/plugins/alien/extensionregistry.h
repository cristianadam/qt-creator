// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vscodemanifest.h"

#include <utils/filepath.h>

#include <QList>

namespace Alien::Internal {

// Discovers VS Code extensions in the configured extensions directory by
// reading each subfolder's package.json. Pure discovery: it does not run
// any extension code (that is the job of the future ExtensionHost).
class ExtensionRegistry
{
public:
    // Scans dir/<extension>/package.json for every immediate subdirectory.
    // Manifests that fail to parse are skipped; their errors are collected.
    static QList<VscodeManifest> scan(const Utils::FilePath &dir, QStringList *errors = nullptr);
};

} // namespace Alien::Internal
