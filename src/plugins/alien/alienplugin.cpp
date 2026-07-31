// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionhost.h"
#include "extensionregistry.h"
#include "alienclient.h"
#include "alienconstants.h"
#include "aliensettings.h"
#include "alientr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <extensionsystem/iplugin.h>

#include <languageclient/languageclientmanager.h>

#include <QInputDialog>
#include <QPointer>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

using namespace Core;
using namespace Utils;

namespace Alien::Internal {

#ifdef WITH_TESTS
class AlienHostTest final : public QObject
{
    Q_OBJECT

private slots:
    void testActivateBundledExtension()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::commandsChanged);

        const Result<> result = host.activateBundledTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QVERIFY(spy.wait(15000));
        QVERIFY(host.registeredCommands().contains("alien.hello"));
    }
};
#endif

class AlienPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Alien.json")

public:
    void initialize() final
    {
        setupAlienSettings();

        ActionBuilder rescan(this, Constants::RESCAN_ACTION_ID);
        rescan.setText(Tr::tr("Rescan VS Code Extensions"));
        rescan.addOnTriggered(this, [this] { reload(); });

        ActionBuilder runTest(this, Constants::RUN_TEST_EXTENSION_ACTION_ID);
        runTest.setText(Tr::tr("Run Alien Test Extension"));
        runTest.addOnTriggered(this, [this] {
            if (const Result<> result = host()->activateBundledTestExtension(); !result)
                MessageManager::writeFlashing(result.error());
        });

        ActionBuilder execute(this, Constants::EXECUTE_COMMAND_ACTION_ID);
        execute.setText(Tr::tr("Execute Alien Extension Command..."));
        execute.addOnTriggered(this, [this] { executeCommand(); });

#ifdef WITH_TESTS
        addTest<AlienHostTest>();
#endif
    }

    bool delayedInitialize() final
    {
        reload();
        connect(&settings(), &AspectContainer::applied, this, &AlienPlugin::reload);
        return true;
    }

    ShutdownFlag aboutToShutdown() final
    {
        for (const QPointer<AlienClient> &client : std::as_const(m_clients)) {
            if (client)
                LanguageClient::LanguageClientManager::shutdownClient(client);
        }
        return SynchronousShutdown;
    }

private:
    ExtensionHost *host()
    {
        if (!m_host)
            m_host = new ExtensionHost(settings().nodeJsPath(), this);
        return m_host;
    }

    void executeCommand()
    {
        const QStringList commands = m_host ? m_host->registeredCommands() : QStringList();
        if (commands.isEmpty()) {
            MessageManager::writeFlashing(Tr::tr("No extension commands are registered yet."));
            return;
        }
        bool ok = false;
        const QString command = QInputDialog::getItem(
            ICore::dialogParent(), Tr::tr("Execute Extension Command"), Tr::tr("Command:"),
            commands, 0, false, &ok);
        if (ok && !command.isEmpty())
            m_host->executeCommand(command);
    }

    void reload()
    {
        for (const QPointer<AlienClient> &client : std::as_const(m_clients)) {
            if (client)
                LanguageClient::LanguageClientManager::shutdownClient(client);
        }
        m_clients.clear();

        if (!settings().enable())
            return;

        QStringList errors;
        const QList<VscodeManifest> manifests
            = ExtensionRegistry::scan(settings().extensionsDir(), &errors);

        for (const QString &error : errors)
            MessageManager::writeSilently(Tr::tr("VS Code extension: %1").arg(error));

        for (const VscodeManifest &manifest : manifests) {
            MessageManager::writeSilently(
                Tr::tr("Discovered VS Code extension \"%1\" (%2 languages, %3 commands)%4.")
                    .arg(manifest.qualifiedId())
                    .arg(manifest.languages.size())
                    .arg(manifest.commands.size())
                    .arg(manifest.hasDebuggers ? Tr::tr(", debugger") : QString()));

            if (const std::optional<CommandLine> command = resolveServerCommand(manifest))
                m_clients << new AlienClient(manifest, *command);
        }
    }

    QList<QPointer<AlienClient>> m_clients;
    QPointer<ExtensionHost> m_host;
};

} // namespace Alien::Internal

#include "alienplugin.moc"
