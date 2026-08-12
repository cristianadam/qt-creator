// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionhost.h"
#include "extensionregistry.h"
#include "alienclient.h"
#include "alienconstants.h"
#include "aliensettings.h"
#include "alientr.h"
#include "alienlocatorfilter.h"
#include "autowebviewrenderer.h"
#include "codicons.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/idocument.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/statusbarmanager.h>

#include <extensionsystem/iplugin.h>

#include <languageclient/languageclientmanager.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <utils/fileutils.h>
#include <utils/layoutbuilder.h>
#include <utils/stylehelper.h>
#include <utils/temporarydirectory.h>
#include <utils/unarchiver.h>

#include <QEventLoop>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSet>

#include <functional>
#include <memory>

#ifdef WITH_TESTS
#include <texteditor/textdocument.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>

#include "vscodemanifest.h"
#endif

using namespace Core;
using namespace Utils;

namespace Alien::Internal {

// One VS Code status bar item. Its text carries icon markup ("$(gear) Build"),
// which is split into an icon we know and the remaining label text.
class StatusBarItem final : public QWidget
{
public:
    StatusBarItem()
    {
        using namespace Layouting;
        Row {
            m_icon,
            m_text,
            noMargin,
            spacing(StyleHelper::SpacingTokens::GapHXs),
        }.attachTo(this);
    }

    void setContent(const QString &markedUpText)
    {
        const QIcon icon = firstCodicon(markedUpText);
        const int size = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_icon->setPixmap(icon.pixmap(size, size));
        m_icon->setVisible(!icon.isNull());
        m_text->setText(stripCodicons(markedUpText));
    }

private:
    QLabel *m_icon = new QLabel;
    QLabel *m_text = new QLabel;
};

#ifdef WITH_TESTS
// Writes a minimal extension (package.json + extension.js) and returns its
// parsed manifest. Free function so moc does not process it.
static Result<VscodeManifest> writeMockExtension(
    const FilePath &dir, const QString &name, const QString &js)
{
    if (const Result<> r = dir.ensureWritableDir(); !r)
        return make_unexpected(r.error());
    const QJsonObject pkg{
        {"name", name},
        {"publisher", "theqtcompany"},
        {"main", "./extension.js"},
        {"engines", QJsonObject{{"vscode", "^1.0.0"}}},
        {"activationEvents", QJsonArray{"*"}},
    };
    const FilePath packageJson = dir / "package.json";
    if (const Result<qint64> r = packageJson.writeFileContents(QJsonDocument(pkg).toJson()); !r)
        return make_unexpected(r.error());
    if (const Result<qint64> r = (dir / "extension.js").writeFileContents(js.toUtf8()); !r)
        return make_unexpected(r.error());
    return VscodeManifest::fromPackageJson(packageJson);
}

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

