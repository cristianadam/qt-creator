// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vscodemanifest.h"

#include <languageclient/client.h>
#include <languageclient/languageclientsettings.h>

#include <utils/commandline.h>

#include <QJsonObject>

#include <optional>

namespace Alien::Internal {

// A Language Client backed by a VS Code extension's language server. The
// documents it serves come either from the languages the extension's manifest
// contributes, or from a documentSelector the extension host resolved.
class AlienClient final : public LanguageClient::Client
{
public:
    AlienClient(const QString &name,
                const Utils::CommandLine &serverCommand,
                const LanguageClient::LanguageFilter &filter,
                const Utils::FilePath &workingDirectory = {},
                const QJsonObject &initializationOptions = {});
    AlienClient(const VscodeManifest &manifest, const Utils::CommandLine &serverCommand);

    const VscodeManifest &manifest() const { return m_manifest; }

private:
    void wireDocuments();

    VscodeManifest m_manifest;
};

// The stdio server command line for an extension, or nullopt where only
// running the extension host can tell. This answers for the "assume main is a
// stdio server" setting alone (see AlienSettings).
std::optional<Utils::CommandLine> resolveServerCommand(const VscodeManifest &manifest);

} // namespace Alien::Internal
