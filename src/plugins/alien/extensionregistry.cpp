// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionregistry.h"

#include <QHash>
#include <QVersionNumber>

using namespace Utils;

namespace Alien::Internal {

QList<VscodeManifest> ExtensionRegistry::scan(const FilePath &dir, QStringList *errors)
{
    if (!dir.isReadableDir())
        return {};

    // ~/.vscode/extensions keeps every installed version in its own folder, so
    // the same publisher.name shows up repeatedly. Keep only the newest version
    // of each extension.
    QHash<QString, VscodeManifest> byId;
    const FilePaths subDirs = dir.dirEntries(
        FileFilter({}, DirFilterFlag::Dirs | DirFilterFlag::NoDotAndDotDot));
    for (const FilePath &subDir : subDirs) {
        const FilePath packageJson = subDir / "package.json";
        if (!packageJson.exists())
            continue;

        const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
        if (!manifest) {
            if (errors)
                *errors << manifest.error();
            continue;
        }

        const auto it = byId.constFind(manifest->qualifiedId());
        if (it == byId.constEnd()
            || QVersionNumber::fromString(it->version) < QVersionNumber::fromString(manifest->version)) {
            byId.insert(manifest->qualifiedId(), *manifest);
        }
    }

    QList<VscodeManifest> result = byId.values();
    std::sort(result.begin(), result.end(), [](const VscodeManifest &a, const VscodeManifest &b) {
        return a.qualifiedId() < b.qualifiedId();
    });
    return result;
}

} // namespace Alien::Internal
