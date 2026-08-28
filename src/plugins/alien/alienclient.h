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

// A Language Client backed by a VS Code extension's language server.
//
// This reuses Qt Creator's existing LSP machinery: it launches a stdio server
// command and wires it to the documents matching a language filter. The filter
// comes either from an extension manifest's contributed languages (Phase 0) or
// from a documentSelector resolved by the extension host (vscode-languageclient
// interception).
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

// Resolves the stdio server command line for an extension, or nullopt if none
// can be determined without running the extension host.
//
// Today this only handles the "assume main is a stdio server" stopgap
// (see AlienSettings). The real resolution - running the
// extension's activate() and intercepting its vscode-languageclient
// ServerOptions - belongs to the future ExtensionHost.
std::optional<Utils::CommandLine> resolveServerCommand(const VscodeManifest &manifest);

} // namespace Alien::Internal