    void testDeactivateStopsLanguageClient()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::languageClientStarted);

        const Result<> result = host.activateBundledLspTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QVERIFY(spy.wait(15000));
        const QString id = spy.first().first().toString(); // "<extensionId>:<clientId>"
        QVERIFY(host.languageClient(id));

        // Deactivating the extension must stop the server it started.
        const QString extensionId = id.left(id.indexOf(':'));
        QVERIFY(!extensionId.isEmpty());
        host.deactivate(extensionId);
        QVERIFY(!host.languageClient(id));
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

        // Editing must reach the extension as a change carrying a range (so
        // vscode-languageclient works with incremental-sync servers).
        TextEditor::TextDocument *textDocument
            = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(textDocument);
        textDocument->document()->setPlainText("hello world");
        auto sawChanged = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("changed:") && text.contains("range=true"))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawChanged(), 15000);

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

    void testCodicons()
    {
        // Real markup seen from cmake-tools, redhat.java and cpptools.
        QCOMPARE(stripCodicons("$(gear) Build"), QString("Build"));
        QCOMPARE(stripCodicons("$(sync~spin) Java: Activating..."),
                 QString("Java: Activating..."));
        QCOMPARE(stripCodicons("$(play)"), QString());
        QCOMPARE(stripCodicons("[ Ax ]"), QString("[ Ax ]"));

        QVERIFY(!firstCodicon("$(gear) Build").isNull());
        QVERIFY(!firstCodicon("$(sync~spin) Java: Activating...").isNull());
        // Nothing of ours looks like a rocket, so only the markup goes.
        QVERIFY(firstCodicon("$(rocket) Java: Lightweight Mode").isNull());
        QCOMPARE(stripCodicons("$(rocket) Java: Lightweight Mode"),
                 QString("Java: Lightweight Mode"));
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

    void testConfigurationBridge()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "cfg", "alien-cfg-test",
            "const vscode = require('vscode');\n"
            "function activate() {\n"
            "  vscode.window.showInformationMessage("
            "'config:' + vscode.workspace.getConfiguration('alien').get('greeting', 'none'));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.setConfiguration(QJsonObject{{"alien.greeting", "hello"}});
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);

        auto saw = [&spy] {
            for (const QList<QVariant> &args : spy)
                if (args.first().toString() == "config:hello")
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(saw(), 15000);
    }

    void testExtensionExports()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const Result<VscodeManifest> base = writeMockExtension(
            root / "base", "alien-base",
            "function activate() { return { value: 'core-api' }; }\n"
            "module.exports = { activate };\n");
        QVERIFY(base.has_value());

        const Result<VscodeManifest> dependent = writeMockExtension(
            root / "dep", "alien-dep",
            "const vscode = require('vscode');\n"
            "function activate() {\n"
            "  const ext = vscode.extensions.getExtension('theqtcompany.alien-base');\n"
            "  vscode.window.showInformationMessage('dep:' + (ext && ext.exports && ext.exports.value));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY(dependent.has_value());

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*base);       // dependency first
        host.activate(*dependent);

        auto saw = [&spy] {
            for (const QList<QVariant> &args : spy)
                if (args.first().toString() == "dep:core-api")
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(saw(), 15000);
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

        m_locatorFilter = std::make_unique<AlienLocatorFilter>(
            [this] { return extensionCommands(); },
            [this](const QString &id) {
                if (m_host)
                    m_host->executeCommand(id);
            });

        ActionBuilder execute(this, Constants::EXECUTE_COMMAND_ACTION_ID);
        execute.setText(Tr::tr("Execute Alien Extension Command..."));
        execute.addOnTriggered(this, [this] { executeCommand(); });

        ActionBuilder install(this, Constants::INSTALL_VSIX_ACTION_ID);
        install.setText(Tr::tr("Install VS Code Extension (.vsix)..."));
        install.addOnTriggered(this, [this] { installVsix(); });

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

            connect(m_host, &ExtensionHost::stopped, this, [this] {
                // Nothing runs in a host that is gone, so the next activation
                // trigger starts a fresh one and puts the extensions back.
                m_activeIds.clear();
                if (m_statusMessage)
                    StatusBarManager::destroyStatusBarWidget(m_statusMessage);
                for (StatusBarItem *item : std::as_const(m_statusItems))
                    StatusBarManager::destroyStatusBarWidget(item);
                m_statusItems.clear();
                MessageManager::writeFlashing(
                    Tr::tr("The VS Code extension host stopped. It restarts with the next "
                           "activation, or on \"Rescan VS Code Extensions\"."));
            });

            connect(m_host, &ExtensionHost::statusBarMessageChanged, this,
                    [this](const QString &text) {
                        if (!m_statusMessage) {
                            m_statusMessage = new QLabel;
                            StatusBarManager::addStatusBarWidget(
                                m_statusMessage, StatusBarManager::LastLeftAligned);
                        }
                        m_statusMessage->setText(stripCodicons(text));
                    });

            connect(m_host, &ExtensionHost::statusBarItemChanged, this,
                    [this](const QString &id, const QString &text, const QString &tooltip,
                           int alignment, bool visible) {
                        StatusBarItem *item = m_statusItems.value(id);
                        if (!item) {
                            item = new StatusBarItem;
                            StatusBarManager::addStatusBarWidget(
                                item, alignment == 2 ? StatusBarManager::RightCorner
                                                     : StatusBarManager::First);
                            m_statusItems.insert(id, item);
                        }
                        item->setContent(text);
                        item->setToolTip(stripCodicons(tooltip));
                        item->setVisible(visible);
                    });

            connect(m_host, &ExtensionHost::statusBarItemRemoved, this, [this](const QString &id) {
                if (StatusBarItem *item = m_statusItems.take(id))
                    StatusBarManager::destroyStatusBarWidget(item);
            });

            m_webviewRenderer = std::make_unique<AutoWebviewRenderer>();
            m_host->setWebviewRenderer(m_webviewRenderer.get());

            auto updateFolders = [this] { m_host->setWorkspaceFolders(workspaceFolders(m_host)); };
            connect(ProjectExplorer::ProjectManager::instance(),
                    &ProjectExplorer::ProjectManager::projectAdded, this, updateFolders);
            connect(ProjectExplorer::ProjectManager::instance(),
                    &ProjectExplorer::ProjectManager::projectRemoved, this, updateFolders);
            connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
                    this, updateFolders);
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

    // Unpacks a .vsix (a zip whose "extension/" folder is the extension) into
    // the extensions directory as <publisher>.<name>-<version>/, then rescans.
    void installVsix()
    {
        const FilePath vsix = FileUtils::getOpenFilePath(
            Tr::tr("Install VS Code Extension"), {}, Tr::tr("VS Code extensions (*.vsix)"));
        if (vsix.isEmpty())
            return;

        Utils::TemporaryDirectory temp("alien-vsix");
        Unarchiver unarchiver;
        unarchiver.setArchive(vsix);
        unarchiver.setDestination(temp.path());
        QEventLoop loop;
        connect(&unarchiver, &Unarchiver::done, &loop, [&loop] { loop.quit(); });
        unarchiver.start();
        loop.exec();
        if (const Result<> r = unarchiver.result(); !r) {
            MessageManager::writeFlashing(
                Tr::tr("Cannot unpack \"%1\": %2").arg(vsix.toUserOutput(), r.error()));
            return;
        }

        const FilePath extracted = temp.path() / "extension";
        const Result<VscodeManifest> manifest
            = VscodeManifest::fromPackageJson(extracted / "package.json");
        if (!manifest) {
            MessageManager::writeFlashing(
                Tr::tr("\"%1\" is not a valid extension: %2")
                    .arg(vsix.toUserOutput(), manifest.error()));
            return;
        }

        const QString folderName = manifest->qualifiedId()
            + (manifest->version.isEmpty() ? QString() : '-' + manifest->version);
        const FilePath target = settings().extensionsDir() / folderName;
        target.removeRecursively();
        if (const Result<> r = extracted.copyRecursively(target); !r) {
            MessageManager::writeFlashing(Tr::tr("Cannot install to \"%1\": %2")
                                              .arg(target.toUserOutput(), r.error()));
            return;
        }

        MessageManager::writeFlashing(
            Tr::tr("Installed VS Code extension \"%1\".").arg(manifest->qualifiedId()));
        reload();
    }

    void reload()
    {
        for (const QPointer<AlienClient> &client : std::as_const(m_clients)) {
            if (client)
                LanguageClient::LanguageClientManager::shutdownClient(client);
        }
        m_clients.clear();

        if (!settings().enable()) {
            deactivateAll();
            return;
        }

        // The host reads the extension files itself, so they have to sit on
        // its own device. Without this the failure is a "Cannot find module"
        // from node, which says nothing about the device mismatch.
        if (!settings().extensionsDir().isSameDevice(settings().nodeJsPath())) {
            MessageManager::writeFlashing(
                Tr::tr("The extensions directory \"%1\" is not on the same device as Node.js "
                       "(\"%2\"), so the extension host cannot read it.")
                    .arg(settings().extensionsDir().toUserOutput(),
                         settings().nodeJsPath().toUserOutput()));
            deactivateAll();
            return;
        }

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

        // Activate extensions with a JS entry point in the host. Defer until a
        // workspace folder exists (a project or an open document): extensions
        // like qt-qml only start their language server for a folder, so they
        // must see one at activation time.
        m_activatable
            = Utils::filtered(manifests, [](const VscodeManifest &m) { return !m.main.isEmpty(); });

        if (!m_activationTriggersConnected) {
            m_activationTriggersConnected = true;
            connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
                    this, [this] { syncActivation(); });
            connect(ProjectExplorer::ProjectManager::instance(),
                    &ProjectExplorer::ProjectManager::projectAdded, this,
                    [this] { syncActivation(); });
        }
        syncActivation();
    }

    // Reconciles the set of extensions running in the host with the desired set
    // (enabled and activatable), once a workspace folder is available. Newly
    // disabled extensions are deactivated, newly enabled ones are activated.
    void syncActivation()
    {
        const QJsonArray folders = workspaceFolders(host());
        if (folders.isEmpty())
            return; // wait until a project or document provides a folder

        const QList<VscodeManifest> desired = extensionsToActivate();
        QSet<QString> desiredIds;
        for (const VscodeManifest &manifest : desired)
            desiredIds.insert(manifest.qualifiedId());

        // Deactivate extensions the user turned off (or that disappeared).
        for (const QString &id : m_activeIds.values()) {
            if (!desiredIds.contains(id)) {
                host()->deactivate(id);
                m_activeIds.remove(id);
            }
        }

        // Activate the ones not running yet.
        const QList<VscodeManifest> toActivate
            = Utils::filtered(desired, [this](const VscodeManifest &manifest) {
                  return !m_activeIds.contains(manifest.qualifiedId());
              });
        if (!toActivate.isEmpty()) {
            host()->setConfiguration(buildConfiguration(m_activatable));
            host()->setWorkspaceFolders(folders);
            // Resolve dependencies against every discovered extension so a listed
            // extension can pull in a dependency that is not itself listed.
            activateInDependencyOrder(toActivate, m_activatable);
        }
    }

    void deactivateAll()
    {
        if (!m_host)
            return;
        for (const QString &id : m_activeIds.values())
            m_host->deactivate(id);
        m_activeIds.clear();
    }

    // The commands the running extensions have registered, paired with the
    // titles their manifests give them - the host only knows the ids.
    QList<AlienCommand> extensionCommands() const
    {
        const QStringList ids = m_host ? m_host->registeredCommands() : QStringList();
        QList<AlienCommand> result;
        for (const QString &id : ids) {
            AlienCommand command;
            command.id = id;
            for (const VscodeManifest &manifest : m_activatable) {
                const auto it = std::find_if(manifest.commands.begin(), manifest.commands.end(),
                                             [&id](const VscodeCommand &c) {
                                                 return c.command == id;
                                             });
                if (it == manifest.commands.end())
                    continue;
                command.title = it->category.isEmpty()
                                    ? it->title : it->category + ": " + it->title;
                command.source = manifest.displayName.isEmpty() ? manifest.qualifiedId()
                                                                : manifest.displayName;
                break;
            }
            result.append(command);
        }
        return result;
    }

    // The subset of discovered extensions the user wants activated: the ones
    // ticked on the Extensions settings page. Discovery alone activates
    // nothing.
    QList<VscodeManifest> extensionsToActivate() const
    {
        const QStringList ids = settings().enabledExtensions.ids();
        const QSet<QString> enabled(ids.begin(), ids.end());
        return Utils::filtered(m_activatable, [&](const VscodeManifest &manifest) {
            return enabled.contains(manifest.qualifiedId());
        });
    }

    static QJsonArray workspaceFolders(ExtensionHost *host)
    {
        QJsonArray folders;
        for (ProjectExplorer::Project *project : ProjectExplorer::ProjectManager::projects()) {
            const FilePath dir = project->projectDirectory();
            if (!host->isOnHostDevice(dir))
                continue;
            folders.append(QJsonObject{
                {"path", host->toHostPath(dir)},
                {"name", project->displayName()},
            });
        }
        // Fall back to the current document's directory so extensions that work
        // per workspace folder (e.g. qt-qml's qmlls) also work on a lone file.
        if (folders.isEmpty()) {
            if (IDocument *document = EditorManager::currentDocument()) {
                const FilePath dir = document->filePath().parentDir();
                if (!dir.isEmpty() && host->isOnHostDevice(dir))
                    folders.append(QJsonObject{{"path", host->toHostPath(dir)},
                                               {"name", dir.fileName()}});
            }
        }
        return folders;
    }

    // Configuration served to extensions through workspace.getConfiguration():
    // defaults declared by each extension's contributes.configuration, with
    // user overrides from a settings.json in the extensions directory on top.
    QJsonObject buildConfiguration(const QList<VscodeManifest> &manifests) const
    {
        QJsonObject config;
        for (const VscodeManifest &manifest : manifests) {
            const QJsonObject defaults = manifest.configurationDefaults;
            for (auto it = defaults.begin(); it != defaults.end(); ++it)
                config.insert(it.key(), it.value());
        }

        const FilePath overrides = settings().extensionsDir() / "settings.json";
        if (const Result<QByteArray> contents = overrides.fileContents()) {
            const QJsonObject user = QJsonDocument::fromJson(*contents).object();
            for (auto it = user.begin(); it != user.end(); ++it)
                config.insert(it.key(), it.value());
        }
        return config;
    }

    void activateInDependencyOrder(const QList<VscodeManifest> &roots,
                                   const QList<VscodeManifest> &all)
    {
        QHash<QString, VscodeManifest> byId;
        for (const VscodeManifest &manifest : all)
            byId.insert(manifest.qualifiedId(), manifest);

        const std::function<void(const VscodeManifest &)> activate =
            [&](const VscodeManifest &manifest) {
                if (m_activeIds.contains(manifest.qualifiedId()))
                    return;
                m_activeIds.insert(manifest.qualifiedId());
                for (const QString &dep : manifest.extensionDependencies) {
                    if (byId.contains(dep))
                        activate(byId.value(dep));
                }
                host()->activate(manifest);
            };
        for (const VscodeManifest &manifest : roots)
            activate(manifest);
    }

    QList<QPointer<AlienClient>> m_clients;
    QPointer<ExtensionHost> m_host;
    QPointer<QLabel> m_statusMessage;
    QHash<QString, StatusBarItem *> m_statusItems;
    QList<VscodeManifest> m_activatable;
    QSet<QString> m_activeIds; // extensions currently running in the host
    bool m_activationTriggersConnected = false;
    std::unique_ptr<AutoWebviewRenderer> m_webviewRenderer;
    std::unique_ptr<AlienLocatorFilter> m_locatorFilter;
};

} // namespace Alien::Internal

#include "alienplugin.moc"
