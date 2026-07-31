// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionregistry.h"

using namespace Utils;

namespace Alien::Internal {

QList<VscodeManifest> ExtensionRegistry::scan(const FilePath &dir, QStringList *errors)
{
    QList<VscodeManifest> result;
    if (!dir.isReadableDir())
        return result;

    const FilePaths subDirs = dir.dirEntries(
        FileFilter({}, DirFilterFlag::Dirs | DirFilterFlag::NoDotAndDotDot));
    for (const FilePath &subDir : subDirs) {
        const FilePath packageJson = subDir / "package.json";
        if (!packageJson.exists())
            continue;

        const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
        if (manifest)
            result << *manifest;
        else if (errors)
            *errors << manifest.error();
    }
    return result;
}

} // namespace Alien::Internal
