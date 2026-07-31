// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionhost.h"
#include "extensionregistry.h"
#include "alienclient.h"
#include "alienconstants.h"
#include "aliensettings.h"
#include "alientr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <extensionsystem/iplugin.h>

#include <languageclient/languageclientmanager.h>

#include <QInputDialog>
#include <QPointer>

#ifdef WITH_TESTS
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
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

    void testLanguageClientInterception()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::languageClientStarted);

        const Result<> result = host.activateBundledLspTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QVERIFY(spy.wait(15000));
        const QString id = spy.first().first().toString();
        AlienClient *client = host.languageClient(id);
        QVERIFY(client);

        // The mock server answers "initialize", so the client becomes reachable.
        QTRY_VERIFY_WITH_TIMEOUT(client->reachable(), 15000);
    }

    void testDocumentSync()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "foo.txt";
        QVERIFY(file.writeFileContents("hello").has_value());

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);

        const Result<> result = host.activateBundledDocSyncTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 10000);
        QVERIFY(EditorManager::openEditor(file));

        auto sawOpened = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("opened:") && text.contains("foo.txt"))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawOpened(), 15000);

        EditorManager::closeAllEditors(false);
    }

    void testDiagnostics()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "foo.txt";
        QVERIFY(file.writeFileContents("hello").has_value());

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::diagnosticsPublished);

        const Result<> result = host.activateBundledDiagnosticsTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 10000);
        QVERIFY(EditorManager::openEditor(file));

        auto sawDiagnostic = [&spy] {
            for (const QList<QVariant> &args : spy) {
                if (args.at(0).toString().contains("foo.txt") && args.at(1).toInt() >= 1)
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawDiagnostic(), 15000);

        EditorManager::closeAllEditors(false);
    }

    void testCompletion()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "foo.txt";
        QVERIFY(file.writeFileContents("a").has_value());

        ExtensionHost host(node);
        const Result<> result = host.activateBundledCompletionTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 10000);
        QVERIFY(EditorManager::openEditor(file));

        // Re-issue the request while activation settles; assert the in-host
        // provider's items come back.
        QStringList labels;
        auto poll = [&] {
            host.requestCompletion(file, 0, 1, [&labels](const QJsonArray &items) {
                labels.clear();
                for (const QJsonValue &item : items)
                    labels << item.toObject().value("label").toString();
            });
            return labels.contains("alienComplete");
        };
        QTRY_VERIFY_WITH_TIMEOUT(poll(), 15000);

        EditorManager::closeAllEditors(false);
    }

    void testHoverAndDefinition()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "foo.txt";
        QVERIFY(file.writeFileContents("abc").has_value());

        ExtensionHost host(node);
        const Result<> result = host.activateBundledHoverDefinitionTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 10000);
        QVERIFY(EditorManager::openEditor(file));

        QString hover;
        auto hoverPoll = [&] {
            host.requestHover(file, 0, 1, [&hover](const QString &text) { hover = text; });
            return hover.contains("Alien hover");
        };
        QTRY_VERIFY_WITH_TIMEOUT(hoverPoll(), 15000);

        QString target;
        auto definitionPoll = [&] {
            host.requestDefinition(file, 0, 1, [&target](const QJsonArray &locations) {
                target = locations.isEmpty()
                    ? QString()
                    : locations.first().toObject().value("uri").toString();
            });
            return target.contains("foo.txt");
        };
        QTRY_VERIFY_WITH_TIMEOUT(definitionPoll(), 15000);

        EditorManager::closeAllEditors(false);
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
        // The clients belong to LanguageClientManager, not to this plugin, and
        // shutting one down only sends it the request. Reporting a synchronous
        // shutdown here lets this library be unloaded while they are still
        // alive - with their vtables and their EditorManager connections in it,
        // so the next opened document jumps into freed code. Wait for them to
        // be gone instead; the client's own 20s timer bounds the wait.
        QList<QPointer<AlienClient>> alive;
        for (const QPointer<AlienClient> &client : std::as_const(m_clients)) {
            if (client)
                alive.append(client);
        }
        if (alive.isEmpty())
            return SynchronousShutdown;

        const auto remaining = std::make_shared<int>(alive.size());
        for (const QPointer<AlienClient> &client : std::as_const(alive)) {
            connect(client, &QObject::destroyed, this, [this, remaining] {
                if (--*remaining == 0)
                    emit asynchronousShutdownFinished();
            });
            LanguageClient::LanguageClientManager::shutdownClient(client);
        }
        return AsynchronousShutdown;
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
