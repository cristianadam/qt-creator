// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vscodemanifest.h"

#include <languageclient/client.h>

#include <utils/commandline.h>

#include <optional>

namespace Alien::Internal {

// A Language Client backed by a VS Code extension's language server.
//
// This reuses Qt Creator's existing LSP machinery: it launches a resolved
// stdio server command and wires it to the documents whose file patterns
// match the extension's contributed languages.
class AlienClient final : public LanguageClient::Client
{
public:
    AlienClient(const VscodeManifest &manifest, const Utils::CommandLine &serverCommand);

    const VscodeManifest &manifest() const { return m_manifest; }

private:
    const VscodeManifest m_manifest;
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
