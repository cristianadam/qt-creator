// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vscodemanifest.h"

#include <utils/filepath.h>
#include <utils/result.h>

#include <QObject>
#include <QStringList>
#include <QTemporaryDir>

#include <functional>

namespace Alien::Internal {

class HostConnection;

// The Node.js VS Code extension host.
//
// A single shared Node process (see host/host.js) loads extensions and serves
// the "vscode" module API over JSON-RPC. This class owns that process, maps
// inbound API calls onto Qt Creator, and lets callers activate extensions and
// invoke the commands they register.
class ExtensionHost final : public QObject
{
    Q_OBJECT

public:
    explicit ExtensionHost(const Utils::FilePath &nodePath, QObject *parent = nullptr);
    ~ExtensionHost() override;

    bool isRunning() const;

    // Activates an on-disk extension in the host.
    void activate(const VscodeManifest &manifest);

    // Extracts the bundled test extension to a temporary directory and
    // activates it. Used to exercise the host without an installed extension.
    Utils::Result<> activateBundledTestExtension();

    QStringList registeredCommands() const { return m_commands; }
    void executeCommand(const QString &command);

signals:
    void commandsChanged();
    void activationFailed(const QString &id, const QString &error);

private:
    Utils::Result<> ensureStarted();
    void installHandlers();
    void whenReady(const std::function<void()> &action);

    Utils::FilePath m_nodePath;
    HostConnection *m_connection = nullptr;
    QTemporaryDir m_runtimeDir;
    QStringList m_commands;
    QList<std::function<void()>> m_deferred;
};

} // namespace Alien::Internal
