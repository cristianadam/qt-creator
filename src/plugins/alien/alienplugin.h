// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>
#include <utils/result.h>

#include <QHash>
#include <QKeySequence>
#include <QSet>
#include <QStringList>

#include <optional>

namespace Alien::Internal {

class VscodeKeybinding;
class VscodeManifest;

// No answer yet means the folder is still being searched.
using WorkspaceContainsCache = QHash<std::pair<Utils::FilePath, QString>, std::optional<bool>>;

QKeySequence keySequenceOf(const VscodeKeybinding &binding);
bool activationEventFired(const VscodeManifest &manifest,
                          const QStringList &openLanguageIds,
                          const Utils::FilePaths &workspaceFolders,
                          const QSet<QString> &commandRequested,
                          const QSet<QString> &kindRequested,
                          WorkspaceContainsCache *workspaceContains = nullptr);
Utils::Result<QString> installFolderName(const VscodeManifest &manifest);
void reportCommandNotRun(const QString &id, const QString &reason);

} // namespace Alien::Internal
