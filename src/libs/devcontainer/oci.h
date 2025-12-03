// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "devcontainer_global.h"

#include <utils/result.h>

#include <QString>
#include <QtTaskTree/qtasktree.h>

#include <optional>

namespace DevContainer::OCI {

struct DEVCONTAINER_EXPORT Ref
{
    QString registry;
    QString repository;

    std::optional<QString> tag;
    std::optional<QString> digest;

    DEVCONTAINER_EXPORT static Utils::Result<Ref> fromString(QString input);
    DEVCONTAINER_EXPORT QUrl toUrl() const;
};

struct Credentials
{
    QString base64EncodedCredentials;
    QString refreshToken;
};

using CredentialsCallback = std::function<std::optional<Credentials>(const Ref &)>;

DEVCONTAINER_EXPORT QtTaskTree::ExecutableItem fetchOCIManifestTask(
    const Ref &ref,
    const std::function<void(const QString &)> &logFunction,
    const CredentialsCallback &credentialsCallback = nullptr);

} // namespace DevContainer::OCI
