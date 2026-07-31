// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionhost.h"
#include "extensionregistry.h"
#include "alienclient.h"
#include "alienconstants.h"
#include "aliensettings.h"
#include "alientr.h"
#ifdef ALIEN_WITH_LITEHTML
#include "litehtmlwebviewrenderer.h"
#endif

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/statusbarmanager.h>

#include <extensionsystem/iplugin.h>

#include <languageclient/languageclientmanager.h>

#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>

#include <memory>

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

    void testQuickPick()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        // Answer the quick pick programmatically (pick "Beta").
        connect(&host, &ExtensionHost::quickPickRequested, &host,
                [&host](int id, const QStringList &, const QString &) {
                    host.resolveQuickPick(id, 1);
                });

        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        const Result<> result = host.activateBundledQuickPickTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.pick"), 10000);
        host.executeCommand("alien.pick");

        auto sawPick = [&spy] {
            for (const QList<QVariant> &args : spy) {
                if (args.first().toString() == "picked:Beta")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawPick(), 15000);
    }

    void testStatusBar()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        QSignalSpy messageSpy(&host, &ExtensionHost::statusBarMessageChanged);
        QSignalSpy itemSpy(&host, &ExtensionHost::statusBarItemChanged);

        const Result<> result = host.activateBundledStatusBarTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        auto sawMessage = [&messageSpy] {
            for (const QList<QVariant> &args : messageSpy) {
                if (args.first().toString() == "Alien ready")
                    return true;
            }
            return false;
        };
        auto sawItem = [&itemSpy] {
            for (const QList<QVariant> &args : itemSpy) {
                if (args.at(1).toString() == "AlienItem" && args.at(4).toBool())
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawMessage(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(sawItem(), 15000);
    }

    void testTreeView()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::treeViewRegistered);

        const Result<> result = host.activateBundledTreeViewTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 10000);
        QCOMPARE(spy.first().first().toString(), QString("alienExplorer"));

        // Root level.
        QStringList rootLabels;
        QString rootAId;
        auto rootPoll = [&] {
            host.requestTreeChildren("alienExplorer", {}, [&](const QJsonArray &nodes) {
                rootLabels.clear();
                for (const QJsonValue &n : nodes) {
                    rootLabels << n.toObject().value("label").toString();
                    if (n.toObject().value("label").toString() == "Root A")
                        rootAId = n.toObject().value("id").toString();
                }
            });
            return rootLabels.contains("Root A") && rootLabels.contains("Root B");
        };
        QTRY_VERIFY_WITH_TIMEOUT(rootPoll(), 15000);

        // Children of Root A.
        QStringList childLabels;
        auto childPoll = [&] {
            host.requestTreeChildren("alienExplorer", rootAId, [&](const QJsonArray &nodes) {
                childLabels.clear();
                for (const QJsonValue &n : nodes)
                    childLabels << n.toObject().value("label").toString();
            });
            return childLabels.contains("Child A1");
        };
        QTRY_VERIFY_WITH_TIMEOUT(childPoll(), 15000);
    }

    void testWebview()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        // No renderer set: exercise the API and bridge via signals only.
        ExtensionHost host(node);
        QSignalSpy createdSpy(&host, &ExtensionHost::webviewCreated);
        QSignalSpy htmlSpy(&host, &ExtensionHost::webviewHtmlChanged);
        QSignalSpy postSpy(&host, &ExtensionHost::webviewMessagePosted);
        QSignalSpy messageSpy(&host, &ExtensionHost::messageShown);

        const Result<> result = host.activateBundledWebviewTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(!createdSpy.isEmpty(), 10000);
        const QString id = createdSpy.first().first().toString();
        QCOMPARE(createdSpy.first().at(1).toString(), QString("alienDemo"));

        auto sawHtml = [&htmlSpy] {
            for (const QList<QVariant> &args : htmlSpy) {
                if (args.at(1).toString().contains("Alien Webview"))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawHtml(), 15000);

        // Extension -> webview postMessage.
        auto sawPost = [&postSpy] {
            for (const QList<QVariant> &args : postSpy) {
                if (args.at(1).toString().contains("extension"))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawPost(), 15000);

        // Webview -> extension message (delivered as a JS renderer would).
        host.deliverWebviewMessage(id, QJsonObject{{"text", "hi"}});
        auto sawGot = [&messageSpy] {
            for (const QList<QVariant> &args : messageSpy) {
                if (args.first().toString() == "got:hi")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawGot(), 15000);
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
        if (!m_host) {
            m_host = new ExtensionHost(settings().nodeJsPath(), this);

            connect(m_host, &ExtensionHost::quickPickRequested, this,
                    [this](int id, const QStringList &items, const QString &placeholder) {
                        bool ok = false;
                        const QString choice = QInputDialog::getItem(
                            ICore::dialogParent(), Tr::tr("Select"),
                            placeholder.isEmpty() ? Tr::tr("Select an item:") : placeholder,
                            items, 0, false, &ok);
                        m_host->resolveQuickPick(id, ok ? int(items.indexOf(choice)) : -1);
                    });

            connect(m_host, &ExtensionHost::inputBoxRequested, this,
                    [this](int id, const QString &prompt, const QString &value,
                           const QString &placeholder) {
                        bool ok = false;
                        const QString text = QInputDialog::getText(
                            ICore::dialogParent(),
                            prompt.isEmpty() ? Tr::tr("Input") : prompt,
                            placeholder, QLineEdit::Normal, value, &ok);
                        m_host->resolveInputBox(id, text, ok);
                    });

            connect(m_host, &ExtensionHost::statusBarMessageChanged, this,
                    [this](const QString &text) {
                        if (!m_statusMessage) {
                            m_statusMessage = new QLabel;
                            StatusBarManager::addStatusBarWidget(
                                m_statusMessage, StatusBarManager::LastLeftAligned);
                        }
                        m_statusMessage->setText(text);
                    });

            connect(m_host, &ExtensionHost::statusBarItemChanged, this,
                    [this](const QString &id, const QString &text, const QString &tooltip,
                           int alignment, bool visible) {
                        QLabel *label = m_statusItems.value(id);
                        if (!label) {
                            label = new QLabel;
                            StatusBarManager::addStatusBarWidget(
                                label, alignment == 2 ? StatusBarManager::RightCorner
                                                      : StatusBarManager::First);
                            m_statusItems.insert(id, label);
                        }
                        label->setText(text);
                        label->setToolTip(tooltip);
                        label->setVisible(visible);
                    });

            connect(m_host, &ExtensionHost::statusBarItemRemoved, this, [this](const QString &id) {
                if (QLabel *label = m_statusItems.take(id))
                    StatusBarManager::destroyStatusBarWidget(label);
            });

#ifdef ALIEN_WITH_LITEHTML
            m_webviewRenderer = std::make_unique<LiteHtmlWebviewRenderer>();
            m_host->setWebviewRenderer(m_webviewRenderer.get());
#endif
        }
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
    QPointer<QLabel> m_statusMessage;
    QHash<QString, QLabel *> m_statusItems;
#ifdef ALIEN_WITH_LITEHTML
    std::unique_ptr<LiteHtmlWebviewRenderer> m_webviewRenderer;
#endif
};

} // namespace Alien::Internal

#include "alienplugin.moc"
