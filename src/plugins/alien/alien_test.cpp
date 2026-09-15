// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alien_test.h"

#include "alienclient.h"
#include "aliencompletion.h"
#include "alienconstants.h"
#include "alienhover.h"
#include "alienlocatorfilter.h"
#include "alienoutline.h"
#include "alienpanes.h"
#include "alienplugin.h"
#include "aliensettings.h"
#include "alientests.h"
#include "alientr.h"
#include "alientreeview.h"
#include "autowebviewrenderer.h"
#include "codicons.h"
#include "extensionhost.h"
#include "extensionregistry.h"
#include "vscodemanifest.h"
#ifdef ALIEN_WITH_WEBENGINE
#include "webenginewebviewrenderer.h"
#endif

#include <autotest/testresult.h>
#include <autotest/testrunner.h>

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/fileutils.h>
#include <coreplugin/find/searchresultwindow.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/ioutputpane.h>
#include <coreplugin/navigationwidget.h>
#include <coreplugin/outputwindow.h>
#include <coreplugin/progressmanager/progressmanager.h>
#include <coreplugin/secretaspect.h>
#include <coreplugin/terminal/searchableterminal.h>

#include <debugger/debuggertooltipmanager.h>

#include <languageclient/languageclientmanager.h>

#include <mcp/server/toolregistry.h>

#include <projectexplorer/kitmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/taskhub.h>

#include <texteditor/autocompleter.h>
#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposaliteminterface.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/genericproposalmodel.h>
#include <texteditor/fontsettings.h>
#include <texteditor/textdocument.h>
#include <texteditor/textdocumentlayout.h>
#include <texteditor/texteditor.h>
#include <texteditor/textmark.h>
#include <texteditor/textsuggestion.h>

#include <utils/infobar.h>
#include <utils/mimeconstants.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>

#include <functional>
#include <memory>

using namespace Core;
using namespace Utils;

namespace Alien::Internal {

// Writes a minimal extension (package.json + extension.js) and returns its
// parsed manifest. Free function so moc does not process it.
static Result<VscodeManifest> writeMockExtension(
    const FilePath &dir, const QString &name, const QString &js,
    const QJsonArray &activationEvents = QJsonArray{"*"},
    const QJsonObject &contributes = {})
{
    if (const Result<> r = dir.ensureWritableDir(); !r)
        return make_unexpected(r.error());
    const QJsonObject pkg{
        {"name", name},
        {"publisher", "theqtcompany"},
        {"main", "./extension.js"},
        {"engines", QJsonObject{{"vscode", "^1.0.0"}}},
        {"activationEvents", activationEvents},
        {"contributes", contributes},
    };
    const FilePath packageJson = dir / "package.json";
    if (const Result<qint64> r = packageJson.writeFileContents(QJsonDocument(pkg).toJson()); !r)
        return make_unexpected(r.error());
    if (const Result<qint64> r = (dir / "extension.js").writeFileContents(js.toUtf8()); !r)
        return make_unexpected(r.error());
    return VscodeManifest::fromPackageJson(packageJson);
}

// A project whose file list the test sets directly. The managers that would
// produce one live in plugins a test run does not load.
class TestProject final : public ProjectExplorer::Project
{
public:
    explicit TestProject(const FilePath &projectFile)
        : ProjectExplorer::Project("text/plain", projectFile)
    {
        setType("Alien.TestProject");
        setDisplayName("Alien Test Project");
    }

    void setFiles(const FilePaths &files)
    {
        auto root = std::make_unique<ProjectExplorer::ProjectNode>(projectDirectory());
        std::vector<std::unique_ptr<ProjectExplorer::FileNode>> nodes;
        for (const FilePath &file : files) {
            nodes.emplace_back(std::make_unique<ProjectExplorer::FileNode>(
                file, ProjectExplorer::FileType::Source));
        }
        root->addNestedNodes(std::move(nodes));
        setRootProjectNode(std::move(root));
    }
};

// An editor of Qt Creator's own that is not a text editor, the kind the binary
// editor and the image viewer are. Those live in plugins a test run does not
// load, and what matters here is only that the document is not a text one.
class TestOtherDocument final : public IDocument
{
public:
    TestOtherDocument() { setId("Alien.TestOtherEditor"); }

    Result<> open(const FilePath &filePath, const FilePath &realFilePath) final
    {
        Q_UNUSED(realFilePath)
        setFilePath(filePath);
        return ResultOk;
    }
};

class TestOtherEditor final : public IEditor
{
public:
    TestOtherEditor() { setWidget(new QWidget); }
    ~TestOtherEditor() final { delete m_widget; }

private:
    IDocument *document() const final { return m_document.get(); }
    QWidget *toolBar() final { return nullptr; }

    std::shared_ptr<TestOtherDocument> m_document{new TestOtherDocument};
};

class TestOtherEditorFactory final : public IEditorFactory
{
public:
    TestOtherEditorFactory()
    {
        setId("Alien.TestOtherEditor");
        setDisplayName("Alien Test Editor");
        addMimeType(Utils::Constants::OCTET_STREAM_MIMETYPE);
        setEditorCreator([] { return new TestOtherEditor; });
    }
};

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

        const Result<> result
            = host.activateBundledTestExtension("lsptestextension", {"mockserver/server.js"});
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QVERIFY(spy.wait(15000));
        const QString id = spy.first().first().toString();
        AlienClient *client = host.languageClient(id);
        QVERIFY(client);
        // Started after activate() returned, it still serves the extension's language.
        QVERIFY(client->isSupportedFile(FilePath::fromString("/a.alientxt"), QString()));

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

        const Result<> result
            = host.activateBundledTestExtension("lsptestextension", {"mockserver/server.js"});
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QVERIFY(spy.wait(15000));
        const QString id = spy.first().first().toString(); // "<extensionId>:<clientId>"
        const QPointer<AlienClient> client = host.languageClient(id);
        QVERIFY(client);
        QTRY_VERIFY_WITH_TIMEOUT(client->reachable(), 15000);

        // Deactivating the extension must stop the server it started.
        const QString extensionId = id.left(id.indexOf(':'));
        QVERIFY(!extensionId.isEmpty());
        host.deactivate(extensionId);
        QVERIFY(!host.languageClient(id));
        // Still waited for on shutdown while it stops.
        QVERIFY(client);
        QVERIFY(host.languageClients().contains(client));
    }

    void testDeactivateWhileHostStarts()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        QSignalSpy activated(&host, &ExtensionHost::activated);
        const Result<> result = host.activateBundledTestExtension("testextension");
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        QVERIFY(!host.isRunning());
        QCOMPARE(activated.count(), 0);

        // Deactivated before the host could even take the activation.
        host.deactivate("theqtcompany.alien-test");
        QTRY_COMPARE_WITH_TIMEOUT(activated.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!host.registeredCommands().contains("alien.hello"), 15000);
    }

    void testStoppedHostStopsLanguageClient()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::languageClientStarted);
        const Result<> result
            = host.activateBundledTestExtension("lsptestextension", {"mockserver/server.js"});
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        QVERIFY(spy.wait(15000));
        const QPointer<AlienClient> client = host.languageClient(spy.first().first().toString());
        QVERIFY(client);
        QTRY_VERIFY_WITH_TIMEOUT(client->reachable(), 15000);

        host.shutdown();
        // Asked to stop, and still waited for until it has.
        QVERIFY(!client || !client->reachable());
        QVERIFY(!client || host.languageClients().contains(client));
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

        const Result<> result = host.activateBundledTestExtension("docsynctestextension");
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

    void testOpenAndShowDocument()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "shown.txt";
        QVERIFY(file.writeFileContents("open me").has_value());

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);

        const Result<> result = host.activateBundledTestExtension("docsynctestextension");
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 10000);

        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.test.openDocument"),
                                 15000);
        host.executeCommand("alien.test.openDocument", QJsonArray{host.toHostPath(file)});

        auto saw = [&spy](const QString &prefix) {
            return [&spy, prefix] {
                for (const QList<QVariant> &args : spy) {
                    if (args.first().toString().startsWith(prefix))
                        return true;
                }
                return false;
            };
        };
        // The extension gets the file's text without an editor for it ...
        QTRY_VERIFY_WITH_TIMEOUT(saw("read:open me")(), 15000);
        // ... and showing it opens that editor in Qt Creator.
        QTRY_VERIFY_WITH_TIMEOUT(saw("shown:")(), 15000);
        QVERIFY(EditorManager::currentDocument());
        QCOMPARE(EditorManager::currentDocument()->filePath(), file);

        EditorManager::closeAllEditors(false);
    }

    void testApplyEdit()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath onDisk = FilePath::fromString(dir.path()) / "closed.txt";
        const FilePath inEditor = FilePath::fromString(dir.path()) / "open.txt";
        QVERIFY(onDisk.writeFileContents("hello\nworld\n").has_value());
        QVERIFY(inEditor.writeFileContents("hello\nworld\n").has_value());

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);

        const Result<> result = host.activateBundledTestExtension("docsynctestextension");
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.test.applyEdit"),
                                 15000);

        // A file nobody has open is edited on disk ...
        host.executeCommand("alien.test.applyEdit", QJsonArray{host.toHostPath(onDisk)});
        auto sawEdited = [&spy] {
            for (const QList<QVariant> &args : spy) {
                if (args.first().toString() == "edited:true:size=1")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawEdited(), 15000);
        QTRY_COMPARE(onDisk.fileContents().value_or(QByteArray()),
                     QByteArray("FIRST\ninserted\nworld\n"));

        // ... and one that is open lands in the editor, undoable in one step.
        QVERIFY(EditorManager::openEditor(inEditor));
        TextEditor::TextDocument *document
            = TextEditor::TextDocument::textDocumentForFilePath(inEditor);
        QVERIFY(document);
        spy.clear();
        host.executeCommand("alien.test.applyEdit", QJsonArray{host.toHostPath(inEditor)});
        QTRY_COMPARE_WITH_TIMEOUT(document->plainText(), QString("FIRST\ninserted\nworld\n"),
                                  15000);
        QVERIFY(document->isModified());
        document->document()->undo();
        QCOMPARE(document->plainText(), QString("hello\nworld\n"));

        EditorManager::closeAllEditors(false);
    }

    void testVirtualDocument()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);

        const Result<> result = host.activateBundledTestExtension("docsynctestextension");
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 10000);

        QTRY_VERIFY_WITH_TIMEOUT(
            host.registeredCommands().contains("alien.test.openVirtualDocument"), 15000);
        host.executeCommand("alien.test.openVirtualDocument");

        // The extension's own scheme is answered by the extension, ...
        auto sawProvided = [&spy] {
            for (const QList<QVariant> &args : spy) {
                if (args.first().toString() == "virtual:provided for /greeting")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawProvided(), 15000);

        // ... and showing it puts that text in an editor, there being no file.
        auto shownText = [] {
            auto document = qobject_cast<TextEditor::TextDocument *>(
                EditorManager::currentDocument());
            return document ? document->plainText() : QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(shownText() == "provided for /greeting", 15000);

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
        QVERIFY(file.writeFileContents("hello\nworld").has_value());

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::diagnosticsPublished);
        QSignalSpy taskSpy(&ProjectExplorer::taskHub(), &ProjectExplorer::TaskHub::taskAdded);

        const Result<> result = host.activateBundledTestExtension("diagnosticstestextension");
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

        // The same diagnostic has to reach Issues: a text mark is only visible
        // in an open editor.
        auto sawTask = [&taskSpy] {
            for (const QList<QVariant> &args : taskSpy) {
                const auto task = args.at(0).value<ProjectExplorer::Task>();
                if (task.category() == Constants::TASK_CATEGORY_DIAGNOSTICS
                    && task.file().fileName() == "foo.txt") {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY(sawTask());

        // A range the extension marked as unnecessary is drawn as such, not
        // just reported.
        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(
            EditorManager::currentEditor());
        QVERIFY(textEditor);
        const QList<QTextEdit::ExtraSelection> tagged
            = textEditor->editorWidget()->extraSelections(Id("Alien.DiagnosticTags"));
        QCOMPARE(tagged.size(), 1);
        QCOMPARE(tagged.first().cursor.selectedText(), QString("hel"));
        QCOMPARE(tagged.first().format.foreground().color(),
                 Utils::creatorColor(Utils::Theme::TextColorDisabled));

        // Where else the diagnostic points is on the mark it left.
        auto document = qobject_cast<TextEditor::TextDocument *>(textEditor->document());
        QVERIFY(document);
        QStringList tooltips;
        QSet<int> lines;
        for (TextEditor::TextMark *mark : document->marks()) {
            tooltips << mark->toolTip();
            // The task in Issues does not bring a second mark for the same line.
            QVERIFY2(!lines.contains(mark->lineNumber()), qPrintable(mark->toolTip()));
            lines.insert(mark->lineNumber());
        }
        QVERIFY2(tooltips.filter("foo.txt:2: first seen here").size() == 1,
                 qPrintable(tooltips.join('|')));

        // What the extension drew goes with it.
        host.shutdown();
        QVERIFY(textEditor->editorWidget()->extraSelections(Id("Alien.DiagnosticTags")).isEmpty());

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
        const Result<> result = host.activateBundledTestExtension("completiontestextension");
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 10000);
        QVERIFY(EditorManager::openEditor(file));

        // Re-issue the request while activation settles; assert the in-host
        // provider's items come back.
        QStringList labels;
        QJsonArray received;
        auto poll = [&] {
            host.editor().requestCompletion(file, 0, 1, [&](const QJsonArray &items) {
                labels.clear();
                received = items;
                for (const QJsonValue &item : items)
                    labels << item.toObject().value("label").toString();
            });
            return labels.contains("alienComplete");
        };
        QTRY_VERIFY_WITH_TIMEOUT(poll(), 15000);

        // What an item needs besides its own text comes across with it.
        const QJsonObject first = received.first().toObject();
        const QJsonArray extra = first.value("additionalTextEdits").toArray();
        QCOMPARE(extra.size(), 1);
        QCOMPARE(extra.first().toObject().value("newText").toString(), QString("import alien\n"));

        const QJsonObject second = received.at(1).toObject();
        QVERIFY(second.value("isSnippet").toBool());
        QCOMPARE(second.value("insertText").toString(), QString("alienSnip(${1:arg})"));

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
        const Result<> result = host.activateBundledTestExtension("hoverdeftestextension");
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 10000);
        QVERIFY(EditorManager::openEditor(file));

        QString hover;
        auto hoverPoll = [&] {
            host.editor().requestHover(file, 0, 1, [&hover](const QString &text) { hover = text; });
            return hover.contains("Alien hover");
        };
        QTRY_VERIFY_WITH_TIMEOUT(hoverPoll(), 15000);

        // An aborted hover reports nothing once its answer comes, which is
        // before the answer to the request sent after it.
        auto widget = TextEditor::TextEditorWidget::currentTextEditorWidget();
        QVERIFY(widget);
        AlienHoverHandler handler(&host);
        int reports = 0;
        handler.checkPriority(widget, 1, [&reports](int) { ++reports; });
        handler.abort();
        bool answered = false;
        host.editor().requestHover(file, 0, 1, [&answered](const QString &) { answered = true; });
        QTRY_VERIFY(answered);
        QCOMPARE(reports, 0);

        QString target;
        auto definitionPoll = [&] {
            host.editor().requestDefinition(file, 0, 1, [&target](const QJsonArray &locations) {
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
                    host.window().resolveQuickPick(id, {1});
                });

        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        const Result<> result = host.activateBundledTestExtension("quickpicktestextension");
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

    void testWorkspaceFolderPick()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        host.setWorkspaceFolders(QJsonArray{QJsonObject{{"path", "/tmp/one"}, {"name", "one"}},
                                            QJsonObject{{"path", "/tmp/two"}, {"name", "two"}}});
        QStringList offered;
        connect(&host, &ExtensionHost::quickPickRequested, &host,
                [&host, &offered](int id, const QStringList &items, const QString &) {
                    offered = items;
                    host.window().resolveQuickPick(id, {1});
                });

        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        const Result<> result = host.activateBundledTestExtension("quickpicktestextension");
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.pickFolder"), 10000);
        host.executeCommand("alien.pickFolder");

        auto sawFolder = [&spy] {
            for (const QList<QVariant> &args : spy) {
                if (args.first().toString() == "folder:two")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawFolder(), 15000);
        QCOMPARE(offered, QStringList({"one", "two"}));
    }

    // A manifest keeps its user-visible strings in package.nls.json, referred
    // to as "%key%", so the IDE can show them in its own language.
    void testManifestTranslation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const QJsonObject pkg{
            {"name", "nlsdemo"},
            {"publisher", "theqtcompany"},
            {"main", "./extension.js"},
            {"displayName", "%ext.name%"},
            {"description", "%ext.missing%"},
            {"contributes",
             QJsonObject{{"commands",
                          QJsonArray{QJsonObject{{"command", "nls.hello"},
                                                 {"title", "%cmd.title%"}},
                                     QJsonObject{{"command", "nls.noted"},
                                                 {"title", "%cmd.noted%"}},
                                     QJsonObject{{"command", "nls.empty"},
                                                 {"title", "%cmd.empty%"}}}}}},
        };
        QVERIFY((root / "package.json").writeFileContents(QJsonDocument(pkg).toJson()));
        // A bundle value carrying a translator's note, which is how
        // cmake-tools keeps several of its command titles.
        const QJsonObject nls{{"ext.name", "Translated Name"},
                              {"cmd.title", "Translated Title"},
                              {"cmd.noted",
                               QJsonObject{{"message", "Noted Title"},
                                           {"comment", QJsonArray{"Keep the product name"}}}},
                              {"cmd.empty", QJsonObject{{"comment", QJsonArray{"A note alone"}}}}};
        QVERIFY((root / "package.nls.json").writeFileContents(QJsonDocument(nls).toJson()));

        const Result<VscodeManifest> manifest
            = VscodeManifest::fromPackageJson(root / "package.json");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        QCOMPARE(manifest->displayName, QString("Translated Name"));
        QCOMPARE(manifest->commands.value(0).title, QString("Translated Title"));
        QCOMPARE(manifest->commands.value(1).title, QString("Noted Title"));
        // An unknown key keeps its placeholder rather than turning into nothing,
        // and so does one whose note came without a string.
        QCOMPARE(manifest->description, QString("%ext.missing%"));
        QCOMPARE(manifest->commands.value(2).title, QString("%cmd.empty%"));
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

        const Result<> result = host.activateBundledTestExtension("statusbartestextension");
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        auto sawMessage = [&messageSpy] {
            for (const QList<QVariant> &args : messageSpy) {
                if (args.first().toString() == "Alien ready")
                    return true;
            }
            return false;
        };
        // The item carries what clicking it runs; without that it is a label
        // the user presses and nothing happens.
        auto sawItem = [&itemSpy] {
            for (const QList<QVariant> &args : itemSpy) {
                const QJsonObject item = args.at(1).toJsonObject();
                if (item.value("text").toString() == "AlienItem"
                    && item.value("visible").toBool()
                    && item.value("command").toString() == "alien.status.clicked") {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawMessage(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(sawItem(), 15000);

        // And pressing it runs that command.
        StatusBarItem widget;
        bool ran = false;
        widget.setCommand("alien.status.clicked", [&host, &ran](const QString &command) {
            ran = true;
            host.executeCommand(command);
        });
        QTest::mouseClick(&widget, Qt::LeftButton);
        QVERIFY(ran);

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        QTRY_VERIFY_WITH_TIMEOUT(
            Utils::anyOf(messages, [](const QList<QVariant> &args) {
                return args.first().toString() == "statusClicked";
            }), 15000);
    }

    void testTreeView()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::treeViewRegistered);

        const Result<> result = host.activateBundledTestExtension("treeviewtestextension");
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 10000);
        QCOMPARE(spy.first().first().toString(), QString("alienExplorer"));

        // A view an extension registers is one the sidebar offers, like
        // Creator's own ones. The sidebars take their list at startup, before
        // any extension has activated.
        QVERIFY(NavigationWidget::activateSubWidget(Id("Alien.alienExplorer"), Side::Left));

        // Root level.
        QStringList rootLabels;
        QString rootAId;
        QString rootAIcon;
        auto rootPoll = [&] {
            host.window().requestTreeChildren("alienExplorer", {}, [&](const QJsonArray &nodes) {
                rootLabels.clear();
                for (const QJsonValue &n : nodes) {
                    rootLabels << n.toObject().value("label").toString();
                    if (n.toObject().value("label").toString() == "Root A") {
                        rootAId = n.toObject().value("id").toString();
                        rootAIcon = n.toObject().value("icon").toString();
                    }
                }
            });
            return rootLabels.contains("Root A") && rootLabels.contains("Root B");
        };
        QTRY_VERIFY_WITH_TIMEOUT(rootPoll(), 15000);

        // An item's own icon comes across, and Qt Creator has one for it.
        QCOMPARE(rootAIcon, QString("$(folder)"));
        QVERIFY(!iconFromSpec(rootAIcon).isNull());
        QVERIFY(iconFromSpec("$(no-such-icon-name)").isNull());

        // Picking a row in the sidebar reaches the extension that made it.
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        QString watchedId;
        auto watchedPoll = [&] {
            host.window().requestTreeChildren("alienWatched", {}, [&](const QJsonArray &nodes) {
                for (const QJsonValue &n : nodes) {
                    if (n.toObject().value("label").toString() == "Root B")
                        watchedId = n.toObject().value("id").toString();
                }
            });
            return !watchedId.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(watchedPoll(), 15000);
        host.window().reportTreeSelection("alienWatched", {watchedId});
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.first().first().toString(), QString("picked:Root B"));

        // Children of Root A.
        QStringList childLabels;
        auto childPoll = [&] {
            host.window().requestTreeChildren("alienExplorer", rootAId, [&](const QJsonArray &nodes) {
                childLabels.clear();
                for (const QJsonValue &n : nodes)
                    childLabels << n.toObject().value("label").toString();
            });
            return childLabels.contains("Child A1");
        };
        QTRY_VERIFY_WITH_TIMEOUT(childPoll(), 15000);

        // A menu entry gated on a context key the extension has not set yet is
        // not offered ...
        QStringList titles;
        auto menuTitles = [&host, &rootAId, &titles] {
            host.window().requestTreeMenu("alienExplorer", rootAId, "view/item/context",
                                 [&titles](const QJsonArray &items) {
                                     titles.clear();
                                     for (const QJsonValue &item : items)
                                         titles << item.toObject().value("title").toString();
                                 });
            return titles;
        };
        QTRY_COMPARE_WITH_TIMEOUT(menuTitles(), QStringList({"Always"}), 15000);

        // The palette offers what the manifest lets it: not the context menu
        // command hidden with "when": "false", and the gated one only once its
        // key is set. All three are registered, so registeredCommands() has
        // them regardless.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.tree.always"), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(host.paletteCommands(),
                                  QStringList({"alien.tree.setReady"}), 15000);

        // The view's own buttons are those of this view, whatever their
        // clause says, so the toolbar can follow one that has not held yet.
        auto titleCommands = [&host] {
            QStringList commands;
            for (const QJsonValue &item : host.window().treeTitleMenu("alienExplorer"))
                commands << item.toObject().value("command").toString();
            return commands;
        };
        QTRY_COMPARE_WITH_TIMEOUT(titleCommands(),
                                  QStringList({"alien.tree.always", "alien.tree.gated"}), 15000);
        auto titleVisible = [&host](const QString &command) {
            for (const QJsonValue &item : host.window().treeTitleMenu("alienExplorer")) {
                if (item.toObject().value("command").toString() == command)
                    return item.toObject().value("visible").toBool();
            }
            return false;
        };
        QVERIFY(titleVisible("alien.tree.always"));
        QVERIFY(!titleVisible("alien.tree.gated"));

        // The button of the gated entry is in the sidebar from the start and
        // keeps out of the way until the extension sets the key.
        auto gatedButton = [] {
            return ICore::mainWindow()->findChild<QToolButton *>("alienViewButton.alien.tree.gated");
        };
        QVERIFY(gatedButton());
        QVERIFY(gatedButton()->isHidden());

        // ... and is once setContext says so.
        host.executeCommand("alien.tree.setReady");
        QTRY_COMPARE_WITH_TIMEOUT(menuTitles(), QStringList({"Always", "Gated"}), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(host.paletteCommands(),
                                  QStringList({"alien.tree.gated", "alien.tree.setReady"}),
                                  15000);
        QTRY_VERIFY_WITH_TIMEOUT(!gatedButton()->isHidden(), 15000);

        // What the user opened stays open when the extension refreshes the
        // view, and nothing else opens with it.
        auto view = ICore::mainWindow()->findChild<QTreeView *>("alienTreeView.alienExplorer");
        QVERIFY(view);
        QAbstractItemModel *model = view->model();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 15000);
        const QModelIndex rootA = model->index(0, 0);
        QCOMPARE(rootA.data().toString(), QString("Root A"));
        view->expand(rootA);
        QTRY_COMPARE_WITH_TIMEOUT(model->index(0, 0, rootA).data().toString(),
                                  QString("Child A1"), 15000);
        QSignalSpy refreshed(&host, &ExtensionHost::treeViewRefreshed);
        host.executeCommand("alien.tree.refresh");
        QTRY_VERIFY_WITH_TIMEOUT(!refreshed.isEmpty(), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 15000);
        QVERIFY(view->isExpanded(model->index(0, 0)));
        QVERIFY(!view->isExpanded(model->index(1, 0)));
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

        const Result<> result = host.activateBundledTestExtension("webviewtestextension");
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
        host.window().deliverWebviewMessage(id, QJsonObject{{"text", "hi"}});
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

    // The settings file in the extensions directory is the user's own, written
    // by hand, so what VS Code allows in such a file is allowed here: a
    // comment does not cost the file its values, and a file that does not
    // parse says which one it is instead of dropping every override in
    // silence.
    void testHandWrittenSettingsFileIsRead()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath extensions = FilePath::fromString(dir.path());
        const FilePath previous = settings().extensionsDir();
        settings().extensionsDir.setValue(extensions);
        const QScopeGuard restore([previous] { settings().extensionsDir.setValue(previous); });

        const FilePath file = extensions / "settings.json";
        QVERIFY(file.writeFileContents("{\n"
                                       "  // Where the qmlls to use lives.\n"
                                       "  \"qt-qml.qmlls.customExePath\": \"/opt/qmlls\"\n"
                                       "}\n"));
        QStringList errors;
        QCOMPARE(ExtensionHost::configurationOverrides(&errors)
                     .value("qt-qml.qmlls.customExePath").toString(),
                 QString("/opt/qmlls"));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(", ")));

        QVERIFY(file.writeFileContents("{ \"qt-qml.qmlls.customExePath\": tru }\n"));
        QVERIFY(!ExtensionHost::configurationOverrides(&errors)
                     .contains("qt-qml.qmlls.customExePath"));
        QCOMPARE(errors.size(), 1);
        QVERIFY2(errors.first().contains(file.toUserOutput()), qPrintable(errors.first()));

        // Valid JSON, but not the object the overrides are kept in.
        errors.clear();
        QVERIFY(file.writeFileContents("[{ \"qt-qml.qmlls.customExePath\": true }]\n"));
        QVERIFY(ExtensionHost::configurationOverrides(&errors).isEmpty());
        QCOMPARE(errors.size(), 1);
    }

    // A declared setting has a value even when its schema names no default:
    // VS Code derives one from the type, and an extension reading it expects
    // that rather than undefined.
    void testUndeclaredDefaultFollowsTheType()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "def", "alien-default-test",
            "const vscode = require('vscode');\n"
            "function activate() {\n"
            "  const config = vscode.workspace.getConfiguration('aliendef');\n"
            "  vscode.window.showInformationMessage('defaults:' + [\n"
            "      String(config.get('anything')),\n"
            "      JSON.stringify(config.get('group')),\n"
            "      String(config.get('name')),\n"
            "      String(config.get('count')),\n"
            "      String(config.get('declared')),\n"
            "  ].join('|'));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"configuration", QJsonObject{{"properties", QJsonObject{
                {"aliendef.anything", QJsonObject{{"description", "no type at all"}}},
                {"aliendef.group", QJsonObject{{"type", "object"}}},
                {"aliendef.name", QJsonObject{{"type", "string"}}},
                {"aliendef.count", QJsonObject{{"type", "integer"}}},
                {"aliendef.declared", QJsonObject{{"type", "string"}, {"default", "kept"}}}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(messages.size() >= 1, 15000);
        QCOMPARE(messages.first().first().toString(), QString("defaults:null|{}||0|kept"));
    }

    void testSettingsPageIsOwned()
    {
        const auto pages = [] {
            return Utils::count(Core::IOptionsPage::allOptionsPages(),
                                [](Core::IOptionsPage *page) {
                                    return page->id() == Constants::SETTINGS_ID;
                                });
        };
        // Whoever asks for the page owns it, and it is off the list again when
        // it goes - a page outliving what answers for it is one the user can
        // still open, in a plugin that is no longer there.
        const int before = pages();
        {
            const std::unique_ptr<Core::IOptionsPage> page = setupAlienSettings();
            QVERIFY(page);
            QCOMPARE(pages(), before + 1);
        }
        QCOMPARE(pages(), before);
    }

    void testSecretsAreKept()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");
        if (!Core::SecretAspect::isSecretStorageAvailable())
            QSKIP("no secret storage on this machine");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "secret", "alien-secret-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Secret');\n"
            "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.secret.go',\n"
            "      async () => {\n"
            "    say('before', await context.secrets.get('token'));\n"
            "    await context.secrets.store('token', 'hunter2');\n"
            "    say('after', await context.secrets.get('token'));\n"
            "    await context.secrets.delete('token');\n"
            "    say('gone', await context.secrets.get('token'));\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.secret.keep',\n"
            "      async () => {\n"
            "    await context.secrets.store('token', 'hunter2');\n"
            "    say('kept', await context.secrets.get('token'));\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.secret.peek',\n"
            "      async () => say('peek', await context.secrets.get('token'))));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand("
            "'alien.secret.forget', async () => {\n"
            "    await context.secrets.delete('token');\n"
            "    say('forgotten', await context.secrets.get('token'));\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.secret.go"), 15000);
        host.executeCommand("alien.secret.go");

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };
        QTRY_VERIFY_WITH_TIMEOUT(lines().size() >= 3, 15000);

        // Nothing before it was stored, what was stored afterwards, and
        // nothing again once it was deleted.
        QCOMPARE(reported.at(0), QString("before=undefined"));
        QCOMPARE(reported.at(1), QString("after=hunter2"));
        QCOMPARE(reported.at(2), QString("gone=undefined"));

        // And one extension cannot read another's. The first one stores
        // without deleting, so there is something to fail to read.
        host.executeCommand("alien.secret.keep");
        QTRY_VERIFY_WITH_TIMEOUT(lines().size() >= 4, 15000);
        QCOMPARE(reported.at(3), QString("kept=hunter2"));

        // A second host is a fresh set of aspects, so what it reads has to come
        // from where the secret was actually put - which is what makes this a
        // stand-in for the next start of Qt Creator.
        ExtensionHost restarted(node);
        QSignalSpy restartedOutput(&restarted, &ExtensionHost::channelOutput);
        restarted.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(restarted.registeredCommands().contains("alien.secret.peek"),
                                 15000);
        restarted.executeCommand("alien.secret.peek");
        QTRY_VERIFY_WITH_TIMEOUT(restartedOutput.size() >= 1, 15000);
        QCOMPARE(restartedOutput.first().at(1).toString(), QString("peek=hunter2"));

        const FilePath otherRoot = FilePath::fromString(dir.path()) / "nosy";
        const Result<VscodeManifest> nosy = writeMockExtension(
            otherRoot, "alien-nosy-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Nosy');\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.nosy.go',\n"
            "      async () => {\n"
            "    channel.appendLine('peeked=' + await context.secrets.get('token'));\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(nosy.has_value(), qPrintable(nosy ? QString() : nosy.error()));

        ExtensionHost second(node);
        QSignalSpy secondOutput(&second, &ExtensionHost::channelOutput);
        second.activate(*nosy);
        QTRY_VERIFY_WITH_TIMEOUT(second.registeredCommands().contains("alien.nosy.go"), 15000);
        second.executeCommand("alien.nosy.go");
        QTRY_VERIFY_WITH_TIMEOUT(secondOutput.size() >= 1, 15000);
        // The same key, and nothing there: it is not this extension's.
        QCOMPARE(secondOutput.first().at(1).toString(), QString("peeked=undefined"));

        // Put the machine's keychain back as it was found.
        host.executeCommand("alien.secret.forget");
        QTRY_VERIFY_WITH_TIMEOUT(lines().size() >= 5, 15000);
        QCOMPARE(reported.at(4), QString("forgotten=undefined"));
    }

    void testDocumentChangeReportsWhatChanged()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "typed.txt";
        QVERIFY(source.writeFileContents("alpha\nbeta\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "typed", "alien-typed-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Typed');\n"
            "  context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => {\n"
            "    const change = event.contentChanges[0];\n"
            "    channel.appendLine([change.range.start.line, change.range.start.character,\n"
            "                        change.range.end.line, change.range.end.character,\n"
            "                        change.rangeOffset, change.rangeLength,\n"
            "                        JSON.stringify(change.text),\n"
            "                        JSON.stringify(event.document.getText())].join('|'));\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy activated(&host, &ExtensionHost::activated);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(activated.size() == 1, 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // One word typed in the middle of the second line.
        QTextCursor cursor(document->document());
        cursor.setPosition(6 + 4); // after "alpha\nbeta"
        cursor.insertText("!");

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 1, 15000);
        const QStringList parts = output.first().at(1).toString().split('|');
        QCOMPARE(parts.size(), 8);
        // Where it happened, not "the whole file was replaced".
        QCOMPARE(parts.at(0), QString("1")); // start line
        QCOMPARE(parts.at(1), QString("4")); // start character
        QCOMPARE(parts.at(2), QString("1")); // end line: an insertion is empty
        QCOMPARE(parts.at(3), QString("4"));
        QCOMPARE(parts.at(4), QString("10")); // offset
        QCOMPARE(parts.at(5), QString("0"));  // nothing removed
        QCOMPARE(parts.at(6), QString("\"!\""));
        // And the document still says what it says.
        QCOMPARE(parts.at(7), QString("\"alpha\\nbeta!\\n\""));

        // Two edits before the event loop runs, as a Replace All makes them:
        // one change covering both, against the text the extension had.
        cursor.setPosition(0);
        cursor.insertText("<");
        cursor.setPosition(12); // after "<alpha\nbeta!"
        cursor.insertText(">");

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 2, 15000);
        const QStringList both = output.at(1).at(1).toString().split('|');
        QCOMPARE(both.size(), 8);
        QCOMPARE(both.mid(0, 6), QStringList({"0", "0", "1", "5", "0", "11"}));
        QCOMPARE(both.at(6), QString("\"<alpha\\nbeta!>\""));
        QCOMPARE(both.at(7), QString("\"<alpha\\nbeta!>\\n\""));

        Core::EditorManager::closeEditors({editor}, false);
    }

    // Under a new name, a document is closed under the old one and opened
    // under the new one, and what is typed into it is reported there.
    void testRenamedDocumentFollows()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "before.txt";
        const FilePath renamed = FilePath::fromString(dir.path()) / "after.txt";
        QVERIFY(source.writeFileContents("text\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "renamed", "alien-renamed-test",
            "const vscode = require('vscode');\n"
            "const path = require('path');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Renamed');\n"
            "  const name = document => path.basename(document.uri.fsPath);\n"
            "  context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(document => {\n"
            "    channel.appendLine('open|' + name(document));\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.workspace.onDidCloseTextDocument(document => {\n"
            "    channel.appendLine('close|' + name(document));\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => {\n"
            "    channel.appendLine('change|' + name(event.document) + '|'\n"
            "                       + JSON.stringify(event.document.getText()));\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.languages.registerCodeLensProvider(\n"
            "      {language: 'plaintext'}, {provideCodeLenses(document) {\n"
            "    channel.appendLine('lens|' + name(document) + '|'\n"
            "                       + JSON.stringify(document.getText()));\n"
            "    return [];\n"
            "  }}));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QStringList lines;
        connect(&host, &ExtensionHost::channelOutput, &host,
                [&lines](const QString &, const QString &text) { lines.append(text.trimmed()); });
        QSignalSpy activated(&host, &ExtensionHost::activated);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(activated.size() == 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().codeLensLanguageIds().contains("plaintext"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);
        QTRY_VERIFY_WITH_TIMEOUT(lines.contains("open|before.txt"), 15000);

        QVERIFY(Core::FileUtils::renameFile(source, renamed));
        QCOMPARE(document->filePath(), renamed);
        QTRY_VERIFY_WITH_TIMEOUT(lines.contains("open|after.txt"), 15000);
        QVERIFY(lines.contains("close|before.txt"));

        QTextCursor cursor(document->document());
        cursor.insertText("!");
        QTRY_VERIFY_WITH_TIMEOUT(lines.contains("change|after.txt|\"!text\\n\""), 15000);
        // What is asked again after typing is asked for the new name.
        QTRY_VERIFY_WITH_TIMEOUT(lines.contains("lens|after.txt|\"!text\\n\""), 15000);

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testCommandThatNeverArrives()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Says it has a command, and registers something else instead: the
        // manifest is a promise the code does not have to keep.
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "liar", "alien-liar-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand("
            "'alien.liar.other', () => {}));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"onCommand:alien.liar.promised"},
            QJsonObject{{"commands", QJsonArray{QJsonObject{
                {"command", "alien.liar.promised"}, {"title", "Promised"}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy activated(&host, &ExtensionHost::activated);

        // Whoever waits for a command waits on this, and it says when waiting
        // is over whether or not the command turned up.
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(activated.size() == 1, 15000);
        QCOMPARE(activated.first().first().toString(), manifest->qualifiedId());

        // It is up, and what it promised is not among what it registered - so
        // the wait ends here rather than never.
        QVERIFY(host.registeredCommands().contains("alien.liar.other"));
        QVERIFY(!host.registeredCommands().contains("alien.liar.promised"));

        // And that is said where the user asked from.
        Core::IOutputPane *general = Utils::findOrDefault(
            Core::IOutputPane::allOutputPanes(),
            [](Core::IOutputPane *pane) { return pane->id() == "GeneralMessages"; });
        QVERIFY(general);
        QVERIFY(!general->outputWindows().isEmpty());
        Core::OutputWindow *window = general->outputWindows().first();
        window->clear();
        reportCommandNotRun("alien.liar.promised",
                            Tr::tr("\"%1\" does not offer it.").arg(manifest->qualifiedId()));
        QTRY_VERIFY_WITH_TIMEOUT(window->toPlainText().contains("alien.liar.promised"), 15000);
        QVERIFY(window->toPlainText().contains("does not offer it"));
    }

    void testPreferencesOpenOnNamedSetting()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path()) / "owner";
        QVERIFY(root.ensureWritableDir());
        const QByteArray packageJson = R"({
            "name": "owns", "publisher": "alien", "version": "0.0.1",
            "engines": {"vscode": "^1.0.0"},
            "contributes": {"configuration": {"properties": {
                "owns.theSetting": {"type": "string", "default": "x"},
                "owns.another": {"type": "boolean", "default": true}
            }}}
        })";
        QVERIFY((root / "package.json").writeFileContents(packageJson).has_value());
        QVERIFY((root / "extension.js").writeFileContents("module.exports={activate(){}};")
                    .has_value());
        // A second one, so narrowing the list to the owner means something.
        const FilePath other = FilePath::fromString(dir.path()) / "other";
        QVERIFY(other.ensureWritableDir());
        QVERIFY((other / "package.json")
                    .writeFileContents(R"({"name": "other", "publisher": "alien",
                                           "version": "0.0.1",
                                           "engines": {"vscode": "^1.0.0"}})")
                    .has_value());
        QVERIFY((other / "extension.js").writeFileContents("module.exports={activate(){}};")
                    .has_value());

        const FilePath previousDir = settings().extensionsDir();
        settings().extensionsDir.setValue(FilePath::fromString(dir.path()));
        const QScopeGuard restore([previousDir] {
            settings().extensionsDir.setValue(previousDir);
            Core::setPreselectedOptionsPageItem(Constants::SETTINGS_ID, Id());
        });

        // What the command that opens preferences leaves behind for the page.
        Core::setPreselectedOptionsPageItem(Constants::SETTINGS_ID,
                                            Id::fromString(QString("owns.theSetting")));

        const std::unique_ptr<Core::IOptionsPage> page = setupAlienSettings();
        QVERIFY(page);

        // The dialog the page opens is modal, so it is answered from a timer
        // that runs inside its own event loop - the same place a user would
        // click.
        QString filterText;
        QString focusedEditor;
        bool sawDialog = false;
        QTimer answer;
        answer.setInterval(20);
        connect(&answer, &QTimer::timeout, &answer, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "extensionSettingsDialog")
                return;
            if (QWidget *focus = dialog->focusWidget())
                focusedEditor = focus->objectName();
            sawDialog = true;
            dialog->reject();
        });
        answer.start();

        const std::unique_ptr<QWidget> widget(page->createWidget());
        QVERIFY(widget);
        const auto filter = widget->findChild<QLineEdit *>("extensionsFilterEdit");
        QVERIFY(filter);
        filterText = filter->text();

        QTRY_VERIFY_WITH_TIMEOUT(sawDialog, 15000);
        answer.stop();

        // The list was narrowed to whoever declares the setting, and that
        // extension's settings opened on it.
        QCOMPARE(filterText, QString("alien.owns"));
        QCOMPARE(focusedEditor, QString("extensionSetting.owns.theSetting"));
        // Used up, so the next time the page is built it does not open again.
        QVERIFY(!Core::preselectedOptionsPageItem(Constants::SETTINGS_ID).isValid());

        // Asked again while the page stands, which builds nothing new.
        filter->clear();
        sawDialog = false;
        focusedEditor.clear();
        Core::setPreselectedOptionsPageItem(Constants::SETTINGS_ID,
                                            Id::fromString(QString("owns.theSetting")));
        answer.start();
        settings().enabledExtensions.openPreselected();
        QTRY_VERIFY_WITH_TIMEOUT(sawDialog, 15000);
        answer.stop();
        QCOMPARE(filter->text(), QString("alien.owns"));
        QCOMPARE(focusedEditor, QString("extensionSetting.owns.theSetting"));
    }

    void testOpenSettingsForOneSetting()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path()) / "owner";
        QVERIFY(root.ensureWritableDir());
        const QByteArray packageJson = R"({
            "name": "owns", "publisher": "alien", "version": "0.0.1",
            "engines": {"vscode": "^1.0.0"},
            "contributes": {"configuration": {"properties": {
                "owns.theSetting": {"type": "string", "default": "x"},
                "owns.another": {"type": "boolean", "default": true}
            }}}
        })";
        QVERIFY((root / "package.json").writeFileContents(packageJson).has_value());
        QVERIFY((root / "extension.js").writeFileContents("module.exports={activate(){}};")
                    .has_value());

        const FilePath previous = settings().extensionsDir();
        settings().extensionsDir.setValue(FilePath::fromString(dir.path()));
        const QScopeGuard restore([previous] { settings().extensionsDir.setValue(previous); });

        const QList<VscodeManifest> found = ExtensionRegistry::scan(settings().extensionsDir());
        QCOMPARE(found.size(), 1);

        // The dialog opens on the setting it was told about: that editor has
        // the focus, so what the caller meant is what the user is looking at.
        ExtensionSettingsDialog dialog(found.first());
        dialog.focusSetting("owns.another");
        QVERIFY(dialog.focusWidget());
        QCOMPARE(dialog.focusWidget()->objectName(), QString("extensionSetting.owns.another"));
        dialog.focusSetting("owns.theSetting");
        QCOMPARE(dialog.focusWidget()->objectName(), QString("extensionSetting.owns.theSetting"));
        // One it does not have changes nothing rather than clearing the focus.
        dialog.focusSetting("owns.missing");
        QCOMPARE(dialog.focusWidget()->objectName(), QString("extensionSetting.owns.theSetting"));

        // A setting names the extension that declares it, which is what the
        // preferences page is opened on.
        QCOMPARE(extensionOwning("owns.theSetting"), QString("alien.owns"));
        QCOMPARE(extensionOwning("owns.another"), QString("alien.owns"));
        // Its identifier works as well, since a caller may name either.
        QCOMPARE(extensionOwning("alien.owns"), QString("alien.owns"));
        // And something no extension declares names nobody, so the page opens
        // as it would have without a query.
        QCOMPARE(extensionOwning("nothing.here"), QString());
        QCOMPARE(extensionOwning({}), QString());
    }

    void testBuiltinCommands()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "built.alienbuiltin";
        QVERIFY(source.writeFileContents("messy   text\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "builtin", "alien-builtin-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Builtin');\n"
            "  context.subscriptions.push("
            "vscode.languages.registerDocumentFormattingEditProvider(\n"
            "      {language: 'alienbuiltin'}, {\n"
            "    provideDocumentFormattingEdits(document) {\n"
            "      const line = document.lineAt(0);\n"
            "      return [vscode.TextEdit.replace(line.range,\n"
            "                                      line.text.replace(/\\s+/g, ' '))];\n"
            "    }\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand("
            "'alien.builtin.format', async () => {\n"
            "    await vscode.commands.executeCommand('editor.action.formatDocument');\n"
            "    channel.appendLine('formatted');\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienbuiltin"}, {"extensions", QJsonArray{".alienbuiltin"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.builtin.format"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(
            host.editor().formattingLanguageIds().contains("alienbuiltin"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // A command of the editor's, asked for by name from an extension, and
        // answered by the editor - here by the extension's own formatter.
        host.executeCommand("alien.builtin.format");
        QTRY_COMPARE_WITH_TIMEOUT(document->plainText(), QString("messy text\n"), 15000);
        QCOMPARE(output.size(), 1);
        QCOMPARE(output.first().at(1).toString(), QString("formatted"));

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testCodeLenses()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "lensed.alienlens";
        QVERIFY(source.writeFileContents("first\nsecond\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "lens", "alien-lens-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Lens');\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.lens.run',\n"
            "      arg => channel.appendLine('ran=' + arg)));\n"
            "  context.subscriptions.push(vscode.languages.registerCodeLensProvider(\n"
            "      {language: 'alienlens'}, {\n"
            "    provideCodeLenses(document) {\n"
            "      const range = new vscode.Range(new vscode.Position(1, 0),\n"
            "                                     new vscode.Position(1, 6));\n"
            "      // No command: it has to be resolved before being shown.\n"
            "      return [new vscode.CodeLens(range)];\n"
            "    },\n"
            "    resolveCodeLens(lens) {\n"
            "      lens.command = {title: '2 uses', command: 'alien.lens.run',\n"
            "                      tooltip: 'Show them', arguments: ['here']};\n"
            "      return lens;\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienlens"}, {"extensions", QJsonArray{".alienlens"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().codeLensLanguageIds().contains("alienlens"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // The lens is shown where the extension put it, with the title its
        // command carries - which only exists because it was resolved.
        const auto lenses = [document] {
            QList<TextEditor::TextMark *> found;
            for (TextEditor::TextMark *mark : document->marks()) {
                if (mark->category().id == Constants::TEXT_MARK_CATEGORY)
                    found.append(mark);
            }
            return found;
        };
        QTRY_VERIFY_WITH_TIMEOUT(lenses().size() == 1, 15000);
        TextEditor::TextMark *lens = lenses().first();
        QCOMPARE(lens->lineNumber(), 2); // the range's line, one-based here
        QCOMPARE(lens->lineAnnotation(), QString("2 uses"));

        // Clicking it runs the command the extension put behind it, with the
        // arguments it gave.
        lens->clicked();
        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 1, 15000);
        QCOMPARE(output.first().at(1).toString(), QString("ran=here"));

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testTaskEndIsReported()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");
        const FilePath shell = FilePath("false").searchInPath();
        if (!shell.isExecutableFile())
            QSKIP("no \"false\" to run");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "taskend", "alien-taskend-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien TaskEnd');\n"
            "  context.subscriptions.push(vscode.tasks.onDidEndTaskProcess(\n"
            "      event => channel.appendLine('code=' + event.exitCode)));\n"
            "  context.subscriptions.push(vscode.tasks.onDidEndTask(\n"
            "      event => channel.appendLine('ended=' + event.execution.task.name)));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.taskend.go',\n"
            "      () => {\n"
            "    const task = new vscode.Task({type: 'shell'}, vscode.TaskScope.Workspace,\n"
            "        'Fails', 'Aliens', new vscode.ShellExecution('false'));\n"
            "    vscode.tasks.executeTask(task);\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.taskend.go"), 15000);
        host.executeCommand("alien.taskend.go");

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };
        // The task is over, and what it made of the job came with the news:
        // "false" fails, and an extension checking the code has to see that.
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("ended=Fails"), 30000);
        QVERIFY(reported.contains("code=1"));
    }

    void testTaskRunsWithTheEnvironmentItAsksFor()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath seen = root / "seen.txt";
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "taskenv", "alien-taskenv-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien TaskEnv');\n"
            "  context.subscriptions.push(vscode.tasks.onDidEndTask(\n"
            "      event => channel.appendLine('ended=' + event.execution.task.name)));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.taskenv.go',\n"
            "      () => {\n"
            "    const task = new vscode.Task({type: 'shell'}, vscode.TaskScope.Workspace,\n"
            "        'Env', 'Aliens', new vscode.ShellExecution(\n"
            "            'printf %s \"$ALIEN_TASK_VAR\" > "
            + seen.path()
            + "',\n"
              "            {env: {ALIEN_TASK_VAR: 'from-the-extension'}}));\n"
              "    vscode.tasks.executeTask(task);\n"
              "  }));\n"
              "}\n"
              "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.taskenv.go"), 15000);
        host.executeCommand("alien.taskenv.go");

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };
        // The task has run, so what it wrote is there to read.
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("ended=Env"), 30000);
        const Result<QByteArray> contents = seen.fileContents();
        QVERIFY2(contents.has_value(), qPrintable(contents ? QString() : contents.error()));
        QCOMPARE(QString::fromUtf8(*contents), QString("from-the-extension"));
    }

    void testFileSystemWatcher()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath watched = root / "watched";
        QVERIFY(watched.ensureWritableDir());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "watch", "alien-watch-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  const channel = vscode.window.createOutputChannel('Alien Watch');\n"
                    "  const watcher = vscode.workspace.createFileSystemWatcher(\n"
                    "      new vscode.RelativePattern('%1', '*.marker'));\n"
                    "  context.subscriptions.push(watcher);\n"
                    "  watcher.onDidCreate(uri => channel.appendLine('created=' + uri.fsPath));\n"
                    "  watcher.onDidDelete(uri => channel.appendLine('deleted=' + uri.fsPath));\n"
                    "  const braces = vscode.workspace.createFileSystemWatcher(\n"
                    "      new vscode.RelativePattern('%2', '**/*.{mark,marked}'));\n"
                    "  context.subscriptions.push(braces);\n"
                    "  braces.onDidCreate(uri => channel.appendLine('braces=' + uri.fsPath));\n"
                    "}\n"
                    "module.exports = { activate };\n")
                .arg(watched.toFSPathString(), root.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy activated(&host, &ExtensionHost::activated);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(activated.size() == 1, 15000);
        // The watch is in place before anything happens in the directory.
        QTRY_VERIFY_WITH_TIMEOUT(host.watchesDirectory(watched), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(host.watchesDirectory(root), 15000);

        // Neither a name that only starts like the pattern, nor one that is
        // not in it, is reported.
        QVERIFY((watched / "here.marker.tmp").writeFileContents("x").has_value());
        const FilePath braced = watched / "deeper.marked";
        QVERIFY(braced.writeFileContents("x").has_value());

        const FilePath marker = watched / "here.marker";
        QVERIFY(marker.writeFileContents("x").has_value());

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("created=" + marker.toFSPathString()), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("braces=" + braced.toFSPathString()), 15000);

        // A file the pattern does not name is not reported.
        QVERIFY((watched / "other.txt").writeFileContents("x").has_value());
        QVERIFY(marker.removeFile());
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("deleted=" + marker.toFSPathString()), 15000);
        QVERIFY(!reported.filter("other.txt").size());
        QVERIFY(!reported.filter("here.marker.tmp").size());
    }

    void testFileWatchReportsOnlyWhatChanged()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath watched = root / "watched";
        QVERIFY(watched.ensureWritableDir());
        // Both are there before anything is watched, so neither is news.
        const FilePath kits = watched / "kits.json";
        QVERIFY(kits.writeFileContents("{\"kits\": []}").has_value());
        QVERIFY((watched / "other.json").writeFileContents("{}").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "settle", "alien-settle-test",
            QString("const vscode = require('vscode');\n"
                    "const fs = require('fs');\n"
                    "const path = require('path');\n"
                    "function activate(context) {\n"
                    "  const channel = vscode.window.createOutputChannel('Alien Settle');\n"
                    "  const watcher = vscode.workspace.createFileSystemWatcher(\n"
                    "      new vscode.RelativePattern('%1', '*.json'));\n"
                    "  context.subscriptions.push(watcher);\n"
                    "  const say = (kind, uri) => {\n"
                    "    let whole = false;\n"
                    "    try {\n"
                    "      JSON.parse(fs.readFileSync(uri.fsPath, 'utf8'));\n"
                    "      whole = true;\n"
                    "    } catch (e) {}\n"
                    "    channel.appendLine(\n"
                    "        kind + '=' + path.basename(uri.fsPath) + ':' + whole);\n"
                    "  };\n"
                    "  watcher.onDidCreate(uri => say('created', uri));\n"
                    "  watcher.onDidChange(uri => say('changed', uri));\n"
                    "  watcher.onDidDelete(uri => say('deleted', uri));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(watched.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.watchesDirectory(watched), 15000);

        // Written the way a program writes a file it is composing, in pieces,
        // and not the whole of it at once.
        QFile file(kits.toFSPathString());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArrayList pieces = {"{\"kits\": [", "{\"name\": \"a\"}", "]}"};
        for (const QByteArray &piece : pieces) {
            QCOMPARE(file.write(piece), piece.size());
            QVERIFY(file.flush());
        }
        file.close();

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };
        // A file created after all that, to say that whatever the rewrite was
        // going to be reported as has been reported by now.
        QVERIFY((watched / "last.json").writeFileContents("{}").has_value());
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("created=last.json:true"), 15000);

        QCOMPARE(reported.filter("kits.json"), QStringList{"changed=kits.json:true"});
        QVERIFY2(reported.filter("other.json").isEmpty(),
                 "a file nobody touched was reported as if it had changed");

        // Rewritten in place with nothing else happening in the directory,
        // which on Linux the directory itself does not report.
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write("{\"kits\": []}") > 0);
        file.close();
        QTRY_COMPARE_WITH_TIMEOUT(lines().filter("kits.json").size(), 2, 15000);
    }

    void testProvidedTasksAreOffered()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "tasks", "alien-tasks-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Tasks');\n"
            "  context.subscriptions.push(vscode.tasks.registerTaskProvider('aliens', {\n"
            "    provideTasks() {\n"
            "      const task = new vscode.Task({type: 'aliens'},\n"
            "          vscode.TaskScope.Workspace, 'Build it', 'Aliens',\n"
            "          new vscode.ShellExecution('true'));\n"
            "      task.detail = 'runs true';\n"
            "      return [task];\n"
            "    },\n"
            "    resolveTask(task) { return task; }\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.workspace.registerTaskProvider('oldies', {\n"
            "    provideTasks() {\n"
            "      return [new vscode.Task({type: 'oldies'},\n"
            "          vscode.TaskScope.Workspace, 'Old way', 'Oldies',\n"
            "          new vscode.ShellExecution('true'))];\n"
            "    },\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.tasks.onDidStartTask(\n"
            "      event => channel.appendLine('started=' + event.execution.task.name)));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy activated(&host, &ExtensionHost::activated);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy started(&host, &ExtensionHost::taskStarted);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(activated.size() == 1, 15000);

        // What the extension offers, as Qt Creator would list it.
        QJsonArray tasks;
        host.run().requestTasks([&tasks](const QJsonArray &found) { tasks = found; });
        // Both places an extension can register a provider in are listed, the
        // deprecated one included.
        QTRY_VERIFY_WITH_TIMEOUT(tasks.size() >= 2, 15000);
        QCOMPARE(tasks.size(), 2);
        auto named = [&tasks](const QString &name) {
            for (const QJsonValue &value : tasks)
                if (value.toObject().value("name").toString() == name)
                    return value.toObject();
            return QJsonObject();
        };
        QCOMPARE(named("Old way").value("source").toString(), QString("Oldies"));
        const QJsonObject task = named("Build it");
        QCOMPARE(task.value("name").toString(), QString("Build it"));
        QCOMPARE(task.value("source").toString(), QString("Aliens"));
        QCOMPARE(task.value("detail").toString(), QString("runs true"));
        QVERIFY(!task.value("id").toString().isEmpty());

        // And running the one that was listed reaches the extension, which
        // sees its own task start.
        host.run().runTask(task.value("id").toString());
        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 1, 15000);
        QCOMPARE(output.first().at(1).toString(), QString("started=Build it"));
    }

    void testRangeFormatting()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "part.alienrange";
        QVERIFY(source.writeFileContents("keep   me\nfix   me\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "range", "alien-range-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push("
            "vscode.languages.registerDocumentRangeFormattingEditProvider(\n"
            "      {language: 'alienrange'}, {\n"
            "    provideDocumentRangeFormattingEdits(document, range) {\n"
            "      const edits = [];\n"
            "      for (let line = range.start.line; line <= range.end.line; ++line) {\n"
            "        const text = document.lineAt(line);\n"
            "        edits.push(vscode.TextEdit.replace(text.range,\n"
            "                                           text.text.replace(/\\s+/g, ' ')));\n"
            "      }\n"
            "      return edits;\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienrange"}, {"extensions", QJsonArray{".alienrange"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(
            host.editor().rangeFormattingLanguageIds().contains("alienrange"), 15000);
        QVERIFY(host.editor().formatsRangesFor(source));

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // Only the second line is asked about, and only it is changed: the
        // point of formatting a selection is that the rest is left alone.
        QJsonArray edits;
        const auto ask = [&host, &edits, &source] {
            host.editor().requestRangeFormatting(source, 4, true, {1, 0}, {1, 8},
                                        [&edits](const QJsonArray &r) { edits = r; });
            return !edits.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        QCOMPARE(edits.size(), 1);
        QCOMPARE(edits.first().toObject().value("newText").toString(), QString("fix me"));
        QCOMPARE(edits.first().toObject().value("range").toObject().value("start").toObject()
                     .value("line").toInt(), 1);

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testTypeDefinition()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath source = root / "use.alientype";
        const FilePath declared = root / "types.alientype";
        QVERIFY(source.writeFileContents("value\n").has_value());
        QVERIFY(declared.writeFileContents("type Value\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "typedef", "alien-typedef-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  context.subscriptions.push("
                    "vscode.languages.registerTypeDefinitionProvider(\n"
                    "      {language: 'alientype'}, {\n"
                    "    provideTypeDefinition(document, position) {\n"
                    "      return new vscode.Location(vscode.Uri.file('%1'),\n"
                    "          new vscode.Position(0, 5));\n"
                    "    }\n"
                    "  }));\n"
                    "  context.subscriptions.push(vscode.languages.registerDefinitionProvider(\n"
                    "      {language: 'alientype'}, {\n"
                    "    provideDefinition(document, position) {\n"
                    "      return new vscode.Location(document.uri, new vscode.Position(0, 0));\n"
                    "    }\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(declared.toFSPathString()),
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alientype"}, {"extensions", QJsonArray{".alientype"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(
            host.editor().typeDefinitionLanguageIds().contains("alientype"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonArray locations;
        const auto ask = [&host, &locations, &source] {
            host.editor().requestTypeDefinition(source, 0, 1,
                                       [&locations](const QJsonArray &r) { locations = r; });
            return !locations.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);

        // Where the type is declared - a different file from where the thing
        // is, which is what makes this a question of its own.
        QCOMPARE(locations.size(), 1);
        const QJsonObject location = locations.first().toObject();
        QCOMPARE(FilePath::fromUserInput(location.value("uri").toString()), declared);
        QCOMPARE(location.value("range").toObject().value("start").toObject()
                     .value("character").toInt(), 5);

        // And the definition provider still answers about the thing itself.
        QJsonArray definition;
        const auto askDefinition = [&host, &definition, &source] {
            host.editor().requestDefinition(source, 0, 1,
                                   [&definition](const QJsonArray &r) { definition = r; });
            return !definition.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(askDefinition(), 15000);
        QCOMPARE(FilePath::fromUserInput(
                     definition.first().toObject().value("uri").toString()), source);

        Core::EditorManager::closeEditors({editor}, false);
    }

    // An extension that says only where a symbol is declared. The editor has a
    // single Follow Symbol, so that is where it lands.
    void testDeclarationFollowsSymbol()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath source = root / "use.aliendecl";
        const FilePath declared = root / "decls.aliendecl";
        QVERIFY(source.writeFileContents("value\n").has_value());
        QVERIFY(declared.writeFileContents("header\nextern value\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "decl", "alien-decl-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  context.subscriptions.push("
                    "vscode.languages.registerDeclarationProvider(\n"
                    "      {language: 'aliendecl'}, {\n"
                    "    provideDeclaration(document, position) {\n"
                    "      return new vscode.Location(vscode.Uri.file('%1'),\n"
                    "          new vscode.Position(1, 0));\n"
                    "    }\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(declared.toFSPathString()),
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "aliendecl"}, {"extensions", QJsonArray{".aliendecl"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().declarationLanguageIds().contains("aliendecl"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        TextEditor::TextEditorWidget *widget = TextEditor::TextEditorWidget::fromEditor(editor);
        QVERIFY(widget);
        QVERIFY(widget->optionalActions()
                & TextEditor::OptionalActions::FollowSymbolUnderCursor);

        QJsonArray locations;
        const auto ask = [&host, &locations, &source] {
            host.editor().requestDeclaration(source, 0, 1,
                                    [&locations](const QJsonArray &r) { locations = r; });
            return !locations.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        QCOMPARE(locations.size(), 1);
        QCOMPARE(FilePath::fromUserInput(locations.first().toObject().value("uri").toString()),
                 declared);

        // No definition provider registered at all, so an editor that follows
        // the symbol at all is one that fell back to the declaration.
        widget->gotoLine(1, 1);
        widget->openLinkUnderCursor();
        QTRY_VERIFY_WITH_TIMEOUT(Core::EditorManager::currentDocument()
                                     && Core::EditorManager::currentDocument()->filePath()
                                            == declared, 15000);
        QCOMPARE(Core::EditorManager::currentEditor()->currentLine(), 2);

        Core::EditorManager::closeAllEditors(false);
    }

    // With no extension to ask, the answer comes at once, from within the
    // request. The locator still finishes, with what it got.
    void testLocatorAnsweredAtOnce()
    {
        AlienSymbolLocatorFilter symbols(
            [](const QString &, const std::function<void(const QList<AlienSymbol> &)> &report) {
                report({AlienSymbol{"foo", {}, FilePath::fromString("/a.txt"), 0, 0}});
            });
        LocatorMatcher symbolMatcher;
        symbolMatcher.setTasks(symbols.matchers());
        symbolMatcher.setInputData("foo");
        QSignalSpy symbolsDone(&symbolMatcher, &LocatorMatcher::done);
        symbolMatcher.start();
        QTRY_VERIFY(!symbolsDone.isEmpty());
        QCOMPARE(symbolMatcher.outputData().size(), 1);

        AlienTaskLocatorFilter tasks(
            [](const std::function<void(const QList<AlienTask> &)> &report) {
                report({AlienTask{"id", "build", "ext", {}}});
            },
            [](const QString &) {});
        LocatorMatcher taskMatcher;
        taskMatcher.setTasks(tasks.matchers());
        taskMatcher.setInputData("build");
        QSignalSpy tasksDone(&taskMatcher, &LocatorMatcher::done);
        taskMatcher.start();
        QTRY_VERIFY(!tasksDone.isEmpty());
        QCOMPARE(taskMatcher.outputData().size(), 1);
    }

    void testWorkspaceSymbols()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "things.alienws";
        QVERIFY(source.writeFileContents("thing one\nthing two\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "ws", "alien-ws-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  context.subscriptions.push("
                    "vscode.languages.registerWorkspaceSymbolProvider({\n"
                    "    provideWorkspaceSymbols(query) {\n"
                    "      const uri = vscode.Uri.file('%1');\n"
                    "      const all = [['alpha', 'Things', 0], ['beta', '', 1]];\n"
                    "      return all.filter(s => s[0].startsWith(query || ''))\n"
                    "                .map(s => new vscode.SymbolInformation(s[0], 12, s[1],\n"
                    "                    new vscode.Location(uri, new vscode.Position(s[2], 6))));\n"
                    "    }\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(source.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy activated(&host, &ExtensionHost::activated);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(activated.size() == 1, 15000);
        QVERIFY(host.editor().hasWorkspaceSymbols());

        // Everything, and then only what the query matches: the extension does
        // the matching, because only it knows what its names look like.
        QJsonArray all;
        host.editor().requestWorkspaceSymbols({}, [&all](const QJsonArray &r) { all = r; });
        QTRY_VERIFY_WITH_TIMEOUT(all.size() == 2, 15000);
        QCOMPARE(all.first().toObject().value("name").toString(), QString("alpha"));
        QCOMPARE(all.first().toObject().value("container").toString(), QString("Things"));
        QCOMPARE(all.first().toObject().value("path").toString(), source.toFSPathString());
        QCOMPARE(all.first().toObject().value("range").toObject().value("start").toObject()
                     .value("character").toInt(), 6);

        QJsonArray filtered;
        host.editor().requestWorkspaceSymbols("bet", [&filtered](const QJsonArray &r) {
            filtered = r;
        });
        QTRY_VERIFY_WITH_TIMEOUT(filtered.size() == 1, 15000);
        QCOMPARE(filtered.first().toObject().value("name").toString(), QString("beta"));
        QCOMPARE(filtered.first().toObject().value("range").toObject().value("start").toObject()
                     .value("line").toInt(), 1);
    }

    void testSignatureHelp()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "calls.aliensig";
        QVERIFY(source.writeFileContents("draw(1, 2)\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "sig", "alien-sig-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerSignatureHelpProvider(\n"
            "      {language: 'aliensig'}, {\n"
            "    provideSignatureHelp(document, position) {\n"
            "      const help = new vscode.SignatureHelp();\n"
            "      const first = new vscode.SignatureInformation('draw(x, y)', 'Draws it.');\n"
            "      first.parameters = [new vscode.ParameterInformation('x'),\n"
            "                          new vscode.ParameterInformation('y')];\n"
            "      const second = new vscode.SignatureInformation('draw(point)');\n"
            "      second.parameters = [{label: [5, 10]}];\n"
            "      help.signatures = [first, second];\n"
            "      help.activeSignature = 1;\n"
            "      help.activeParameter = 1;\n"
            "      return help;\n"
            "    }\n"
            "  }, '(', ','));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "aliensig"}, {"extensions", QJsonArray{".aliensig"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().signatureLanguageIds().contains("aliensig"), 15000);
        // The characters that open the hint came with the registration.
        QVERIFY(host.editor().signatureTriggers().contains("("));
        QVERIFY(host.editor().signatureTriggers().contains(","));

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonObject help;
        const auto ask = [&host, &help, &source] {
            host.editor().requestSignatureHelp(source, 0, 6,
                                      [&help](const QJsonObject &result) { help = result; });
            return !help.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);

        const QJsonArray signatures = help.value("signatures").toArray();
        QCOMPARE(signatures.size(), 2);
        QCOMPARE(signatures.first().toObject().value("label").toString(), QString("draw(x, y)"));
        QCOMPARE(signatures.first().toObject().value("documentation").toString(),
                 QString("Draws it."));
        QCOMPARE(signatures.first().toObject().value("parameters").toArray().size(), 2);
        // A parameter may name itself by where it sits in the signature, which
        // is what a language server sends.
        const QJsonArray second = signatures.at(1).toObject().value("parameters").toArray();
        QCOMPARE(second.size(), 1);
        QCOMPARE(second.first().toObject().value("label").toString(), QString("point"));

        // Which one applies, and which argument the cursor is in.
        QCOMPARE(help.value("activeSignature").toInt(), 1);
        QCOMPARE(help.value("activeParameter").toInt(), 1);

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testDocumentLinks()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath source = root / "links.alienlink";
        const FilePath target = root / "target.txt";
        QVERIFY(source.writeFileContents("see target.txt\n").has_value());
        QVERIFY(target.writeFileContents("here\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "lnk", "alien-link-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  context.subscriptions.push("
                    "vscode.languages.registerDocumentLinkProvider(\n"
                    "      {language: 'alienlink'}, {\n"
                    "    provideDocumentLinks(document) {\n"
                    "      const at = document.lineAt(0).text.indexOf('target.txt');\n"
                    "      const range = new vscode.Range(new vscode.Position(0, at),\n"
                    "          new vscode.Position(0, at + 'target.txt'.length));\n"
                    "      return [new vscode.DocumentLink(range,\n"
                    "          vscode.Uri.file('%1'))];\n"
                    "    }\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(target.toFSPathString()),
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienlink"}, {"extensions", QJsonArray{".alienlink"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().linkLanguageIds().contains("alienlink"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        TextEditor::TextEditorWidget *widget = TextEditor::TextEditorWidget::fromEditor(editor);
        QVERIFY(widget);

        QJsonArray links;
        const auto ask = [&host, &links, &source] {
            host.editor().requestDocumentLinks(source,
                                      [&links](const QJsonArray &result) { links = result; });
            return !links.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        QCOMPARE(links.size(), 1);

        // Where the cursor is on the link, the editor is given something to
        // underline and to open.
        QTextCursor cursor(widget->document());
        cursor.setPosition(6); // inside "target.txt"
        Utils::Link found;
        bool answered = false;
        host.editor().followDocumentLink(widget, cursor,
                                [&found, &answered](const Utils::Link &link) {
                                    found = link;
                                    answered = true;
                                },
                                false);
        QTRY_VERIFY_WITH_TIMEOUT(answered, 15000);
        QCOMPARE(found.targetFilePath, target);
        QCOMPARE(found.linkTextStart, 4);
        QCOMPARE(found.linkTextEnd, 14);

        // And where it is not, nothing is offered.
        cursor.setPosition(0);
        answered = false;
        host.editor().followDocumentLink(widget, cursor,
                                [&found, &answered](const Utils::Link &link) {
                                    found = link;
                                    answered = true;
                                },
                                false);
        QTRY_VERIFY_WITH_TIMEOUT(answered, 15000);
        QVERIFY(!found.hasValidTarget());

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testDocumentHighlights()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "same.alienhl";
        QVERIFY(source.writeFileContents("word other\nword\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "hl", "alien-hl-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerDocumentHighlightProvider(\n"
            "      {language: 'alienhl'}, {\n"
            "    provideDocumentHighlights(document, position) {\n"
            "      const wordRange = document.getWordRangeAtPosition(position);\n"
            "      if (!wordRange)\n"
            "        return [];\n"
            "      const word = document.getText(wordRange);\n"
            "      const found = [];\n"
            "      for (let line = 0; line < document.lineCount; ++line) {\n"
            "        const at = document.lineAt(line).text.indexOf(word);\n"
            "        if (at >= 0)\n"
            "          found.push(new vscode.DocumentHighlight(new vscode.Range(\n"
            "              new vscode.Position(line, at),\n"
            "              new vscode.Position(line, at + word.length))));\n"
            "      }\n"
            "      return found;\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienhl"}, {"extensions", QJsonArray{".alienhl"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().highlightLanguageIds().contains("alienhl"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        TextEditor::TextEditorWidget *widget = TextEditor::TextEditorWidget::fromEditor(editor);
        QVERIFY(widget);

        QJsonArray ranges;
        const auto ask = [&host, &ranges, &source] {
            host.editor().requestHighlights(source, 0, 1,
                                   [&ranges](const QJsonArray &result) { ranges = result; });
            return !ranges.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        QCOMPARE(ranges.size(), 2);

        // And they are marked in the editor, the way its own occurrences are.
        host.editor().highlightOccurrences(widget);
        const auto marked = [widget] {
            return widget->extraSelections(
                TextEditor::TextEditorWidget::CodeSemanticsSelection);
        };
        QTRY_COMPARE_WITH_TIMEOUT(marked().size(), 2, 15000);
        QCOMPARE(marked().first().cursor.selectedText(), QString("word"));
        QCOMPARE(marked().last().cursor.selectedText(), QString("word"));

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testDocumentSymbols()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "outline.aliensym";
        QVERIFY(source.writeFileContents("parent\n  child\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "sym", "alien-sym-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider(\n"
            "      {language: 'aliensym'}, {\n"
            "    provideDocumentSymbols(document) {\n"
            "      const whole = new vscode.Range(new vscode.Position(0, 0),\n"
            "                                     new vscode.Position(1, 7));\n"
            "      const head = new vscode.Range(new vscode.Position(0, 0),\n"
            "                                    new vscode.Position(0, 6));\n"
            "      const parent = new vscode.DocumentSymbol('parent', 'a thing', 11,\n"
            "                                               whole, head);\n"
            "      const childRange = new vscode.Range(new vscode.Position(1, 2),\n"
            "                                          new vscode.Position(1, 7));\n"
            "      parent.children = [new vscode.DocumentSymbol('child', '', 12,\n"
            "                                                   childRange, childRange)];\n"
            "      return [parent];\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "aliensym"}, {"extensions", QJsonArray{".aliensym"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().symbolLanguageIds().contains("aliensym"), 15000);
        // The outline offers itself for this file, and for no other.
        QVERIFY(host.editor().providesSymbolsFor(source));
        QVERIFY(!host.editor().providesSymbolsFor(FilePath::fromString(dir.path()) / "other.txt"));

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonArray symbols;
        const auto ask = [&host, &symbols, &source] {
            host.editor().requestDocumentSymbols(source,
                                        [&symbols](const QJsonArray &r) { symbols = r; });
            return !symbols.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);

        // The tree as the extension built it, each symbol knowing where it is.
        QCOMPARE(symbols.size(), 1);
        const QJsonObject parent = symbols.first().toObject();
        QCOMPARE(parent.value("name").toString(), QString("parent"));
        QCOMPARE(parent.value("detail").toString(), QString("a thing"));
        const QJsonArray children = parent.value("children").toArray();
        QCOMPARE(children.size(), 1);
        QCOMPARE(children.first().toObject().value("name").toString(), QString("child"));
        QCOMPARE(children.first().toObject().value("selectionRange").toObject()
                     .value("start").toObject().value("line").toInt(), 1);

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testFindUsages()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "uses.alienref";
        QVERIFY(source.writeFileContents("thing here\nand thing\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "refs", "alien-refs-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerReferenceProvider(\n"
            "      {language: 'alienref'}, {\n"
            "    provideReferences(document, position) {\n"
            "      const found = [];\n"
            "      for (let line = 0; line < document.lineCount; ++line) {\n"
            "        const text = document.lineAt(line).text;\n"
            "        const at = text.indexOf('thing');\n"
            "        if (at >= 0)\n"
            "          found.push(new vscode.Location(document.uri,\n"
            "              new vscode.Range(new vscode.Position(line, at),\n"
            "                               new vscode.Position(line, at + 5))));\n"
            "      }\n"
            "      return found;\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienref"}, {"extensions", QJsonArray{".alienref"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().referenceLanguageIds().contains("alienref"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonArray found;
        const auto ask = [&host, &found, &source] {
            host.editor().requestReferences(source, 0, 0,
                                   [&found](const QJsonArray &result) { found = result; });
            return !found.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);

        // Both places, each carrying where in the file it is.
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.first().toObject().value("range").toObject()
                     .value("start").toObject().value("character").toInt(), 0);
        QCOMPARE(found.last().toObject().value("range").toObject()
                     .value("start").toObject().value("line").toInt(), 1);
        QCOMPARE(found.last().toObject().value("range").toObject()
                     .value("start").toObject().value("character").toInt(), 4);

        // And they reach the pane every other search goes to.
        Core::SearchResult *search = nullptr;
        host.editor().findUsages(source, 0, 0, "thing",
                        [&search](Core::SearchResult *result) { search = result; });
        QTRY_VERIFY_WITH_TIMEOUT(search, 15000);
        QCOMPARE(search->count(), 2);

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testRenameSymbol()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "names.alienname";
        QVERIFY(source.writeFileContents("old\nold\n").has_value());
        // A use in a file nobody has open.
        const FilePath other = FilePath::fromString(dir.path()) / "other.alienname";
        QVERIFY(other.writeFileContents("old\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "ren", "alien-rename-test",
            QString("const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerRenameProvider(\n"
            "      {language: 'alienname'}, {\n"
            "    provideRenameEdits(document, position, newName) {\n"
            "      const edit = new vscode.WorkspaceEdit();\n"
            "      for (let line = 0; line < document.lineCount; ++line) {\n"
            "        const text = document.lineAt(line);\n"
            "        if (text.text.length)\n"
            "          edit.replace(document.uri, text.range, newName);\n"
            "      }\n"
            "      edit.replace(vscode.Uri.file('%1'), new vscode.Range(0, 0, 0, 3), newName);\n"
            "      return edit;\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n").arg(other.toFSPathString()),
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienname"}, {"extensions", QJsonArray{".alienname"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().renameLanguageIds().contains("alienname"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // Every place the extension knows of, renamed at once. Asking again is
        // how we wait for the document to have reached the host.
        QJsonArray changes;
        const auto ask = [&host, &changes, &source] {
            host.editor().requestRenameEdits(source, 0, 0, "new",
                                    [&changes](const QJsonArray &result) { changes = result; });
            return !changes.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        host.editor().applyChanges(changes);
        QCOMPARE(document->plainText(), QString("new\nnew\n"));
        QCOMPARE(other.fileContents().value_or(QByteArray()), QByteArray("new\n"));

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testCodeActions()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "fixme.alienfix";
        QVERIFY(source.writeFileContents("wrong\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "fix", "alien-fix-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerCodeActionsProvider(\n"
            "      {language: 'alienfix'}, {\n"
            "    provideCodeActions(document, range) {\n"
            "      const action = new vscode.CodeAction('Make it right');\n"
            "      action.edit = new vscode.WorkspaceEdit();\n"
            "      action.edit.replace(document.uri, document.lineAt(0).range, 'right');\n"
            "      return [action];\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienfix"}, {"extensions", QJsonArray{".alienfix"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().codeActionLanguageIds().contains("alienfix"), 15000);

        // The provider is asked about a document the host knows, which is one
        // Qt Creator has open: opening it is what tells the host about it.
        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        // What the editor asks for when the user asks for a quick fix. Asking
        // again is how we wait for the document to have reached the host: the
        // answer to a later request proves the earlier notification arrived.
        QJsonArray offered;
        const auto ask = [&host, &offered, &source] {
            host.editor().requestCodeActions(source, {0, 0}, {0, 5},
                                    [&offered](const QJsonArray &actions) { offered = actions; });
            return !offered.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered.first().toObject().value("title").toString(), QString("Make it right"));

        // Applying it is applying the edits it came with.
        host.editor().applyChanges(offered.first().toObject().value("changes").toArray());
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(document->plainText(), QString("right\n"));
        Core::EditorManager::closeEditors({editor}, false);
    }

    // What an action is, beyond a title and an edit: one that only runs a
    // command, one the extension says does not apply, one whose edit is worked
    // out only when offered, and the one it calls the obvious choice.
    void testCodeActionShapes()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "shapes.alienfix";
        QVERIFY(source.writeFileContents("wrong\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "shapes", "alien-shapes-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerCodeActionsProvider(\n"
            "      {language: 'alienfix'}, {\n"
            "    provideCodeActions(document, range) {\n"
            "      const runIt = new vscode.CodeAction('Run it');\n"
            "      runIt.command = {command: 'alien.shapes.run', arguments: [7]};\n"
            "      const notHere = new vscode.CodeAction('Not here');\n"
            "      notHere.edit = new vscode.WorkspaceEdit();\n"
            "      notHere.edit.replace(document.uri, document.lineAt(0).range, 'no');\n"
            "      notHere.disabled = {reason: 'not applicable'};\n"
            "      const late = new vscode.CodeAction('Late edit');\n"
            "      const preferred = new vscode.CodeAction('Preferred');\n"
            "      preferred.isPreferred = true;\n"
            "      preferred.edit = new vscode.WorkspaceEdit();\n"
            "      preferred.edit.replace(document.uri, document.lineAt(0).range, 'best');\n"
            "      return [runIt, notHere, late, preferred];\n"
            "    },\n"
            "    resolveCodeAction(action) {\n"
            "      action.edit = new vscode.WorkspaceEdit();\n"
            "      action.edit.insert(vscode.Uri.file('/late'), new vscode.Position(0, 0), 'x');\n"
            "      return action;\n"
            "    }\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.shapes.run',\n"
            "      value => vscode.window.showInformationMessage('ran:' + value)));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienfix"}, {"extensions", QJsonArray{".alienfix"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().codeActionLanguageIds().contains("alienfix"), 15000);
        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonArray offered;
        const auto ask = [&host, &offered, &source] {
            host.editor().requestCodeActions(source, {0, 0}, {0, 5},
                                    [&offered](const QJsonArray &actions) { offered = actions; });
            return !offered.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);

        QStringList titles;
        for (const QJsonValue &value : offered)
            titles << value.toObject().value("title").toString();
        // The disabled one is not offered, and the preferred one leads.
        QCOMPARE(titles, QStringList({"Preferred", "Run it", "Late edit"}));

        const QJsonObject runIt = offered.at(1).toObject();
        QCOMPARE(runIt.value("command").toString(), QString("alien.shapes.run"));
        QVERIFY(runIt.value("changes").toArray().isEmpty());

        // The one with no edit of its own got one when it was offered.
        QVERIFY(!offered.at(2).toObject().value("changes").toArray().isEmpty());

        // Taking the command-only action runs the command.
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand(runIt.value("command").toString(),
                            runIt.value("arguments").toArray());
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.first().first().toString(), QString("ran:7"));

        Core::EditorManager::closeEditors({editor}, false);
    }

    // The documentation and the extra edits a server works out only for the
    // item that was actually taken.
    void testCompletionWithoutAHost()
    {
        // Without a host the answer comes before perform() returns. Handed on
        // as an async proposal, the assistant would delete the processor twice.
        ExtensionHost host({});
        AlienCompletionAssistProvider provider(&host);
        const std::unique_ptr<TextEditor::IAssistProcessor> processor(
            provider.createProcessor(nullptr));
        bool async = false;
        processor->setAsyncCompletionAvailableHandler(
            [&async](TextEditor::IAssistProposal *proposal) {
                async = true;
                delete proposal;
            });
        QTextDocument document("a");
        const std::unique_ptr<TextEditor::IAssistProposal> proposal(
            processor->start(std::make_unique<TextEditor::AssistInterface>(
                QTextCursor(&document), FilePath(), TextEditor::ExplicitlyInvoked)));
        QVERIFY(!async);
        QVERIFY(!processor->running());
    }

    void testCompletionResolvesTheItemTaken()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "resolve.txt";
        QVERIFY(file.writeFileContents("head\nuse ").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "resolve", "alien-resolve-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerCompletionItemProvider(\n"
            "      {language: 'plaintext'}, {\n"
            "    provideCompletionItems() {\n"
            "      const item = new vscode.CompletionItem('Symbol');\n"
            "      item.additionalTextEdits = [vscode.TextEdit.insert(\n"
            "          new vscode.Position(1, 0), '# ')];\n"
            "      return [item];\n"
            "    },\n"
            "    resolveCompletionItem(item) {\n"
            "      item.documentation = 'The one symbol.';\n"
            "      item.additionalTextEdits.push(vscode.TextEdit.insert(\n"
            "          new vscode.Position(0, 0), 'import Symbol\\n'));\n"
            "      return item;\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 15000);

        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(
            EditorManager::openEditor(file));
        QVERIFY(textEditor);
        TextEditor::TextEditorWidget *widget = textEditor->editorWidget();
        widget->gotoDocumentEnd();

        QJsonArray items;
        const auto ask = [&] {
            host.editor().requestCompletion(file, 1, 4, [&items](const QJsonArray &r) {
                items = r;
            });
            return !items.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        // The item as offered knows nothing yet.
        QCOMPARE(items.first().toObject().value("documentation").toString(), QString());
        QCOMPARE(items.first().toObject().value("additionalTextEdits").toArray().size(), 1);
        QVERIFY(!items.first().toObject().value("id").toString().isEmpty());

        std::unique_ptr<TextEditor::IAssistProposal> proposal(
            createCompletionProposal(widget->position(), items, &host));
        TextEditor::GenericProposalModelPtr model
            = qSharedPointerCast<TextEditor::GenericProposalModel>(proposal->model());
        model->proposalItem(0)->apply(widget, widget->position());

        // Taking it is what asks, and the import arrives with the answer. The
        // edit the item had from the start comes back with it, and is not
        // made twice.
        QTRY_COMPARE_WITH_TIMEOUT(widget->document()->toPlainText(),
                                  QString("import Symbol\nhead\n# use Symbol"), 15000);
        EditorManager::closeAllEditors(false);
    }

    // A host that went away takes what it set up with it, the editor goes on
    // without it, and the next host is told about what is open.
    // A document asks the next host once per edit, not once per host it has seen.
    void testRestartedHostIsAskedOnce()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "folded.txt";
        QVERIFY(file.writeFileContents("text\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "restarting", "alien-restarting-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  vscode.languages.registerFoldingRangeProvider({language: 'plaintext'}, {\n"
            "    provideFoldingRanges() {\n"
            "      vscode.window.showInformationMessage('fold');\n"
            "      return [];\n"
            "    }\n"
            "  });\n"
            "  vscode.commands.registerCommand('alien.restarting.exit', () => process.exit(0));\n"
            "  vscode.commands.registerCommand('alien.restarting.mark',\n"
            "      () => vscode.window.showInformationMessage('mark'));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(
            EditorManager::openEditor(file));
        QVERIFY(textEditor);

        ExtensionHost host(node);
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        QSignalSpy stopped(&host, &ExtensionHost::stopped);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.restarting.exit"),
                                 15000);
        host.executeCommand("alien.restarting.exit");
        QTRY_VERIFY_WITH_TIMEOUT(!stopped.isEmpty(), 15000);

        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.restarting.mark"),
                                 15000);
        // Commands are answered in order, so what the registration asked for
        // is in once the mark is.
        host.executeCommand("alien.restarting.mark");
        QTRY_VERIFY_WITH_TIMEOUT(messages.contains({QString("mark")}), 15000);
        messages.clear();

        textEditor->editorWidget()->insertPlainText("more ");
        QCoreApplication::processEvents(); // the queued refreshes
        host.executeCommand("alien.restarting.mark");
        QTRY_VERIFY_WITH_TIMEOUT(messages.contains({QString("mark")}), 15000);
        QCOMPARE(messages.count({QString("fold")}), 1);

        EditorManager::closeAllEditors(false);
    }

    void testAHostThatStopped()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "open.txt";
        QVERIFY(file.writeFileContents("text").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "stopping", "alien-stopping-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  for (const doc of vscode.workspace.textDocuments)\n"
            "    vscode.window.showInformationMessage('open:' + doc.uri.fsPath);\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.test.exit', () => process.exit(0)));\n"
            "  vscode.window.withProgress({location: vscode.ProgressLocation.Window,\n"
            "                              title: 'busy'}, () => new Promise(() => {}));\n"
            "  vscode.window.showInformationMessage('ready');\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(
            EditorManager::openEditor(file));
        QVERIFY(textEditor);
        TextEditor::TextEditorWidget *widget = textEditor->editorWidget();

        ExtensionHost host(node);
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        QSignalSpy status(&host, &ExtensionHost::statusBarMessageChanged);
        QSignalSpy stopped(&host, &ExtensionHost::stopped);
        const QString opened = "open:" + file.path();
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(messages.contains({QString("ready")}), 15000);
        QVERIFY(messages.contains({opened}));
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.test.exit"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!status.isEmpty(), 15000);
        QCOMPARE(status.last().at(0).toString(), QString("busy"));

        host.executeCommand("alien.test.exit");
        QTRY_VERIFY_WITH_TIMEOUT(!stopped.isEmpty(), 15000);
        QVERIFY(host.registeredCommands().isEmpty());
        QCOMPARE(status.last().at(0).toString(), QString());

        widget->insertPlainText("more ");
        widget->gotoDocumentEnd();
        QCoreApplication::processEvents(); // the queued didChange

        messages.clear();
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(messages.contains({QString("ready")}), 15000);
        QVERIFY(messages.contains({opened}));

        // Stopped on purpose, it leaves the same behind.
        host.shutdown();
        QCOMPARE(stopped.size(), 2);
        QVERIFY(host.registeredCommands().isEmpty());
        QCOMPARE(status.last().at(0).toString(), QString());
        widget->insertPlainText("again ");
        QCoreApplication::processEvents();

        EditorManager::closeAllEditors(false);
    }

    // A host that never came up still answers who waited for it.
    void testAHostThatNeverStarted()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath node = root / "node";
        QVERIFY(node.writeFileContents("#!/nonexistent/interpreter\n").has_value());
        QVERIFY(node.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "never", "alien-never-test", "module.exports = { activate() {} };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);
        bool answered = false;
        host.editor().requestCompletion(root / "file.txt", 0, 0,
                               [&answered](const QJsonArray &) { answered = true; });
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 15000);
        QCOMPARE(failed.first().first().toString(), manifest->qualifiedId());
        QVERIFY(answered);
    }

    // Which workspace folder a file is in, and what to call it from there. A
    // folder is not a parent just because its path is a prefix.
    void testWorkspaceFolderQuestions()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath app = root / "app";
        const FilePath apple = root / "apple";
        QVERIFY(app.ensureWritableDir());
        QVERIFY(apple.ensureWritableDir());
        QVERIFY((apple / "core.txt").writeFileContents("x").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "folders", "alien-folders-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.folders.ask', target => {\n"
            "        const uri = vscode.Uri.file(target);\n"
            "        const folder = vscode.workspace.getWorkspaceFolder(uri);\n"
            "        vscode.window.showInformationMessage(\n"
            "            (folder ? folder.name : 'none') + '|'\n"
            "            + vscode.workspace.asRelativePath(uri, true) + '|'\n"
            "            + vscode.workspace.asRelativePath(uri, false));\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.setWorkspaceFolders(
            QJsonArray{QJsonObject{{"path", app.toFSPathString()}, {"name", "app"}},
                       QJsonObject{{"path", apple.toFSPathString()}, {"name", "apple"}}});
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.folders.ask"), 15000);

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.folders.ask",
                            QJsonArray{(apple / "core.txt").toFSPathString()});
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        // "app" is not a parent of "apple/core.txt", and with more than one
        // folder the relative path says which one it was.
        QCOMPARE(messages.first().first().toString(),
                 QString("apple|apple/core.txt|core.txt"));
    }

    // The editor's own commands, which extensions call as readily as their
    // own: copying a path, opening the locator, showing Issues, previewing
    // Markdown, and asking what another extension's hover says.
    void testBuiltinEditorCommands()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath text = root / "some.txt";
        const FilePath markdown = root / "readme.md";
        QVERIFY(text.writeFileContents("hover here\n").has_value());
        QVERIFY(markdown.writeFileContents("# Title\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "builtins", "alien-builtins-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerHoverProvider(\n"
            "      {language: 'plaintext'},\n"
            "      {provideHover: () => new vscode.Hover('from the provider')}));\n"
            "  const run = async (command, ...args) => {\n"
            "    if (command.startsWith('vscode.execute'))\n"
            "      args = [vscode.Uri.file(args[0]), new vscode.Position(0, 0)];\n"
            "    const answer = await vscode.commands.executeCommand(command, ...args);\n"
            "    vscode.window.showInformationMessage(\n"
            "        command + '=' + (Array.isArray(answer)\n"
            "            ? answer.map(h => String(h.contents)).join(';')\n"
            "            : String(answer)));\n"
            "  };\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.builtins.run', run));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.builtins.run"), 15000);
        QVERIFY(EditorManager::openEditor(text));

        QString answer;
        const auto answerFor = [&host, &answer](const QString &command, const QJsonArray &args) {
            answer.clear();
            QSignalSpy messages(&host, &ExtensionHost::messageShown);
            host.executeCommand("alien.builtins.run", QJsonArray{command} + args);
            const auto answered = [&] {
                for (const QList<QVariant> &arguments : messages) {
                    const QString message = arguments.at(0).toString();
                    if (message.startsWith(command + '=')) {
                        answer = message.mid(command.size() + 1);
                        return true;
                    }
                }
                return false;
            };
            QTRY_VERIFY_WITH_TIMEOUT(answered(), 15000);
        };

        // Copying the open file's path puts it on the clipboard.
        QGuiApplication::clipboard()->setText("nothing");
        answerFor("copyFilePath", {});
        QCOMPARE(answer, QString("true"));
        QCOMPARE(QGuiApplication::clipboard()->text(), text.toUserOutput());

        // The locator opens on what the extension asked to look for.
        answerFor("workbench.action.quickOpen", QJsonArray{"needle"});
        QCOMPARE(answer, QString("true"));

        // Issues is a pane Qt Creator already has.
        answerFor("workbench.panel.markers.view.focus", {});
        QCOMPARE(answer, QString("true"));

        // Markdown opens in the editor that previews it.
        answerFor("markdown.showPreview", QJsonArray{markdown.toFSPathString()});
        QCOMPARE(answer, QString("true"));
        QVERIFY(EditorManager::currentEditor());
        QCOMPARE(EditorManager::currentEditor()->document()->filePath(), markdown);

        // And a provider can be asked for its answer from the outside.
        answerFor("vscode.executeHoverProvider", QJsonArray{text.toFSPathString()});
        QCOMPARE(answer, QString("from the provider"));

        EditorManager::closeAllEditors(false);
    }

    // Writing nothing over a setting takes it out, so that what the extension
    // declared as its default applies again - it does not leave a null behind.
    void testSettingIsRemovedNotNulled()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "settings", "alien-settings-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.settings.write', async () => {\n"
            "        const config = vscode.workspace.getConfiguration('alienset');\n"
            "        await config.update('mode', 'loud');\n"
            "        vscode.window.showInformationMessage('wrote:' + config.get('mode'));\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.settings.clear', async () => {\n"
            "        const config = vscode.workspace.getConfiguration('alienset');\n"
            "        await config.update('mode', undefined);\n"
            "        vscode.window.showInformationMessage('cleared:' + config.get('mode'));\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"configuration", QJsonObject{{"properties", QJsonObject{
                {"alienset.mode", QJsonObject{{"type", "string"}, {"default", "quiet"}}}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.settings.clear"), 15000);

        const FilePath written = ExtensionHost::writtenConfigurationFile();
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.settings.write");
        QTRY_VERIFY_WITH_TIMEOUT(messages.size() >= 1, 15000);
        QCOMPARE(messages.first().first().toString(), QString("wrote:loud"));
        QTRY_VERIFY_WITH_TIMEOUT(
            QJsonDocument::fromJson(written.fileContents().value_or(QByteArray()))
                .object().value("alienset.mode").toString() == "loud", 15000);

        host.executeCommand("alien.settings.clear");
        QTRY_VERIFY_WITH_TIMEOUT(messages.size() >= 2, 15000);
        // The default is back, and the file no longer mentions the key at all.
        QCOMPARE(messages.at(1).first().toString(), QString("cleared:quiet"));
        QTRY_VERIFY_WITH_TIMEOUT(
            !QJsonDocument::fromJson(written.fileContents().value_or(QByteArray()))
                 .object().contains("alienset.mode"), 15000);
    }

    // Providers answering from the outside, which is how one extension reuses
    // what another already worked out.
    void testExecuteProviderCommands()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath source = root / "run.txt";
        QVERIFY(source.writeFileContents("messy   text\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "run", "alien-run-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const sel = {language: 'plaintext'};\n"
            "  context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider(\n"
            "      sel, {provideDocumentSymbols: () => [new vscode.SymbolInformation(\n"
            "          'alpha', vscode.SymbolKind.Function, 'Things',\n"
            "          new vscode.Location(vscode.Uri.file('/x'),\n"
            "                              new vscode.Range(0, 0, 0, 5)))]}));\n"
            "  context.subscriptions.push(vscode.languages.registerWorkspaceSymbolProvider(\n"
            "      {provideWorkspaceSymbols: () => [new vscode.SymbolInformation(\n"
            "          'beta', vscode.SymbolKind.Class, '',\n"
            "          new vscode.Location(vscode.Uri.file('/y'),\n"
            "                              new vscode.Range(1, 0, 1, 4)))]}));\n"
            "  context.subscriptions.push(\n"
            "      vscode.languages.registerDocumentFormattingEditProvider(sel, {\n"
            "        provideDocumentFormattingEdits: doc => [vscode.TextEdit.replace(\n"
            "            doc.lineAt(0).range, 'tidy text')]}));\n"
            "  context.subscriptions.push(\n"
            "      vscode.languages.registerDocumentRangeFormattingEditProvider(sel, {\n"
            "        provideDocumentRangeFormattingEdits: () => [vscode.TextEdit.replace(\n"
            "            new vscode.Range(0, 0, 0, 5), 'neat')]}));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.run.ask', async (command, path) => {\n"
            "        const answer = await vscode.commands.executeCommand(\n"
            "            command, vscode.Uri.file(path), new vscode.Range(0, 0, 0, 5));\n"
            "        vscode.window.showInformationMessage(\n"
            "            command + '=' + JSON.stringify(answer));\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.run.ask"), 15000);
        QVERIFY(EditorManager::openEditor(source));

        QString answer;
        const auto ask = [&host, &answer, &source](const QString &command) {
            answer.clear();
            QSignalSpy messages(&host, &ExtensionHost::messageShown);
            host.executeCommand("alien.run.ask",
                                QJsonArray{command, source.toFSPathString()});
            const auto answered = [&] {
                for (const QList<QVariant> &arguments : messages) {
                    const QString message = arguments.at(0).toString();
                    if (message.startsWith(command + '=')) {
                        answer = message.mid(command.size() + 1);
                        return true;
                    }
                }
                return false;
            };
            QTRY_VERIFY_WITH_TIMEOUT(answered(), 15000);
        };

        ask("vscode.executeDocumentSymbolProvider");
        QVERIFY2(answer.contains("\"alpha\""), qPrintable(answer));
        ask("vscode.executeWorkspaceSymbolProvider");
        QVERIFY2(answer.contains("\"beta\""), qPrintable(answer));
        ask("vscode.executeFormatDocumentProvider");
        QVERIFY2(answer.contains("tidy text"), qPrintable(answer));
        ask("vscode.executeFormatRangeProvider");
        QVERIFY2(answer.contains("neat"), qPrintable(answer));

        EditorManager::closeAllEditors(false);
    }

    // What Qt Creator is told is every language that still has a provider, not
    // the one the last registration named.
    void testProviderLanguagesAccumulate()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "languages", "alien-languages-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const format = {provideDocumentFormattingEdits: () => []};\n"
            "  const rename = {provideRenameEdits: () => null};\n"
            "  vscode.languages.registerDocumentFormattingEditProvider('alpha', format);\n"
            "  vscode.languages.registerDocumentFormattingEditProvider('beta', format);\n"
            "  vscode.languages.registerRenameProvider('alpha', rename);\n"
            "  vscode.languages.registerRenameProvider('beta', rename).dispose();\n"
            "  vscode.commands.registerCommand('alien.languages.done', () => {});\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        // Registered last, so the languages have arrived by the time it has.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.languages.done"),
                                 15000);
        QCOMPARE(host.editor().formattingLanguageIds(), (QSet<QString>{"alpha", "beta"}));
        QCOMPARE(host.editor().renameLanguageIds(), QSet<QString>{"alpha"});
    }

    // A disposed provider gives the document back what it had before.
    void testDisposedProvidersAreReleased()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath source = root / "release.txt";
        QVERIFY(source.writeFileContents("word\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "release", "alien-release-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const sel = {language: 'plaintext'};\n"
            "  const providers = [\n"
            "    vscode.languages.registerCompletionItemProvider(\n"
            "        sel, {provideCompletionItems: () => []}),\n"
            "    vscode.languages.registerDocumentFormattingEditProvider(\n"
            "        sel, {provideDocumentFormattingEdits: () => []})];\n"
            "  vscode.commands.registerCommand('alien.release.drop', () => {\n"
            "    providers.forEach(p => p.dispose());\n"
            "    vscode.commands.registerCommand('alien.release.dropped', () => {});\n"
            "  });\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        QVERIFY(EditorManager::openEditor(source));
        auto document = qobject_cast<TextEditor::TextDocument *>(
            EditorManager::currentDocument());
        QVERIFY(document);
        TextEditor::CompletionAssistProvider *previous = document->completionAssistProvider();

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.release.drop"),
                                 15000);
        QTRY_VERIFY(document->completionAssistProvider() != previous);
        QTRY_VERIFY(document->formatter());

        host.executeCommand("alien.release.drop");
        // Registered after the disposal, so its announcements have arrived.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.release.dropped"),
                                 15000);
        QCOMPARE(document->completionAssistProvider(), previous);
        QVERIFY(!document->formatter());

        EditorManager::closeAllEditors(false);
    }

    // Every provider that has something to say says it, rather than whichever
    // registered first.
    void testHoverAndDefinitionMergeProviders()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath source = root / "both.txt";
        QVERIFY(source.writeFileContents("word\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "both", "alien-both-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const sel = {language: 'plaintext'};\n"
            "  for (const which of ['first', 'second']) {\n"
            "    context.subscriptions.push(vscode.languages.registerHoverProvider(\n"
            "        sel, {provideHover: () => new vscode.Hover(which)}));\n"
            "    context.subscriptions.push(vscode.languages.registerDefinitionProvider(\n"
            "        sel, {provideDefinition: () => new vscode.Location(\n"
            "            vscode.Uri.file('/' + which), new vscode.Position(0, 0))}));\n"
            "  }\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.both.ready', () => {}));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.both.ready"), 15000);
        QVERIFY(EditorManager::openEditor(source));

        QString hover;
        const auto askHover = [&host, &hover, &source] {
            host.editor().requestHover(source, 0, 1, [&hover](const QString &text) {
                hover = text;
            });
            return hover.contains("first");
        };
        QTRY_VERIFY_WITH_TIMEOUT(askHover(), 15000);
        QVERIFY2(hover.contains("second"), qPrintable(hover));

        QJsonArray locations;
        const auto askDefinition = [&host, &locations, &source] {
            host.editor().requestDefinition(source, 0, 1,
                                   [&locations](const QJsonArray &r) { locations = r; });
            return locations.size() >= 1;
        };
        QTRY_VERIFY_WITH_TIMEOUT(askDefinition(), 15000);
        QCOMPARE(locations.size(), 2);

        EditorManager::closeAllEditors(false);
    }

    // A menu entry gated on the editor having the keyboard, or on what is in
    // it being unchangeable: neither could be answered before.
    void testEditorFocusContextKeys()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath writable = root / "writable.txt";
        const FilePath locked = root / "locked.txt";
        QVERIFY(writable.writeFileContents("one\n").has_value());
        QVERIFY(locked.writeFileContents("two\n").has_value());
        QVERIFY(locked.setPermissions(QFile::ReadOwner));

        const QJsonObject contributes{
            {"commands", QJsonArray{QJsonObject{{"command", "alien.focus.go"},
                                                {"title", "Only When Read Only"}}}},
            {"menus", QJsonObject{{"editor/context",
                                   QJsonArray{QJsonObject{
                                       {"command", "alien.focus.go"},
                                       {"when", "editorReadonly"}}}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "focus", "alien-focus",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.focus.go', () => {}));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.focus.go"), 15000);

        Core::ActionContainer *container = Core::ActionManager::actionContainer(
            TextEditor::Constants::M_STANDARDCONTEXTMENU);
        QVERIFY(container);
        const auto offered = [container] {
            for (QAction *action : container->menu()->actions()) {
                if (action->text() == "Only When Read Only" && action->isVisible())
                    return true;
            }
            return false;
        };

        QVERIFY(EditorManager::openEditor(writable));
        QTRY_VERIFY_WITH_TIMEOUT(!offered(), 15000);

        QVERIFY(EditorManager::openEditor(locked));
        QTRY_VERIFY_WITH_TIMEOUT(offered(), 15000);

        EditorManager::closeAllEditors(false);
        QVERIFY(locked.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
    }

    // A row's menu may hold a menu of its own, and a symbol may say it is
    // deprecated - neither survived the trip before.
    void testSubmenusAndDeprecatedSymbols()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath source = root / "old.txt";
        QVERIFY(source.writeFileContents("legacy\n").has_value());

        const QJsonObject contributes{
            {"commands", QJsonArray{QJsonObject{{"command", "alien.sub.inner"},
                                                {"title", "Inner Thing"}}}},
            {"submenus", QJsonArray{QJsonObject{{"id", "alien.sub.more"},
                                                {"label", "More"}}}},
            {"menus", QJsonObject{
                {"view/item/context", QJsonArray{QJsonObject{{"submenu", "alien.sub.more"}}}},
                {"alien.sub.more", QJsonArray{QJsonObject{{"command", "alien.sub.inner"}}}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "sub", "alien-sub",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.sub.inner', () => {}));\n"
            "  context.subscriptions.push(vscode.window.registerTreeDataProvider('alienSub', {\n"
            "    getChildren: () => ['Row'],\n"
            "    getTreeItem: e => new vscode.TreeItem(e,\n"
            "        vscode.TreeItemCollapsibleState.None),\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider(\n"
            "      {language: 'plaintext'}, {provideDocumentSymbols: () => {\n"
            "        const s = new vscode.DocumentSymbol('gone', '',\n"
            "            vscode.SymbolKind.Function, new vscode.Range(0, 0, 0, 6),\n"
            "            new vscode.Range(0, 0, 0, 6));\n"
            "        s.tags = [vscode.SymbolTag.Deprecated];\n"
            "        return [s];\n"
            "      }}));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.sub.inner"), 15000);

        QString rowId;
        const auto rows = [&host, &rowId] {
            host.window().requestTreeChildren("alienSub", {}, [&rowId](const QJsonArray &nodes) {
                if (!nodes.isEmpty())
                    rowId = nodes.first().toObject().value("id").toString();
            });
            return !rowId.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(rows(), 15000);

        QJsonArray menu;
        const auto askMenu = [&host, &menu, &rowId] {
            host.window().requestTreeMenu("alienSub", rowId, "view/item/context",
                                 [&menu](const QJsonArray &items) { menu = items; });
            return !menu.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(askMenu(), 15000);
        // The entry is the submenu, and what it holds came with it.
        QCOMPARE(menu.size(), 1);
        QCOMPARE(menu.first().toObject().value("submenu").toString(), QString("More"));
        const QJsonArray inner = menu.first().toObject().value("items").toArray();
        QCOMPARE(inner.size(), 1);
        QCOMPARE(inner.first().toObject().value("title").toString(), QString("Inner Thing"));

        QVERIFY(EditorManager::openEditor(source));
        QJsonArray symbols;
        const auto askSymbols = [&host, &symbols, &source] {
            host.editor().requestDocumentSymbols(source,
                                        [&symbols](const QJsonArray &r) { symbols = r; });
            return !symbols.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(askSymbols(), 15000);
        QCOMPARE(symbols.first().toObject().value("tags").toArray(), QJsonArray{1});

        EditorManager::closeAllEditors(false);
    }

    // What tells two entries of a quick pick apart, and two more of the
    // editor's own commands.
    void testQuickPickDetailAndMoreCommands()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath source = root / "pick.txt";
        QVERIFY(source.writeFileContents("word\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "pick", "alien-pick-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.pick.ask', async () => {\n"
            "        const chosen = await vscode.window.showQuickPick([\n"
            "          {label: 'Run', description: 'the tests', detail: 'all of them'},\n"
            "          {label: 'Run', description: 'the app'}]);\n"
            "        vscode.window.showInformationMessage(\n"
            "            'picked:' + (chosen ? chosen.description : 'none'));\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.pick.run', async command => {\n"
            "        const answer = await vscode.commands.executeCommand(command);\n"
            "        vscode.window.showInformationMessage(command + '=' + String(answer));\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QStringList offered;
        connect(&host, &ExtensionHost::quickPickRequested, &host,
                [&host, &offered](int id, const QStringList &items, const QString &, bool) {
                    offered = items;
                    host.window().resolveQuickPick(id, {1});
                });
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.pick.ask"), 15000);

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.pick.ask");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        // Two items called "Run" are told apart by what they say about
        // themselves, and the one picked is the one meant.
        QCOMPARE(offered, QStringList({"Run - the tests (all of them)", "Run - the app"}));
        QCOMPARE(messages.first().first().toString(), QString("picked:the app"));

        QVERIFY(EditorManager::openEditor(source));
        QString answer;
        const auto ask = [&host, &answer](const QString &command) {
            answer.clear();
            QSignalSpy replies(&host, &ExtensionHost::messageShown);
            host.executeCommand("alien.pick.run", QJsonArray{command});
            const auto answered = [&] {
                for (const QList<QVariant> &arguments : replies) {
                    const QString message = arguments.at(0).toString();
                    if (message.startsWith(command + '=')) {
                        answer = message.mid(command.size() + 1);
                        return true;
                    }
                }
                return false;
            };
            QTRY_VERIFY_WITH_TIMEOUT(answered(), 15000);
        };
        ask("editor.action.rename");
        QCOMPARE(answer, QString("true"));
        ask("extension.open");
        QCOMPARE(answer, QString("true"));

        EditorManager::closeAllEditors(false);
    }

    // What a setting says about itself beyond its type: where it belongs, what
    // it may hold, whether it should still be used, and what its choices are
    // really called.
    void testSettingSchemaIsHonoured()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath packageJson = root / "package.json";
        const QJsonObject properties{
            {"z.last", QJsonObject{{"type", "string"}, {"order", 2},
                                   {"markdownDescription", "Uses `code` here."}}},
            {"a.first", QJsonObject{{"type", "string"}, {"order", 1},
                                    {"deprecationMessage", "Gone in the next one."}}},
            {"m.opacity", QJsonObject{{"type", "number"}, {"default", 0.55},
                                      {"minimum", 0.1}, {"maximum", 1.0}}},
            {"m.count", QJsonObject{{"type", "integer"}, {"default", 5},
                                    {"minimum", 1}, {"maximum", 10}}},
            {"m.mode", QJsonObject{{"type", "string"},
                                   {"enum", QJsonArray{"a", "b"}},
                                   {"enumItemLabels", QJsonArray{"Alpha", "Beta"}}}},
            {"m.sort", QJsonObject{{"enum", QJsonArray{true, false}}, {"default", false}}},
        };
        const QJsonObject package{
            {"name", "schema"}, {"publisher", "alien"}, {"version", "1.0.0"},
            {"contributes", QJsonObject{{"configuration", QJsonObject{
                {"title", "Schema"}, {"properties", properties}}}}}};
        QVERIFY(packageJson.writeFileContents(QJsonDocument(package).toJson()).has_value());

        const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        const QList<VscodeSetting> &settings = manifest->configurationSettings;

        // Each setting knows the heading its extension groups it under.
        QCOMPARE(settings.at(0).section, QString("Schema"));

        // The order the extension asked for wins over the order they arrived in.
        QCOMPARE(settings.at(0).key, QString("a.first"));
        QCOMPARE(settings.at(1).key, QString("z.last"));

        const auto find = [&settings](const QString &key) {
            return Utils::findOrDefault(settings, [&key](const VscodeSetting &s) {
                return s.key == key;
            });
        };
        QCOMPARE(find("a.first").deprecationMessage, QString("Gone in the next one."));
        QVERIFY(find("z.last").descriptionIsMarkdown);
        QCOMPARE(find("m.opacity").minimum.value_or(-1), 0.1);
        QCOMPARE(find("m.opacity").maximum.value_or(-1), 1.0);
        QCOMPARE(find("m.count").minimum.value_or(-1), 1.0);
        QCOMPARE(find("m.mode").enumItemLabels, QStringList({"Alpha", "Beta"}));
        // The values themselves need not be strings.
        QCOMPARE(find("m.sort").enumRawValues, QJsonArray({true, false}));

        // A number is not an integer: its editor must keep the fraction.
        ExtensionSettingsDialog dialog(*manifest);
        auto opacity = dialog.findChild<QDoubleSpinBox *>("extensionSetting.m.opacity");
        QVERIFY2(opacity, "the number setting is not edited as a number");
        QCOMPARE(opacity->value(), 0.55);
        QCOMPARE(opacity->minimum(), 0.1);
        QCOMPARE(opacity->maximum(), 1.0);

        auto count = dialog.findChild<QSpinBox *>("extensionSetting.m.count");
        QVERIFY(count);
        QCOMPARE(count->minimum(), 1);
        QCOMPARE(count->maximum(), 10);

        // The combo shows the labels, not the values behind them.
        auto mode = dialog.findChild<QComboBox *>("extensionSetting.m.mode");
        QVERIFY(mode);
        QCOMPARE(mode->itemText(0), QString("Alpha"));

        // And the heading is on the page, not only in the manifest.
        QVERIFY(dialog.findChild<QLabel *>("extensionSettingSection.Schema"));
    }

    // How a glob is read: the same way everywhere, and to the depth it names.
    void testGlobsAreReadTheSameWay()
    {
        // A comma outside a group is part of a name, not an alternation.
        QVERIFY(globExpression("{a,b}.txt").match("a.txt").hasMatch());
        QVERIFY(globExpression("{a,b}.txt").match("b.txt").hasMatch());
        QVERIFY(!globExpression("{a,b}.txt").match("c.txt").hasMatch());
        QVERIFY(globExpression("a,b.txt").match("a,b.txt").hasMatch());
        QVERIFY(!globExpression("a,b.txt").match("a").hasMatch());

        QVERIFY(globExpression("**/*.cpp").match("a/b/c.cpp").hasMatch());
        QVERIFY(globExpression("**/*.cpp").match("c.cpp").hasMatch());
        QVERIFY(globExpression("*/pom.xml").match("a/pom.xml").hasMatch());
        QVERIFY(!globExpression("*/pom.xml").match("a/b/pom.xml").hasMatch());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        // The language matcher reads the same globs, groups included.
        VscodeLanguage language;
        language.id = "alienyaml";
        language.filenamePatterns = QStringList{"*.{yml,yaml}"};
        QVERIFY(languageMatchesFile(language, root / "a.yml"));
        QVERIFY(languageMatchesFile(language, root / "a.yaml"));
        QVERIFY(!languageMatchesFile(language, root / "a.txt"));

        // And an activation event names a depth, which is kept.
        const QString js = "function activate() {}\nmodule.exports = { activate };\n";
        const Result<VscodeManifest> shallow = writeMockExtension(
            root / "shallow", "alien-shallow", js, QJsonArray{"workspaceContains:*/pom.xml"});
        QVERIFY2(shallow.has_value(), qPrintable(shallow ? QString() : shallow.error()));

        const FilePath deep = root / "one" / "two";
        QVERIFY(deep.ensureWritableDir());
        QVERIFY((deep / "pom.xml").writeFileContents("<x/>").has_value());
        QVERIFY(!activationEventFired(*shallow, {}, {root}, {}, {}));

        QVERIFY((root / "one" / "pom.xml").writeFileContents("<x/>").has_value());
        QVERIFY(activationEventFired(*shallow, {}, {root}, {}, {}));
    }

    // A URI serialised as JSON carries the file name the file system knows,
    // not the URI path; and asking for a debug adapter wakes whoever has it.
    void testUriJsonAndDebugWake()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "uri", "alien-uri-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.uri.ask', () => {\n"
            "        const uri = vscode.Uri.file('C:/work/x.txt');\n"
            "        vscode.window.showInformationMessage(\n"
            "            'json:' + JSON.parse(JSON.stringify(uri)).fsPath);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.uri.ask"), 15000);

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.uri.ask");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        // The path keeps the slash before the drive letter; fsPath does not.
        QCOMPARE(messages.first().first().toString(), QString("json:c:/work/x.txt"));

        // An extension waiting for a debug session of its type is woken by one
        // being asked for.
        const Result<VscodeManifest> debugOnly = writeMockExtension(
            root / "dbg", "alien-dbg-wake",
            "function activate() {}\nmodule.exports = { activate };\n",
            QJsonArray{"onDebugResolve:alienlang"});
        QVERIFY2(debugOnly.has_value(), qPrintable(debugOnly ? QString() : debugOnly.error()));
        QVERIFY(!activationEventFired(*debugOnly, {}, {}, {}, {}));
        QVERIFY(activationEventFired(*debugOnly, {}, {}, {}, {"alienlang"}));

        // Asking is what says so.
        QSignalSpy asked(&host, &ExtensionHost::debugTypeRequested);
        host.run().requestDebugAdapter("alienlang", {}, [](const QJsonObject &) {});
        QCOMPARE(asked.size(), 1);
        QCOMPARE(asked.first().first().toString(), QString("alienlang"));
    }

    // What the extension's own language-configuration.json says, which was
    // read as a path and never opened.
    void testLanguageConfigurationIsRead()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        QVERIFY((root / "language-configuration.json").writeFileContents(
                    "{\n"
                    "  // A comment, which this file is allowed to have.\n"
                    "  \"wordPattern\": \"\\\\\", // and after a string ending in \\\n"
                    "  \"brackets\": [[\"{\", \"}\"],],\n"
                    "  \"autoClosingPairs\": [{\"open\": \"(\", \"close\": \")\"}],\n"
                    "  \"surroundingPairs\": [[\"'\", \"'\"]],\n"
                    "  \"folding\": {\"markers\": {\"start\": \"^//#region\",\n"
                    "                              \"end\": \"^//#endregion\"}}\n"
                    "}\n").has_value());
        const QJsonObject package{
            {"name", "langcfg"}, {"publisher", "alien"}, {"version", "1.0.0"},
            {"contributes", QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienlc"},
                {"extensions", QJsonArray{".alienlc"}},
                {"configuration", "./language-configuration.json"}}}}}}};
        const FilePath packageJson = root / "package.json";
        QVERIFY(packageJson.writeFileContents(QJsonDocument(package).toJson()).has_value());

        const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        QCOMPARE(manifest->languages.size(), 1);
        const VscodeLanguage &language = manifest->languages.first();
        QCOMPARE(language.brackets, (QList<QPair<QString, QString>>{{"{", "}"}}));
        QCOMPARE(language.autoClosingPairs, (QList<QPair<QString, QString>>{{"(", ")"}}));
        QCOMPARE(language.surroundingPairs, (QList<QPair<QString, QString>>{{"'", "'"}}));
        QCOMPARE(language.foldingStartMarker, QString("^//#region"));
        QCOMPARE(language.foldingEndMarker, QString("^//#endregion"));
    }

    // A key the manifest bound to one place only, and the arguments it named.
    void testKeybindingWhenAndArguments()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const QJsonObject package{
            {"name", "keys"}, {"publisher", "alien"}, {"version", "1.0.0"},
            {"contributes", QJsonObject{{"keybindings", QJsonArray{QJsonObject{
                {"command", "alien.keys.go"},
                {"key", "ctrl+alt+r"},
                {"when", "editorTextFocus"},
                {"args", "seven"}}}}}}};
        const FilePath packageJson = root / "package.json";
        QVERIFY(packageJson.writeFileContents(QJsonDocument(package).toJson()).has_value());

        const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        QCOMPARE(manifest->keybindings.size(), 1);
        QCOMPARE(manifest->keybindings.first().when, QString("editorTextFocus"));
        QCOMPARE(manifest->keybindings.first().arguments.toString(), QString("seven"));

        // Every key the corpus binds has to come out as a real shortcut.
        // A comma is a key an extension binds and the separator Qt splits
        // chords on, so the ones naming it are what a joined string breaks on.
        const QStringList corpusKeys{"ctrl+shift+v", "shift+alt+u", "delete", "enter", "f7",
                                     "shift+f1", "ctrl+k ctrl+shift+c", "ctrl+k v", "F2",
                                     "ctrl+shift+alt+s", "shift+ctrl+f5", "alt+,",
                                     "ctrl+shift+g ,", "ctrl+shift+g shift+,", "shift+alt+.",
                                     "alt+/"};
        for (const QString &key : corpusKeys) {
            VscodeKeybinding binding;
            binding.command = "x";
            binding.key = key;
            QVERIFY2(!keySequenceOf(binding).isEmpty(), qPrintable(key));
        }
    }

    // What the host is told about a document and an editor beyond their text.
    void testDocumentAndEditorFacts()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "facts.txt";
        QVERIFY(file.writeFileContents("one\r\ntwo\r\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "facts", "alien-facts-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.facts.ask', () => {\n"
            "        const editor = vscode.window.activeTextEditor;\n"
            "        const doc = editor.document;\n"
            "        vscode.window.showInformationMessage(\n"
            "            'facts:' + doc.eol + ':' + doc.isDirty + ':'\n"
            "            + editor.options.tabSize + ':' + editor.options.insertSpaces);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.facts.ask"), 15000);
        QVERIFY(EditorManager::openEditor(file));
        // Settings the host cannot have guessed.
        auto opened = qobject_cast<TextEditor::TextDocument *>(EditorManager::currentDocument());
        QVERIFY(opened);
        TextEditor::TabSettingsData tabs;
        tabs.m_indentSize = 3;
        tabs.m_tabPolicy = TextEditor::TabSettingsData::TabsOnlyTabPolicy;
        opened->setTabSettings(tabs);

        QString answer;
        const auto ask = [&host, &answer] {
            QSignalSpy messages(&host, &ExtensionHost::messageShown);
            host.executeCommand("alien.facts.ask");
            const auto answered = [&] {
                for (const QList<QVariant> &arguments : messages) {
                    const QString message = arguments.at(0).toString();
                    if (message.startsWith("facts:")) {
                        answer = message.mid(6);
                        return true;
                    }
                }
                return false;
            };
            QTRY_VERIFY_WITH_TIMEOUT(answered(), 15000);
        };
        ask();
        // CRLF is 2, the file is unchanged, and the editor's own tab settings
        // came with it.
        QCOMPARE(answer, QString("2:false:3:false"));

        // Changing it says so.
        auto document = qobject_cast<TextEditor::TextDocument *>(
            EditorManager::currentDocument());
        QVERIFY(document);
        QTextCursor cursor(document->document());
        cursor.insertText("x");
        QVERIFY(document->isModified());
        EditorManager::closeAllEditors(false);
    }

    // What a prompt is called, whether a message has to be answered, and what
    // an extension can read back out of its own diagnostics.
    void testPromptsMessagesAndDiagnosticEntries()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "prompts", "alien-prompts-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.prompts.pick', () => vscode.window.showQuickPick(\n"
            "          ['one'], {title: 'Pick one'})));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.prompts.ask', () => vscode.window.showInformationMessage(\n"
            "          'Really?', {modal: true, detail: 'It cannot be undone.'}, 'Yes')));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.prompts.diag', () => {\n"
            "        const c = vscode.languages.createDiagnosticCollection('alien-entries');\n"
            "        c.set(vscode.Uri.file('/a.txt'), [new vscode.Diagnostic(\n"
            "            new vscode.Range(0, 0, 0, 1), 'first')]);\n"
            "        const entries = c.entries();\n"
            "        vscode.window.showInformationMessage('entries:' + entries.length + ':'\n"
            "            + entries[0][0].fsPath + ':' + entries[0][1][0].message);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QString pickTitle;
        connect(&host, &ExtensionHost::quickPickRequested, &host,
                [&host](int id, const QStringList &, const QString &, bool) {
                    host.window().resolveQuickPick(id, {0});
                });
        QString questionDetail;
        bool questionModal = false;
        connect(&host, &ExtensionHost::messageQuestionRequested, &host,
                [&](int id, MessageLevel, const QString &, const QStringList &,
                    const QString &detail, bool modal) {
                    questionDetail = detail;
                    questionModal = modal;
                    host.window().resolveMessageQuestion(id, 0);
                });
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.prompts.diag"), 15000);

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.prompts.diag");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        // A collection can be walked as the pairs it holds.
        QCOMPARE(messages.first().first().toString(), QString("entries:1:/a.txt:first"));

        // A message the extension means as a question, with its second line.
        host.executeCommand("alien.prompts.ask");
        QTRY_VERIFY_WITH_TIMEOUT(questionModal, 15000);
        QCOMPARE(questionDetail, QString("It cannot be undone."));
    }

    // The pairs and region markers the extension named, in the editor: a
    // language only it knows gets its own closing characters and its own folds.
    void testLanguageConfigurationReachesTheEditor()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath extension = root / "lc";
        QVERIFY(extension.ensureWritableDir());
        QVERIFY((extension / "language-configuration.json").writeFileContents(
                    "{\"autoClosingPairs\": [[\"(\", \")\"], [\"<\", \">\"]],\n"
                    " \"folding\": {\"markers\": {\"start\": \"^//#region\",\n"
                    "                             \"end\": \"^//#endregion\"}}}\n").has_value());

        const FilePath source = root / "code.alienlc";
        QVERIFY(source.writeFileContents("//#region\ninside\n//#endregion\n").has_value());
        const FilePath json = root / "code.json";
        QVERIFY(json.writeFileContents("{}\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            extension, "alien-lc",
            "function activate() {}\nmodule.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienlc"},
                {"extensions", QJsonArray{".alienlc"}},
                {"configuration", "./language-configuration.json"}}, QJsonObject{
                {"id", "json"},
                {"extensions", QJsonArray{".json"}},
                {"configuration", "./language-configuration.json"}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 15000);

        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(
            EditorManager::openEditor(source));
        QVERIFY(textEditor);
        TextEditor::TextEditorWidget *widget = textEditor->editorWidget();
        QVERIFY(widget);

        // Typing the opening character brings its closing one, and typing
        // that one over it does not double it.
        const auto typed = [](TextEditor::TextEditorWidget *widget, const QString &text) {
            QTextCursor cursor = widget->textCursor();
            const QString completed = widget->autoCompleter()->autoComplete(cursor, text, true);
            cursor.insertText(text + completed);
            return cursor.document()->firstBlock().text();
        };
        QTRY_VERIFY_WITH_TIMEOUT([widget] {
            QTextCursor cursor = widget->textCursor();
            return widget->autoCompleter()->autoComplete(cursor, "(", true) == ")";
        }(), 15000);
        QCOMPARE(typed(widget, "("), QString("()//#region"));
        widget->moveCursor(QTextCursor::Start);
        widget->moveCursor(QTextCursor::NextCharacter);
        QCOMPARE(typed(widget, ")"), QString("()//#region"));

        // A language the editor knows keeps its own completer.
        auto jsonEditor
            = qobject_cast<TextEditor::BaseTextEditor *>(EditorManager::openEditor(json));
        QVERIFY(jsonEditor);
        QCOMPARE(jsonEditor->document()->id(), Id("Editors.Json"));
        QCOMPARE(typed(jsonEditor->editorWidget(), "<"), QString("<{}"));
        EditorManager::activateEditor(textEditor);

        // The region the markers name folds as one block.
        TextEditor::TextDocument *document = widget->textDocument();
        const QTextBlock inside = document->document()->findBlockByNumber(1);
        QVERIFY(inside.isValid());
        QCOMPARE(TextEditor::TextBlockUserData::foldingIndent(inside), 1);
        QVERIFY(document->isFoldingIndentExternallyProvided());

        // Without the extension the editor folds by itself again.
        host.deactivate(manifest->qualifiedId());
        QTextCursor(document->document()).insertText("\n");
        QTRY_VERIFY_WITH_TIMEOUT(!document->isFoldingIndentExternallyProvided(), 15000);
        widget->moveCursor(QTextCursor::Start);
        QCOMPARE(typed(widget, "<"), QString("<"));

        EditorManager::closeAllEditors(false);
    }

    // What the extension said its webview needs, which beats guessing from the
    // markup: quoting a script tag is not the same as running one.
    void testWebviewScriptsAreDeclaredNotGuessed()
    {
        AutoWebviewRenderer renderer;
        // Without a declaration the markup decides, as before.
        renderer.createPanel("guessed", "test", "Guessed");
        renderer.setHtml("guessed", "<p>plain</p>");
        QVERIFY(!renderer.engineStarted());

        // A panel that said it needs no scripts keeps none, whatever its page
        // happens to contain.
        renderer.createPanel("static", "test", "Static");
        renderer.setPanelOptions("static", QJsonObject{{"enableScripts", false}});
        renderer.setHtml("static", "<pre>&lt;script&gt;</pre><script>x</script>");
        QVERIFY(!renderer.engineStarted());
    }

    // A virtual document the extension makes up says when it has changed, and
    // what is shown follows.
    void testVirtualDocumentIsReread()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "virt", "alien-virt-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  let text = 'first';\n"
            "  const changed = new vscode.EventEmitter();\n"
            "  context.subscriptions.push(\n"
            "      vscode.workspace.registerTextDocumentContentProvider('alienv', {\n"
            "        onDidChange: changed.event,\n"
            "        provideTextDocumentContent: () => text,\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.virt.open', async () => {\n"
            "        const doc = await vscode.workspace.openTextDocument(\n"
            "            vscode.Uri.parse('alienv:/thing'));\n"
            "        vscode.window.showInformationMessage('opened:' + doc.getText());\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.virt.change', async () => {\n"
            "        text = 'second';\n"
            "        changed.fire(vscode.Uri.parse('alienv:/thing'));\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.virt.change"), 15000);

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.virt.open");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.first().first().toString(), QString("opened:first"));

        // The provider says it changed; asking again gives the new content.
        host.executeCommand("alien.virt.change");
        QSignalSpy again(&host, &ExtensionHost::messageShown);
        const auto reread = [&host, &again] {
            host.executeCommand("alien.virt.open");
            for (const QList<QVariant> &arguments : again) {
                if (arguments.at(0).toString() == "opened:second")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(reread(), 15000);
    }

    // The launch configurations an extension offers for its debug type, the
    // shell it wants a terminal to run, what became of that terminal, what its
    // tasks are for, and asking for the trash rather than for removal.
    void testDebugTasksTerminalsAndTrash()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath doomed = root / "doomed.txt";
        QVERIFY(doomed.writeFileContents("bye\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "mix", "alien-mix-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider(\n"
            "      'aliendbg', {\n"
            "        provideDebugConfigurations: () => [\n"
            "          {type: 'aliendbg', request: 'launch', name: 'Launch It'}],\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.tasks.registerTaskProvider('aliental', {\n"
            "    provideTasks: () => {\n"
            "      const task = new vscode.Task({type: 'aliental'}, 'Build It', 'alien',\n"
            "          new vscode.ShellExecution('true'));\n"
            "      task.group = vscode.TaskGroup.Build;\n"
            "      task.problemMatchers = ['$gcc'];\n"
            "      return [task];\n"
            "    },\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.mix.shell', () => {\n"
            "        vscode.window.createTerminal(\n"
            "            {name: 'Alien Shell', shellPath: '/bin/echo', shellArgs: ['-n']});\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.mix.trash', async path => {\n"
            "        try {\n"
            "          await vscode.workspace.fs.delete(vscode.Uri.file(path),\n"
            "                                          {useTrash: true});\n"
            "          vscode.window.showInformationMessage('trashed:' + path);\n"
            "        } catch (e) {\n"
            "          vscode.window.showInformationMessage('refused:' + path);\n"
            "        }\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.mix.trash"), 15000);

        // What the extension offers to put in a launch configuration.
        QJsonArray configurations;
        const auto askConfigurations = [&host, &configurations] {
            host.run().requestDebugConfigurations(
                "aliendbg", [&configurations](const QJsonArray &r) { configurations = r; });
            return !configurations.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(askConfigurations(), 15000);
        QCOMPARE(configurations.first().toObject().value("name").toString(), QString("Launch It"));

        // What its tasks are for, and who reads their output.
        QJsonArray tasks;
        const auto askTasks = [&host, &tasks] {
            host.run().requestTasks([&tasks](const QJsonArray &r) { tasks = r; });
            return !tasks.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(askTasks(), 15000);
        const QJsonObject task = tasks.first().toObject();
        QCOMPARE(task.value("group").toString(), QString("build"));
        QCOMPARE(task.value("problemMatchers").toArray().first().toString(), QString("$gcc"));

        // Asking for the trash is not asking for removal: a file that is not
        // there cannot be put in the trash, where removing it would have been
        // quietly fine.
        const FilePath missing = root / "never-existed.txt";
        QSignalSpy refusals(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.mix.trash", QJsonArray{missing.toFSPathString()});
        const auto answered = [&refusals](const QString &prefix) {
            for (const QList<QVariant> &arguments : refusals) {
                if (arguments.at(0).toString().startsWith(prefix))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(answered("refused:"), 15000);

        // And one that is there goes.
        host.executeCommand("alien.mix.trash", QJsonArray{doomed.toFSPathString()});
        QTRY_VERIFY_WITH_TIMEOUT(answered("trashed:"), 15000);
        QVERIFY(!doomed.exists());
    }

    // A terminal the extension knows how to set up, offered by the name its
    // manifest gives it. Registering one used to throw, which took the rest of
    // the extension's activation down with it.
    void testTerminalProfileProvider()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "prof", "alien-profile-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(\n"
            "      vscode.window.registerTerminalProfileProvider('alien.shell', {\n"
            "        // The type the API names, which extensions construct.\n"
            "        provideTerminalProfile: () => new vscode.TerminalProfile({\n"
            "            name: 'Alien Build Shell', shellPath: '/bin/echo',\n"
            "            shellArgs: ['-n']}),\n"
            "      }));\n"
            "  context.subscriptions.push(\n"
            "      vscode.window.registerTerminalProfileProvider('alien.default', {\n"
            "        // No shell named: the user own is what opens.\n"
            "        provideTerminalProfile: () => new vscode.TerminalProfile(\n"
            "            {name: 'Alien Plain Shell'}),\n"
            "      }));\n"
            "  // Registered after the provider: if registering one threw, this\n"
            "  // would never happen.\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.profile.after', () => {}));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"terminal", QJsonObject{{"profiles", QJsonArray{
                QJsonObject{{"id", "alien.shell"}, {"title", "Alien Build Shell"},
                            {"icon", "terminal"}},
                QJsonObject{{"id", "alien.default"}, {"title", "Alien Plain Shell"}}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        // Activation ran to the end.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.profile.after"), 15000);

        // The profiles are offered, under the names the manifest gives them.
        QTRY_COMPARE_WITH_TIMEOUT(host.run().terminalProfiles().size(), 2, 15000);
        const QJsonObject profile = host.run().terminalProfiles().first().toObject();
        QCOMPARE(profile.value("id").toString(), QString("alien.shell"));
        QCOMPARE(profile.value("title").toString(), QString("Alien Build Shell"));

        // Opening it asks the extension what to run, and a terminal appears
        // running the shell it named.
        QSignalSpy opened(&host, &ExtensionHost::terminalOpened);
        host.run().openTerminalProfile("alien.shell");
        QTRY_VERIFY_WITH_TIMEOUT(!opened.isEmpty(), 15000);
        QCOMPARE(opened.first().first().toString(), QString("Alien Build Shell"));
        QCOMPARE(opened.first().at(1).value<Utils::CommandLine>().executable().fileName(),
                 QString("echo"));

        // A profile that names no shell opens the user's own, rather than
        // waiting for a command that is never coming.
        QSignalSpy plain(&host, &ExtensionHost::terminalOpened);
        host.run().openTerminalProfile("alien.default");
        QTRY_VERIFY_WITH_TIMEOUT(!plain.isEmpty(), 15000);
        QCOMPARE(plain.first().first().toString(), QString("Alien Plain Shell"));
        QVERIFY(!plain.first().at(1).value<Utils::CommandLine>().executable().isEmpty());

        // Nobody is left to provide them once the host is gone.
        QSignalSpy changed(&host, &ExtensionHost::terminalProfilesChanged);
        host.shutdown();
        QVERIFY(host.run().terminalProfiles().isEmpty());
        QCOMPARE(changed.size(), 1);
    }

    // A status bar button written as icon markup alone: seen as its icon, or as
    // a stand-in when we have none for that name - but never as a blank patch
    // of status bar that still answers a click.
    void testStatusBarIconOnlyItems()
    {
        QVERIFY(isIconOnly("$(play)"));
        QVERIFY(isIconOnly("$(sync~spin)"));
        QVERIFY(!isIconOnly("$(folder) undefined"));
        QVERIFY(!isIconOnly("Building"));
        QVERIFY(!isIconOnly(""));

        // The names this extension's buttons use resolve to icons of ours.
        for (const QString &name : QStringList{"debug-rerun", "play", "cloud-upload",
                                               "debug-all", "arrow-circle-up", "project",
                                               "folder-opened", "preview"}) {
            QVERIFY2(!firstCodicon('$' + QString("(%1)").arg(name)).isNull(), qPrintable(name));
        }

        StatusBarItem item;
        item.setContent("$(play)");
        QVERIFY(item.hasContent());
        // No icon of ours for this one, and nothing left after the markup.
        item.setContent("$(some-icon-we-do-not-have)");
        QVERIFY(item.hasContent());
        item.setContent("Building");
        QVERIFY(item.hasContent());
        // Left blank on purpose: not shown.
        item.setContent("");
        QVERIFY(!item.hasContent());
    }

    // A webview panel is the extension's own interface, so it has to open wide
    // enough to show one.
    void testWebviewPanelOpensUsablyWide()
    {
        AutoWebviewRenderer renderer;
        renderer.createPanel("sized", "test", "Sized Panel");
        renderer.setHtml("sized", "<p>hello</p>");

        auto dock = ICore::mainWindow()->findChild<QDockWidget *>("Alien.Webview.sized");
        QVERIFY2(dock, "the panel has no dock");
        QVERIFY(dock->widget());
        // Left to itself a web view asks for nothing and the dock obliges.
        QVERIFY2(dock->widget()->minimumWidth() >= 320,
                 qPrintable(QString("minimum width is %1").arg(dock->widget()->minimumWidth())));
        renderer.disposePanel("sized");
    }

    // Closing a panel is disposing of it, which the extension hears about. One
    // the extension disposed of itself is not reported back to it.
    void testWebviewPanelClosedByUser()
    {
        AutoWebviewRenderer renderer;
        QStringList disposed;
        renderer.onDisposed = [&disposed](const QString &id) { disposed.append(id); };
        renderer.createPanel("asked", "test", "Asked");
        renderer.disposePanel("asked");
        renderer.createPanel("closed", "test", "Closed");

        auto dock = ICore::mainWindow()->findChild<QDockWidget *>("Alien.Webview.closed");
        QVERIFY(dock);
        dock->close();
        QTRY_COMPARE(disposed, QStringList("closed"));
    }

    // Without qwebchannel.js a panel renders but can never answer its
    // extension, and the failure looks like a page that is merely slow. Where
    // the file sits depends on how Qt was built, so all the places it can be
    // have to be looked in.
    void testWebChannelScriptIsFound()
    {
#ifdef ALIEN_WITH_WEBENGINE
        const FilePath js = webChannelScriptPath();
        QVERIFY2(!js.isEmpty(), "qwebchannel.js was not found in any known location");
        QVERIFY(js.isReadableFile());
        QCOMPARE(js.fileName(), QString("qwebchannel.js"));
#else
        QSKIP("built without QtWebEngine");
#endif
    }

    // What the extension sends has to arrive in the page's own listener, and
    // a message is any JSON value, not only an object. Both have been broken:
    // a page checks where a message came from, and qt-core's wizard ignores
    // everything that is not from a vscode-webview: origin.
    void testWebviewMessageRoundTrip()
    {
#ifdef ALIEN_WITH_WEBENGINE
        AutoWebviewRenderer renderer;
        QJsonValue back;
        renderer.onMessage = [&back](const QString &, const QJsonValue &message) {
            back = message;
        };
        renderer.createPanel("delivery", "test", "Delivery");
        renderer.setPanelOptions("delivery", QJsonObject{{"enableScripts", true}});
        renderer.setHtml("delivery",
                         "<html><body><script>\n"
                         "const api = acquireVsCodeApi();\n"
                         "window.addEventListener('message', event => {\n"
                         "  if (event.origin.startsWith('vscode-webview://'))\n"
                         "    api.postMessage(event.data);\n"
                         "});\n"
                         "api.postMessage('listening');\n"
                         "</script></body></html>");
        QVERIFY(renderer.engineStarted());

        // Nothing can reach a page that has not run its scripts yet, and the
        // page itself is the only one that knows when it has. This is also the
        // page -> host direction carrying something that is not an object.
        QTRY_VERIFY_WITH_TIMEOUT(back.toString() == QString("listening"), 30000);

        back = {};
        renderer.postMessage("delivery", QJsonObject{{"greeting", "hello"}});
        QTRY_VERIFY_WITH_TIMEOUT(back.isObject(), 30000);
        QCOMPARE(back.toObject().value("greeting").toString(), QString("hello"));

        back = {};
        renderer.postMessage("delivery", QJsonValue("bare"));
        QTRY_COMPARE_WITH_TIMEOUT(back.toString(), QString("bare"), 30000);

        renderer.disposePanel("delivery");
#else
        QSKIP("built without QtWebEngine");
#endif
    }

    // An extension that inlines its whole bundle hands over a page bigger than
    // a data: URL can carry.
    void testWebviewLargePage()
    {
#ifdef ALIEN_WITH_WEBENGINE
        AutoWebviewRenderer renderer;
        QJsonValue back;
        renderer.onMessage = [&back](const QString &, const QJsonValue &message) {
            back = message;
        };
        renderer.createPanel("large", "test", "Large");
        renderer.setPanelOptions("large", QJsonObject{{"enableScripts", true}});
        renderer.setHtml("large",
                         "<html><body><!-- " + QString(3 * 1024 * 1024, 'x') + " -->"
                         "<script>acquireVsCodeApi().postMessage('loaded');</script>"
                         "</body></html>");
        QTRY_COMPARE_WITH_TIMEOUT(back.toString(), QString("loaded"), 30000);
        renderer.disposePanel("large");
#else
        QSKIP("built without QtWebEngine");
#endif
    }

    // A webview message is structured-cloned in VS Code, so it carries bytes,
    // and the pipe here is JSON, where a Uint8Array reads as an empty object.
    // The hex editor answers a read with one, and its cells came out blank.
    void testWebviewBinaryMessage()
    {
#ifdef ALIEN_WITH_WEBENGINE
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "bytes", "alien-bytes-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const panel = vscode.window.createWebviewPanel('alien.bytes', 'Bytes', 1,\n"
            "                                                 {enableScripts: true});\n"
            "  panel.webview.onDidReceiveMessage(message => {\n"
            "    if (message === 'ready') {\n"
            "      panel.webview.postMessage({probe: new Uint8Array([1, 2, 254]),\n"
            "                                 buffer: new Uint8Array([3, 4]).buffer});\n"
            "      return;\n"
            "    }\n"
            "    const shown = value => value instanceof Uint8Array\n"
            "                           ? Array.from(value).join('.') : 'no';\n"
            "    vscode.window.showInformationMessage(\n"
            "        'bytes:' + message.seen + ':' + message.buf\n"
            "        + ':' + shown(message.back)\n"
            "        + ':' + shown(message.nested && message.nested.deep)\n"
            "        + ':' + message.plain);\n"
            "  });\n"
            "  panel.webview.html = '<html><body><script>'\n"
            "    + 'const api = acquireVsCodeApi();'\n"
            "    + 'window.addEventListener(\"message\", event => {'\n"
            "    + '  const probe = event.data.probe;'\n"
            "    + '  const seen = (probe instanceof Uint8Array ? \"u8\" :'\n"
            "    + '                probe ? typeof probe : \"none\")'\n"
            "    + '             + \":\" + (probe ? Array.from(probe).join(\".\") : \"\");'\n"
            "    + '  const buffer = event.data.buffer;'\n"
            "    + '  const buf = buffer instanceof ArrayBuffer'\n"
            "    + '            ? Array.from(new Uint8Array(buffer)).join(\".\") : \"no\";'\n"
            "    + '  api.postMessage({seen: seen, buf: buf,'\n"
            "    + '                   back: new Uint8Array([250, 0, 7]),'\n"
            "    + '                   nested: {deep: new Uint8Array([1, 255])},'\n"
            "    + '                   plain: \"text\"});'\n"
            "    + '});'\n"
            "    + 'api.postMessage(\"ready\");'\n"
            "    + '</script></body></html>';\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        AutoWebviewRenderer renderer;
        ExtensionHost host(node);
        host.window().setWebviewRenderer(&renderer);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("bytes:"))
                    return text;
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty() || failed.size() >= 1, 60000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        // Both directions, plain values among the bytes, and a typed array
        // nested in an object rather than handed over on its own.
        QCOMPARE(said(), QString("bytes:u8:1.2.254:3.4:250.0.7:1.255:text"));
#else
        QSKIP("built without QtWebEngine");
#endif
    }

    // What list_webviews answers about a panel has to be what is true of it
    // now. A page is moved to a backend that runs script as soon as it turns
    // out to need one, and a caller that goes by what the extension declared
    // leaves a page alone it could have read: hexeditor's panel says nothing
    // about script and its whole interface is built in the page.
    void testWebviewScriptsReflectsTheBackend()
    {
#ifdef ALIEN_WITH_WEBENGINE
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "backend", "alien-backend-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const scripted = vscode.window.createWebviewPanel('alien.scripted',\n"
            "                                                    'Scripted', 1, {});\n"
            "  scripted.webview.onDidReceiveMessage(\n"
            "      message => vscode.window.showInformationMessage('page:' + message));\n"
            "  scripted.webview.html = '<html><body><script>'\n"
            "    + 'acquireVsCodeApi().postMessage(\"ready\");</script></body></html>';\n"
            "  const plain = vscode.window.createWebviewPanel('alien.plain', 'Plain', 1, {});\n"
            "  plain.webview.html = '<p>static</p>';\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        AutoWebviewRenderer renderer;
        ExtensionHost host(node);
        host.window().setWebviewRenderer(&renderer);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        // The page saying so is what proves it runs script, and it is also
        // what puts both panels in the list before it is read.
        QTRY_VERIFY_WITH_TIMEOUT(Utils::anyOf(spy, [](const QList<QVariant> &args) {
            return args.first().toString() == "page:ready";
        }) || failed.size() >= 1, 60000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        const QJsonArray panels = host.window().webviewPanels();
        QVERIFY(panels.size() == 2);
        const QJsonObject scripted = panels.at(0).toObject();
        const QJsonObject plain = panels.at(1).toObject();
        QCOMPARE(scripted.value("viewType").toString(), QString("alien.scripted"));
        QCOMPARE(plain.value("viewType").toString(), QString("alien.plain"));
        // Neither asked for script. One of them runs it all the same.
        QVERIFY2(scripted.value("scripts").toBool(), "a page that runs script says it cannot");
        QVERIFY2(!plain.value("scripts").toBool(), "a page without script says it can");
#else
        QSKIP("built without QtWebEngine");
#endif
    }

    // The panels an extension puts up, as the outside sees them.
    void testWebviewMcpTools()
    {
        // The tool is there as soon as the plugin is, whether or not a host is.
        const Utils::Result<Mcp::Schema::CallToolResult> listed
            = Mcp::ToolRegistry::callToolForTests("list_webviews", {});
        QVERIFY2(listed.has_value(), qPrintable(listed ? QString() : listed.error()));
        if (runningExtensionHost()) {
            QVERIFY(listed->structuredContent().has_value());
            QVERIFY(listed->structuredContent()->contains("webviews"));
        } else {
            // Nothing running is said out loud, not answered with an empty list
            // that reads as "this extension has no panels".
            QVERIFY(listed->isError().value_or(false));
        }

        AutoWebviewRenderer renderer;
        renderer.createPanel("tooled", "toolTest", "Tooled Panel");
        // Script in a panel is answered by whichever backend it sits on, and a
        // backend that cannot run any says so rather than going quiet.
        bool answered = false;
        QString failure;
        renderer.runScript("no-such-panel", "1", [&](const QJsonValue &, const QString &error) {
            answered = true;
            failure = error;
        });
        QVERIFY(answered);
        QVERIFY2(!failure.isEmpty(), "an unknown panel reported no trouble");
        renderer.disposePanel("tooled");
    }

    // Running a command by id. The tool reaches the host the plugin runs, which
    // a test cannot put there, so what is checked is that asking with nothing
    // running is refused and says so - not answered as though it had run. That
    // an id no extension claimed is refused too is only visible with a host,
    // and is not covered here.
    void testRunExtensionCommandMcpTool()
    {
        const Utils::Result<Mcp::Schema::CallToolResult> unknown
            = Mcp::ToolRegistry::callToolForTests(
                "run_extension_command",
                Mcp::Schema::CallToolRequestParams{}.arguments(
                    QJsonObject{{"id", "nobody.claimed.this"}}));
        QVERIFY2(unknown.has_value(), qPrintable(unknown ? QString() : unknown.error()));
        QVERIFY2(unknown->isError().value_or(false),
                 "an unclaimed command id was accepted rather than refused");

        // And it says which of the two reasons it was, rather than failing mutely.
        QString reason;
        for (const Mcp::Schema::ContentBlock &block : unknown->content()) {
            if (const auto *text = std::get_if<Mcp::Schema::TextContent>(&block))
                reason += text->text();
        }
        if (runningExtensionHost())
            QVERIFY2(reason.contains("nobody.claimed.this"), qPrintable(reason));
        else
            QVERIFY2(reason.contains("not running"), qPrintable(reason));
    }

    // The multi-step wizard pattern extensions build on top of createQuickPick
    // and createInputBox: a list shown before it has items, hidden again once
    // it has them, then a prompt whose value is read back on accept.
    // The answer to a showing that was taken back is not the answer to the one
    // on screen now.
    void testQuickPickShownAgain()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "reshow", "alien-reshow-test",
            "const vscode = require('vscode');\n"
            "let hides = 0;\n"
            "function activate(context) {\n"
            "  vscode.commands.registerCommand('alien.reshow.go', () => {\n"
            "    const pick = vscode.window.createQuickPick();\n"
            "    pick.onDidHide(() => ++hides);\n"
            "    pick.onDidAccept(() => vscode.window.showInformationMessage(\n"
            "        'accepted:' + pick.selectedItems[0]));\n"
            "    pick.items = ['a'];\n"
            "    pick.show();\n"
            "    pick.hide();\n"
            "    pick.items = ['b'];\n"
            "    pick.show();\n"
            "  });\n"
            "  // Two turns: a hide that is still to come is queued before the second.\n"
            "  vscode.commands.registerCommand('alien.reshow.count', () => setTimeout(\n"
            "      () => setTimeout(() => vscode.window.showInformationMessage(\n"
            "          'hides:' + hides)), 0));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QList<int> requests;
        connect(&host, &ExtensionHost::quickPickRequested, &host,
                [&requests](int id, const QStringList &, const QString &, bool,
                            const QString &) { requests.append(id); });
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.reshow.count"),
                                 15000);
        host.executeCommand("alien.reshow.go");
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 2, 15000);

        // What the rejected first dialog answers.
        host.window().resolveQuickPick(requests.first(), {-1});
        host.executeCommand("alien.reshow.count");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.takeFirst().first().toString(), QString("hides:1"));

        host.window().resolveQuickPick(requests.last(), {0});
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.first().first().toString(), QString("accepted:b"));
    }

    void testMultiStepInputWizard()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "wizard", "alien-wizard-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.wizard.run', async () => {\n"
            "        // Step 0: a list put up before it has anything in it.\n"
            "        const loading = vscode.window.createQuickPick();\n"
            "        loading.items = [];\n"
            "        loading.busy = true;\n"
            "        loading.placeholder = 'Loading...';\n"
            "        loading.onDidTriggerButton(() => {});\n"
            "        loading.show();\n"
            "        loading.dispose();\n"
            "        // Step 1: the list, resolved from the selection event.\n"
            "        const picked = await new Promise(resolve => {\n"
            "          const pick = vscode.window.createQuickPick();\n"
            "          pick.items = [{label: 'alpha', description: 'the first',\n"
            "                         detail: 'detail one'},\n"
            "                        {label: 'beta', description: 'the second',\n"
            "                         detail: 'detail two'}];\n"
            "          pick.onDidTriggerButton(() => {});\n"
            "          pick.onDidChangeSelection(items => resolve(items[0]));\n"
            "          pick.show();\n"
            "        });\n"
            "        // Step 2: a prompt whose accept handler validates first - the\n"
            // pattern every multi-step wizard uses - and whose hide handler\n"
            // gives up. The one must not lose the race to the other.\n"
            "        const named = await new Promise((resolve, reject) => {\n"
            "          const box = vscode.window.createInputBox();\n"
            "          box.value = 'suggested';\n"
            "          box.prompt = 'Choose a name';\n"
            "          box.onDidTriggerButton(() => {});\n"
            "          box.onDidAccept(async () => {\n"
            "            const value = box.value;\n"
            "            box.enabled = false;\n"
            "            box.busy = true;\n"
            "            if (!(await Promise.resolve(undefined)))\n"
            "              resolve(value);\n"
            "          });\n"
            "          box.onDidHide(() => reject(new Error('cancelled')));\n"
            "          box.show();\n"
            "        });\n"
            "        vscode.window.showInformationMessage(\n"
            "            'wizard:' + (picked && picked.label) + ':'\n"
            "            + (picked && picked.detail) + ':' + named);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        int shown = 0;
        connect(&host, &ExtensionHost::quickPickRequested, &host,
                [&host, &shown](int id, const QStringList &items, const QString &, bool,
                                const QString &) {
                    ++shown;
                    // The empty loading list is dismissed by the extension
                    // itself; the real one is answered with "beta".
                    if (items.isEmpty())
                        return;
                    host.window().resolveQuickPick(id, {int(items.indexOf(
                        items.filter("beta").value(0)))});
                });
        connect(&host, &ExtensionHost::inputBoxRequested, &host,
                [&host](int id, const QString &, const QString &value, const QString &, bool) {
                    // Answered with what was suggested, as pressing Return does.
                    host.window().resolveInputBox(id, value, true);
                });

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        // A list the extension takes back off the screen says so, or it stays
        // up with nobody to answer it.
        QSignalSpy hidden(&host, &ExtensionHost::quickPickHidden);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.wizard.run"), 15000);
        host.executeCommand("alien.wizard.run");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 20000);
        // Every step handed its answer to the next.
        QCOMPARE(messages.first().first().toString(),
                 QString("wizard:beta:detail two:suggested"));
        QVERIFY2(shown >= 2, qPrintable(QString("only %1 lists were shown").arg(shown)));
        QTRY_VERIFY_WITH_TIMEOUT(!hidden.isEmpty(), 10000);
    }

    // Starting a debug session on what an extension asks for: the
    // configuration is resolved by the extension's own provider first, then
    // Qt Creator debugs what it names.
    void testStartDebuggingFromExtension()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");
        if (!ProjectExplorer::KitManager::defaultKit())
            QSKIP("no kit to debug with");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath program = FilePath("sleep").searchInPath();
        if (!program.isExecutableFile())
            QSKIP("no program to debug");

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "dbg", "alien-startdebug-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider(\n"
            "      'aliendbg', {\n"
            "        // What the extension fills in before a session starts.\n"
            "        resolveDebugConfiguration: (folder, config) => Object.assign({}, config,\n"
            "            {program: '" + program.toFSPathString() + "', args: ['30']}),\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.dbg.start', async () => {\n"
            "        const ok = await vscode.debug.startDebugging(undefined,\n"
            "            {type: 'aliendbg', name: 'Alien Debug', request: 'launch'});\n"
            "        vscode.window.showInformationMessage('started:' + ok + ':'\n"
            "            + ((vscode.debug.activeDebugSession || {}).name));\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.debug.onDidTerminateDebugSession(\n"
            "      session => vscode.window.showInformationMessage(\n"
            "          'terminated:' + session.name)));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.dbg.stop', async () => {\n"
            "        await vscode.debug.stopDebugging();\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.dbg.nothing', async () => {\n"
            "        // A configuration naming no program cannot be debugged.\n"
            "        const ok = await vscode.debug.startDebugging(undefined,\n"
            "            {type: 'nobody', name: 'Nothing', request: 'launch'});\n"
            "        vscode.window.showInformationMessage('nothing:' + ok);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.dbg.start"), 15000);

        // A configuration with nothing to debug is refused, not started.
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.dbg.nothing");
        const auto said = [&messages](const QString &prefix) {
            for (const QList<QVariant> &arguments : messages) {
                if (arguments.at(0).toString().startsWith(prefix))
                    return arguments.at(0).toString();
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!said("nothing:").isEmpty(), 15000);
        QCOMPARE(said("nothing:"), QString("nothing:false"));
        QCOMPARE(host.run().debugSessionCount(), 0);

        // And one the extension filled in is debugged.
        host.executeCommand("alien.dbg.start");
        QTRY_VERIFY_WITH_TIMEOUT(!said("started:").isEmpty(), 20000);
        QCOMPARE(said("started:"), QString("started:true:Alien Debug"));
        QCOMPARE(host.run().debugSessionCount(), 1);

        // Stopping it is reported back, so the extension hears it ended rather
        // than believing it is still debugging.
        host.executeCommand("alien.dbg.stop");
        QTRY_COMPARE_WITH_TIMEOUT(host.run().debugSessionCount(), 0, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!said("terminated:").isEmpty(), 20000);
        QCOMPARE(said("terminated:"), QString("terminated:Alien Debug"));
    }

    // How an adapter named in a manifest is turned into something to run: the
    // program is the extension's own, and what runs it is either the node the
    // host uses or whatever the manifest asked for by name.
    void testManifestAdapterResolution()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const auto declaring = [&root](const QString &name, const QJsonObject &debugger) {
            return writeMockExtension(root / name, "alien-" + name + "-test",
                                      "function activate() {}\n"
                                      "module.exports = { activate };\n",
                                      QJsonArray{"*"},
                                      QJsonObject{{"debuggers", QJsonArray{debugger}}});
        };
        const Result<VscodeManifest> viaNode = declaring(
            "vianode", QJsonObject{{"type", "aliennode"}, {"label", "N"},
                                   {"program", "adapter.js"}, {"runtime", "node"}});
        const Result<VscodeManifest> viaNamed = declaring(
            "vianamed", QJsonObject{{"type", "aliennamed2"}, {"label", "M"},
                                    {"program", "adapter.js"}, {"runtime", "nodejs"}});
        const Result<VscodeManifest> viaProgram = declaring(
            "viaprogram", QJsonObject{{"type", "alienbare"}, {"label", "B"},
                                      {"program", "adapter"}});
        QVERIFY(viaNode.has_value());
        QVERIFY(viaNamed.has_value());
        QVERIFY(viaProgram.has_value());

        ExtensionHost host(node);
        host.setKnownExtensions({*viaNode, *viaNamed, *viaProgram});

        // A runtime of node is the one the host itself runs under.
        const QJsonObject byNode = host.manifestAdapterFor("aliennode");
        QCOMPARE(byNode.value("kind").toString(), QString("executable"));
        QCOMPARE(byNode.value("command").toString(), settings().nodeJsPath().toFSPathString());
        QCOMPARE(byNode.value("args").toArray().size(), 1);
        QCOMPARE(byNode.value("args").toArray().first().toString(),
                 (root / "vianode" / "adapter.js").toFSPathString());

        // Any other runtime is looked for by name, which is what an adapter
        // written in something else needs.
        const FilePath nodejs = FilePath("nodejs").searchInPath();
        if (nodejs.isExecutableFile()) {
            const QJsonObject byName = host.manifestAdapterFor("aliennamed2");
            QCOMPARE(byName.value("command").toString(), nodejs.toFSPathString());
            QCOMPARE(byName.value("args").toArray().first().toString(),
                     (root / "vianamed" / "adapter.js").toFSPathString());
        }

        // A program with no runtime is run directly, with nothing in front.
        const QJsonObject byProgram = host.manifestAdapterFor("alienbare");
        QCOMPARE(byProgram.value("command").toString(),
                 (root / "viaprogram" / "adapter").toFSPathString());
        QVERIFY(!byProgram.contains("args"));

        // A type nobody declared has no adapter, which is what sends the
        // session to the debugger of the kit instead.
        QVERIFY(host.manifestAdapterFor("nobodydeclaredthis").isEmpty());
    }

    void testDebugsThroughAProvidedAdapter()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");
        if (!ProjectExplorer::KitManager::defaultKit())
            QSKIP("no kit to debug with");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath shared = root / "adapter.js";
        QVERIFY(QFile::copy(":/alien/host/mockdapadapter/adapter.js", shared.toFSPathString()));

        const Result<VscodeManifest> byFactory = writeMockExtension(
            root / "factory", "alien-dapfactory-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory(\n"
            "      'alienfactory', {\n"
            "        createDebugAdapterDescriptor: () => new vscode.DebugAdapterExecutable(\n"
            "            '" + node.toFSPathString() + "',\n"
            "            ['" + shared.toFSPathString() + "']),\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.debug.onDidTerminateDebugSession(\n"
            "      session => vscode.window.showInformationMessage('ended:' + session.name)));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.factory.start', async () => {\n"
            "        const ok = await vscode.debug.startDebugging(undefined,\n"
            "            {type: 'alienfactory', name: 'Factory', request: 'launch'});\n"
            "        vscode.window.showInformationMessage('factory:' + ok);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(byFactory.has_value(), qPrintable(byFactory ? QString() : byFactory.error()));

        ExtensionHost host(node);
        host.activate(*byFactory);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.factory.start"), 15000);

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        const auto said = [&messages](const QString &prefix) {
            for (const QList<QVariant> &arguments : messages) {
                if (arguments.at(0).toString().startsWith(prefix))
                    return arguments.at(0).toString();
            }
            return QString();
        };

        host.executeCommand("alien.factory.start");
        QTRY_VERIFY_WITH_TIMEOUT(!said("factory:").isEmpty(), 20000);
        QCOMPARE(said("factory:"), QString("factory:true"));
        // The adapter ends the session the moment it has launched, and the
        // extension hears about it: nothing else could have said so.
        QTRY_VERIFY_WITH_TIMEOUT(!said("ended:").isEmpty(), 20000);
        QCOMPARE(said("ended:"), QString("ended:Factory"));
        QTRY_COMPARE_WITH_TIMEOUT(host.run().debugSessionCount(), 0, 20000);

        // The same again for an extension that registers nothing and only says
        // in its manifest what its adapter is, which is the usual way.
        const FilePath named = root / "named";
        const Result<VscodeManifest> byManifest = writeMockExtension(
            named, "alien-dapmanifest-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.debug.onDidTerminateDebugSession(\n"
            "      session => vscode.window.showInformationMessage('over:' + session.name)));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.named.start', async () => {\n"
            "        const ok = await vscode.debug.startDebugging(undefined,\n"
            "            {type: 'aliennamed', name: 'Named', request: 'launch'});\n"
            "        vscode.window.showInformationMessage('named:' + ok);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"debuggers", QJsonArray{QJsonObject{
                {"type", "aliennamed"},
                {"label", "Alien Named"},
                {"program", "adapter.js"},
                {"runtime", "node"}}}}});
        QVERIFY2(byManifest.has_value(), qPrintable(byManifest ? QString() : byManifest.error()));
        // The manifest names it relative to the extension, so that is where it
        // has to be found.
        QVERIFY(QFile::copy(":/alien/host/mockdapadapter/adapter.js",
                            (named / "adapter.js").toFSPathString()));

        host.setKnownExtensions({*byManifest});
        host.activate(*byManifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.named.start"), 15000);

        host.executeCommand("alien.named.start");
        QTRY_VERIFY_WITH_TIMEOUT(!said("named:").isEmpty(), 20000);
        QCOMPARE(said("named:"), QString("named:true"));
        QTRY_VERIFY_WITH_TIMEOUT(!said("over:").isEmpty(), 20000);
        QCOMPARE(said("over:"), QString("over:Named"));
        QTRY_COMPARE_WITH_TIMEOUT(host.run().debugSessionCount(), 0, 20000);
    }

    void testAnExtensionDrivesItsOwnTerminal()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "pty", "alien-pty-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const writer = new vscode.EventEmitter();\n"
            "  const pty = {\n"
            "    onDidWrite: writer.event,\n"
            "    open: () => writer.fire('opened\\r\\n'),\n"
            "    close: () => vscode.window.showInformationMessage('closed'),\n"
            "    handleInput: data =>\n"
            "        vscode.window.showInformationMessage('typed:' + data.trim()),\n"
            "  };\n"
            "  context.subscriptions.push(\n"
            "      vscode.window.createTerminal({name: 'Alien Console', pty}));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy opened(&host, &ExtensionHost::extensionTerminalOpened);
        QSignalSpy output(&host, &ExtensionHost::extensionTerminalOutput);
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);

        // The terminal is the extension's own, so Qt Creator hears of it
        // rather than starting anything.
        QTRY_VERIFY_WITH_TIMEOUT(!opened.isEmpty(), 15000);
        const int id = opened.first().at(0).toInt();
        QCOMPARE(opened.first().at(1).toString(), QString("Alien Console"));

        // open() runs before the terminal has an id, and what it wrote then is
        // still what the terminal has to show.
        QTRY_VERIFY_WITH_TIMEOUT(!output.isEmpty(), 15000);
        QCOMPARE(output.first().at(0).toInt(), id);
        QCOMPARE(output.first().at(1).toString(), QString("opened\r\n"));

        // And what is typed into it reaches the extension behind it.
        host.run().sendTerminalInput(id, "hello\n");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.first().at(0).toString(), QString("typed:hello"));

        // Its tab closed, the extension hears of it, and the terminal is gone
        // for whatever the extension still sends it.
        QSignalSpy closed(&host, &ExtensionHost::extensionTerminalClosed);
        AlienTerminalPane pane(nullptr);
        pane.open(id, "Alien Console", &host);
        auto tabs = qobject_cast<QTabWidget *>(pane.outputWidget(nullptr));
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1);
        emit tabs->tabCloseRequested(0);
        QCOMPARE(tabs->count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!closed.isEmpty(), 15000);
        QCOMPARE(closed.first().at(0).toInt(), id);
        QCOMPARE(messages.last().at(0).toString(), QString("closed"));
        pane.write(id, "late");
        pane.show(id);
        pane.close(id);
    }

    void testARefusingConfigurationProviderStopsTheSession()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath adapter = root / "adapter.js";
        QVERIFY(QFile::copy(":/alien/host/mockdapadapter/adapter.js", adapter.toFSPathString()));

        // A provider answering with nothing refuses the session - cortex-debug
        // does that while its server console is still coming up. Starting one
        // anyway runs an adapter against a configuration it was refused.
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "refuse", "alien-daprefuse-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory(\n"
            "      'alienrefuse', {\n"
            "        createDebugAdapterDescriptor: () => new vscode.DebugAdapterExecutable(\n"
            "            '" + node.toFSPathString() + "',\n"
            "            ['" + adapter.toFSPathString() + "']),\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider(\n"
            "      'alienrefuse', {resolveDebugConfiguration: () => undefined}));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.refuse.start', async () => {\n"
            "        const ok = await vscode.debug.startDebugging(undefined,\n"
            "            {type: 'alienrefuse', name: 'Refused', request: 'launch'});\n"
            "        vscode.window.showInformationMessage('refused:' + ok);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.refuse.start"), 15000);

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.refuse.start");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 20000);
        QCOMPARE(messages.first().at(0).toString(), QString("refused:false"));
        // The answer is in, so any session there was to start would be here.
        QCOMPARE(host.run().debugSessionCount(), 0);
    }

    void testCustomRequestReachesTheAdapter()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");
        if (!ProjectExplorer::KitManager::defaultKit())
            QSKIP("no kit to debug with");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath adapter = root / "adapter.js";
        QVERIFY(QFile::copy(":/alien/host/mockdapadapter/adapter.js", adapter.toFSPathString()));

        // What cortex-debug does the moment a session starts: ask the adapter
        // for the arguments it was launched with, and read a field off the
        // answer. An answer of nothing is what used to end the session there.
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "custom", "alien-dapcustom-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory(\n"
            "      'aliencustom', {\n"
            "        createDebugAdapterDescriptor: () => new vscode.DebugAdapterExecutable(\n"
            "            '" + node.toFSPathString() + "',\n"
            "            ['" + adapter.toFSPathString() + "']),\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.custom.start', async () => {\n"
            "        await vscode.debug.startDebugging(undefined, {type: 'aliencustom',\n"
            "            name: 'Custom', request: 'launch', stayAlive: true,\n"
            "            svdFile: 'from-the-configuration'});\n"
            "        const answer = await vscode.debug.activeDebugSession.customRequest(\n"
            "            'get-arguments');\n"
            "        vscode.window.showInformationMessage(\n"
            "            'asked:' + (answer && answer.svdFile));\n"
            "        await vscode.debug.activeDebugSession.customRequest('finish');\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.custom.start"), 15000);

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        const auto said = [&messages](const QString &prefix) {
            for (const QList<QVariant> &arguments : messages) {
                if (arguments.at(0).toString().startsWith(prefix))
                    return arguments.at(0).toString();
            }
            return QString();
        };

        host.executeCommand("alien.custom.start");
        QTRY_VERIFY_WITH_TIMEOUT(!said("asked:").isEmpty(), 30000);
        // Only the adapter knows what it was launched with, so this came from
        // it and nowhere else.
        QCOMPARE(said("asked:"), QString("asked:from-the-configuration"));
        QTRY_COMPARE_WITH_TIMEOUT(host.run().debugSessionCount(), 0, 20000);
    }

    void testDocumentFormatting()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "unformatted.alienlang";
        QVERIFY(source.writeFileContents("messy   text\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "fmt", "alien-fmt-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push("
            "vscode.languages.registerDocumentFormattingEditProvider(\n"
            "      {language: 'alienlang'}, {\n"
            "    provideDocumentFormattingEdits(document, options) {\n"
            "      const line = document.lineAt(0);\n"
            "      return [vscode.TextEdit.replace(line.range,\n"
            "          line.text.replace(/\\s+/g, ' ') + ' tab' + options.tabSize)];\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienlang"}, {"extensions", QJsonArray{".alienlang"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        // The provider has to be registered before the document is opened, or
        // nothing attaches the formatter to it.
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().formattingLanguageIds().contains("alienlang"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        document->autoFormat(QTextCursor(document->document()));
        // The extension answers over the connection, so the edits arrive after
        // the call that asked for them.
        QTRY_COMPARE_WITH_TIMEOUT(document->plainText(), QString("messy text tab4\n"), 15000);

        Core::EditorManager::closeEditors({editor}, false);
    }

    // Edits computed for the text before the user typed on would land in the
    // wrong places of the text after it.
    void testFormattingOfEditedText()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "late.alienlang";
        QVERIFY(source.writeFileContents("hello world\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "late", "alien-late-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  let release;\n"
            "  vscode.commands.registerCommand('alien.late.release', () => {\n"
            "    release([vscode.TextEdit.replace(new vscode.Range(0, 0, 0, 5), 'HELLO')]);\n"
            "    setTimeout(() => vscode.commands.registerCommand(\n"
            "        'alien.late.answered', () => {}), 0);\n"
            "  });\n"
            "  vscode.languages.registerDocumentFormattingEditProvider(\n"
            "      {language: 'alienlang'}, {\n"
            "    provideDocumentFormattingEdits() {\n"
            "      vscode.commands.registerCommand('alien.late.asked', () => {});\n"
            "      return new Promise(resolve => { release = resolve; });\n"
            "    }\n"
            "  });\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienlang"}, {"extensions", QJsonArray{".alienlang"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.editor().formattingLanguageIds().contains("alienlang"),
                                 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        document->autoFormat(QTextCursor(document->document()));
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.late.asked"), 15000);
        QTextCursor(document->document()).insertText("ab");
        host.executeCommand("alien.late.release");
        // Registered after the answer was sent, so the answer is in.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.late.answered"),
                                 15000);
        QCOMPARE(document->plainText(), QString("abhello world\n"));

        Core::EditorManager::closeEditors({editor}, false);
    }

    void testQuickPickAndInputBoxObjects()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "pick", "alien-pick-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Pick');\n"
            "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.pick.go',\n"
            "      () => {\n"
            "    const pick = vscode.window.createQuickPick();\n"
            "    pick.title = 'Pick one';\n"
            "    pick.items = [{label: 'first'}, {label: 'second'}];\n"
            "    pick.onDidChangeSelection(items => say('selected', items[0].label));\n"
            "    pick.onDidAccept(() => say('accepted', pick.selectedItems.length));\n"
            "    pick.onDidHide(() => { say('hidden', true); pick.dispose(); });\n"
            "    pick.show();\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.box.go',\n"
            "      () => {\n"
            "    const box = vscode.window.createInputBox();\n"
            "    box.prompt = 'Say something';\n"
            "    box.onDidAccept(() => { say('typed', box.value); box.dispose(); });\n"
            "    box.show();\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        // Answer as the user would, from outside the extension.
        connect(&host, &ExtensionHost::quickPickRequested, &host,
                [&host](int id, const QStringList &items, const QString &) {
                    host.window().resolveQuickPick(id, {int(items.indexOf("second"))});
                });
        connect(&host, &ExtensionHost::inputBoxRequested, &host,
                [&host](int id, const QString &, const QString &, const QString &) {
                    host.window().resolveInputBox(id, "an answer", true);
                });
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.pick.go"), 15000);

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };

        host.executeCommand("alien.pick.go");
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("hidden=true"), 15000);
        // The events an extension builds a multi-step flow out of, in order.
        QCOMPARE(reported, QStringList({"selected=second", "accepted=1", "hidden=true"}));

        host.executeCommand("alien.box.go");
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("typed=an answer"), 15000);
    }

    void testEditorDecorations()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "marked.txt";
        QVERIFY(source.writeFileContents("alpha beta\ngamma\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "deco", "alien-deco-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  const channel = vscode.window.createOutputChannel('Alien Deco');\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand("
                    "'alien.deco.go', async () => {\n"
                    "    const doc = await vscode.workspace.openTextDocument('%1');\n"
                    "    const editor = await vscode.window.showTextDocument(doc);\n"
                    "    const type = vscode.window.createTextEditorDecorationType("
                    "{backgroundColor: '#112233', fontWeight: 'bold'});\n"
                    "    editor.setDecorations(type, [new vscode.Range("
                    "new vscode.Position(0, 6), new vscode.Position(0, 10))]);\n"
                    "    channel.appendLine('marked');\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(source.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.deco.go"), 15000);
        host.executeCommand("alien.deco.go");

        const auto marked = [&source]() -> QList<QTextEdit::ExtraSelection> {
            TextEditor::TextDocument *document
                = TextEditor::TextDocument::textDocumentForFilePath(source);
            if (!document)
                return {};
            const QList<TextEditor::TextEditorWidget *> widgets
                = TextEditor::TextEditorWidget::textEditorWidgetsForDocument(document);
            if (widgets.isEmpty())
                return {};
            // Every decoration type gets a kind of its own, so find whichever
            // one the extension's type ended up under.
            for (int key = 1; key < 8; ++key) {
                const QList<QTextEdit::ExtraSelection> selections = widgets.first()->extraSelections(
                    Id::fromString(QString("alien-decoration-%1").arg(key)));
                if (!selections.isEmpty())
                    return selections;
            }
            return {};
        };
        QTRY_VERIFY_WITH_TIMEOUT(!marked().isEmpty(), 15000);

        // The range the extension asked for, with what it asked for on it.
        const QTextEdit::ExtraSelection selection = marked().first();
        QCOMPARE(selection.cursor.selectedText(), QString("beta"));
        QCOMPARE(selection.format.background().color(), QColor("#112233"));
        QCOMPARE(selection.format.fontWeight(), int(QFont::Bold));

        host.shutdown();
        QVERIFY(marked().isEmpty());

        if (Core::IEditor *editor = Utils::findOrDefault(
                Core::EditorManager::visibleEditors(), [&source](Core::IEditor *candidate) {
                    return candidate->document()->filePath() == source;
                })) {
            Core::EditorManager::closeEditors({editor}, false);
        }
    }

    // A range revealed at the top is the first line in view, not one in the
    // middle.
    void testRevealRangeAtTop()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "long.txt";
        QString text;
        for (int line = 0; line < 400; ++line)
            text += QString("line %1\n").arg(line);
        QVERIFY(source.writeFileContents(text.toUtf8()).has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "reveal", "alien-reveal-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand("
                    "'alien.reveal.go', async () => {\n"
                    "    const doc = await vscode.workspace.openTextDocument('%1');\n"
                    "    const editor = await vscode.window.showTextDocument(doc);\n"
                    "    const at = new vscode.Position(200, 0);\n"
                    "    await editor.revealRange(new vscode.Range(at, at),\n"
                    "                             vscode.TextEditorRevealType.AtTop);\n"
                    "    vscode.window.showInformationMessage('revealed');\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(source.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.reveal.go"), 15000);
        host.executeCommand("alien.reveal.go");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.first().first().toString(), QString("revealed"));

        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(
            EditorManager::currentEditor());
        QVERIFY(textEditor);
        QCOMPARE(textEditor->document()->filePath(), source);
        QCOMPARE(textEditor->editorWidget()->firstVisibleBlockNumber(), 200);

        EditorManager::closeAllEditors(false);
    }

    void testEditorWriteApi()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "written.txt";
        QVERIFY(source.writeFileContents("one\ntwo\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "editor", "alien-editor-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  const channel = vscode.window.createOutputChannel('Alien Editor');\n"
                    "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand("
                    "'alien.editor.go', async () => {\n"
                    "    const doc = await vscode.workspace.openTextDocument('%1');\n"
                    "    const editor = await vscode.window.showTextDocument(doc);\n"
                    "    say('hasSelection', editor.selection.active.line);\n"
                    "    const done = await editor.edit(builder => {\n"
                    "      builder.insert(new vscode.Position(0, 0), 'zero\\n');\n"
                    "      builder.replace(new vscode.Range(new vscode.Position(1, 0),\n"
                    "                                      new vscode.Position(1, 3)), 'ONE');\n"
                    "    });\n"
                    "    say('edited', done);\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(source.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.editor.go"), 15000);
        host.executeCommand("alien.editor.go");

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("edited=true"), 15000);

        // An editor comes with a selection, before the cursor has moved.
        QCOMPARE(reported.first(), QString("hasSelection=0"));
        // Both edits landed, each placed by the offsets the extension gave:
        // one edit() is computed against the document as it was, so the
        // replacement hits the line the extension counted, not the line the
        // insertion moved into its place.
        Core::IEditor *editor = Utils::findOrDefault(
            Core::EditorManager::visibleEditors(), [&source](Core::IEditor *candidate) {
                return candidate->document()->filePath() == source;
            });
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);
        QCOMPARE(document->plainText(), QString("zero\none\nONE\n"));
        // Without asking: the document is modified, and a dialog here would
        // wait for an answer nobody is going to give.
        Core::EditorManager::closeEditors({editor}, false);
    }

    void testFileSystemErrors()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        QVERIFY((root / "there.txt").writeFileContents("x").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "fserr", "alien-fserr-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  const channel = vscode.window.createOutputChannel('Alien Fs');\n"
                    "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
                    "  const base = '%1';\n"
                    "  (async () => {\n"
                    "    try {\n"
                    "      await vscode.workspace.fs.stat(vscode.Uri.file(base + '/nope.txt'));\n"
                    "      say('missing', 'no error');\n"
                    "    } catch (e) {\n"
                    "      say('missing', e.code);\n"
                    "      say('isType', e instanceof vscode.FileSystemError);\n"
                    "    }\n"
                    "    try {\n"
                    "      await vscode.workspace.fs.readDirectory(\n"
                    "          vscode.Uri.file(base + '/there.txt'));\n"
                    "      say('notDir', 'no error');\n"
                    "    } catch (e) { say('notDir', e.code); }\n"
                    "    say('thrown', vscode.FileSystemError.FileExists(\n"
                    "        vscode.Uri.file(base)).code);\n"
                    "    const stat = await vscode.workspace.fs.stat(\n"
                    "        vscode.Uri.file(base + '/there.txt'));\n"
                    "    say('fileType', stat.type);\n"
                    "  })();\n"
                    "  context.subscriptions.push(channel);\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(root.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 5 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        QStringList reported;
        for (const QList<QVariant> &args : output)
            reported.append(args.at(1).toString());
        // What an extension branches on: the code says which failure it was,
        // not which errno the file system used for it.
        QCOMPARE(reported,
                 QStringList({"missing=FileNotFound", "isType=true", "notDir=FileNotADirectory",
                              "thrown=FileExists", "fileType=1"}));
    }

    void testDisposableAndEmitterContract()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "events", "alien-events-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Events');\n"
            "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
            "  let ran = 0;\n"
            "  const one = new vscode.Disposable(() => { ran += 1; });\n"
            "  one.dispose(); one.dispose(); one.dispose();\n"
            "  say('onceOnly', ran);\n"
            "  let parts = 0;\n"
            "  const both = vscode.Disposable.from(new vscode.Disposable(() => { parts += 1; }),\n"
            "                                     new vscode.Disposable(() => { parts += 1; }));\n"
            "  both.dispose(); both.dispose();\n"
            "  say('fromOnce', parts);\n"
            "  const emitter = new vscode.EventEmitter();\n"
            "  let heard = 0;\n"
            "  emitter.event(() => { heard += 1; });\n"
            "  emitter.fire('before');\n"
            "  emitter.dispose();\n"
            "  emitter.fire('after');\n"
            "  say('heard', heard);\n"
            "  const other = new vscode.EventEmitter();\n"
            "  let counted = 0;\n"
            "  const subscription = other.event(() => { counted += 1; });\n"
            "  subscription.dispose();\n"
            "  other.fire('ignored');\n"
            "  say('unsubscribed', counted);\n"
            "  context.subscriptions.push(channel);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 4 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        QStringList reported;
        for (const QList<QVariant> &args : output)
            reported.append(args.at(1).toString());
        QCOMPARE(reported,
                 QStringList({// Disposing twice is what extensions do; it has to
                              // mean what disposing once meant.
                              "onceOnly=1", "fromOnce=2",
                              // A disposed emitter has no listeners left.
                              "heard=1", "unsubscribed=0"}));
    }

    void testDocumentTextApi()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "probe.cpp";
        QVERIFY(source.writeFileContents("int alpha = 1;\n  beta();\nlast").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "doc", "alien-doc-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  const channel = vscode.window.createOutputChannel('Alien Doc');\n"
                    "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
                    "  vscode.workspace.openTextDocument('%1').then(doc => {\n"
                    "    say('whole', doc.getText().length);\n"
                    "    say('ranged', doc.getText(new vscode.Range(new vscode.Position(0, 4),\n"
                    "                                              new vscode.Position(0, 9))));\n"
                    "    say('byNumber', doc.lineAt(1).text);\n"
                    "    say('byPosition', doc.lineAt(new vscode.Position(1, 3)).text);\n"
                    "    say('indent', doc.lineAt(1).firstNonWhitespaceCharacterIndex);\n"
                    "    say('lineEnd', doc.lineAt(1).range.end.character);\n"
                    "    const word = doc.getWordRangeAtPosition(new vscode.Position(0, 6));\n"
                    "    say('word', doc.getText(word));\n"
                    "    say('noWord', doc.getWordRangeAtPosition(new vscode.Position(1, 1)));\n"
                    "    say('clamped', doc.validatePosition(new vscode.Position(99, 99)).line);\n"
                    "  });\n"
                    "  context.subscriptions.push(channel);\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(source.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 9 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        QStringList reported;
        for (const QList<QVariant> &args : output)
            reported.append(args.at(1).toString());
        QCOMPARE(reported,
                 QStringList({"whole=29",
                              // A range asks for that range, not for the file.
                              "ranged=alpha",
                              "byNumber=  beta();", "byPosition=  beta();",
                              "indent=2", "lineEnd=9",
                              // What a hover is asked about.
                              "word=alpha", "noWord=undefined", "clamped=2"}));
    }

    void testUriSemantics()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "uri", "alien-uri-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Uri');\n"
            "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
            "  say('drivePath', vscode.Uri.file('C:\\\\x\\\\y').path);\n"
            "  say('driveFs', vscode.Uri.parse('file:///c:/x/y').fsPath);\n"
            "  say('driveUri', vscode.Uri.file('C:\\\\x').toString());\n"
            "  say('uncHost', vscode.Uri.file('//srv/share/x').authority);\n"
            "  say('uncFs', vscode.Uri.parse('file://srv/share/x').fsPath);\n"
            "  say('untitled', vscode.Uri.parse('untitled:Untitled-1').toString());\n"
            "  say('escaped', vscode.Uri.file('/a b/c').toString());\n"
            "  say('unescaped', vscode.Uri.parse('file:///a%20b').path);\n"
            "  say('roundtrip', vscode.Uri.parse(vscode.Uri.file('/a b/c').toString()).fsPath);\n"
            "  say('fromParts', vscode.Uri.from({scheme: 'git', path: '/a', query: 'q'}));\n"
            // A class of the extension's own deriving from Uri hands the URI
            // it was made from straight up to the constructor.
            "  class GitUri extends vscode.Uri { constructor(uri) { super(uri); } }\n"
            "  say('derived', new GitUri(vscode.Uri.file('/a b/c')).fsPath);\n"
            "  context.subscriptions.push(channel);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 11 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        QStringList reported;
        for (const QList<QVariant> &args : output)
            reported.append(args.at(1).toString());
        // The path of a URI is not the name of the file: a Windows drive letter
        // is preceded by a slash there and not here, and a UNC host lives in
        // the authority until a path wants it back. Windows itself only adds
        // the backslashes; these are what a host anywhere reports.
        QCOMPARE(reported,
                 QStringList({"drivePath=/C:/x/y", "driveFs=c:/x/y", "driveUri=file:///C:/x",
                              "uncHost=srv", "uncFs=//srv/share/x",
                              // An authority is what makes the double slash.
                              "untitled=untitled:Untitled-1",
                              "escaped=file:///a%20b/c", "unescaped=/a b",
                              "roundtrip=/a b/c", "fromParts=git:/a?q",
                              "derived=/a b/c"}));
    }

    void testWorkspaceEditFileOperations()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath doomed = root / "doomed.txt";
        const FilePath oldName = root / "old.txt";
        QVERIFY(doomed.writeFileContents("gone soon").has_value());
        QVERIFY(oldName.writeFileContents("keep me").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "files", "alien-fileops-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  const channel = vscode.window.createOutputChannel('Alien Files');\n"
                    "  const base = '%1';\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand("
                    "'alien.files.go', async () => {\n"
                    "    const made = vscode.Uri.file(base + '/made.txt');\n"
                    "    const edit = new vscode.WorkspaceEdit();\n"
                    "    edit.createFile(made);\n"
                    "    edit.insert(made, new vscode.Position(0, 0), 'written into a new file');\n"
                    "    edit.deleteFile(vscode.Uri.file(base + '/doomed.txt'));\n"
                    "    edit.renameFile(vscode.Uri.file(base + '/old.txt'),\n"
                    "                    vscode.Uri.file(base + '/new.txt'));\n"
                    "    channel.appendLine('size=' + edit.size);\n"
                    "    channel.appendLine('applied=' + await vscode.workspace.applyEdit(edit));\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(root.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.files.go"), 15000);
        host.executeCommand("alien.files.go");

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("applied=true"), 15000);

        // The file operations count towards the edit, and were all carried out.
        QCOMPARE(reported.first(), QString("size=4"));
        QVERIFY(!doomed.exists());
        QVERIFY(!oldName.exists());
        QCOMPARE((root / "new.txt").fileContents().value_or(QByteArray()), QByteArray("keep me"));
        // In order: the file was created before the edit that writes into it.
        QCOMPARE((root / "made.txt").fileContents().value_or(QByteArray()),
                 QByteArray("written into a new file"));
    }

    void testValueTypeSemantics()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "values", "alien-values-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Values');\n"
            "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
            "  const snippet = new vscode.SnippetString();\n"
            "  snippet.appendText('a$b').appendPlaceholder('name').appendTabstop()\n"
            "         .appendChoice(['x', 'y']).appendVariable('TM_FILENAME');\n"
            "  say('snippet', snippet.value);\n"
            "  const md = new vscode.MarkdownString();\n"
            "  md.appendCodeblock('int x;', 'cpp');\n"
            "  say('code', JSON.stringify(md.value));\n"
            "  const source = new vscode.CancellationTokenSource();\n"
            "  let fired = 0;\n"
            "  source.token.onCancellationRequested(() => { fired += 1; });\n"
            "  source.cancel();\n"
            "  say('cancelled', source.token.isCancellationRequested);\n"
            "  say('notified', fired);\n"
            "  say('fileIcon', vscode.ThemeIcon.File.id);\n"
            "  context.subscriptions.push(channel);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 5 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        QStringList reported;
        for (const QList<QVariant> &args : output)
            reported.append(args.at(1).toString());
        QCOMPARE(reported,
                 QStringList({
                     // A snippet is syntax: placeholders and tabstops are
                     // numbered, and literal text is escaped against it.
                     "snippet=a\\$b${1:name}$2${3|x,y|}${TM_FILENAME}",
                     "code=\"\\n```cpp\\nint x;\\n```\\n\"",
                     // Cancelling tells whoever asked to be told.
                     "cancelled=true", "notified=1",
                     "fileIcon=file"}));
    }

    void testPositionAndRangeSemantics()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // These are value types an extension does arithmetic with, so a missing
        // method is not a missing feature but a TypeError in the middle of one.
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "geom", "alien-geom-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Geom');\n"
            "  const p = new vscode.Position(1, 5), q = new vscode.Position(3, 2);\n"
            "  const r = new vscode.Range(p, q);\n"
            "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
            "  say('before', p.isBefore(q));\n"
            "  say('after', p.isAfter(q));\n"
            "  say('cmp', q.compareTo(p));\n"
            "  say('swapped', new vscode.Range(q, p).start.line);\n"
            "  say('contains', r.contains(new vscode.Position(2, 0)));\n"
            "  say('outside', r.contains(new vscode.Position(9, 0)));\n"
            "  say('equal', r.isEqual(new vscode.Range(p, q)));\n"
            "  say('cut', r.intersection(new vscode.Range(new vscode.Position(2, 0), q)).start.line);\n"
            "  say('apart', r.intersection(new vscode.Range(new vscode.Position(8, 0),\n"
            "                                              new vscode.Position(9, 0))));\n"
            "  say('joined', r.union(new vscode.Range(new vscode.Position(0, 0), p)).start.line);\n"
            "  say('reversed', new vscode.Selection(q, p).isReversed);\n"
            "  say('cursor', new vscode.Selection(q, p).active.line);\n"
            "  context.subscriptions.push(channel);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 12 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        QStringList reported;
        for (const QList<QVariant> &args : output)
            reported.append(args.at(1).toString());
        QCOMPARE(reported,
                 QStringList({"before=true", "after=false", "cmp=1",
                              // Ends the wrong way round describe the same range.
                              "swapped=1", "contains=true", "outside=false", "equal=true",
                              "cut=2", "apart=undefined", "joined=0",
                              // A backwards selection keeps its direction.
                              "reversed=true", "cursor=1"}));
    }

    void testOutputPaneFilters()
    {
        AlienOutputPane pane(nullptr);
        QVERIFY(pane.hasFilterContext());
        QVERIFY(!pane.outputWindows().isEmpty());
        Core::OutputWindow *window = pane.outputWindows().first();

        pane.append("channel", "keep me", true);
        pane.append("channel", "drop me", true);

        const auto visibleLines = [window] {
            QStringList lines;
            for (QTextBlock block = window->document()->begin(); block != window->document()->end();
                 block = block.next()) {
                if (block.isVisible() && !block.text().isEmpty())
                    lines.append(block.text());
            }
            return lines;
        };
        // Appended output is queued behind a timer, so wait for the window to
        // have it before asking what the filter does to it.
        QTRY_COMPARE_WITH_TIMEOUT(visibleLines(),
                                  QStringList({"[channel] keep me", "[channel] drop me"}), 15000);

        // The pane sets up the filter UI, so what is typed there has to reach
        // the window - unimplemented, the base class only asserts.
        QLineEdit *filter = nullptr;
        for (QWidget *widget : pane.toolBarWidgets()) {
            if (auto lineEdit = qobject_cast<QLineEdit *>(widget))
                filter = lineEdit;
        }
        QVERIFY(filter);
        filter->setText("keep");
        QCOMPARE(visibleLines(), QStringList{"[channel] keep me"});

        filter->clear();
        QCOMPARE(visibleLines(), QStringList({"[channel] keep me", "[channel] drop me"}));
    }

    void testMessageAnsweredLater()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "later", "alien-later-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Later');\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.later.ask',\n"
            "      () => { vscode.window.showWarningMessage('pick', 'Now', 'Later')\n"
            "                  .then(answer => channel.appendLine('answered:' + answer));\n"
            "              channel.appendLine('asked'); }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.later.ping',\n"
            "      () => channel.appendLine('pong')));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        int pending = -1;
        connect(&host, &ExtensionHost::messageQuestionRequested, &host,
                [&pending](int id, MessageLevel, const QString &, const QStringList &) {
                    pending = id; // deliberately not answered here
                });
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.later.ask"), 15000);

        const auto lines = [&output] {
            QStringList result;
            for (const QList<QVariant> &args : output)
                result.append(args.at(1).toString());
            return result;
        };

        host.executeCommand("alien.later.ask");
        QTRY_VERIFY_WITH_TIMEOUT(pending >= 0, 15000);

        // The question is outstanding, and the host is still being served: a
        // later command runs and its output arrives. Whoever asks the user must
        // not sit on the connection while waiting for the answer.
        host.executeCommand("alien.later.ping");
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("pong"), 15000);
        QVERIFY(!lines().contains("answered:Later"));

        // Answering afterwards still reaches the extension.
        host.window().resolveMessageQuestion(pending, 1);
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("answered:Later"), 15000);
    }

    void testTaskCategoryGoesWithItsOwner()
    {
        const Id category("Task.Category.Alien.Test");
        ProjectExplorer::TaskHub *hub = &ProjectExplorer::taskHub();
        QSignalSpy added(hub, &ProjectExplorer::TaskHub::categoryAdded);
        QSignalSpy removed(hub, &ProjectExplorer::TaskHub::categoryRemoved);
        const ProjectExplorer::TaskCategory testCategory{category, "Alien Test", "For a test."};

        ProjectExplorer::TaskHub::addCategory(testCategory);
        QCOMPARE(added.size(), 1);

        // The Issues pane is told, so the category leaves its filter menu
        // rather than sitting there with nothing behind it.
        ProjectExplorer::TaskHub::removeCategory(category);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed.first().first().value<Id>(), category);

        // And the id is free again: addCategory() refuses one still taken, so
        // a second event here is what proves the first was really given up.
        ProjectExplorer::TaskHub::addCategory(testCategory);
        QCOMPARE(added.size(), 2);
        ProjectExplorer::TaskHub::removeCategory(category);
    }

    void testConfigurationChangeScope()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "scope", "alien-cfgscope-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Scope');\n"
            "  context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event => {\n"
            "    channel.appendLine('mine=' + event.affectsConfiguration('alien.mine')\n"
            "                       + ' other=' + event.affectsConfiguration('other'));\n"
            "  }));\n"
            "  channel.appendLine('ready');\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.setConfiguration(QJsonObject{{"alien.mine", "one"}});
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 1, 15000);

        auto lines = [&output] {
            QStringList result;
            for (const QList<QVariant> &args : output)
                result.append(args.at(1).toString());
            return result;
        };
        QCOMPARE(lines(), QStringList{"ready"});

        // The same configuration again is not a change - saying it is made
        // extensions redo their setup, down to restarting language servers.
        host.setConfiguration(QJsonObject{{"alien.mine", "one"}});

        // A change elsewhere is a change, but not one that concerns this
        // extension's own settings.
        host.setConfiguration(QJsonObject{{"alien.mine", "one"}, {"other.thing", 1}});
        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 2, 15000);
        QCOMPARE(lines().last(), QString("mine=false other=true"));

        host.setConfiguration(QJsonObject{{"alien.mine", "two"}, {"other.thing", 1}});
        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 3, 15000);
        QCOMPARE(lines().last(), QString("mine=true other=false"));

        // Three events for three changes, and none for the repeat in between.
        QCOMPARE(output.size(), 3);
    }

    void testFolderSettingsAreReadForTheirFolder()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath project = root / "project";
        QVERIFY((project / ".vscode").ensureWritableDir());
        // As a user writes it, comments and trailing commas and all.
        QVERIFY((project / ".vscode" / "settings.json").writeFileContents(
                    "{\n"
                    "  // The Qt this project is built against.\n"
                    "  \"alien.qtFolder\": \"from-the-folder\",\n"
                    "}\n").has_value());
        const FilePath inside = project / "src" / "main.cpp";
        QVERIFY(inside.parentDir().ensureWritableDir());
        QVERIFY(inside.writeFileContents("int main() {}\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "scope", "alien-scope-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Scope');\n"
            "  context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(\n"
            "      event => channel.appendLine('affects mine='\n"
            "          + event.affectsConfiguration('alien.qtFolder')\n"
            "          + ' other=' + event.affectsConfiguration('other.thing'))));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.scope.go',\n"
            "      () => {\n"
            "    const folder = vscode.workspace.workspaceFolders[0];\n"
            "    const of = scope => vscode.workspace.getConfiguration('alien', scope)\n"
            "        .get('qtFolder');\n"
            "    channel.appendLine('folder=' + of(folder));\n"
            "    channel.appendLine('file=' + of(vscode.Uri.file(folder.uri.fsPath\n"
            "        + '/src/main.cpp')));\n"
            "    channel.appendLine('global=' + of(undefined));\n"
            "    channel.appendLine('elsewhere=' + of(vscode.Uri.file('/')));\n"
            "    const seen = vscode.workspace.getConfiguration('alien', folder)\n"
            "        .inspect('qtFolder');\n"
            "    channel.appendLine('inspect=' + seen.workspaceFolderValue + ','\n"
            "        + seen.globalValue + ',' + seen.workspaceValue);\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.setConfiguration(QJsonObject{{"alien.qtFolder", "from-the-settings"}});
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.scope.go"), 15000);

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };
        host.setWorkspaceFolders(QJsonArray{
            QJsonObject{{"path", host.toHostPath(project)}, {"name", "project"}}});
        // The folder brought a setting of its own with it, and says whose.
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("affects mine=true other=false"), 15000);

        host.executeCommand("alien.scope.go");
        QTRY_VERIFY_WITH_TIMEOUT(lines().size() >= 6, 15000);
        QCOMPARE(reported.mid(1),
                 QStringList({"folder=from-the-folder",
                              "file=from-the-folder",
                              "global=from-the-settings",
                              "elsewhere=from-the-settings",
                              "inspect=from-the-folder,from-the-settings,undefined"}));
    }

    void testSettingWrittenForAFolderStaysThere()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath one = root / "one";
        const FilePath two = root / "two";
        QVERIFY(one.ensureWritableDir());
        QVERIFY(two.ensureWritableDir());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "write", "alien-write-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Write');\n"
            "  const folders = () => vscode.workspace.workspaceFolders;\n"
            "  const of = scope => vscode.workspace.getConfiguration('alien', scope)\n"
            "      .get('asked');\n"
            "  const first = folders() && folders()[0];\n"
            "  channel.appendLine('at start=' + (first ? of(first) : 'no folder'));\n"
            "  context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(\n"
            "      event => channel.appendLine('affects one='\n"
            "          + event.affectsConfiguration('alien.asked', folders()[0])\n"
            "          + ' two=' + event.affectsConfiguration('alien.asked', folders()[1]))));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.write.go',\n"
            "      async () => {\n"
            "    const [a, b] = folders();\n"
            "    await vscode.workspace.getConfiguration('alien', a)\n"
            "        .update('asked', true, vscode.ConfigurationTarget.WorkspaceFolder);\n"
            "    channel.appendLine('one=' + of(a) + ' two=' + of(b)\n"
            "        + ' global=' + of(undefined));\n"
            "    const seen = vscode.workspace.getConfiguration('alien', a).inspect('asked');\n"
            "    channel.appendLine('inspect=' + seen.workspaceFolderValue + ','\n"
            "        + seen.globalValue);\n"
            "    // No target at all, which VS Code reads as the folder the\n"
            "    // configuration was asked for.\n"
            "    await vscode.workspace.getConfiguration('alien', b).update('later', 1);\n"
            "    const later = scope => vscode.workspace.getConfiguration('alien', scope)\n"
            "        .get('later');\n"
            "    channel.appendLine('later two=' + later(b) + ' one=' + later(a));\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.write.go"), 15000);
        host.setWorkspaceFolders(QJsonArray{
            QJsonObject{{"path", host.toHostPath(one)}, {"name", "one"}},
            QJsonObject{{"path", host.toHostPath(two)}, {"name", "two"}}});

        QStringList reported;
        const auto lines = [&output, &reported] {
            reported.clear();
            for (const QList<QVariant> &args : output)
                reported.append(args.at(1).toString());
            return reported;
        };
        host.executeCommand("alien.write.go");
        QTRY_VERIFY_WITH_TIMEOUT(lines().filter("later two=").size() == 1, 15000);
        QCOMPARE(reported.filter(QRegularExpression("^one=")).first(),
                 QString("one=true two=undefined global=undefined"));
        QCOMPARE(reported.filter("inspect=").first(), QString("inspect=true,undefined"));
        QCOMPARE(reported.filter("later two=").first(), QString("later two=1 one=undefined"));

        // The event about one folder is not about the other one.
        QVERIFY(reported.contains("affects one=true two=false"));
        QVERIFY2(!reported.contains("affects one=true two=true"),
                 "a write for one folder was announced for both of them");

        // On disk in the folder's own file, and nowhere near the settings that
        // apply to every project.
        const FilePath written = ExtensionHost::folderConfigurationFile(one);
        QVERIFY(written.exists());
        QVERIFY(QJsonDocument::fromJson(written.fileContents().value_or(QByteArray()))
                    .object()
                    .contains("alien.asked"));
        const QJsonObject global
            = QJsonDocument::fromJson(ExtensionHost::writtenConfigurationFile()
                                          .fileContents()
                                          .value_or(QByteArray()))
                  .object();
        QVERIFY2(!global.contains("alien.asked") && !global.contains("alien.later"),
                 "a setting written for one folder was written for all of them");

        // A host started later finds the value where it was written, so it is
        // the folder that keeps it and not the run that wrote it.
        ExtensionHost later(node);
        QSignalSpy laterOutput(&later, &ExtensionHost::channelOutput);
        later.setWorkspaceFolders(QJsonArray{
            QJsonObject{{"path", later.toHostPath(one)}, {"name", "one"}},
            QJsonObject{{"path", later.toHostPath(two)}, {"name", "two"}}});
        later.activate(*manifest);
        const auto laterLines = [&laterOutput] {
            QStringList lines;
            for (const QList<QVariant> &args : laterOutput)
                lines.append(args.at(1).toString());
            return lines;
        };
        QTRY_VERIFY_WITH_TIMEOUT(laterLines().contains("at start=true"), 15000);

        written.removeFile();
        ExtensionHost::folderConfigurationFile(two).removeFile();
    }

    void testWebviewTheme()
    {
        // An extension writes var(--vscode-editor-background) and expects the
        // editor to have said what that is.
        const QString themed = themedWebviewHtml(
            "<html><head><title>t</title></head><body><p>x</p></body></html>");
        QVERIFY(themed.contains("--vscode-editor-background"));
        QVERIFY(themed.contains("--vscode-editor-foreground"));
        QVERIFY(themed.contains("--vscode-font-family"));
        QVERIFY(themed.contains("--vscode-editor-font-family"));

        // The colors are the IDE's, not invented ones, and in a form CSS reads:
        // Qt's HexArgb puts the alpha first, where CSS expects it last.
        QVERIFY(!themed.contains(
            Utils::creatorColor(Utils::Theme::SplitterColor).name(QColor::HexArgb)));
        const QColor splitter = Utils::creatorColor(Utils::Theme::SplitterColor);
        QVERIFY2(themed.contains(QString("rgba(%1, %2, %3")
                                     .arg(splitter.red()).arg(splitter.green())
                                     .arg(splitter.blue())),
                 qPrintable(themed.left(400)));

        // Extensions branch on the theme kind, in CSS and in script.
        const bool dark = Utils::creatorTheme()
                          && Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface);
        const QString kind = dark ? QString("vscode-dark") : QString("vscode-light");
        QVERIFY(themed.contains("<body class=\"" + kind + "\""));
        QVERIFY(themed.contains("data-vscode-theme-kind=\"" + kind + "\""));

        // What was already there stays: the style joins the head, and the
        // document keeps its own.
        QVERIFY(themed.contains("<title>t</title>"));
        QVERIFY(themed.contains("<p>x</p>"));
        QVERIFY(themed.indexOf("--vscode-editor-background") < themed.indexOf("</head>"));

        // A fragment without a head still gets one.
        const QString fragment = themedWebviewHtml("<p>bare</p>");
        QVERIFY(fragment.contains("--vscode-editor-background"));
        QVERIFY(fragment.contains("<p>bare</p>"));

        // Everything a webview sizes is relative to these, and CSS counts in
        // pixels where the fonts here are usually described in points.
        const QFont uiFont = QApplication::font();
        const int uiPixels = QFontInfo(uiFont).pixelSize();
        const int editorPixels = QFontInfo(TextEditor::globalFontSettings().data().font())
                                     .pixelSize();
        if (uiFont.pointSize() == uiPixels)
            QSKIP("Points and pixels coincide here, so the two cannot be told apart.");
        QVERIFY2(themed.contains(QString("--vscode-font-size: %1px;").arg(uiPixels)),
                 qPrintable(themed.left(800)));
        QVERIFY2(themed.contains(QString("--vscode-editor-font-size: %1px;").arg(editorPixels)),
                 qPrintable(themed.left(800)));
    }

    // Where the variables have to be, and what has to let them in. An
    // extension reads them off the root element's style attribute rather than
    // out of CSS, and a page that declares a policy of its own refuses the
    // <style> that carries them: hexeditor's rows came out NaN pixels high and
    // its grid stayed empty.
    void testWebviewThemeReachesAStrictPage()
    {
        const QString strict = themedWebviewHtml(
            "<html><head><meta http-equiv=\"Content-Security-Policy\" content=\"default-src"
            " 'none'; img-src file: blob:; style-src\tfile:; script-src 'nonce-1'\">"
            "</head><body></body></html>");

        // The variables are on the root element, where the extension looks.
        const int html = strict.indexOf("<html");
        const int head = strict.indexOf("<head");
        QVERIFY(html >= 0 && head > html);
        const QString rootTag = strict.mid(html, head - html);
        QVERIFY2(rootTag.contains("style=\""), qPrintable(rootTag));
        QVERIFY2(rootTag.contains("--vscode-editor-font-size:"), qPrintable(rootTag));

        // The page's own policy now allows the style that carries them, and
        // says so once: a second style-src would be the one that counts.
        QVERIFY2(strict.contains("style-src\tfile: 'unsafe-inline'"), qPrintable(strict.left(400)));
        QVERIFY(strict.count(QString("style-src")) == 1);
        // The rest of the policy is untouched.
        QVERIFY(strict.contains("script-src 'nonce-1'"));
        QVERIFY(strict.contains("default-src 'none'"));

        // A policy that mentions no style at all still restricts one through
        // default-src, and what that allows is what a style may do, minus the
        // "none" a list drops as soon as it says anything else.
        const QString fallback = themedWebviewHtml(
            "<html><head><meta http-equiv=\"Content-Security-Policy\" content=\"default-src"
            " 'none' file:; script-src 'nonce-2'\"></head><body></body></html>");
        QVERIFY2(fallback.contains("style-src file: 'unsafe-inline'"),
                 qPrintable(fallback.left(400)));

        // A policy written with the other quote is read all the same, and one
        // full of single quotes does not end at the first of them.
        const QString quoted = themedWebviewHtml(
            "<html><head><meta http-equiv='Content-Security-Policy'"
            " content='default-src file:'></head><body></body></html>");
        QVERIFY2(quoted.contains("style-src file: 'unsafe-inline'"),
                 qPrintable(quoted.left(400)));

        // An element that has a style of its own keeps it.
        const QString styled = themedWebviewHtml(
            "<html style=\"color: red\"><body></body></html>");
        QVERIFY2(styled.contains("color: red;--vscode-"), qPrintable(styled.left(200)));

        // Next to a nonce 'unsafe-inline' does nothing, the style's hash does.
        const QString nonced = themedWebviewHtml(
            "<html><head><meta http-equiv=\"Content-Security-Policy\" content=\"style-src"
            " 'nonce-3'\"></head><body></body></html>");
        const int styleStart = nonced.indexOf("<style>") + 7;
        const QByteArray style = nonced.mid(styleStart, nonced.indexOf("</style>") - styleStart)
                                     .toUtf8();
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(style, QCryptographicHash::Sha256).toBase64());
        QVERIFY2(nonced.contains("style-src 'nonce-3' 'sha256-" + hash + "'"),
                 qPrintable(nonced.left(400)));

        // A page without a policy is left as it is.
        QVERIFY(!themedWebviewHtml("<html><body></body></html>").contains("unsafe-inline"));
    }

    void testVisibleEditors()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "visible.txt";
        QVERIFY(file.writeFileContents("shown").has_value());

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "visible", "alien-visible-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand("
            "'alien.visible.ready', () => {}));\n"
            "  const report = editors => vscode.window.showInformationMessage(\n"
            "    'visible:' + editors.map(e => e.document.uri.fsPath.split('/').pop())"
            ".join(','));\n"
            "  context.subscriptions.push("
            "vscode.window.onDidChangeVisibleTextEditors(report));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.visible.ready"),
                                 15000);

        QVERIFY(EditorManager::openEditor(file));
        auto saw = [&spy] {
            for (const QList<QVariant> &args : spy) {
                if (args.first().toString() == "visible:visible.txt")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(saw(), 15000);

        EditorManager::closeAllEditors(false);
    }

    void testFileEvents()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path()) / "proj";
        QVERIFY(root.ensureWritableDir());
        const FilePath first = root / "first.txt";
        const FilePath added = root / "added.txt";
        QVERIFY((root / "proj.txt").writeFileContents("").has_value());
        QVERIFY(first.writeFileContents("one").has_value());

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "files", "alien-files-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand("
            "'alien.files.ready', () => {}));\n"
            "  context.subscriptions.push(vscode.workspace.onDidCreateFiles(event =>\n"
            "    vscode.window.showInformationMessage("
            "'created:' + event.files.map(f => f.fsPath.split('/').pop()).join(','))));\n"
            "  context.subscriptions.push(vscode.workspace.onDidDeleteFiles(event =>\n"
            "    vscode.window.showInformationMessage("
            "'deleted:' + event.files.map(f => f.fsPath.split('/').pop()).join(','))));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.files.ready"), 15000);

        // The manager takes ownership, so removing the project deletes it.
        auto project = new TestProject(root / "proj.txt");
        project->setFiles({first});
        ProjectExplorer::ProjectManager::addProject(project);
        const QScopeGuard close(
            [project] { ProjectExplorer::ProjectManager::removeProject(project); });
        QVERIFY(project->files(ProjectExplorer::Project::AllFiles).contains(first));

        auto saw = [&spy](const QString &message) {
            for (const QList<QVariant> &args : spy) {
                if (args.first().toString() == message)
                    return true;
            }
            return false;
        };

        // A file appearing in the project is one an extension is told about.
        QVERIFY(added.writeFileContents("two").has_value());
        project->setFiles({first, added});
        QTRY_VERIFY_WITH_TIMEOUT(saw("created:added.txt"), 30000);

        // And so is one that goes away again.
        project->setFiles({first});
        QTRY_VERIFY_WITH_TIMEOUT(saw("deleted:added.txt"), 30000);
    }

    void testSaveEvents()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "saved.txt";
        QVERIFY(file.writeFileContents("before").has_value());

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "save", "alien-save-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand("
            "'alien.save.ready', () => {}));\n"
            "  context.subscriptions.push(vscode.workspace.onWillSaveTextDocument(event =>\n"
            "    vscode.window.showInformationMessage('willSave:' + event.document.uri.fsPath)));\n"
            "  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(document =>\n"
            "    vscode.window.showInformationMessage("
            "'didSave:' + document.getText().trim())));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        host.activate(*manifest);
        // Listening starts when the extension has run, not when the host has.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.save.ready"), 15000);

        QVERIFY(EditorManager::openEditor(file));
        TextEditor::TextDocument *document
            = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(document);
        document->document()->setPlainText("after");
        QVERIFY(DocumentManager::saveDocument(document));

        auto saw = [&spy](const QString &prefix) {
            return [&spy, prefix] {
                for (const QList<QVariant> &args : spy) {
                    if (args.first().toString().startsWith(prefix))
                        return true;
                }
                return false;
            };
        };
        // Both halves: the one an extension uses to get in before the write,
        // and the one it uses to act on what was written.
        QTRY_VERIFY_WITH_TIMEOUT(saw("willSave:")(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(saw("didSave:after")(), 15000);

        EditorManager::closeAllEditors(false);
    }

    void testOutputChannel()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "out", "alien-output-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Channel');\n"
            "  channel.appendLine('first line');\n"
            "  channel.append('second');\n"
            "  context.subscriptions.push(channel);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);

        // What an extension logs is its own, named by the channel it chose ...
        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 2, 15000);
        QCOMPARE(output.first().at(0).toString(), QString("Alien Channel"));
        QCOMPARE(output.first().at(1).toString(), QString("first line"));
        QCOMPARE(output.first().at(2).toBool(), true);
        QCOMPARE(output.at(1).at(2).toBool(), false); // append(), not appendLine()

        // ... and it does not go to the pane Qt Creator reports its own trouble
        // in, which one talkative extension is enough to drown.
        Core::IOutputPane *general = Utils::findOrDefault(
            Core::IOutputPane::allOutputPanes(),
            [](Core::IOutputPane *pane) { return pane->id() == "GeneralMessages"; });
        QVERIFY(general);
        QVERIFY(!general->outputWindows().isEmpty());
        QVERIFY(!general->outputWindows().first()->toPlainText().contains("first line"));
    }

    void testApplicationRoot()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // What an extension does with it: read the description of the
        // application it is running in, and fail its activation without one.
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "out", "alien-approot-test",
            "const vscode = require('vscode');\n"
            "const fs = require('fs');\n"
            "const path = require('path');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Root');\n"
            "  const root = vscode.env.appRoot;\n"
            "  const product = JSON.parse(\n"
            "      fs.readFileSync(path.join(root, 'product.json'), 'utf8'));\n"
            "  channel.appendLine(path.isAbsolute(root) + ' ' + product.nameShort + ' '\n"
            "                     + Boolean(product.version));\n"
            "  context.subscriptions.push(channel);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 1 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QCOMPARE(output.first().at(1).toString(), QString("true Qt Creator true"));
    }

    // A telemetry reporter asks how much it may send before it sends
    // anything, and the one bundled with ms-vscode.hexeditor reads the three
    // parts rather than the flag.
    void testTelemetryOff()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "out", "alien-telemetry-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Telemetry');\n"
            "  const config = vscode.env.telemetryConfiguration;\n"
            "  const level = config.isUsageEnabled && config.isErrorsEnabled\n"
            "          && config.isCrashEnabled\n"
            "      ? 'on'\n"
            "      : (config.isErrorsEnabled && config.isCrashEnabled ? 'error' : 'off');\n"
            "  context.subscriptions.push(\n"
            "      vscode.env.onDidChangeTelemetryConfiguration(() => {}));\n"
            "  channel.appendLine(level + ' ' + vscode.env.isTelemetryEnabled);\n"
            "  context.subscriptions.push(channel);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        QSignalSpy missing(&host, &ExtensionHost::apiUnimplemented);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 1 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QCOMPARE(output.first().at(1).toString(), QString("off false"));
        // The reads are over by the time their line arrives, so a member the
        // host does not have would have been reported by now.
        QVERIFY2(missing.isEmpty(),
                 qPrintable(missing.isEmpty() ? QString() : missing.first().at(1).toString()));
    }

    void testMessageWithChoices()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "ask", "alien-ask-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.ask', "
            "async () => {\n"
            "    const answer = await vscode.window.showWarningMessage("
            "'Configure now?', 'Configure', 'Later');\n"
            "    vscode.window.showInformationMessage('answered:' + answer);\n"
            "  }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.askPlain', "
            "async () => {\n"
            "    const answer = await vscode.window.showInformationMessage('Just saying');\n"
            "    vscode.window.showInformationMessage('plain:' + answer);\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QStringList offered;
        MessageLevel offeredLevel = MessageLevel::Info;
        connect(&host, &ExtensionHost::messageQuestionRequested, &host,
                [&host, &offered, &offeredLevel](int id, MessageLevel level, const QString &,
                                                 const QStringList &items) {
                    offeredLevel = level;
                    offered = items;
                    host.window().resolveMessageQuestion(id, 1); // "Later"
                });

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.ask"), 15000);

        // An extension offering choices is asked, and gets back the one picked.
        host.executeCommand("alien.ask");
        auto saw = [&messages](const QString &text) {
            return [&messages, text] {
                for (const QList<QVariant> &args : messages) {
                    if (args.first().toString() == text)
                        return true;
                }
                return false;
            };
        };
        QTRY_VERIFY_WITH_TIMEOUT(saw("answered:Later")(), 15000);
        QVERIFY(offeredLevel == MessageLevel::Warning);
        QCOMPARE(offered, QStringList({"Configure", "Later"}));

        // A message with nothing to choose is still not a question: it is
        // reported, and the extension is told that nothing was picked.
        host.executeCommand("alien.askPlain");
        QTRY_VERIFY_WITH_TIMEOUT(saw("plain:undefined")(), 15000);
    }

    void testCreateTreeView()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "treeview", "alien-createtreeview-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const provider = {\n"
            "    getChildren: element => element ? [] : ['Only child'],\n"
            "    getTreeItem: element => new vscode.TreeItem(element,"
            " vscode.TreeItemCollapsibleState.None),\n"
            "  };\n"
            "  const view = vscode.window.createTreeView('alienCreated',"
            " {treeDataProvider: provider});\n"
            "  context.subscriptions.push(view);\n"
            // Every event of a tree view is one an extension subscribes to
            // while it builds it, and one that is not there takes it down.
            "  for (const name of ['onDidChangeSelection', 'onDidChangeVisibility',\n"
            "                      'onDidExpandElement', 'onDidCollapseElement',\n"
            "                      'onDidChangeCheckboxState'])\n"
            "    context.subscriptions.push(view[name](() => {}));\n"
            "  vscode.window.showInformationMessage('view:' + view.visible + ':' + view.title);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy registered(&host, &ExtensionHost::treeViewRegistered);
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);

        // The view reaches the sidebar the same way the other registration
        // does, and the extension gets an object back to hold on to.
        QTRY_VERIFY_WITH_TIMEOUT(!registered.isEmpty(), 15000);
        QCOMPARE(registered.first().first().toString(), QString("alienCreated"));
        auto sawView = [&messages] {
            for (const QList<QVariant> &args : messages) {
                // Visible, because registering it is what shows it.
                if (args.first().toString().startsWith("view:true:"))
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawView(), 15000);

        // And it is a working tree, not just a registration.
        QStringList labels;
        auto poll = [&host, &labels] {
            host.window().requestTreeChildren("alienCreated", {}, [&labels](const QJsonArray &nodes) {
                labels.clear();
                for (const QJsonValue &node : nodes)
                    labels << node.toObject().value("label").toString();
            });
            return labels;
        };
        QTRY_COMPARE_WITH_TIMEOUT(poll(), QStringList({"Only child"}), 15000);
    }

    void testKeybindings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path()) / "keys";
        QVERIFY(root.ensureWritableDir());
        QVERIFY((root / "package.json").writeFileContents(R"({
            "name": "keys-test", "publisher": "alien", "version": "0.0.1",
            "engines": { "vscode": "^1.0.0" }, "main": "./extension.js",
            "contributes": {
                "commands": [{ "command": "alien.keys.build", "title": "Build",
                               "category": "Alien" }],
                "keybindings": [
                    { "command": "alien.keys.build", "key": "f7" },
                    { "command": "alien.keys.side", "key": "ctrl+k v", "mac": "cmd+k v" },
                    { "command": "alien.keys.comma", "key": "ctrl+shift+g ," },
                    { "command": "alien.keys.control", "key": "ctrl+j", "mac": "ctrl+h" },
                    { "command": "alien.keys.nothing", "key": "" }
                ] } })").has_value());

        const Result<VscodeManifest> manifest
            = VscodeManifest::fromPackageJson(root / "package.json");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        QCOMPARE(manifest->keybindings.size(), 5);

        const auto sequenceFor = [&manifest](const QString &command) {
            const VscodeKeybinding binding
                = Utils::findOrDefault(manifest->keybindings,
                                       [&command](const VscodeKeybinding &b) {
                                           return b.command == command;
                                       });
            return keySequenceOf(binding).toString();
        };
        QCOMPARE(sequenceFor("alien.keys.build"), QString("F7"));
        // A chord: a space in the manifest, a comma in Qt.
        QCOMPARE(sequenceFor("alien.keys.side"),
                 QString(HostOsInfo::isMacHost() ? "Ctrl+K, V" : "Ctrl+K, V"));
        // A chord whose second half is the comma itself.
        QCOMPARE(sequenceFor("alien.keys.comma"),
                 QString(HostOsInfo::isMacHost() ? "Meta+Shift+G, ," : "Ctrl+Shift+G, ,"));
        // On macOS that is the Control key, which Qt calls Meta there.
        QCOMPARE(sequenceFor("alien.keys.control"),
                 QString(HostOsInfo::isMacHost() ? "Meta+H" : "Ctrl+J"));
        // Nothing asked for stays nothing, rather than becoming a shortcut that
        // answers to no key.
        QVERIFY(sequenceFor("alien.keys.nothing").isEmpty());
    }

    // One command bound twice, as a manifest does for the two directions of a
    // move: each key is a shortcut of its own, not just the first.
    void testOneCommandBoundTwice()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path()) / "twice";
        QVERIFY(root.ensureWritableDir());
        QVERIFY((root / "package.json").writeFileContents(R"({
            "name": "twice", "publisher": "alien", "version": "0.0.1",
            "engines": { "vscode": "^1.0.0" }, "main": "./extension.js",
            "contributes": {
                "commands": [{ "command": "alien.twice.move", "title": "Move" }],
                "keybindings": [
                    { "command": "alien.twice.move", "key": "ctrl+alt+shift+f9",
                      "args": { "direction": "up" } },
                    { "command": "alien.twice.move", "key": "ctrl+alt+shift+f10",
                      "args": { "direction": "down" } }
                ] } })").has_value());
        QVERIFY((root / "extension.js").writeFileContents("module.exports={activate(){}};")
                    .has_value());

        AlienSettings &s = settings();
        const bool wasEnabled = s.enable();
        const bool wasAccepted = s.legalNoticeAccepted();
        const FilePath previousDir = s.extensionsDir();
        const QString previousIds = s.enabledExtensions();
        const auto rescan = [] {
            Core::Command *command = ActionManager::command(Constants::RESCAN_ACTION_ID);
            QVERIFY(command);
            command->action()->trigger();
        };
        const QScopeGuard restore([&] {
            s.enable.setValue(wasEnabled);
            s.legalNoticeAccepted.setValue(wasAccepted);
            s.extensionsDir.setValue(previousDir);
            s.enabledExtensions.setValue(previousIds);
            rescan();
        });
        s.enable.setValue(true);
        s.legalNoticeAccepted.setValue(true);
        s.extensionsDir.setValue(FilePath::fromString(dir.path()));
        s.enabledExtensions.setValue("alien.twice");
        rescan();

        const Id id("Alien.alien.twice.move");
        Core::Command *first = ActionManager::command(id);
        Core::Command *second = ActionManager::command(id.withSuffix("#2"));
        QVERIFY(first);
        QVERIFY(second);
        QCOMPARE(first->defaultKeySequences(), {QKeySequence("Ctrl+Alt+Shift+F9")});
        QCOMPARE(second->defaultKeySequences(), {QKeySequence("Ctrl+Alt+Shift+F10")});
    }

    void testDeclinedNoticeAsksAgain()
    {
        const Id notice("Alien.LegalNotice");
        InfoBar *infoBar = ICore::popupInfoBar();
        AlienSettings &s = settings();
        const bool wasEnabled = s.enable();
        const bool wasAccepted = s.legalNoticeAccepted();
        const auto rescan = [] {
            Core::Command *command = ActionManager::command(Constants::RESCAN_ACTION_ID);
            QVERIFY(command);
            command->action()->trigger();
        };
        const QScopeGuard restore([&] {
            infoBar->removeInfo(notice);
            InfoBar::globallyUnsuppressInfo(notice);
            s.enable.setValue(wasEnabled);
            s.legalNoticeAccepted.setValue(wasAccepted);
            s.writeSettings();
            rescan();
        });
        infoBar->removeInfo(notice);
        InfoBar::globallyUnsuppressInfo(notice);
        s.enable.setValue(true);
        s.legalNoticeAccepted.setValue(false);
        rescan();
        QVERIFY(infoBar->containsInfo(notice));

        const InfoBarEntry entry = Utils::findOrDefault(
            infoBar->entries(), [&notice](const InfoBarEntry &e) { return e.id() == notice; });
        const InfoBarEntry::Button decline = Utils::findOrDefault(
            entry.buttons(),
            [](const InfoBarEntry::Button &b) { return b.text == Tr::tr("Do Not Run"); });
        QVERIFY(decline.callback);
        infoBar->triggerButton(notice, decline);
        QVERIFY(!infoBar->containsInfo(notice));
        QVERIFY(!s.enable());

        // Turned on again, which the notice says is where to change one's mind.
        s.enable.setValue(true);
        rescan();
        QVERIFY(infoBar->containsInfo(notice));
    }

    void testDependencyRunsOnlyWhenEnabled()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // "user" depends on "dep", "other" on nothing. Each registers a command.
        for (const auto &[name, dependency] : QList<std::pair<QString, QString>>{
                 {"dep", {}}, {"user", "alien.dep"}, {"other", {}}}) {
            const FilePath root = FilePath::fromString(dir.path()) / name;
            QVERIFY(root.ensureWritableDir());
            const QJsonObject pkg{
                {"name", name}, {"publisher", "alien"}, {"version", "0.0.1"},
                {"engines", QJsonObject{{"vscode", "^1.0.0"}}}, {"main", "./extension.js"},
                {"activationEvents", QJsonArray{"*"}},
                {"extensionDependencies",
                 dependency.isEmpty() ? QJsonArray{} : QJsonArray{dependency}}};
            QVERIFY((root / "package.json").writeFileContents(QJsonDocument(pkg).toJson())
                        .has_value());
            QVERIFY((root / "extension.js")
                        .writeFileContents(QString("exports.activate = context => context"
                                                   ".subscriptions.push(require('vscode')"
                                                   ".commands.registerCommand('alien.%1', "
                                                   "() => {}));").arg(name).toUtf8())
                        .has_value());
        }
        // "other" asks for "user" by name and says what it got.
        QVERIFY((FilePath::fromString(dir.path()) / "other" / "extension.js")
                    .writeFileContents(R"(const vscode = require('vscode');
exports.activate = async context => {
    context.subscriptions.push(vscode.commands.registerCommand('alien.other', () => {}));
    const user = vscode.extensions.getExtension('alien.user');
    let outcome = user ? 'resolved' : 'unknown';
    if (user) {
        try { await user.activate(); } catch (e) { outcome = 'rejected'; }
    }
    context.subscriptions.push(
        vscode.commands.registerCommand('alien.other.' + outcome, () => {}));
};
)")
                    .has_value());

        AlienSettings &s = settings();
        const bool wasEnabled = s.enable();
        const bool wasAccepted = s.legalNoticeAccepted();
        const bool wasStdio = s.assumeMainIsStdioServer();
        const FilePath previousNode = s.nodeJsPath();
        const FilePath previousDir = s.extensionsDir();
        const QString previousIds = s.enabledExtensions();
        const auto rescan = [] {
            Core::Command *command = ActionManager::command(Constants::RESCAN_ACTION_ID);
            QVERIFY(command);
            command->action()->trigger();
        };
        const QScopeGuard restore([&] {
            EditorManager::closeAllEditors(false);
            s.enable.setValue(wasEnabled);
            s.legalNoticeAccepted.setValue(wasAccepted);
            s.assumeMainIsStdioServer.setValue(wasStdio);
            s.nodeJsPath.setValue(previousNode);
            s.extensionsDir.setValue(previousDir);
            s.enabledExtensions.setValue(previousIds);
            rescan();
        });
        s.enable.setValue(true);
        s.legalNoticeAccepted.setValue(true);
        s.assumeMainIsStdioServer.setValue(false);
        s.nodeJsPath.setValue(node);
        s.extensionsDir.setValue(FilePath::fromString(dir.path()));
        // A document names the folder activation waits for.
        const FilePath document = FilePath::fromString(dir.path()) / "a.txt";
        QVERIFY(document.writeFileContents("a").has_value());
        QVERIFY(EditorManager::openEditor(document));
        const auto commands = [] {
            return runningExtensionHost() ? runningExtensionHost()->registeredCommands()
                                          : QStringList();
        };

        // The dependency is not enabled, so neither runs. The host activates
        // in the order it is asked, so once "other" has run, so would have they.
        s.enabledExtensions.setValue("alien.user");
        rescan();
        s.enabledExtensions.setValue("alien.user\nalien.other");
        rescan();
        QTRY_VERIFY_WITH_TIMEOUT(commands().contains("alien.other"), 15000);
        // Asked for by name, it does not start either, and the caller hears so.
        QTRY_VERIFY_WITH_TIMEOUT(commands().contains("alien.other.rejected"), 15000);
        QVERIFY(!commands().contains("alien.dep"));
        QVERIFY(!commands().contains("alien.user"));

        // Enabled, it runs, and the one depending on it with it.
        s.enabledExtensions.setValue("alien.user\nalien.dep");
        rescan();
        QTRY_VERIFY_WITH_TIMEOUT(commands().contains("alien.user"), 15000);
        QVERIFY(commands().contains("alien.dep"));
    }

    void testDisabledExtensionStartsNoServer()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const QString &name : QStringList{"on", "off"}) {
            const FilePath root = FilePath::fromString(dir.path()) / name;
            QVERIFY(root.ensureWritableDir());
            QVERIFY((root / "package.json").writeFileContents(QString(R"({
                "name": "%1", "publisher": "alien", "version": "0.0.1",
                "engines": { "vscode": "^1.0.0" }, "main": "./server.js",
                "contributes": { "languages": [{ "id": "alien%1", "extensions": [".%1"] }] }
                })").arg(name).toUtf8()).has_value());
            QVERIFY((root / "server.js").writeFileContents("process.stdin.resume();")
                        .has_value());
        }

        AlienSettings &s = settings();
        const bool wasEnabled = s.enable();
        const bool wasAccepted = s.legalNoticeAccepted();
        const bool wasStdio = s.assumeMainIsStdioServer();
        const FilePath previousNode = s.nodeJsPath();
        const FilePath previousDir = s.extensionsDir();
        const QString previousIds = s.enabledExtensions();
        const auto rescan = [] {
            Core::Command *command = ActionManager::command(Constants::RESCAN_ACTION_ID);
            QVERIFY(command);
            command->action()->trigger();
        };
        const QScopeGuard restore([&] {
            s.enable.setValue(wasEnabled);
            s.legalNoticeAccepted.setValue(wasAccepted);
            s.assumeMainIsStdioServer.setValue(wasStdio);
            s.nodeJsPath.setValue(previousNode);
            s.extensionsDir.setValue(previousDir);
            s.enabledExtensions.setValue(previousIds);
            rescan();
        });
        s.enable.setValue(true);
        s.legalNoticeAccepted.setValue(true);
        s.assumeMainIsStdioServer.setValue(true);
        s.nodeJsPath.setValue(node);
        s.extensionsDir.setValue(FilePath::fromString(dir.path()));
        s.enabledExtensions.setValue("alien.on");
        rescan();

        const auto started = [](const QString &name) {
            return Utils::anyOf(LanguageClient::LanguageClientManager::clients(),
                                [&name](LanguageClient::Client *c) { return c->name() == name; });
        };
        QVERIFY(started("alien.on"));
        QVERIFY(!started("alien.off"));
    }

    void testTerminal()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath marker = FilePath::fromString(dir.path()) / "terminal.txt";
        const FilePath shellMarker = FilePath::fromString(dir.path()) / "shell.txt";
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "term", "alien-terminal-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    // Terminal events an extension subscribes to as it
                    // starts. One that is not there is a TypeError, and the
                    // extension is gone before it has done anything.
                    "  for (const name of ['onDidChangeTerminalShellIntegration',\n"
                    "                      'onDidStartTerminalShellExecution',\n"
                    "                      'onDidEndTerminalShellExecution',\n"
                    "                      'onDidChangeTerminalState'])\n"
                    "    context.subscriptions.push(vscode.window[name](() => {}));\n"
                    "  context.subscriptions.push(vscode.window.onDidChangeActiveTerminal("
                    "terminal => {\n"
                    "    vscode.window.showInformationMessage('active:'"
                    " + (terminal ? terminal.name : 'none'));\n"
                    "  }));\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand("
                    "'alien.term.run', () => {\n"
                    "    const terminal = vscode.window.createTerminal('alien');\n"
                    "    terminal.sendText('echo ran > %1');\n"
                    "    terminal.show();\n"
                    "  }));\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand("
                    "'alien.term.shell', () => {\n"
                    "    vscode.window.createTerminal({name: 'sh', shellPath: '/bin/sh'})\n"
                    "      .sendText('echo shell > %2');\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n")
                .arg(marker.toFSPathString(), shellMarker.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy opened(&host, &ExtensionHost::terminalOpened);
        QSignalSpy shown(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.term.run"), 15000);
        host.executeCommand("alien.term.run");

        // What the extension sends is what the terminal is opened with ...
        QTRY_VERIFY_WITH_TIMEOUT(!opened.isEmpty(), 15000);
        QCOMPARE(opened.first().first().toString(), QString("alien"));
        QVERIFY(opened.first().last().value<CommandLine>().arguments().contains("echo ran"));

        // ... showing it is what makes it the active one ...
        auto saidActive = [&shown] {
            for (const QList<QVariant> &args : shown)
                if (args.first().toString() == "active:alien")
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(saidActive(), 15000);

        // ... and it runs, which is the point of asking for a terminal.
        QTRY_VERIFY_WITH_TIMEOUT(marker.exists(), 20000);
        QCOMPARE(marker.fileContents().value_or(QByteArray()).trimmed(), QByteArray("ran"));

        // A shell the extension named runs the line too, not a script by that name.
        if (HostOsInfo::isWindowsHost())
            return;
        host.executeCommand("alien.term.shell");
        QTRY_VERIFY_WITH_TIMEOUT(shellMarker.exists(), 20000);
        QCOMPARE(shellMarker.fileContents().value_or(QByteArray()).trimmed(),
                 QByteArray("shell"));
    }

    void testTaskProviderAndExecution()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath marker = FilePath::fromString(dir.path()) / "ran.txt";
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "task", "alien-task-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  context.subscriptions.push(vscode.tasks.registerTaskProvider('alien', {\n"
                    "    provideTasks() {\n"
                    "      const task = new vscode.Task({type: 'alien'},"
                    " vscode.TaskScope.Workspace, 'write marker', 'alien',\n"
                    "        new vscode.ShellExecution('/bin/sh', ['-c', 'echo ran > %1']));\n"
                    "      return [task];\n"
                    "    }}));\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand("
                    "'alien.task.run', async () => {\n"
                    "    const tasks = await vscode.tasks.fetchTasks({type: 'alien'});\n"
                    "    vscode.window.showInformationMessage("
                    "'tasks:' + tasks.map(t => t.name).join(','));\n"
                    "    await vscode.tasks.executeTask(tasks[0]);\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(marker.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        QSignalSpy started(&host, &ExtensionHost::taskStarted);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.task.run"), 15000);
        host.executeCommand("alien.task.run");

        // The provider is asked, so the task is there to run at all ...
        auto sawTasks = [&messages] {
            for (const QList<QVariant> &args : messages) {
                if (args.first().toString() == "tasks:write marker")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawTasks(), 15000);

        // ... and running it runs the command line it carries.
        QTRY_VERIFY_WITH_TIMEOUT(!started.isEmpty(), 15000);
        QVERIFY(started.first().first().toString() == "write marker");
        QTRY_VERIFY_WITH_TIMEOUT(marker.exists(), 15000);
        QCOMPARE(marker.fileContents().value_or(QByteArray()).trimmed(), QByteArray("ran"));
    }

    void testHostFailureIsReported()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "boom", "alien-boom-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand("
            "'alien.boom', () => {}));\n"
            "  setTimeout(() => { throw new Error('boom'); }, 0);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::hostFailed);
        host.activate(*manifest);

        // What nobody catches in the host is the user's problem too: it can
        // leave the host unable to answer anything.
        QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 15000);
        QVERIFY(spy.first().first().toString().contains("boom"));

        // The host survives it, so what did register still works.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.boom"), 15000);
    }

    void testDebugAdapterDescription()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "dbg", "alien-debug-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand("
            "'alien.dbg.ready', () => {}));\n"
            "  context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider("
            "'alienlang', {\n"
            "    resolveDebugConfiguration(folder, config) {\n"
            "      return Object.assign({}, config, {program: '/tmp/a.out', stopOnEntry: true});\n"
            "    }}));\n"
            "  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory("
            "'alienlang', {\n"
            "    createDebugAdapterDescriptor(session) {\n"
            "      return new vscode.DebugAdapterExecutable('/usr/bin/alien-dap',\n"
            "                                               ['--stdio', session.configuration"
            ".program]);\n"
            "    }}));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"debuggers", QJsonArray{QJsonObject{{"type", "alienlang"},
                                                             {"label", "Alien Debugger"},
                                                             {"runtime", "node"}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        // What the manifest says on its own, without running anything.
        QCOMPARE(manifest->debuggers.size(), 1);
        QCOMPARE(manifest->debuggers.first().type, QString("alienlang"));
        QCOMPARE(manifest->debuggers.first().label, QString("Alien Debugger"));

        ExtensionHost host(node);
        host.activate(*manifest);
        // Asking before the extension has run gets the empty answer it is:
        // nothing has registered a provider yet.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.dbg.ready"), 15000);

        QJsonObject answer;
        host.run().requestDebugAdapter("alienlang", {{"request", "launch"}},
                                 [&answer](const QJsonObject &result) { answer = result; });
        QTRY_VERIFY_WITH_TIMEOUT(!answer.isEmpty(), 15000);

        // The provider filled the configuration in ...
        const QJsonObject configuration = answer.value("configuration").toObject();
        QCOMPARE(configuration.value("program").toString(), QString("/tmp/a.out"));
        QCOMPARE(configuration.value("stopOnEntry").toBool(), true);
        QCOMPARE(configuration.value("request").toString(), QString("launch"));

        // ... and the factory said how its adapter is started, which is what an
        // engine would need to run it.
        const QJsonObject adapter = answer.value("adapter").toObject();
        QCOMPARE(adapter.value("kind").toString(), QString("executable"));
        QCOMPARE(adapter.value("command").toString(), QString("/usr/bin/alien-dap"));
        QCOMPARE(adapter.value("args").toArray().last().toString(), QString("/tmp/a.out"));
    }

    void testActivationEvents()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const QString js = "const vscode = require('vscode');\n"
                           "function activate() {}\n"
                           "module.exports = { activate };\n";

        const Result<VscodeManifest> eager
            = writeMockExtension(root / "eager", "alien-eager", js, QJsonArray{"*"});
        const Result<VscodeManifest> onCommand
            = writeMockExtension(root / "cmd", "alien-cmd", js,
                                 QJsonArray{"onCommand:alien.lazy.run"});
        const Result<VscodeManifest> onLanguage
            = writeMockExtension(root / "lang", "alien-lang", js,
                                 QJsonArray{"onLanguage:plaintext"});
        const Result<VscodeManifest> contains
            = writeMockExtension(root / "contains", "alien-contains", js,
                                 QJsonArray{"workspaceContains:**/*.marker"});
        const Result<VscodeManifest> debugOnly
            = writeMockExtension(root / "dbg", "alien-dbg", js,
                                 QJsonArray{"onDebugResolve:cppdbg"});
        for (const Result<VscodeManifest> *m : {&eager, &onCommand, &onLanguage, &contains,
                                                &debugOnly}) {
            QVERIFY2(m->has_value(), qPrintable(*m ? QString() : m->error()));
        }

        // Nothing is open and no workspace has the marker, so only the eager
        // one is ready. The others are waiting, not broken.
        const auto fired = [](const VscodeManifest &manifest, const QStringList &languages,
                              const FilePaths &folders, const QSet<QString> &requested = {},
                              const QSet<QString> &kinds = {}) {
            return activationEventFired(manifest, languages, folders, requested, kinds);
        };
        QVERIFY(fired(*eager, {}, {}));
        QVERIFY(!fired(*onCommand, {}, {}));
        QVERIFY(!fired(*onLanguage, {}, {}));
        QVERIFY(!fired(*contains, {}, {}));

        // Asking for one of its commands wakes the extension that has it.
        QVERIFY(fired(*onCommand, {}, {}, {"theqtcompany.alien-cmd"}));
        QVERIFY(!fired(*onLanguage, {}, {}, {"theqtcompany.alien-cmd"}));

        // So does a document of a language it asked for.
        QVERIFY(fired(*onLanguage, {"plaintext"}, {}));
        QVERIFY(!fired(*onLanguage, {"cpp"}, {}));

        // And a workspace holding what it named - only then.
        QVERIFY(!fired(*contains, {}, {root}));
        QVERIFY((root / "here.marker").writeFileContents("x").has_value());
        QVERIFY(fired(*contains, {}, {root}));

        // A brace group names either.
        const Result<VscodeManifest> braces
            = writeMockExtension(root / "braces", "alien-braces", js,
                                 QJsonArray{"workspaceContains:**/*.{other,marker}"});
        QVERIFY2(braces.has_value(), qPrintable(braces ? QString() : braces.error()));
        QVERIFY(fired(*braces, {}, {root}));

        // Given a cache, the walk is the caller's: the question is entered
        // unanswered, and the event waits for the answer.
        WorkspaceContainsCache cache;
        const std::pair<FilePath, QString> question{root, "**/*.marker"};
        QVERIFY(!activationEventFired(*contains, {}, {root}, {}, {}, &cache));
        QVERIFY(cache.contains(question));
        QVERIFY(!cache.value(question).has_value());
        cache.insert(question, true);
        QVERIFY(activationEventFired(*contains, {}, {root}, {}, {}, &cache));

        // The one that names only an event we cannot offer keeps waiting, so
        // it never runs on the strength of an event that will not come.
        QVERIFY(!fired(*debugOnly, {"cpp"}, {root}, {"other.extension"}));
    }

    void testCommandWakesTheExtensionThatHasIt()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        // The one that has the command, waiting for exactly it.
        const Result<VscodeManifest> provider = writeMockExtension(
            root / "provider", "alien-provider",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(\n"
            "      vscode.commands.registerCommand('alien.provided.answer', () => 42));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"onCommand:alien.provided.answer"});
        // The one that asks for it without knowing whether it is running.
        const Result<VscodeManifest> caller = writeMockExtension(
            root / "caller", "alien-caller",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(\n"
            "      vscode.commands.registerCommand('alien.caller.ask', async () => {\n"
            "        const answer = await vscode.commands.executeCommand("
            "'alien.provided.answer');\n"
            "        vscode.window.showInformationMessage('answer:' + answer);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        for (const Result<VscodeManifest> *m : {&provider, &caller})
            QVERIFY2(m->has_value(), qPrintable(*m ? QString() : m->error()));

        ExtensionHost host(node);
        int woken = 0;
        // What the plugin does with such a request: start the extension that
        // contributes the command, and answer once it has registered it.
        host.wakeCommandOwner = [&host, &provider, &woken](
                                    const QString &command,
                                    const std::function<void(bool)> &done) {
            if (command != "alien.provided.answer") {
                done(false);
                return;
            }
            ++woken;
            const auto waiting = std::make_shared<QMetaObject::Connection>();
            *waiting = connect(&host, &ExtensionHost::commandsChanged, &host,
                               [&host, waiting, command, done] {
                                   if (!host.registeredCommands().contains(command))
                                       return;
                                   disconnect(*waiting);
                                   done(true);
                               });
            host.activate(*provider);
        };

        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*caller);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.caller.ask"), 15000);
        host.executeCommand("alien.caller.ask");

        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("answer:"))
                    return text;
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 15000);
        // Not waking it leaves the call resolving to undefined, which the
        // caller has no way to tell from a command that answered nothing.
        QCOMPARE(said(), QString("answer:42"));
        QCOMPARE(woken, 1);
    }

    void testDiagnosticsAreReadableBack()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // One reports, the other asks - which is how an extension finds out
        // whether the project it is about to launch still has errors.
        const Result<VscodeManifest> reporter = writeMockExtension(
            FilePath::fromString(dir.path()) / "reporter", "alien-diag-reporter",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const collection = vscode.languages.createDiagnosticCollection('alien-diag');\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.diag.report',\n"
            "      () => collection.set(vscode.Uri.file('/tmp/alien-diag.txt'), [\n"
            "        {range: new vscode.Range(2, 0, 2, 4), message: 'broken',\n"
            "         severity: vscode.DiagnosticSeverity.Error, source: 'alien'}])));\n"
            "}\n"
            "module.exports = { activate };\n");
        const Result<VscodeManifest> asker = writeMockExtension(
            FilePath::fromString(dir.path()) / "asker", "alien-diag-asker",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const uri = vscode.Uri.file('/tmp/alien-diag.txt');\n"
            "  context.subscriptions.push(vscode.languages.onDidChangeDiagnostics(() => {\n"
            "    const forFile = vscode.languages.getDiagnostics(uri);\n"
            "    const all = vscode.languages.getDiagnostics();\n"
            "    const listed = all.filter(e => e[0].fsPath === uri.fsPath).length;\n"
            "    vscode.window.showInformationMessage('diag:' + forFile.length + ':'\n"
            "        + (forFile[0] || {}).message + ':' + (forFile[0] || {}).severity\n"
            "        + ':' + listed);\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        for (const Result<VscodeManifest> *m : {&reporter, &asker})
            QVERIFY2(m->has_value(), qPrintable(*m ? QString() : m->error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*asker);
        host.activate(*reporter);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.diag.report"), 15000);
        host.executeCommand("alien.diag.report");

        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("diag:"))
                    return text;
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 15000);
        // One diagnostic, with what was reported about it intact, and the same
        // file listed when asked for everything.
        QCOMPARE(said(), QString("diag:1:broken:0:1"));
    }

    void testEditorTitleButton()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "titled.txt";
        QVERIFY(file.writeFileContents("hello\n").has_value());

        const QJsonObject contributes{
            {"commands", QJsonArray{QJsonObject{{"command", "alien.title.go"},
                                                {"title", "Go Somewhere"}}}},
            {"menus", QJsonObject{{"editor/title",
                                   QJsonArray{QJsonObject{{"command", "alien.title.go"},
                                                          {"when", "alienTitleReady"},
                                                          {"group", "navigation"}}}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "titled", "alien-title",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.title.go',\n"
            "      () => vscode.window.showInformationMessage('pressed')));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.title.ready',\n"
            "      () => vscode.commands.executeCommand('setContext', 'alienTitleReady', true)));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.title.ping',\n"
            "      () => vscode.window.showInformationMessage('ping')));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.title.go"), 15000);

        QVERIFY(EditorManager::openEditor(file));
        TextEditor::TextEditorWidget *widget
            = TextEditor::TextEditorWidget::fromEditor(EditorManager::currentEditor());
        QVERIFY(widget);
        const auto button = [widget] {
            return Utils::findOr(widget->toolBar()->actions(), nullptr,
                                 [](QAction *action) {
                                     return action->text() == "Go Somewhere"
                                            && action->isVisible();
                                 });
        };

        const auto said = [&spy](const QString &text) {
            return Utils::anyOf(spy, [&text](const QList<QVariant> &args) {
                return args.first().toString() == text;
            });
        };
        // Answering this took everything the editor being opened produced with
        // it, so what is not there now was never coming: the clause is not
        // satisfied, and the button its entry asks for is not offered.
        host.executeCommand("alien.title.ping");
        QTRY_VERIFY_WITH_TIMEOUT(said("ping"), 15000);
        QVERIFY(!button());

        host.executeCommand("alien.title.ready");
        QTRY_VERIFY_WITH_TIMEOUT(button(), 15000);

        // And it runs what it was contributed for.
        button()->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(said("pressed"), 15000);

        // Without a host it would do nothing.
        host.shutdown();
        QVERIFY(!button());
        EditorManager::closeAllEditors(false);
    }

    void testEditorContextMenuEntry()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath plain = root / "plain.txt";
        const FilePath special = root / "special.ctx";
        QVERIFY(plain.writeFileContents("one\n").has_value());
        QVERIFY(special.writeFileContents("two\n").has_value());

        const QJsonObject contributes{
            {"commands", QJsonArray{QJsonObject{{"command", "alien.ctx.go"},
                                                {"title", "Do The Thing"}}}},
            {"menus", QJsonObject{{"editor/context",
                                   QJsonArray{QJsonObject{
                                       {"command", "alien.ctx.go"},
                                       {"when", "resourceExtname == '.ctx'"}}}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "ctx", "alien-ctx",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.ctx.go',\n"
            "      () => vscode.window.showInformationMessage('ran')));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.ctx.ping',\n"
            "      () => vscode.window.showInformationMessage('ping')));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.ctx.go"), 15000);

        Core::ActionContainer *container = Core::ActionManager::actionContainer(
            TextEditor::Constants::M_STANDARDCONTEXTMENU);
        QVERIFY(container);
        const auto entry = [container] {
            return Utils::findOr(container->menu()->actions(), nullptr, [](QAction *action) {
                return action->text() == "Do The Thing" && action->isVisible();
            });
        };
        const auto said = [&spy](const QString &text) {
            return Utils::anyOf(spy, [&text](const QList<QVariant> &args) {
                return args.first().toString() == text;
            });
        };

        // The clause names a file this is not, and answering the ping took
        // everything the editor being opened produced with it.
        QVERIFY(EditorManager::openEditor(plain));
        host.executeCommand("alien.ctx.ping");
        QTRY_VERIFY_WITH_TIMEOUT(said("ping"), 15000);
        QVERIFY(!entry());

        QVERIFY(EditorManager::openEditor(special));
        QTRY_VERIFY_WITH_TIMEOUT(entry(), 15000);
        entry()->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(said("ran"), 15000);
        EditorManager::closeAllEditors(false);
    }

    void testWhenClauseShapes()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath other = root / "other.md";
        const FilePath wanted = root / "sub" / "deep" / "plain+one.txt";
        QVERIFY(other.writeFileContents("md\n").has_value());
        QVERIFY(wanted.parentDir().ensureWritableDir().has_value());
        QVERIFY(wanted.writeFileContents("txt\n").has_value());

        // The three shapes the installed extensions write and that were read
        // wrongly: a bare literal holding a "+", a regular expression holding a
        // "/" inside a character class, and a guard on workspace trust.
        const auto entry = [](const QString &command, const QString &title,
                              const QString &when) {
            return QJsonObject{{"command", command}, {"title", title}, {"when", when}};
        };
        const QJsonObject contributes{
            {"commands", QJsonArray{
                QJsonObject{{"command", "alien.when.plus"}, {"title", "Plus Literal"}},
                QJsonObject{{"command", "alien.when.regex"}, {"title", "Regex Class"}},
                QJsonObject{{"command", "alien.when.trust"}, {"title", "Trust Guarded"}}}},
            {"menus", QJsonObject{{"editor/context", QJsonArray{
                entry("alien.when.plus", "Plus Literal",
                      "resourceFilename == plain+one.txt"),
                entry("alien.when.regex", "Regex Class",
                      "resourcePath =~ /.*sub[/|\\\\]deep.*\\.txt/"),
                entry("alien.when.trust", "Trust Guarded",
                      "isWorkspaceTrusted && resourceExtname == '.txt'")}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "when", "alien-when",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  for (const name of ['plus', 'regex', 'trust', 'ping']) {\n"
            "    context.subscriptions.push(vscode.commands.registerCommand(\n"
            "        'alien.when.' + name,\n"
            "        () => vscode.window.showInformationMessage(name)));\n"
            "  }\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.when.plus"), 15000);

        Core::ActionContainer *container = Core::ActionManager::actionContainer(
            TextEditor::Constants::M_STANDARDCONTEXTMENU);
        QVERIFY(container);
        const auto shown = [container] {
            QStringList titles;
            for (QAction *action : container->menu()->actions()) {
                const QString text = action->text();
                if (action->isVisible()
                    && (text == "Plus Literal" || text == "Regex Class"
                        || text == "Trust Guarded")) {
                    titles.append(text);
                }
            }
            titles.sort();
            return titles;
        };

        // None of the three names this file, and answering the ping took what
        // opening it produced with it.
        QVERIFY(EditorManager::openEditor(other));
        host.executeCommand("alien.when.ping");
        QTRY_VERIFY_WITH_TIMEOUT(Utils::anyOf(spy, [](const QList<QVariant> &args) {
            return args.first().toString() == "ping";
        }), 15000);
        QCOMPARE(shown(), QStringList());

        QVERIFY(EditorManager::openEditor(wanted));
        QTRY_COMPARE_WITH_TIMEOUT(shown(),
                                  QStringList({"Plus Literal", "Regex Class", "Trust Guarded"}),
                                  15000);
        EditorManager::closeAllEditors(false);
    }

    // The languages no extension contributes because the editor is expected to
    // have them: without these, every real source file is "plaintext" and an
    // extension waiting for "onLanguage:cpp" never starts.
    void testLanguageIdOfAKnownFile()
    {
        const QList<QPair<QString, QString>> expected = {
            {"a.cpp", "cpp"}, {"a.hpp", "cpp"}, {"a.c", "c"},
            {"a.py", "python"}, {"a.json", "json"}, {"a.md", "markdown"},
            {"a.xml", "xml"}, {"CMakeLists.txt", "cmake"}, {"a.txt", "plaintext"},
            // No suffix and no mime type of its own, so only the name says so.
            {"Dockerfile", "dockerfile"}, {"Dockerfile.debug", "dockerfile"},
            {"debug.dockerfile", "dockerfile"},
            // Nothing recognizes this, and saying so is not the same as guessing.
            {"a.zzzz", ""},
        };
        for (const auto &[fileName, languageId] : expected) {
            const QString found
                = builtinLanguageId(FilePath::fromString("/tmp/alien-lang/" + fileName));
            QVERIFY2(found == languageId,
                     qPrintable(QString("%1: got \"%2\", expected \"%3\"")
                                    .arg(fileName, found, languageId)));
        }
    }

    // A colour an extension found in the text, shown where the editor already
    // marks lines - VS Code paints a swatch inside the line, which there is no
    // room for here.
    void testDocumentColors()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "colored.txt";
        QVERIFY(file.writeFileContents("one\ntwo\nthree\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "color", "alien-color",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerColorProvider(\n"
            "      {language: 'plaintext'}, {\n"
            "        provideDocumentColors(document) {\n"
            "          return [{range: new vscode.Range(1, 0, 1, 3),\n"
            "                   color: new vscode.Color(1, 0, 0.5, 1)}];\n"
            "        },\n"
            "        provideColorPresentations() { return []; },\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 15000);
        QVERIFY(EditorManager::openEditor(file));
        TextEditor::TextDocument *document
            = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(document);

        const auto colorOfMark = [document] {
            for (TextEditor::TextMark *mark : document->marks()) {
                if (mark->lineNumber() == 2)
                    return mark->annotationColor();
            }
            return QColor();
        };
        QTRY_VERIFY_WITH_TIMEOUT(colorOfMark().isValid(), 15000);
        QCOMPARE(colorOfMark().name(QColor::HexRgb), QColor::fromRgbF(1, 0, 0.5).name());
        EditorManager::closeAllEditors(false);
    }

    // A language is claimed by whole name and by glob as well as by suffix:
    // "Dockerfile" and "qmldir" have no extension to go by, and a pattern like
    // "Dockerfile.*" covers what a name cannot.
    void testLanguageClaimedByNameOrGlob()
    {
        VscodeLanguage byExtension;
        byExtension.id = "qml";
        byExtension.extensions = {".qml"};

        VscodeLanguage byName;
        byName.id = "qmldir";
        byName.filenames = {"qmldir"};

        VscodeLanguage byGlob;
        byGlob.id = "dockerfile";
        byGlob.filenamePatterns = {"*.dockerfile", "Dockerfile", "Dockerfile.*"};

        VscodeLanguage byPathGlob;
        byPathGlob.id = "ssh_config";
        byPathGlob.filenamePatterns = {"**/.ssh/config"};

        const auto path = [](const QString &s) { return FilePath::fromString(s); };

        QVERIFY(languageMatchesFile(byExtension, path("/p/a.qml")));
        QVERIFY(!languageMatchesFile(byExtension, path("/p/qmldir")));

        VscodeLanguage byLongExtension;
        byLongExtension.id = "terraform-vars";
        byLongExtension.extensions = {".tf.json"};
        QVERIFY(languageMatchesFile(byLongExtension, path("/p/a.tf.json")));
        QVERIFY(!languageMatchesFile(byLongExtension, path("/p/a.json")));

        QVERIFY(languageMatchesFile(byName, path("/p/qmldir")));
        QVERIFY(!languageMatchesFile(byName, path("/p/qmldir.txt")));

        QVERIFY(languageMatchesFile(byGlob, path("/p/Dockerfile")));
        QVERIFY(languageMatchesFile(byGlob, path("/p/Dockerfile.dev")));
        QVERIFY(languageMatchesFile(byGlob, path("/p/build.dockerfile")));
        QVERIFY(!languageMatchesFile(byGlob, path("/p/Dockerfile/inside.txt")));

        // A pattern with a separator is about the path, and "**" crosses
        // directories where a single "*" does not.
        QVERIFY(languageMatchesFile(byPathGlob, path("/home/u/.ssh/config")));
        QVERIFY(!languageMatchesFile(byPathGlob, path("/home/u/.ssh/config.bak")));
        QVERIFY(!languageMatchesFile(byPathGlob, path("/home/u/config")));
    }

    // What a view says when it has nothing in it, and the clause that decides
    // whether it says it: an empty panel with no explanation is what an
    // extension contributes this to avoid.
    void testViewWelcome()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QJsonObject contributes{
            {"views", QJsonObject{{"alienBar", QJsonArray{
                QJsonObject{{"id", "alien.welcome.view"}, {"name", "Welcome View"}}}}}},
            {"viewsWelcome", QJsonArray{
                QJsonObject{{"view", "alien.welcome.view"},
                            {"when", "!alienWelcomeReady"},
                            {"contents", "Nothing here yet.\n"
                                         "[Set It Up](command:alien.welcome.setup)"}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "welcome", "alien-welcome",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.welcome.setup', () => {}));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.welcome.ready',\n"
            "      () => vscode.commands.executeCommand('setContext',\n"
            "                                           'alienWelcomeReady', true)));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.welcome.ping',\n"
            "      () => vscode.window.showInformationMessage('ping')));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(!host.window().viewWelcome("alien.welcome.view").isEmpty(), 15000);
        QVERIFY(host.window().viewWelcome("alien.welcome.view").contains("Nothing here yet."));
        QVERIFY(
            host.window().viewWelcome("alien.welcome.view").contains("command:alien.welcome.setup"));

        // Once the clause it is guarded by holds, the view has nothing to say.
        host.executeCommand("alien.welcome.ready");
        QTRY_VERIFY_WITH_TIMEOUT(host.window().viewWelcome("alien.welcome.view").isEmpty(), 15000);

        // A view nobody contributed anything for says nothing, and answering
        // the ping took everything that was on its way with it.
        host.executeCommand("alien.welcome.ping");
        QTRY_VERIFY_WITH_TIMEOUT(Utils::anyOf(spy, [](const QList<QVariant> &args) {
            return args.first().toString() == "ping";
        }), 15000);
        QVERIFY(host.window().viewWelcome("alien.other.view").isEmpty());
    }

    void testViewWelcomeRichText()
    {
        // A link becomes one, and what is around it stays text - including the
        // characters that would otherwise be markup.
        QCOMPARE(welcomeAsRichText("[Set It Up](command:alien.setup)"),
                 QString("<a href=\"alien.setup\">Set It Up</a>"));
        QCOMPARE(welcomeAsRichText("a < b & c"), QString("a &lt; b &amp; c"));
        QCOMPARE(welcomeAsRichText("one\ntwo"), QString("one<br/>two"));
        QCOMPARE(welcomeAsRichText("Run [it](command:x) now"),
                 QString("Run <a href=\"x\">it</a> now"));
        // A link to anything else is not one of ours, so it stays as written.
        QCOMPARE(welcomeAsRichText("[Doc](https://example.com)"),
                 QString("[Doc](https://example.com)"));
    }

    // What an extension offers for a file in the project tree. The clause is
    // about the file that was picked, not the one being edited, so what is
    // offered for one file is not what is offered for the next.
    void testProjectTreeMenu()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const auto entry = [](const QString &command, const QString &when) {
            return QJsonObject{{"command", command}, {"when", when}};
        };
        const QJsonObject contributes{
            {"commands", QJsonArray{
                QJsonObject{{"command", "alien.tree.build"}, {"title", "Build Image"}},
                QJsonObject{{"command", "alien.tree.folder"}, {"title", "Add Folder"}}}},
            {"menus", QJsonObject{{"explorer/context", QJsonArray{
                entry("alien.tree.build", "resourceFilename == Dockerfile"),
                entry("alien.tree.folder", "explorerResourceIsFolder")}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "tree", "alien-tree",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  for (const name of ['build', 'folder']) {\n"
            "    context.subscriptions.push(vscode.commands.registerCommand(\n"
            "        'alien.tree.' + name,\n"
            "        uri => vscode.window.showInformationMessage(\n"
            "                   name + ':' + (uri && uri.fsPath))));\n"
            "  }\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.tree.build"), 15000);

        QStringList offered;
        // QTRY_VERIFY returns on failure, so this fills a result rather than
        // handing one back.
        const auto ask = [&host, &offered](const FilePath &path, bool isFolder) {
            offered.clear();
            bool answered = false;
            host.editor().requestResourceMenu("explorer/context", path, isFolder,
                                     [&offered, &answered](const QJsonArray &items) {
                                         for (const QJsonValue &value : items) {
                                             offered.append(
                                                 value.toObject().value("command").toString());
                                         }
                                         answered = true;
                                     });
            QTRY_VERIFY_WITH_TIMEOUT(answered, 15000);
            offered.sort();
        };

        ask(root / "Dockerfile", false);
        QCOMPARE(offered, QStringList({"alien.tree.build"}));
        ask(root / "other.txt", false);
        QCOMPARE(offered, QStringList());
        ask(root / "sub", true);
        QCOMPARE(offered, QStringList({"alien.tree.folder"}));

        // Stepping on before the first answer is back: what was offered for
        // the file left behind is not offered for the one now selected.
        ProjectExplorer::FileNode docker(root / "Dockerfile", ProjectExplorer::FileType::Unknown);
        ProjectExplorer::FileNode other(root / "other.txt", ProjectExplorer::FileType::Unknown);
        host.editor().updateProjectTreeActions(&docker);
        host.editor().updateProjectTreeActions(&other);
        // Answered in order, so both of those are in once this one is.
        ask(root / "other.txt", false);
        QAction *build = host.editor().m_projectTreeActions.value("file|alien.tree.build");
        QVERIFY(!build || !build->isVisible());
        QCOMPARE(host.editor().m_projectTreeResource, host.toHostPath(root / "other.txt"));

        // And the command is called with the file it was offered for, as a Uri
        // rather than the path Qt Creator holds it as.
        host.executeCommand("alien.tree.build",
                            QJsonArray{QJsonObject{{"$uri", host.toHostPath(root / "Dockerfile")}}});
        const QString expected = "build:" + host.toHostPath(root / "Dockerfile");
        QTRY_VERIFY_WITH_TIMEOUT(Utils::anyOf(spy, [&expected](const QList<QVariant> &args) {
            return args.first().toString() == expected;
        }), 15000);
    }

    // What getConfiguration() answers, against what the API says it should:
    // a section is a value too, and a default is not something the user set.
    void testConfigurationSemantics()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QJsonObject contributes{
            {"configuration", QJsonObject{{"properties", QJsonObject{
                {"alienp.num", QJsonObject{{"type", "number"}, {"default", 42}}},
                {"alienp.nested.deep", QJsonObject{{"type", "string"}, {"default", "d"}}},
                {"alienp.nested.other", QJsonObject{{"type", "string"}, {"default", "o"}}}}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "cfg", "alien-cfg-semantics",
            "const vscode = require('vscode');\n"
            "function activate() {\n"
            "  const c = vscode.workspace.getConfiguration('alienp');\n"
            "  const say = (n, v) => vscode.window.showInformationMessage(\n"
            "      n + '=' + JSON.stringify(v));\n"
            "  say('num', c.get('num'));\n"
            "  say('deep', c.get('nested.deep'));\n"
            "  say('group', c.get('nested'));\n"
            "  say('hasGroup', c.has('nested'));\n"
            "  say('hasMissing', c.has('missing'));\n"
            "  say('fallback', c.get('missing', 'fb'));\n"
            "  say('globalValue', c.inspect('num').globalValue);\n"
            "  say('defaultValue', c.inspect('num').defaultValue);\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        // Nothing has been written over the defaults, which is what the
        // production side sends in that case.
        host.setConfiguration({});
        host.activate(*manifest);

        const auto answer = [&spy](const QString &name) {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith(name + "="))
                    return text.mid(name.size() + 1);
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!answer("defaultValue").isEmpty(), 15000);

        QCOMPARE(answer("num"), QString("42"));
        QCOMPARE(answer("deep"), QString("\"d\""));
        // A section is a value: reading a group of settings at once finds them.
        QCOMPARE(answer("group"), QString("{\"deep\":\"d\",\"other\":\"o\"}"));
        QCOMPARE(answer("hasGroup"), QString("true"));
        QCOMPARE(answer("hasMissing"), QString("false"));
        QCOMPARE(answer("fallback"), QString("\"fb\""));
        QCOMPARE(answer("defaultValue"), QString("42"));
        // What the extension declared is not what the user chose, and an
        // extension asks which of the two it is looking at.
        QCOMPARE(answer("globalValue"), QString("undefined"));
    }

    // Finding a neighbour before it runs, which is how an extension decides
    // whether it is installed at all and then starts it for its API.
    void testExtensionFoundBeforeItRuns()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const Result<VscodeManifest> neighbour = writeMockExtension(
            root / "neighbour", "alien-neighbour",
            "function activate() { return { answer: 42 }; }\n"
            "module.exports = { activate };\n",
            QJsonArray{"onCommand:alien.neighbour.never"});
        const Result<VscodeManifest> asker = writeMockExtension(
            root / "asker", "alien-asker",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.ask.look',\n"
            "      () => {\n"
            "        const e = vscode.extensions.getExtension('theqtcompany.alien-neighbour');\n"
            "        vscode.window.showInformationMessage(\n"
            "            'look:' + (e ? e.isActive : 'missing')\n"
            "            + ':' + (e && e.packageJSON ? e.packageJSON.name : ''));\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.ask.use',\n"
            "      async () => {\n"
            "        const e = vscode.extensions.getExtension('theqtcompany.alien-neighbour');\n"
            "        const api = await e.activate();\n"
            "        vscode.window.showInformationMessage('use:' + (api && api.answer));\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        // Waits for the neighbour while activating itself.
        const Result<VscodeManifest> eager = writeMockExtension(
            root / "eager", "alien-eager",
            "const vscode = require('vscode');\n"
            "async function activate() {\n"
            "  const e = vscode.extensions.getExtension('theqtcompany.alien-neighbour');\n"
            "  const api = await e.activate();\n"
            "  vscode.window.showInformationMessage('eager:' + (api && api.answer));\n"
            "}\n"
            "module.exports = { activate };\n");
        for (const Result<VscodeManifest> *m : {&neighbour, &asker, &eager})
            QVERIFY2(m->has_value(), qPrintable(*m ? QString() : m->error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        // What the plugin does: it knows every enabled extension, and starts
        // one when another asks for it.
        host.setKnownExtensions({*neighbour, *asker});
        host.startExtension = [&host, &neighbour](const QString &id,
                                                  const std::function<void(bool)> &done) {
            if (id != neighbour->qualifiedId()) {
                done(false);
                return;
            }
            const auto waiting = std::make_shared<QMetaObject::Connection>();
            *waiting = connect(&host, &ExtensionHost::activated, &host,
                               [waiting, id, done](const QString &activated) {
                                   if (activated != id)
                                       return;
                                   disconnect(*waiting);
                                   done(true);
                               });
            host.activate(*neighbour);
        };

        host.activate(*asker);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.ask.look"), 15000);

        const auto said = [&spy](const QString &prefix) {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith(prefix))
                    return text;
            }
            return QString();
        };

        // Installed and enabled but not running: found, and known not to be
        // running, with its manifest readable.
        host.executeCommand("alien.ask.look");
        QTRY_VERIFY_WITH_TIMEOUT(!said("look:").isEmpty(), 15000);
        QCOMPARE(said("look:"), QString("look:false:alien-neighbour"));

        host.activate(*eager);
        QTRY_VERIFY_WITH_TIMEOUT(!said("eager:").isEmpty(), 15000);
        QCOMPARE(said("eager:"), QString("eager:42"));

        // Asking it to activate starts it, and what it returns is its API.
        host.executeCommand("alien.ask.use");
        QTRY_VERIFY_WITH_TIMEOUT(!said("use:").isEmpty(), 15000);
        QCOMPARE(said("use:"), QString("use:42"));
    }

    // A document is the one Qt Creator has, whatever the path it was asked
    // for looked like. An extension builds paths out of its own directory
    // ("__dirname/../x"), and a document filed under that spelling never hears
    // about the file again - edits to it went nowhere, and said they had not.
    void testDocumentPathIsResolved()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "sample.txt";
        QVERIFY((root / "sub").ensureWritableDir().has_value());
        QVERIFY(file.writeFileContents("zero\nAAA\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "ext", "alien-path",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.path.edit',\n"
            "      async dotted => {\n"
            "        const d = await vscode.workspace.openTextDocument(dotted);\n"
            "        const ed = await vscode.window.showTextDocument(d);\n"
            "        await ed.edit(b => b.replace(new vscode.Range(1, 0, 1, 3), 'XX'));\n"
            "        vscode.window.showInformationMessage('edited:' + d.getText().split('\\n')[1]\n"
            "                                             + ':' + d.version);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.path.edit"), 15000);

        // The same file, spelled the way an extension would build it.
        const QString dotted = host.toHostPath(root) + "/sub/../sample.txt";
        host.executeCommand("alien.path.edit", QJsonArray{QJsonObject{{"$uri", dotted}}});

        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("edited:"))
                    return text;
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 15000);
        // The edit reached the document, and the extension can see that it did.
        QCOMPARE(said(), QString("edited:XX:2"));

        TextEditor::TextDocument *document
            = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(document);
        QCOMPARE(document->plainText(), QString("zero\nXX\n"));
        EditorManager::closeAllEditors(false);
    }

    // Picking several, and typing a secret. Both are options on the prompt an
    // extension puts up, and both were ignored: one item came back where a
    // list was asked for, and a token was typed in the clear.
    void testPromptOptions()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "prompt", "alien-prompt",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.prompt.many',\n"
            "      async () => {\n"
            "        const picked = await vscode.window.showQuickPick(\n"
            "            ['one', 'two', 'three'], {canPickMany: true});\n"
            "        vscode.window.showInformationMessage(\n"
            "            'many:' + (Array.isArray(picked) ? picked.join('+') : 'notAList'));\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.prompt.one',\n"
            "      async () => {\n"
            "        const picked = await vscode.window.showQuickPick(['one', 'two', 'three']);\n"
            "        vscode.window.showInformationMessage('one:' + picked);\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.prompt.secret',\n"
            "      async () => {\n"
            "        const token = await vscode.window.showInputBox({password: true});\n"
            "        vscode.window.showInformationMessage('secret:' + token);\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);

        bool askedForMany = false;
        connect(&host, &ExtensionHost::quickPickRequested, &host,
                [&host, &askedForMany](int id, const QStringList &, const QString &,
                                       bool canPickMany) {
                    askedForMany = canPickMany;
                    host.window()
                        .resolveQuickPick(id, canPickMany ? QList<int>{0, 2} : QList<int>{1});
                });
        bool askedForSecret = false;
        connect(&host, &ExtensionHost::inputBoxRequested, &host,
                [&host, &askedForSecret](int id, const QString &, const QString &,
                                         const QString &, bool password) {
                    askedForSecret = password;
                    host.window().resolveInputBox(id, "hunter2", true);
                });

        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.prompt.many"), 15000);

        const auto said = [&spy](const QString &prefix) {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith(prefix))
                    return text;
            }
            return QString();
        };

        // Asking to pick many is answered with every one that was picked.
        host.executeCommand("alien.prompt.many");
        QTRY_VERIFY_WITH_TIMEOUT(!said("many:").isEmpty(), 15000);
        QVERIFY(askedForMany);
        QCOMPARE(said("many:"), QString("many:one+three"));

        // Asking for one still answers with the item itself, not a list.
        host.executeCommand("alien.prompt.one");
        QTRY_VERIFY_WITH_TIMEOUT(!said("one:").isEmpty(), 15000);
        QCOMPARE(said("one:"), QString("one:two"));

        // And a prompt for a secret is told that it is one.
        host.executeCommand("alien.prompt.secret");
        QTRY_VERIFY_WITH_TIMEOUT(!said("secret:").isEmpty(), 15000);
        QVERIFY(askedForSecret);
        QCOMPARE(said("secret:"), QString("secret:hunter2"));
    }

    // A long job an extension says can be stopped: the button Qt Creator shows
    // is the only way it hears about the user giving up, and the token it was
    // handed never turned true.
    void testProgressCancellation()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "prog", "alien-progress",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.prog.run',\n"
            "      () => vscode.window.withProgress(\n"
            "          {location: 15, title: 'Working', cancellable: true},\n"
            "          async (progress, token) => {\n"
            "            let asked = false;\n"
            "            token.onCancellationRequested(() => { asked = true; });\n"
            "            vscode.window.showInformationMessage('started');\n"
            "            for (let i = 0; i < 200; ++i) {\n"
            "              if (token.isCancellationRequested)\n"
            "                break;\n"
            "              progress.report({increment: 1});\n"
            "              await new Promise(r => setTimeout(r, 50));\n"
            "            }\n"
            "            vscode.window.showInformationMessage(\n"
            "                'stopped:' + token.isCancellationRequested + ':' + asked);\n"
            "          })));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.prog.run"), 15000);

        const auto said = [&spy](const QString &prefix) {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith(prefix))
                    return text;
            }
            return QString();
        };

        host.executeCommand("alien.prog.run");
        // Running, so there is something to stop.
        QTRY_VERIFY_WITH_TIMEOUT(!said("started").isEmpty(), 15000);

        // What the button does.
        Core::ProgressManager::cancelTasks("Alien.Progress");

        QTRY_VERIFY_WITH_TIMEOUT(!said("stopped:").isEmpty(), 20000);
        // It stopped because it was asked to, and it was told as well.
        QCOMPARE(said("stopped:"), QString("stopped:true:true"));
    }

    // What can be asked for is more than what is running: asking for a
    // contributed command is what starts the extension that has it, so an
    // extension checking the list first must be told it is there.
    void testCommandListIncludesContributed()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());

        const QJsonObject contributes{
            {"commands", QJsonArray{
                QJsonObject{{"command", "alien.sleeping.run"}, {"title", "Run"}},
                QJsonObject{{"command", "_alien.sleeping.hidden"}, {"title", "Hidden"}}}}};
        const Result<VscodeManifest> sleeping = writeMockExtension(
            root / "sleeping", "alien-sleeping",
            "function activate() {}\nmodule.exports = { activate };\n",
            QJsonArray{"onCommand:alien.sleeping.run"}, contributes);
        const Result<VscodeManifest> asker = writeMockExtension(
            root / "asker", "alien-cmdlist",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.cmdlist.ask',\n"
            "      async () => {\n"
            "        const all = await vscode.commands.getCommands(true);\n"
            "        const every = await vscode.commands.getCommands();\n"
            "        vscode.window.showInformationMessage(\n"
            "            'list:' + all.includes('alien.sleeping.run')\n"
            "            + ':' + all.includes('_alien.sleeping.hidden')\n"
            "            + ':' + every.includes('_alien.sleeping.hidden'));\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        for (const Result<VscodeManifest> *m : {&sleeping, &asker})
            QVERIFY2(m->has_value(), qPrintable(*m ? QString() : m->error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.setKnownExtensions({*sleeping, *asker});
        host.activate(*asker); // the other one stays asleep
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.cmdlist.ask"), 15000);

        host.executeCommand("alien.cmdlist.ask");
        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("list:"))
                    return text;
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 15000);
        // Listed although nothing has started it, and the internal one is left
        // out only when the caller asked for that.
        QCOMPARE(said(), QString("list:true:false:true"));
    }

    // What an extension asks of its own output pane. Showing it without
    // taking the mode is the difference between a log that reports and one
    // that interrupts, and the level it is told decides how much it writes.
    void testOutputChannelAsks()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "out", "alien-output",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const c = vscode.window.createOutputChannel('Alien Out', {log: true});\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.out.level',\n"
            "      () => vscode.window.showInformationMessage('level:' + c.logLevel)));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.out.quiet',\n"
            "      () => c.show(true)));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.out.loud',\n"
            "      () => c.show()));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        QSignalSpy shown(&host, &ExtensionHost::channelShowRequested);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.out.quiet"), 15000);

        // Info, the level the editor itself reports, so an extension does not
        // decide to write every trace line it has.
        host.executeCommand("alien.out.level");
        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("level:"))
                    return text;
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 15000);
        QCOMPARE(said(), QString("level:3"));

        host.executeCommand("alien.out.quiet");
        QTRY_COMPARE_WITH_TIMEOUT(shown.count(), 1, 15000);
        QCOMPARE(shown.first().first().toBool(), true); // asked not to be switched to

        host.executeCommand("alien.out.loud");
        QTRY_COMPARE_WITH_TIMEOUT(shown.count(), 2, 15000);
        QCOMPARE(shown.at(1).first().toBool(), false);
    }

    // The tabs the user has in front of them. Extensions walk these to find
    // what is open; without them, reading tabGroups.all threw and took the
    // extension down with it.
    void testTabGroups()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath one = root / "one.txt";
        const FilePath two = root / "two.txt";
        QVERIFY(one.writeFileContents("one\n").has_value());
        QVERIFY(two.writeFileContents("two\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "tabs", "alien-tabs",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  let opened = 0, changed = 0;\n"
            "  context.subscriptions.push(\n"
            "      vscode.window.tabGroups.onDidChangeTabs(e => {\n"
            "        opened += e.opened.length; changed += e.changed.length;\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.tabs.report',\n"
            "      () => {\n"
            "        const groups = vscode.window.tabGroups.all;\n"
            "        const g = groups[0];\n"
            "        const names = g.tabs.map(t => t.label).sort().join('+');\n"
            "        const active = g.activeTab;\n"
            "        vscode.window.showInformationMessage(\n"
            "            'tabs:' + groups.length + ':' + names\n"
            "            + ':' + (active ? active.label : 'none')\n"
            "            + ':' + (active && active.input instanceof vscode.TabInputText\n"
            "                     ? 'uri' : 'noUri')\n"
            "            + ':o' + (opened > 0) + 'x' + (changed > 0));\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.tabs.ping',\n"
            "      () => vscode.window.showInformationMessage('ping')));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.tabs.report"), 15000);

        QVERIFY(EditorManager::openEditor(one));
        QVERIFY(EditorManager::openEditor(two));

        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("tabs:"))
                    return text;
            }
            return QString();
        };
        // Answering the ping took the two documents being opened with it, so
        // what is asked next is asked of a host that knows about both.
        host.executeCommand("alien.tabs.ping");
        QTRY_VERIFY_WITH_TIMEOUT(Utils::anyOf(spy, [](const QList<QVariant> &args) {
            return args.first().toString() == "ping";
        }), 15000);

        host.executeCommand("alien.tabs.report");
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 15000);
        // Both files are open, the second is the one in front, its tab names a
        // file, and the extension was told when that changed.
        QCOMPARE(said(), QString("tabs:1:one.txt+two.txt:two.txt:uri:otruextrue"));
        EditorManager::closeAllEditors(false);
    }

    // What a tab holds is found out with `instanceof`, so every kind of input
    // has to be a class even where no tab here ever carries one: an undefined
    // right-hand side throws instead of answering false, which is how
    // qt-core's New Item command died on TabInputCustom.
    void testTabInputKinds()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "held.txt";
        QVERIFY(file.writeFileContents("held\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "inputs", "alien-inputs",
            "const vscode = require('vscode');\n"
            "const kinds = ['TabInputText', 'TabInputTextDiff', 'TabInputCustom',\n"
            "               'TabInputNotebook', 'TabInputNotebookDiff',\n"
            "               'TabInputWebview', 'TabInputTerminal'];\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.inputs.report', () => {\n"
            "        const absent = kinds.filter(k => typeof vscode[k] !== 'function');\n"
            "        const tab = vscode.window.tabGroups.activeTabGroup.activeTab;\n"
            "        const input = tab && tab.input;\n"
            "        const which = input instanceof vscode.TabInputCustom ? 'custom'\n"
            "                    : input instanceof vscode.TabInputText ? 'text' : 'other';\n"
            "        const made = new vscode.TabInputCustom(vscode.Uri.file('/x'), 'view');\n"
            "        vscode.window.showInformationMessage(\n"
            "            'inputs:' + (absent.join(',') || 'none') + ':' + which\n"
            "            + ':' + made.viewType);\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.inputs.ping',\n"
            "      () => vscode.window.showInformationMessage('ping')));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.inputs.report"), 15000);

        QVERIFY(EditorManager::openEditor(file));
        // The ping is answered after the document reached the host, so the
        // report that follows is asked of a host that has the tab.
        host.executeCommand("alien.inputs.ping");
        QTRY_VERIFY_WITH_TIMEOUT(Utils::anyOf(spy, [](const QList<QVariant> &args) {
            return args.first().toString() == "ping";
        }), 15000);

        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("inputs:"))
                    return text;
            }
            return QString();
        };
        host.executeCommand("alien.inputs.report");
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 15000);
        QCOMPARE(said(), QString("inputs:none:text:view"));
        EditorManager::closeAllEditors(false);
    }

    // A file open in an editor of Qt Creator's own that is not a text editor
    // has no text document, and going by the documents alone leaves the user
    // looking at a tab the extension cannot see. hexEditor.openFile reads
    // activeTab.input.uri and threw on undefined.
    void testTabForANonTextEditor()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath blob = root / "blob.bin";
        // Which editor opens a file is decided by what is in it, and a short
        // run of one byte reads as an image.
        QByteArray bytes;
        for (int i = 0; i < 1024; ++i)
            bytes.append(char(i % 256));
        QVERIFY(blob.writeFileContents(bytes).has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "other", "alien-other-editor",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.other.report', () => {\n"
            "        const tab = vscode.window.tabGroups.activeTabGroup.activeTab;\n"
            "        const uri = tab && tab.input && tab.input.uri;\n"
            "        vscode.window.showInformationMessage(\n"
            "            'other:' + (tab ? tab.label : 'none')\n"
            "            + ':' + (uri ? uri.path.split('/').pop() : 'noUri')\n"
            "            + ':' + vscode.window.tabGroups.activeTabGroup.tabs.length);\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.other.ping',\n"
            "      () => vscode.window.showInformationMessage('ping')));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.other.report"), 15000);

        TestOtherEditorFactory factory;
        IEditor *editor = EditorManager::openEditor(blob, "Alien.TestOtherEditor");
        QVERIFY(editor);
        QVERIFY(!qobject_cast<TextEditor::TextDocument *>(editor->document()));

        // The ping is answered after the editor reached the host, so the
        // report that follows is asked of a host that has seen it.
        host.executeCommand("alien.other.ping");
        QTRY_VERIFY_WITH_TIMEOUT(Utils::anyOf(spy, [](const QList<QVariant> &args) {
            return args.first().toString() == "ping";
        }), 15000);

        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("other:"))
                    return text;
            }
            return QString();
        };
        host.executeCommand("alien.other.report");
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 15000);
        QCOMPARE(said(), QString("other:blob.bin:blob.bin:1"));
        EditorManager::closeAllEditors(false);
    }

    // The constants an extension reads off the module. Reading one that is
    // not there throws where the extension expected a number, which takes the
    // extension with it - the same way a missing tabGroups did.
    void testApiConstants()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "enums", "alien-enums",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  // Read at load, as an extension does when it builds its tables.\n"
            "  const values = [\n"
            "    vscode.TextEditorRevealType.InCenter,\n"
            "    vscode.QuickPickItemKind.Separator,\n"
            "    vscode.ShellQuoting.Strong,\n"
            "    vscode.TaskRevealKind.Silent,\n"
            "    vscode.DocumentHighlightKind.Write,\n"
            "    vscode.FileChangeType.Deleted,\n"
            "    vscode.TextDocumentSaveReason.FocusOut,\n"
            "    vscode.QuickInputButtonLocation.Input,\n"
            "    vscode.ColorThemeKind.Dark,\n"
            "    vscode.DecorationRangeBehavior.ClosedClosed,\n"
            "  ].join(',');\n"
            "  const back = vscode.QuickInputButtons.Back ? 'back' : 'noBack';\n"
            "  const list = new vscode.CompletionList([], true);\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.enums.report',\n"
            "      () => vscode.window.showInformationMessage(\n"
            "                'enums:' + values + ':' + back + ':' + list.isIncomplete)));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        // Registering at all means the reads above did not throw.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.enums.report"), 15000);

        host.executeCommand("alien.enums.report");
        const auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("enums:"))
                    return text;
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 15000);
        // The documented numbers, not merely something.
        QCOMPARE(said(), QString("enums:1,-1,2,2,2,3,3,3,2,1:back:true"));
    }

    // Where the text folds, as the extension's server sees it. Qt Creator
    // otherwise folds an unknown language by indentation, which is a guess.
    void testFoldingRanges()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "folded.txt";
        // Flat text: nothing here folds by indentation.
        QVERIFY(file.writeFileContents("zero\none\ntwo\nthree\nfour\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "fold", "alien-folding",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerFoldingRangeProvider(\n"
            "      {language: 'plaintext'}, {\n"
            "        provideFoldingRanges() {\n"
            "          return [new vscode.FoldingRange(1, 3)];\n"
            "        },\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 15000);
        QVERIFY(EditorManager::openEditor(file));
        TextEditor::TextDocument *document
            = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(document);

        // Lines 2 and 3 (0-based 1..3) sit inside the fold the extension named.
        const auto foldedLines = [document] {
            QList<int> lines;
            QTextDocument *contents = document->document();
            for (QTextBlock block = contents->begin(); block != contents->end();
                 block = block.next()) {
                if (TextEditor::TextBlockUserData::foldingIndent(block) > 0)
                    lines.append(block.blockNumber());
            }
            return lines;
        };
        QTRY_COMPARE_WITH_TIMEOUT(foldedLines(), QList<int>({2, 3}), 15000);
        QVERIFY(document->isFoldingIndentExternallyProvided());
        EditorManager::closeAllEditors(false);
    }

    // What the server calls each piece of the text. The tokens arrive packed
    // and relative to one another, so the unpacking is the part that can be
    // wrong without anything failing.
    void testSemanticTokens()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "tokens.txt";
        QVERIFY(file.writeFileContents("  aa    bb     cc\ngamma\n").has_value());

        // Three tokens, the second on the same line as the first and the third
        // on the next: exactly what the delta encoding exists to express.
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "sem", "alien-semantic",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const legend = new vscode.SemanticTokensLegend(\n"
            "      ['keyword', 'string'], ['declaration', 'static']);\n"
            "  context.subscriptions.push(\n"
            "      vscode.languages.registerDocumentSemanticTokensProvider(\n"
            "          {language: 'plaintext'}, {\n"
            "            provideDocumentSemanticTokens() {\n"
            "              const b = new vscode.SemanticTokensBuilder(legend);\n"
            "              b.push(new vscode.Range(0, 2, 0, 4), 'keyword');\n"
            "              b.push(new vscode.Range(0, 8, 0, 10), 'keyword');\n"
            "              b.push(new vscode.Range(0, 15, 0, 17), 'keyword');\n"
            "              b.push(new vscode.Range(1, 0, 1, 5), 'keyword', ['declaration']);\n"
            "              return b.build();\n"
            "            },\n"
            "          }, legend));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 15000);
        QVERIFY(EditorManager::openEditor(file));
        TextEditor::TextDocument *document
            = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(document);
        QVERIFY(document->syntaxHighlighter());

        // The keyword colour, as this editor draws keywords.
        const QTextCharFormat keyword
            = TextEditor::globalFontSettings().data().toTextCharFormat(TextEditor::C_KEYWORD);
        const auto coloured = [document, keyword] {
            QList<int> starts;
            const QTextBlock block = document->document()->findBlockByNumber(0);
            for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
                if (range.format.foreground() == keyword.foreground())
                    starts.append(range.start);
            }
            return starts;
        };
        // Three tokens on one line, at the columns the extension named. Their
        // deltas differ from their absolute positions, so an encoding that
        // confused the two would put the later ones at 10 and 25.
        QTRY_COMPARE_WITH_TIMEOUT(coloured(), QList<int>({2, 8, 15}), 15000);

        // A modifier is part of what a token is: the same keyword marked as a
        // declaration is drawn the way this editor draws declarations.
        const QTextCharFormat declared = TextEditor::globalFontSettings().data().toTextCharFormat(
            TextEditor::TextStyles::mixinStyle(TextEditor::C_KEYWORD,
                                               TextEditor::C_DECLARATION));
        const auto formatOnSecondLine = [document] {
            const QTextBlock block = document->document()->findBlockByNumber(1);
            const QList<QTextLayout::FormatRange> ranges = block.layout()->formats();
            return ranges.isEmpty() ? QTextCharFormat() : ranges.first().format;
        };
        const QTextCharFormat plain
            = TextEditor::globalFontSettings().data().toTextCharFormat(TextEditor::C_KEYWORD);
        // Compared on what the two actually differ in: a whole-format equality
        // would also compare what the highlighter merges in around them.
        const auto looksDeclared = [&] {
            const QTextCharFormat applied = formatOnSecondLine();
            return applied.foreground() == declared.foreground()
                   && applied.fontWeight() == declared.fontWeight();
        };
        QTRY_VERIFY_WITH_TIMEOUT(looksDeclared(), 15000);
        QVERIFY(declared.fontWeight() != plain.fontWeight()
                || declared.foreground() != plain.foreground());
        EditorManager::closeAllEditors(false);
    }

    // What the extension would write next, shown ahead of the caret as ghost
    // text - the same suggestion Qt Creator shows for any other source, so
    // accepting it works the way the user already knows.
    void testInlineCompletion()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "suggested.txt";
        QVERIFY(file.writeFileContents("one\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "inline", "alien-inline",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(\n"
            "      vscode.languages.registerInlineCompletionItemProvider(\n"
            "          {language: 'plaintext'}, {\n"
            "            provideInlineCompletionItems(document, position) {\n"
            "              return [new vscode.InlineCompletionItem(\n"
            "                  ' and then some',\n"
            "                  new vscode.Range(position, position))];\n"
            "            },\n"
            "          }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.inline.ready', () => {}));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        // The command appears when activate() has run, which is also when the
        // provider is registered - typing before that asks nobody.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.inline.ready"), 15000);
        QVERIFY(EditorManager::openEditor(file));
        TextEditor::TextEditorWidget *widget
            = TextEditor::TextEditorWidget::fromEditor(EditorManager::currentEditor());
        QVERIFY(widget);

        // Typing is what asks for one.
        QTextCursor cursor(widget->textDocument()->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText("two");
        widget->setTextCursor(cursor);
        QCOMPARE(widget->textDocument()->plainText(), QString("one\ntwo"));

        const auto suggested = [widget] {
            const QTextBlock block = widget->textCursor().block();
            TextEditor::TextSuggestion *suggestion
                = TextEditor::TextBlockUserData::suggestion(block);
            return suggestion ? suggestion->replacementDocument()->toPlainText() : QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!suggested().isEmpty(), 20000);
        QVERIFY2(suggested().contains("and then some"), qPrintable(suggested()));
        EditorManager::closeAllEditors(false);
    }

    // A document outlives the host. What an extension put on it - folds,
    // colours, marks - is not the editor's own and has to come off again,
    // folding above all: left switched to externally provided with nobody
    // providing, the document can never fold at all.
    void testDocumentIsPutBackWhenTheHostGoes()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "left.txt";
        QVERIFY(file.writeFileContents("zero\none\ntwo\nthree\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "leave", "alien-leave",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerFoldingRangeProvider(\n"
            "      {language: 'plaintext'}, {\n"
            "        provideFoldingRanges() { return [new vscode.FoldingRange(1, 3)]; },\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.languages.registerColorProvider(\n"
            "      {language: 'plaintext'}, {\n"
            "        provideDocumentColors() {\n"
            "          return [{range: new vscode.Range(0, 0, 0, 4),\n"
            "                   color: new vscode.Color(1, 0, 0, 1)}];\n"
            "        },\n"
            "        provideColorPresentations() { return []; },\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.leave.ready', () => {}));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        QVERIFY(EditorManager::openEditor(file));
        TextEditor::TextDocument *document
            = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(document);

        const auto marks = [document] { return document->marks().size(); };
        {
            ExtensionHost host(node);
            host.activate(*manifest);
            QTRY_VERIFY_WITH_TIMEOUT(
                host.registeredCommands().contains("alien.leave.ready"), 15000);
            // Both features have taken hold of the document.
            QTRY_VERIFY_WITH_TIMEOUT(document->isFoldingIndentExternallyProvided(), 15000);
            QTRY_VERIFY_WITH_TIMEOUT(marks() > 0, 15000);
        }

        // The host is gone; the document is the editor's own again.
        QVERIFY(!document->isFoldingIndentExternallyProvided());
        QCOMPARE(marks(), 0);
        EditorManager::closeAllEditors(false);
    }

    // Switching one extension off takes its folds and colours off the
    // document with it, rather than leaving them until the user happens to
    // type something.
    void testDeactivatedExtensionLetsGo()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "letgo.txt";
        QVERIFY(file.writeFileContents("zero\none\ntwo\nthree\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "letgo", "alien-letgo",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.languages.registerFoldingRangeProvider(\n"
            "      {language: 'plaintext'}, {\n"
            "        provideFoldingRanges() { return [new vscode.FoldingRange(1, 3)]; },\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.languages.registerColorProvider(\n"
            "      {language: 'plaintext'}, {\n"
            "        provideDocumentColors() {\n"
            "          return [{range: new vscode.Range(0, 0, 0, 4),\n"
            "                   color: new vscode.Color(1, 0, 0, 1)}];\n"
            "        },\n"
            "        provideColorPresentations() { return []; },\n"
            "      }));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.letgo.ready', () => {}));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.letgo.ready"), 15000);

        QVERIFY(EditorManager::openEditor(file));
        TextEditor::TextDocument *document
            = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(document);
        QTRY_VERIFY_WITH_TIMEOUT(document->isFoldingIndentExternallyProvided(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!document->marks().isEmpty(), 15000);

        // Switched off, and nothing is typed afterwards.
        host.deactivate(manifest->qualifiedId());
        QTRY_VERIFY_WITH_TIMEOUT(!document->isFoldingIndentExternallyProvided(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(document->marks().isEmpty(), 15000);
        EditorManager::closeAllEditors(false);
    }

    // Stopping the host is the end of the session for every extension in it,
    // and each gets its deactivate() for it, as in VS Code.
    void testStoppedHostDeactivates()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath said = root / "said.txt";
        const FilePath go = root / "go.txt";
        const auto quoted = [](const FilePath &path) {
            return QString::fromUtf8(QJsonDocument(QJsonArray{path.path()})
                                         .toJson(QJsonDocument::Compact)).mid(1).chopped(1);
        };
        // Its deactivate() holds until the test says go, which the test can
        // only do once stopping has returned.
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "bye", "alien-bye",
            QString("const vscode = require('vscode');\n"
                    "const fs = require('fs');\n"
                    "function activate(context) {\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand(\n"
                    "      'alien.bye.ready', () => {}));\n"
                    "}\n"
                    "function deactivate() {\n"
                    "  return new Promise(resolve => {\n"
                    "    const timer = setInterval(() => {\n"
                    "      if (!fs.existsSync(%2))\n"
                    "        return;\n"
                    "      clearInterval(timer);\n"
                    "      fs.writeFileSync(%1, 'bye');\n"
                    "      resolve();\n"
                    "    }, 10);\n"
                    "  });\n"
                    "}\n"
                    "module.exports = { activate, deactivate };\n")
                .arg(quoted(said), quoted(go)));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.bye.ready"), 15000);

        // Stopping does not wait for the host, but lets it end on its own.
        const QPointer<QObject> ending = host.shutdown();
        QVERIFY(ending);
        QVERIFY(go.writeFileContents("go").has_value());
        QTRY_VERIFY_WITH_TIMEOUT(!ending, 15000);
        QCOMPARE(said.fileContents().value_or(QByteArray()), QByteArray("bye"));
    }

    // A completion item whose text is a snippet in the language servers'
    // notation must be expanded, not typed in as it stands.
    void testSnippetCompletionIsExpanded()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "snippet.txt";
        QVERIFY(file.writeFileContents("").has_value());

        Core::IEditor *editor = EditorManager::openEditor(file);
        QVERIFY(editor);
        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor);
        QVERIFY(textEditor);
        TextEditor::TextEditorWidget *widget = textEditor->editorWidget();
        QVERIFY(widget);

        const QJsonArray items{QJsonObject{
            {"label", "ifStatement"},
            {"insertText", "if (${1:cond}) ${0}then"},
            {"isSnippet", true},
        }};
        std::unique_ptr<TextEditor::IAssistProposal> proposal(
            createCompletionProposal(widget->position(), items, nullptr));
        QVERIFY(proposal);
        TextEditor::GenericProposalModelPtr model
            = qSharedPointerCast<TextEditor::GenericProposalModel>(proposal->model());
        QCOMPARE(model->size(), 1);
        TextEditor::AssistProposalItemInterface *item = model->proposalItem(0);
        QVERIFY(item);
        // The popup names the item by its label, not by the snippet body.
        QCOMPARE(item->text(), QString("ifStatement"));

        item->apply(widget, widget->position());
        const QString text = widget->document()->toPlainText();
        QVERIFY2(!text.contains("${"), qPrintable(text));
        QCOMPARE(text, QString("if (cond) then"));

        EditorManager::closeAllEditors(false);
    }

    // Completing a symbol also brings in what it needs - the import line the
    // server hands over beside the item itself.
    void testCompletionBringsItsImport()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "import.txt";
        QVERIFY(file.writeFileContents("head\nuse ").has_value());

        Core::IEditor *editor = EditorManager::openEditor(file);
        QVERIFY(editor);
        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor);
        QVERIFY(textEditor);
        TextEditor::TextEditorWidget *widget = textEditor->editorWidget();
        QVERIFY(widget);
        widget->gotoDocumentEnd();

        const QJsonArray items{QJsonObject{
            {"label", "Symbol"},
            {"insertText", "Symbol"},
            {"additionalTextEdits", QJsonArray{QJsonObject{
                {"range", QJsonObject{{"start", QJsonObject{{"line", 0}, {"character", 0}}},
                                      {"end", QJsonObject{{"line", 0}, {"character", 0}}}}},
                {"newText", "import Symbol\n"},
            }}},
        }};
        std::unique_ptr<TextEditor::IAssistProposal> proposal(
            createCompletionProposal(widget->position(), items, nullptr));
        QVERIFY(proposal);
        TextEditor::GenericProposalModelPtr model
            = qSharedPointerCast<TextEditor::GenericProposalModel>(proposal->model());
        QCOMPARE(model->size(), 1);
        model->proposalItem(0)->apply(widget, widget->position());

        QCOMPARE(widget->document()->toPlainText(),
                 QString("import Symbol\nhead\nuse Symbol"));
        EditorManager::closeAllEditors(false);
    }

    // An item that says which range it replaces is put exactly there.
    void testCompletionReplacesItsOwnRange()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "range.txt";
        QVERIFY(file.writeFileContents("foo.bar").has_value());

        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(
            EditorManager::openEditor(file));
        QVERIFY(textEditor);
        TextEditor::TextEditorWidget *widget = textEditor->editorWidget();
        widget->gotoDocumentEnd();

        const QJsonArray items{QJsonObject{
            {"label", "baz"},
            {"insertText", "baz"},
            {"range", QJsonObject{{"start", QJsonObject{{"line", 0}, {"character", 0}}},
                                  {"end", QJsonObject{{"line", 0}, {"character", 7}}}}},
        }};
        std::unique_ptr<TextEditor::IAssistProposal> proposal(
            createCompletionProposal(widget->position(), items, nullptr));
        TextEditor::GenericProposalModelPtr model
            = qSharedPointerCast<TextEditor::GenericProposalModel>(proposal->model());
        model->proposalItem(0)->apply(widget, widget->position());

        QCOMPARE(widget->document()->toPlainText(), QString("baz"));
        EditorManager::closeAllEditors(false);
    }

    // A .vsix names the folder it is installed into, and must not name one
    // outside the extensions directory.
    void testInstallFolderName()
    {
        VscodeManifest manifest;
        manifest.publisher = "pub";
        manifest.name = "ext";
        manifest.version = "1.2.3-beta+4";
        QCOMPARE(installFolderName(manifest).value_or(QString()), QString("pub.ext-1.2.3-beta+4"));

        manifest.version = "1/../../../../Documents";
        QVERIFY(!installFolderName(manifest));
        manifest.version = "..";
        QVERIFY(!installFolderName(manifest));
        manifest.version = "1.0";
        manifest.publisher = "a\\b";
        QVERIFY(!installFolderName(manifest));
        manifest.publisher.clear();
        manifest.name = "x..y";
        QVERIFY(!installFolderName(manifest));
    }

    // Edits at the same place: insertions keep their order, and one before a
    // replacement survives it.
    void testEditsAtTheSameStart()
    {
        const auto edit = [](int start, int end, const QString &text) {
            return QJsonObject{{"range", QJsonObject{{"start", QJsonObject{{"line", 0},
                                                                           {"character", start}}},
                                                     {"end", QJsonObject{{"line", 0},
                                                                         {"character", end}}}}},
                               {"newText", text}};
        };
        const auto apply = [](QString text, const QJsonArray &edits) {
            const QString original = text;
            for (const QJsonObject &e : editsBackToFront(original, edits)) {
                const QJsonObject range = e.value("range").toObject();
                const int start = offsetOf(original, range.value("start"));
                const int end = offsetOf(original, range.value("end"));
                text.replace(start, end - start, e.value("newText").toString());
            }
            return text;
        };
        QCOMPARE(apply("0123456789", {edit(0, 0, "a"), edit(0, 0, "b")}), QString("ab0123456789"));
        QCOMPARE(apply("0123456789", {edit(5, 5, "X"), edit(5, 10, "Y")}), QString("01234XY"));
        QCOMPARE(apply("0123456789", {edit(5, 10, "Y"), edit(5, 5, "X")}), QString("01234XY"));
        QCOMPARE(apply("0123456789", {edit(8, 9, "c"), edit(1, 2, "a"), edit(5, 5, "b")}),
                 QString("0a234b567c9"));
    }

    // The list was filtered by what was typed after the extension answered,
    // so the item replaces that too, and a range past the end of a line stays
    // on that line.
    void testCompletionCoversWhatWasTypedSince()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "typed.txt";
        QVERIFY(file.writeFileContents("fo\nnext").has_value());

        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(
            EditorManager::openEditor(file));
        QVERIFY(textEditor);
        TextEditor::TextEditorWidget *widget = textEditor->editorWidget();
        widget->setCursorPosition(2);

        const auto apply = [widget](int endCharacter) {
            const QJsonArray items{QJsonObject{
                {"label", "foobar"},
                {"insertText", "foobar"},
                {"range", QJsonObject{
                    {"start", QJsonObject{{"line", 0}, {"character", 0}}},
                    {"end", QJsonObject{{"line", 0}, {"character", endCharacter}}}}},
            }};
            std::unique_ptr<TextEditor::IAssistProposal> proposal(
                createCompletionProposal(0, items, nullptr));
            TextEditor::GenericProposalModelPtr model
                = qSharedPointerCast<TextEditor::GenericProposalModel>(proposal->model());
            model->proposalItem(0)->apply(widget, 0);
        };

        widget->insertPlainText("o");
        apply(2);
        QCOMPARE(widget->document()->toPlainText(), QString("foobar\nnext"));

        widget->setCursorPosition(0);
        apply(99);
        QCOMPARE(widget->document()->toPlainText(), QString("foobar\nnext"));

        EditorManager::closeAllEditors(false);
    }

    // Typing a character the item names takes the item, and the character with
    // it.
    void testCompletionCommitCharacter()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath file = FilePath::fromString(dir.path()) / "commit.txt";
        QVERIFY(file.writeFileContents("").has_value());

        auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(
            EditorManager::openEditor(file));
        QVERIFY(textEditor);
        TextEditor::TextEditorWidget *widget = textEditor->editorWidget();

        const QJsonArray items{QJsonObject{
            {"label", "call"},
            {"insertText", "call"},
            {"commitCharacters", QJsonArray{"("}},
        }};
        std::unique_ptr<TextEditor::IAssistProposal> proposal(
            createCompletionProposal(widget->position(), items, nullptr));
        TextEditor::GenericProposalModelPtr model
            = qSharedPointerCast<TextEditor::GenericProposalModel>(proposal->model());
        TextEditor::AssistProposalItemInterface *item = model->proposalItem(0);
        QVERIFY(!item->prematurelyApplies(QChar('x')));
        QVERIFY(item->prematurelyApplies(QChar('(')));
        item->apply(widget, widget->position());

        QCOMPARE(widget->document()->toPlainText(), QString("call("));
        EditorManager::closeAllEditors(false);
    }

    // What language a file counts as is the editor's guess until an extension
    // says otherwise, and everything keyed by language follows the new answer.
    void testExtensionRenamesADocumentsLanguage()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "guessed.txt";
        QVERIFY(file.writeFileContents("text").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "lang", "alien-lang",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.lang.claim', async () => {\n"
            "        const doc = vscode.window.activeTextEditor.document;\n"
            "        await vscode.languages.setTextDocumentLanguage(doc, 'alienlang');\n"
            "        const known = await vscode.languages.getLanguages();\n"
            "        vscode.window.showInformationMessage(\n"
            "            'known:' + (known.includes('alienlang') ? 'yes' : 'no'));\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.lang.claim"), 15000);
        QVERIFY(EditorManager::openEditor(file));

        // The editor's own guess for a .txt file.
        QCOMPARE(host.languageIdFor(file), QString("plaintext"));

        QSignalSpy messages(&host, &ExtensionHost::messageShown);
        host.executeCommand("alien.lang.claim");
        QTRY_COMPARE_WITH_TIMEOUT(host.languageIdFor(file), QString("alienlang"), 15000);
        // ... and the new id is among the ones the host can name.
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.first().first().toString(), QString("known:yes"));

        EditorManager::closeAllEditors(false);
    }

    void testOnTypeFormatting()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "typed.txt";
        QVERIFY(file.writeFileContents("start\n").has_value());

        // Typing ";" makes the extension put "!" in front of it; typing
        // anything else asks nobody.
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "ontype", "alien-ontype",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(\n"
            "      vscode.languages.registerOnTypeFormattingEditProvider(\n"
            "          {language: 'plaintext'}, {\n"
            "            provideOnTypeFormattingEdits(document, position, ch) {\n"
            "              return [vscode.TextEdit.insert(\n"
            "                  new vscode.Position(position.line, position.character - 1),\n"
            "                  '!')];\n"
            "            },\n"
            "          }, ';'));\n"
            "  context.subscriptions.push(vscode.commands.registerCommand(\n"
            "      'alien.ontype.ready', () => {}));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        // The provider is registered by the time the command is.
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.ontype.ready"), 15000);

        QVERIFY(EditorManager::openEditor(file));
        TextEditor::TextDocument *document
            = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(document);

        QTextCursor cursor(document->document());
        cursor.movePosition(QTextCursor::End);
        // A character nobody asked about changes nothing.
        cursor.insertText("x");
        // ... and the one that was asked about does.
        cursor.insertText(";");
        QTRY_COMPARE_WITH_TIMEOUT(document->plainText(), QString("start\nx!;"), 15000);
        EditorManager::closeAllEditors(false);
    }

    void testConfigurationSchema()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path()) / "schema";
        QVERIFY(root.ensureWritableDir());
        const QByteArray packageJson = R"({
            "name": "schema-test", "publisher": "alien", "version": "0.0.1",
            "engines": { "vscode": "^1.0.0" },
            "contributes": { "configuration": {
                "title": "Schema Test",
                "properties": {
                    "alien.mode": { "type": "string", "default": "auto",
                                    "enum": ["auto", "manual"],
                                    "enumDescriptions": ["Pick for me", "I pick"],
                                    "description": "How to choose." },
                    "alien.count": { "type": "integer", "default": 3 },
                    "alien.loud": { "type": ["boolean", "null"], "default": false },
                    "alien.sections": { "type": "array", "default": [{"label": "One"}] },
                    "alien.path": { "type": "string" }
                } } } })";
        QVERIFY((root / "package.json").writeFileContents(packageJson).has_value());

        const Result<VscodeManifest> manifest
            = VscodeManifest::fromPackageJson(root / "package.json");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        QCOMPARE(manifest->configurationSettings.size(), 5);
        const auto settingFor = [&manifest](const QString &key) {
            return Utils::findOrDefault(manifest->configurationSettings,
                                        [&key](const VscodeSetting &s) { return s.key == key; });
        };
        const VscodeSetting mode = settingFor("alien.mode");
        QCOMPARE(mode.type, QString("string"));
        QCOMPARE(mode.enumValues, QStringList({"auto", "manual"}));
        QCOMPARE(mode.enumDescriptions.first(), QString("Pick for me"));
        QCOMPARE(mode.description, QString("How to choose."));
        QCOMPARE(mode.defaultValue.toString(), QString("auto"));
        QCOMPARE(settingFor("alien.count").defaultValue.toInt(), 3);
        // A union type is named by what it really holds, not by the array.
        QCOMPARE(settingFor("alien.loud").type, QString("boolean"));
        QCOMPARE(settingFor("alien.sections").type, QString("array"));

        // Writing a value leaves the others alone and is read back merged.
        const FilePath written = ExtensionHost::writtenConfigurationFile();
        written.removeFile();
        QVERIFY(ExtensionHost::writeConfigurationFile({{"alien.mode", "manual"}}));
        QVERIFY(ExtensionHost::writeConfigurationFile({{"alien.count", 7}}));
        const Result<QByteArray> contents = written.fileContents();
        QVERIFY(contents.has_value());
        const QJsonObject values = QJsonDocument::fromJson(*contents).object();
        QCOMPARE(values.value("alien.mode").toString(), QString("manual"));
        QCOMPARE(values.value("alien.count").toInt(), 7);

        // The user may have put a comment in, and what is there stays. What
        // does not parse is not replaced by the one value written now.
        QVERIFY(written.writeFileContents("// mine\n{\"alien.loud\": true}").has_value());
        QVERIFY(ExtensionHost::writeConfigurationFile({{"alien.count", 8}}));
        const QJsonObject kept
            = QJsonDocument::fromJson(written.fileContents().value_or(QByteArray())).object();
        QCOMPARE(kept.value("alien.loud").toBool(), true);
        QCOMPARE(kept.value("alien.count").toInt(), 8);
        const QByteArray broken = "{\"alien.loud\": true,";
        QVERIFY(written.writeFileContents(broken).has_value());
        QVERIFY(!ExtensionHost::writeConfigurationFile({{"alien.count", 9}}));
        QCOMPARE(written.fileContents().value_or(QByteArray()), broken);
        written.removeFile();
        QVERIFY(ExtensionHost::writeConfigurationFile({{"alien.mode", "manual"}}));
        // Declared without a default is declared all the same.
        QVERIFY(ExtensionHost::writeConfigurationFile({{"alien.path", "/x"}}));

        // The editor an extension gets for each of them, and what it reports as
        // changed - only what was touched, so the rest keeps following the
        // extension's own default.
        ExtensionSettingsDialog dialog(*manifest);
        auto editor = [&dialog](const QString &key) {
            return dialog.findChild<QWidget *>("extensionSetting." + key);
        };
        auto combo = qobject_cast<QComboBox *>(editor("alien.mode"));
        QVERIFY(combo);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->currentText(), QString("manual")); // what was written above
        QVERIFY(qobject_cast<QSpinBox *>(editor("alien.count")));
        QVERIFY(qobject_cast<QCheckBox *>(editor("alien.loud")));
        QVERIFY(editor("alien.sections")); // no obvious editor: the JSON itself
        auto path = qobject_cast<QLineEdit *>(editor("alien.path"));
        QVERIFY(path);
        QCOMPARE(path->text(), QString("/x"));

        QVERIFY(dialog.editedValues().isEmpty());
        qobject_cast<QCheckBox *>(editor("alien.loud"))->setChecked(true);
        combo->setCurrentText("auto");
        emit combo->activated(combo->currentIndex());
        QCOMPARE(dialog.editedValues().keys(), QStringList({"alien.loud", "alien.mode"}));
        QCOMPARE(dialog.editedValues().value("alien.loud").toBool(), true);
        QCOMPARE(dialog.editedValues().value("alien.mode").toString(), QString("auto"));

        written.removeFile();
    }

    void testConfigurationWrite()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        const FilePath written = ExtensionHost::writtenConfigurationFile();
        written.removeFile();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "cfgwrite", "alien-cfg-write-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand("
            "'alien.cfg.write', async () => {\n"
            "    const config = vscode.workspace.getConfiguration('alien');\n"
            "    await config.update('remembered', 'yes');\n"
            "    vscode.window.showInformationMessage("
            "'wrote:' + config.get('remembered', 'none'));\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.cfg.write"), 15000);
        host.executeCommand("alien.cfg.write");

        // The extension reads back what it wrote ...
        auto saw = [&spy] {
            for (const QList<QVariant> &args : spy)
                if (args.first().toString() == "wrote:yes")
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(saw(), 15000);

        // ... and it is on disk, where the next run will find it.
        QTRY_VERIFY_WITH_TIMEOUT(written.exists(), 5000);
        const Result<QByteArray> contents = written.fileContents();
        QVERIFY(contents.has_value());
        QCOMPARE(QJsonDocument::fromJson(*contents).object().value("alien.remembered").toString(),
                 QString("yes"));
        written.removeFile();
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

    // Signing in. The providers are the ones extensions bring themselves, so
    // registering one and then asking for its session has to come back with
    // what that provider said, and asking without createIfNone must not sign
    // anybody in.
    void testAuthenticationProvider()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "auth", "alien-auth",
            "const vscode = require('vscode');\n"
            "async function activate(context) {\n"
            "  let sessions = [];\n"
            "  const session = {id: 's1', accessToken: 'tok', scopes: ['read'],\n"
            "                   account: {id: 'a1', label: 'Anna'}};\n"
            "  const changed = new vscode.EventEmitter();\n"
            "  context.subscriptions.push(vscode.authentication.registerAuthenticationProvider(\n"
            "      'alien-auth', 'Alien', {\n"
            "        onDidChangeSessions: changed.event,\n"
            "        getSessions: async () => sessions,\n"
            "        createSession: async () => { sessions = [session]; return session; },\n"
            "        removeSession: async () => { sessions = []; },\n"
            "      }));\n"
            "  const none = await vscode.authentication.getSession('alien-auth', ['read']);\n"
            "  const made = await vscode.authentication.getSession('alien-auth', ['read'],\n"
            "                                                      {createIfNone: true});\n"
            "  const again = await vscode.authentication.getSession('alien-auth', ['read']);\n"
            "  const accounts = await vscode.authentication.getAccounts('alien-auth');\n"
            "  vscode.window.showInformationMessage('auth:' + (none === undefined) + ':'\n"
            "      + made.accessToken + ':' + again.id + ':' + accounts[0].label);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);

        auto saw = [&spy] {
            for (const QList<QVariant> &args : spy)
                if (args.first().toString() == "auth:true:tok:s1:Anna")
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(saw(), 15000);
    }

    // The git extension VS Code ships, which extensions ask for git rather
    // than looking for it themselves. A repository of the test's own: the
    // answers are read off one, and a real checkout would be written to.
    void testGitExtensionApi()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");
        const FilePath git = FilePath("git").searchInPath();
        if (!git.isExecutableFile())
            QSKIP("git not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath repository = root / "repo";
        QVERIFY(repository.ensureWritableDir());

        // A git that reads neither the user's configuration nor a repository
        // above the temporary directory.
        Environment environment = Environment::systemEnvironment();
        environment.set("HOME", root.toFSPathString());
        environment.set("USERPROFILE", root.toFSPathString());
        environment.set("GIT_CONFIG_NOSYSTEM", "1");
        environment.set("GIT_CEILING_DIRECTORIES", root.toFSPathString());
        const auto run = [&](const QStringList &arguments) {
            Process process;
            process.setCommand({git, QStringList{"-c", "user.name=Alien Test",
                                                 "-c", "user.email=alien@example.invalid"}
                                         + arguments});
            process.setWorkingDirectory(repository);
            process.setEnvironment(environment);
            process.runBlocking();
            return process.result() == ProcessResult::FinishedWithSuccess;
        };
        QVERIFY(run({"init", "-q", "-b", "main"}));
        QVERIFY((repository / "a.txt").writeFileContents("committed\n").has_value());
        QVERIFY(run({"add", "--", "a.txt"}));
        QVERIFY(run({"commit", "-q", "-m", "first"}));
        // One change that is not committed, which is what a status reports.
        QVERIFY((repository / "a.txt").writeFileContents("changed\n").has_value());

        const QString path = repository.toFSPathString().replace('\\', '/');
        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "git", "alien-git",
            "const vscode = require('vscode');\n"
            "async function activate() {\n"
            "  const extension = vscode.extensions.getExtension('vscode.git');\n"
            "  const api = extension.exports.getAPI(1);\n"
            "  const repository = await api.openRepository(vscode.Uri.file('" + path + "'));\n"
            "  const commit = await repository.getCommit('HEAD');\n"
            "  const found = api.getRepository(vscode.Uri.joinPath(repository.rootUri, 'a.txt'));\n"
            "  const change = repository.state.workingTreeChanges[0];\n"
            "  vscode.window.showInformationMessage('git:' + api.state + ':'\n"
            "      + repository.state.HEAD.name + ':' + commit.message + ':'\n"
            "      + change.status + ':' + change.uri.fsPath.endsWith('a.txt') + ':'\n"
            "      + (found === repository) + ':' + (api.git.path.length > 0));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);

        auto said = [&spy] {
            for (const QList<QVariant> &args : spy) {
                const QString text = args.first().toString();
                if (text.startsWith("git:"))
                    return text;
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!said().isEmpty(), 30000);
        // Status 5 is Status.MODIFIED in the working tree.
        QCOMPARE(said(), QString("git:initialized:main:first:5:true:true:true"));
    }

    // A view the extension draws itself. Registering one opens nothing: only
    // the view that is focused is resolved, so an extension contributing half
    // a dozen of them does not open half a dozen panels.
    void testWebviewViewProvider()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QJsonObject contributes{
            {"viewsContainers", QJsonObject{{"activitybar", QJsonArray{
                 QJsonObject{{"id", "alienBar"}, {"title", "Alien"}}}}}},
            {"views", QJsonObject{{"alienBar", QJsonArray{
                 QJsonObject{{"id", "alien.graph"}, {"name", "Graph"}, {"type", "webview"}},
                 QJsonObject{{"id", "alien.other"}, {"name", "Other"}, {"type", "webview"}}}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "view", "alien-webview-view-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien View');\n"
            "  const register = id => vscode.window.registerWebviewViewProvider(id, {\n"
            "    resolveWebviewView(view) {\n"
            "      channel.appendLine('resolved=' + id + ':' + view.title);\n"
            "      view.webview.html = '<p>Alien Sidebar</p>';\n"
            "    },\n"
            "  });\n"
            "  context.subscriptions.push(register('alien.graph'), register('alien.other'),\n"
            "                             channel);\n"
            "  vscode.commands.executeCommand('alien.graph.focus');\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy createdSpy(&host, &ExtensionHost::webviewCreated);
        QSignalSpy htmlSpy(&host, &ExtensionHost::webviewHtmlChanged);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(!htmlSpy.isEmpty() || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QCOMPARE(htmlSpy.first().at(1).toString(), QString("<p>Alien Sidebar</p>"));
        // The html is there, so a view registered eagerly would be too.
        QCOMPARE(createdSpy.size(), 1);
        QCOMPARE(createdSpy.first().at(1).toString(), QString("alien.graph"));
        QVERIFY(!output.isEmpty());
        // The container says whose view it is, as it does for a tree.
        QCOMPARE(output.first().at(1).toString(), QString("resolved=alien.graph:Alien: Graph"));
    }

    // An editor an extension draws itself, opened the one way it can be here:
    // the extension asking for that view type by name.
    void testCustomEditorProvider()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath drawn = FilePath::fromString(dir.path()) / "board.alien";
        QVERIFY(drawn.writeFileContents("two cells\n").has_value());

        const QJsonObject contributes{
            {"customEditors", QJsonArray{QJsonObject{
                 {"viewType", "alien.board"}, {"displayName", "Board"},
                 {"selector", QJsonArray{QJsonObject{{"filenamePattern", "*.alien"}}}}}}},
        };
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "editor", "alien-custom-editor-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  context.subscriptions.push(vscode.window.registerCustomEditorProvider(\n"
                    "    'alien.board', {\n"
                    "      resolveCustomTextEditor(document, panel) {\n"
                    "        panel.webview.html = '<p>' + document.getText().trim() + '</p>';\n"
                    "      },\n"
                    "    }));\n"
                    "  vscode.commands.executeCommand('vscode.openWith',\n"
                    "                                 vscode.Uri.file('%1'), 'alien.board');\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(drawn.toFSPathString()),
            QJsonArray{"*"}, contributes);
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy createdSpy(&host, &ExtensionHost::webviewCreated);
        QSignalSpy htmlSpy(&host, &ExtensionHost::webviewHtmlChanged);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(!htmlSpy.isEmpty() || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        // The editor drew the contents of the file it was opened on.
        QCOMPARE(htmlSpy.first().at(1).toString(), QString("<p>two cells</p>"));
        QCOMPARE(createdSpy.first().at(1).toString(), QString("alien.board"));
    }

    // A panel belongs to the extension that serializes it: rescanning the
    // extensions closes it and hands it back, rather than leaving a dead one
    // behind and opening a second.
    void testWebviewPanelSerializer()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "serializer", "alien-serializer-test",
            "const vscode = require('vscode');\n"
            "let opened = false;\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.window.registerWebviewPanelSerializer(\n"
            "    'alien.doc', {\n"
            "      deserializeWebviewPanel(panel, state) {\n"
            "        panel.webview.html = '<p>restored:' + state + '</p>';\n"
            "      },\n"
            "    }));\n"
            "  if (opened)\n"
            "    return;\n"
            "  opened = true;\n"
            "  const panel = vscode.window.createWebviewPanel('alien.doc', 'Doc', 1, {});\n"
            "  panel.webview.html = '<p>fresh</p>';\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy htmlSpy(&host, &ExtensionHost::webviewHtmlChanged);
        QSignalSpy disposedSpy(&host, &ExtensionHost::webviewDisposed);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(!htmlSpy.isEmpty() || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QCOMPARE(htmlSpy.first().at(1).toString(), QString("<p>fresh</p>"));
        const QString first = htmlSpy.first().first().toString();

        host.deactivate(manifest->qualifiedId());
        QTRY_VERIFY_WITH_TIMEOUT(!disposedSpy.isEmpty(), 15000);
        QCOMPARE(disposedSpy.first().first().toString(), first);

        host.activate(*manifest);
        auto restored = [&htmlSpy] {
            for (const QList<QVariant> &args : htmlSpy) {
                if (args.at(1).toString().startsWith("<p>restored"))
                    return args.at(1).toString();
            }
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!restored().isEmpty(), 15000);
        // Nothing stored the page's state, which VS Code also reports as
        // undefined rather than as a state of its own.
        QCOMPARE(restored(), QString("<p>restored:undefined</p>"));
    }

    // A file system of the extension's own. What it serves is read by
    // workspace.fs and by a document opened on such a URI, since no scheme but
    // file: is anybody's disk.
    void testFileSystemProvider()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "fs", "alien-fs-test",
            QString("const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Fs');\n"
            "  const say = (name, value) => channel.appendLine(name + '=' + value);\n"
            "  const written = new Map();\n"
            "  context.subscriptions.push(vscode.workspace.registerFileSystemProvider(\n"
            "    'alienfs', {\n"
            "      onDidChangeFile: () => ({dispose() {}}),\n"
            "      watch: () => ({dispose() {}}),\n"
            "      stat: uri => ({type: 1, ctime: 1, mtime: 2, size: 7}),\n"
            "      readDirectory: () => [['one', 1], ['sub', 2]],\n"
            "      readFile: uri => Buffer.from('served ' + uri.path),\n"
            "      writeFile: (uri, content) => { written.set(uri.path, content); },\n"
            "      createDirectory: () => {},\n"
            "      delete: () => {},\n"
            "      rename: () => {},\n"
            "    }));\n"
            "  const uri = vscode.Uri.parse('alienfs:/a/b.txt');\n"
            "  vscode.workspace.fs.readFile(uri).then(async bytes => {\n"
            "    say('read', Buffer.from(bytes).toString());\n"
            "    say('size', (await vscode.workspace.fs.stat(uri)).size);\n"
            "    say('dir', (await vscode.workspace.fs.readDirectory(uri)).join(','));\n"
            "    await vscode.workspace.fs.writeFile(uri, Buffer.from('back'));\n"
            "    say('wrote', Buffer.from(written.get('/a/b.txt')).toString());\n"
            "    const document = await vscode.workspace.openTextDocument(uri);\n"
            "    say('document', document.getText());\n"
            "    say('onDisk', (await vscode.workspace.fs.stat(\n"
            "        vscode.Uri.file('%1'))).type);\n"
            "  });\n"
            "  context.subscriptions.push(channel);\n"
            "}\n"
            "module.exports = { activate };\n").arg(
                (FilePath::fromString(dir.path()) / "fs" / "package.json").toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 6 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        QStringList reported;
        for (const QList<QVariant> &args : output)
            reported.append(args.at(1).toString());
        // The real file system is still what a file: URI means.
        QCOMPARE(reported,
                 QStringList({"read=served /a/b.txt", "size=7", "dir=one,1,sub,2",
                              "wrote=back", "document=served /a/b.txt", "onDisk=1"}));
    }

    // A link into an extension is answered by that extension, not handed to a
    // desktop that has nothing to open a vscode: URI with.
    void testUriHandler()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "uriHandler", "alien-uri-handler-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Link');\n"
            "  context.subscriptions.push(vscode.window.registerUriHandler({\n"
            "    handleUri(uri) { channel.appendLine('handled=' + uri.path + ':' + uri.query); },\n"
            "  }), channel);\n"
            "  const link = where => vscode.Uri.parse(\n"
            "    'vscode://TheQtCompany.alien-uri-handler-test/' + where);\n"
            "  vscode.env.openExternal(link('link?id=7'));\n"
            "  vscode.commands.executeCommand('vscode.open', link('opened'));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 2 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QStringList reported;
        for (const QList<QVariant> &args : output)
            reported.append(args.at(1).toString());
        // Both ways of following a link end up at the extension, and the
        // authority reaches it whichever way its id is spelled.
        QCOMPARE(reported, QStringList({"handled=/link:id=7", "handled=/opened:"}));
    }

    // A documentation page an extension wants shown. VS Code has a browser in
    // an editor tab for it and Qt Creator has none, so the page goes to the
    // real browser; asking for it must at least reach the same place as
    // openExternal, not report the command missing and do nothing.
    void testSimpleBrowserOpensThePage()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "browser", "alien-browser-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Browser');\n"
            "  context.subscriptions.push(vscode.window.registerUriHandler({\n"
            "    handleUri(uri) { channel.appendLine('opened=' + uri.path); },\n"
            "  }), channel);\n"
            // A page of the extension's own, so the test never hands a URL to
            // a real browser: what it proves is where the command arrives.
            "  const page = where => 'vscode://TheQtCompany.alien-browser-test/' + where;\n"
            "  vscode.commands.executeCommand('simpleBrowser.api.open', page('api'),\n"
            "                                 {viewColumn: 2})\n"
            "      .then(() => vscode.commands.executeCommand(\n"
            "          'simpleBrowser.show', vscode.Uri.parse(page('shown'))));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        QSignalSpy missing(&host, &ExtensionHost::apiUnimplemented);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 2 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QStringList reported;
        for (const QList<QVariant> &args : output)
            reported.append(args.at(1).toString());
        QCOMPARE(reported, QStringList({"opened=/api", "opened=/shown"}));
        QVERIFY2(missing.isEmpty(),
                 qPrintable(missing.isEmpty() ? QString() : missing.first().at(1).toString()));
    }

    void testComments()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath source = FilePath::fromString(dir.path()) / "reviewed.txt";
        QVERIFY(source.writeFileContents("first\nsecond\nthird\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "comments", "alien-comments-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  const controller = vscode.comments.createCommentController(\n"
                    "      'alien.review', 'Review');\n"
                    "  context.subscriptions.push(controller);\n"
                    "  const uri = vscode.Uri.file('%1');\n"
                    "  const range = new vscode.Range(new vscode.Position(1, 0),\n"
                    "                                 new vscode.Position(1, 6));\n"
                    "  const thread = controller.createCommentThread(uri, range, [{\n"
                    "    author: {name: 'Reviewer'},\n"
                    "    body: new vscode.MarkdownString('Please rename this'),\n"
                    "  }]);\n"
                    "  thread.label = 'Naming';\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand(\n"
                    "      'alien.review.answer', () => {\n"
                    "    thread.comments = thread.comments.concat([{\n"
                    "      author: {name: 'Author'}, body: 'Done',\n"
                    "    }]);\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n")
                .arg(source.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy changed(&host, &ExtensionHost::commentsChanged);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(changed.size() >= 1 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        const auto threads = [document] {
            QList<TextEditor::TextMark *> found;
            for (TextEditor::TextMark *mark : document->marks()) {
                if (mark->category().id == Constants::TEXT_MARK_CATEGORY)
                    found.append(mark);
            }
            return found;
        };
        // The thread is on the line the extension put it on, and says who
        // wrote what.
        QTRY_VERIFY_WITH_TIMEOUT(threads().size() == 1, 15000);
        QCOMPARE(threads().first()->lineNumber(), 2);
        QCOMPARE(threads().first()->lineAnnotation(), QString("Naming"));
        QCOMPARE(threads().first()->toolTip(), QString("Reviewer: Please rename this"));

        // An answer reaches the line, the thread is not a snapshot of the
        // moment it was created.
        host.executeCommand("alien.review.answer", {});
        QTRY_VERIFY_WITH_TIMEOUT(
            threads().size() == 1
                && threads().first()->toolTip()
                       == QString("Reviewer: Please rename this\nAuthor: Done"),
            15000);

        Core::EditorManager::closeAllEditors(false);
    }

    void testSourceControl()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath changed = FilePath::fromString(dir.path()) / "changed.txt";
        QVERIFY(changed.writeFileContents("work\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "scm", "alien-scm-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  const control = vscode.scm.createSourceControl(\n"
                    "      'alien', 'Alien', vscode.Uri.file('%1'));\n"
                    "  context.subscriptions.push(control);\n"
                    "  control.inputBox.value = 'A commit message';\n"
                    "  control.count = 1;\n"
                    "  const group = control.createResourceGroup('changes', 'Changes');\n"
                    "  const empty = control.createResourceGroup('staged', 'Staged');\n"
                    "  empty.hideWhenEmpty = true;\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand(\n"
                    "      'alien.scm.fill', () => {\n"
                    "    group.resourceStates = [{\n"
                    "      resourceUri: vscode.Uri.file('%2'),\n"
                    "      decorations: {tooltip: 'Modified'},\n"
                    "    }];\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n")
                .arg(dir.path(), changed.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy activated(&host, &ExtensionHost::activated);
        QSignalSpy scmChanged(&host, &ExtensionHost::sourceControlsChanged);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(activated.size() == 1, 15000);
        QVERIFY(!scmChanged.isEmpty());
        QCOMPARE(scmChanged.first().at(0).toString(), QString("alien"));

        QJsonArray listed;
        const auto fetch = [&host, &listed] {
            listed = {};
            host.run().requestSourceControls(
                [&listed](const QJsonArray &result) { listed = result; });
        };

        // What the extension offers, as a pane would list it. The group that
        // asked to be hidden while empty is not there.
        fetch();
        QTRY_VERIFY_WITH_TIMEOUT(!listed.isEmpty(), 15000);
        QCOMPARE(listed.size(), 1);
        QJsonObject control = listed.first().toObject();
        QCOMPARE(control.value("label").toString(), QString("Alien"));
        QCOMPARE(control.value("message").toString(), QString("A commit message"));
        QCOMPARE(control.value("count").toInt(), 1);
        QCOMPARE(control.value("groups").toArray().size(), 1);
        QJsonObject group = control.value("groups").toArray().first().toObject();
        QCOMPARE(group.value("label").toString(), QString("Changes"));
        QVERIFY(group.value("resources").toArray().isEmpty());

        // Filling a group is assigning to it, and reaches the host.
        const int seen = scmChanged.size();
        host.executeCommand("alien.scm.fill", {});
        QTRY_VERIFY_WITH_TIMEOUT(scmChanged.size() > seen, 15000);
        fetch();
        QTRY_VERIFY_WITH_TIMEOUT(!listed.isEmpty(), 15000);
        group = listed.first().toObject().value("groups").toArray().first().toObject();
        QCOMPARE(group.value("resources").toArray().size(), 1);
        const QJsonObject resource = group.value("resources").toArray().first().toObject();
        QCOMPARE(resource.value("tooltip").toString(), QString("Modified"));
        QVERIFY(resource.value("uri").toString().endsWith("changed.txt"));
    }

    void testTerminalLinks()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "links", "alien-terminal-link-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const channel = vscode.window.createOutputChannel('Alien Links');\n"
            "  context.subscriptions.push(vscode.window.registerTerminalLinkProvider({\n"
            "    provideTerminalLinks(context) {\n"
            "      const at = context.line.indexOf('Foo.java:12');\n"
            "      if (at < 0)\n"
            "        return [];\n"
            "      return [{startIndex: at, length: 11, tooltip: 'Open Foo.java',\n"
            "               where: context.line.trim()}];\n"
            "    },\n"
            "    handleTerminalLink(link) { channel.appendLine('opened=' + link.where); },\n"
            "  }), channel);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        // A terminal only asks once an extension has said it looks at all.
        QTRY_VERIFY_WITH_TIMEOUT(host.run().hasTerminalLinkProviders() || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        // The whole line is what the provider matches in, and it answers with
        // offsets into it.
        QJsonArray links;
        host.run().requestTerminalLinks("at Foo.java:12 in main",
                                  [&links](const QJsonArray &found) { links = found; });
        QTRY_VERIFY_WITH_TIMEOUT(!links.isEmpty(), 15000);
        QCOMPARE(links.size(), 1);
        const QJsonObject link = links.first().toObject();
        QCOMPARE(link.value("startIndex").toInt(), 3);
        QCOMPARE(link.value("length").toInt(), 11);
        QCOMPARE(link.value("tooltip").toString(), QString("Open Foo.java"));
        QCOMPARE(link.value("text").toString(), QString("Foo.java:12"));
        QVERIFY(!link.value("id").toString().isEmpty());

        // And following it reaches the extension with the link it gave, not a
        // copy of what it looked like.
        host.run().handleTerminalLink(link.value("id").toString());
        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 1, 15000);
        QCOMPARE(output.first().at(1).toString(), QString("opened=at Foo.java:12 in main"));
    }

    // The value types around the language features. An extension constructs
    // them itself, so one that is missing is not a feature that stays off, it
    // is a "not a constructor" the extension dies of.
    void testLanguageFeatureValueTypes()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "types", "alien-value-type-test",
            "const vscode = require('vscode');\n"
            "function activate() {\n"
            "  const range = new vscode.Range(1, 0, 1, 5);\n"
            "  const item = new vscode.CallHierarchyItem(\n"
            "      vscode.SymbolKind.Function, 'f', '', vscode.Uri.file('/tmp/a.js'),\n"
            "      range, range);\n"
            "  const parts = [\n"
            "      new vscode.CallHierarchyIncomingCall(item, [range]).from.name,\n"
            "      new vscode.CallHierarchyOutgoingCall(item, [range]).to.name,\n"
            "      new vscode.LinkedEditingRanges([range]).ranges.length,\n"
            "      new vscode.InlayHintLabelPart('part').value,\n"
            "      new vscode.InlineValueText(range, 'x = 1').text,\n"
            "      new vscode.InlineValueVariableLookup(range, 'x').caseSensitiveLookup,\n"
            "      new vscode.InlineValueEvaluatableExpression(range, 'x').expression,\n"
            "      new vscode.SemanticTokensEdit(0, 1, new Uint32Array(1)).deleteCount,\n"
            "      new vscode.SemanticTokensEdits([], 'r1').resultId,\n"
            "      new vscode.LanguageModelTextPart('hello').value,\n"
            "      new vscode.LanguageModelToolResult([]).content.length,\n"
            "  ];\n"
            "  vscode.window.showInformationMessage('types:' + parts.join('|'));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy shown(&host, &ExtensionHost::messageShown);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(!shown.isEmpty() || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QCOMPARE(shown.first().at(0).toString(),
                 QString("types:f|f|1|part|x = 1|true|x|1|r1|hello|0"));
    }

    // What an extension reached for and did not get is reported against the
    // extension, so a corpus sweep says whose feature is off, not just that
    // something somewhere is missing.
    void testUnimplementedNamesTheExtension()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "missing", "alien-missing-test",
            "const vscode = require('vscode');\n"
            "function activate() {\n"
            "  vscode.workspace.registerRemoteAuthorityResolver('sweep', {});\n"
            "  void vscode.workspace.nothingLikeThisExists;\n"
            "  vscode.window.showInformationMessage('asked');\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy reported(&host, &ExtensionHost::apiUnimplemented);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(reported.size() >= 2 || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));

        // A stub that answers and a member that is not there at all are both
        // reported, and both name the extension that was activating.
        QStringList members;
        for (const QList<QVariant> &args : reported) {
            QCOMPARE(args.at(0).toString(), QString("theqtcompany.alien-missing-test"));
            members.append(args.at(1).toString());
        }
        QVERIFY2(members.contains("workspace.registerRemoteAuthorityResolver"),
                 qPrintable(members.join(", ")));
        QVERIFY2(members.contains("vscode.workspace.nothingLikeThisExists"),
                 qPrintable(members.join(", ")));
    }

    // One extension's code running inside another's activation is still that
    // extension's: what it reaches for is reported against it, not against
    // whoever happened to be activating at the time.
    void testUnimplementedNamesTheCaller()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const Result<VscodeManifest> teller = writeMockExtension(
            root / "teller", "alien-teller",
            "const vscode = require('vscode');\n"
            "function activate() {\n"
            "  return {touch: () => { void vscode.workspace.nothingLikeThisExists; }};\n"
            "}\n"
            "module.exports = { activate };\n");
        const Result<VscodeManifest> caller = writeMockExtension(
            root / "caller", "alien-caller",
            "const vscode = require('vscode');\n"
            "function activate() {\n"
            "  vscode.extensions.getExtension('theqtcompany.alien-teller').exports.touch();\n"
            "  vscode.window.showInformationMessage('called');\n"
            "}\n"
            "module.exports = { activate };\n");
        for (const Result<VscodeManifest> *m : {&teller, &caller})
            QVERIFY2(m->has_value(), qPrintable(*m ? QString() : m->error()));

        ExtensionHost host(node);
        QSignalSpy activated(&host, &ExtensionHost::activated);
        QSignalSpy reported(&host, &ExtensionHost::apiUnimplemented);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*teller);
        QTRY_VERIFY_WITH_TIMEOUT(!activated.isEmpty() || failed.size() >= 1, 15000);
        host.activate(*caller);

        QTRY_VERIFY_WITH_TIMEOUT(!reported.isEmpty() || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QCOMPARE(reported.first().at(0).toString(), teller->qualifiedId());
        QCOMPARE(reported.first().at(1).toString(),
                 QString("vscode.workspace.nothingLikeThisExists"));
    }

    // What the debugger evaluates when the user points at something, which the
    // extension knows and the C++ heuristic cannot: the range alone is an
    // answer too, and the debugger counts lines and columns from one where the
    // extension counts from zero.
    void testEvaluatableExpression()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "stopped.txt";
        QVERIFY(file.writeFileContents("aaabbbb\nhello world\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "evaluatable", "alien-evaluatable",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(\n"
            "      vscode.languages.registerEvaluatableExpressionProvider({language: 'plaintext'}, {\n"
            "        provideEvaluatableExpression(document, position) {\n"
            "          const range = new vscode.Range(new vscode.Position(position.line, 0),\n"
            "                                         new vscode.Position(position.line, 5));\n"
            "          if (position.line === 0)\n"
            "            return new vscode.EvaluatableExpression(range, 'counter.total');\n"
            "          return new vscode.EvaluatableExpression(range);\n"
            "        },\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 15000);
        QVERIFY(EditorManager::openEditor(file));

        QString expression;
        const auto poll = [&] {
            host.editor().requestEvaluatableExpression(
                file, 0, 1, [&expression](const QString &answer) { expression = answer; });
            return expression == "counter.total";
        };
        QTRY_VERIFY_WITH_TIMEOUT(poll(), 15000);

        // Without an expression the range is the expression.
        QString ranged;
        host.editor().requestEvaluatableExpression(
            file, 1, 1, [&ranged](const QString &answer) { ranged = answer; });
        QTRY_COMPARE_WITH_TIMEOUT(ranged, QString("hello"), 15000);

        // What the debugger's tooltip asks, in the debugger's own counting: a
        // provider that is off by one would answer with the other line.
        Debugger::Internal::DebuggerToolTipExpressionProvider provider
            = Debugger::Internal::debuggerToolTipExpressionProvider();
        QVERIFY(provider);
        QString pointed;
        const auto answer = [&pointed](const QString &text) { pointed = text; };
        QTRY_VERIFY_WITH_TIMEOUT(provider(file, 1, 2, answer), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(pointed, QString("counter.total"), 15000);

        // A language nobody provides for is left to the built-in heuristic.
        const FilePath other = root / "elsewhere.cpp";
        QVERIFY(other.writeFileContents("int main() {}\n").has_value());
        QVERIFY(!provider(other, 1, 2, answer));

        EditorManager::closeAllEditors(false);
    }

    // What the extension has to say about the lines on screen while the
    // program stands still. The three shapes a value comes in are told apart
    // here; what the values then look like in the editor needs a stopped
    // program, and is not covered.
    void testInlineValues()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "values.txt";
        QVERIFY(file.writeFileContents("zero\none\ntwo\nthree\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "values", "alien-inline-values",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(\n"
            "      vscode.languages.registerInlineValuesProvider({language: 'plaintext'}, {\n"
            "        provideInlineValues(document, viewPort, context) {\n"
            "          const at = n => new vscode.Range(new vscode.Position(n, 0),\n"
            "                                           new vscode.Position(n, 5));\n"
            "          return [\n"
            "            new vscode.InlineValueText(\n"
            "                at(0), 'frame ' + context.frameId + ' to ' + viewPort.end.line),\n"
            "            new vscode.InlineValueVariableLookup(at(1), 'total', false),\n"
            "            new vscode.InlineValueEvaluatableExpression(at(2), 'a + b'),\n"
            "            new vscode.InlineValueVariableLookup(at(3)),\n"
            "          ];\n"
            "        },\n"
            "      }));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 15000);
        QVERIFY(EditorManager::openEditor(file));

        QJsonArray values;
        const auto poll = [&] {
            host.editor().requestInlineValues(file, 0, 3, 7, 1,
                                     [&values](const QJsonArray &answer) { values = answer; });
            return values.size() == 4;
        };
        QTRY_VERIFY_WITH_TIMEOUT(poll(), 15000);

        // The frame and the lines asked about are what the provider was told.
        const QJsonObject text = values.at(0).toObject();
        QCOMPARE(text.value("kind").toString(), QString("text"));
        QCOMPARE(text.value("text").toString(), QString("frame 7 to 3"));
        QCOMPARE(text.value("line").toInt(), 0);

        const QJsonObject lookup = values.at(1).toObject();
        QCOMPARE(lookup.value("kind").toString(), QString("variable"));
        QCOMPARE(lookup.value("text").toString(), QString("total"));
        QCOMPARE(lookup.value("caseSensitive").toBool(), false);

        const QJsonObject expression = values.at(2).toObject();
        QCOMPARE(expression.value("kind").toString(), QString("expression"));
        QCOMPARE(expression.value("text").toString(), QString("a + b"));

        // A lookup without a name looks up what is written there.
        const QJsonObject unnamed = values.at(3).toObject();
        QCOMPARE(unnamed.value("kind").toString(), QString("variable"));
        QCOMPARE(unnamed.value("text").toString(), QString("three"));
        QCOMPARE(unnamed.value("caseSensitive").toBool(), true);

        // Values asked for at a stop the program went on from are not shown.
        auto document = TextEditor::TextDocument::textDocumentForFilePath(file);
        QVERIFY(document);
        host.editor().showInlineValues(0, 7, file, 1);
        host.editor().showInlineValues(0, -1, {}, 0);
        // Answered after the values asked for by the first stop.
        bool answered = false;
        host.editor().requestInlineValues(file, 0, 3, 7, 1,
                                          [&answered](const QJsonArray &) { answered = true; });
        QTRY_VERIFY_WITH_TIMEOUT(answered, 15000);
        QVERIFY(Utils::allOf(document->marks(), [](TextEditor::TextMark *mark) {
            return mark->category().id != Constants::TEXT_MARK_CATEGORY;
        }));

        EditorManager::closeAllEditors(false);
    }

    // The tests an extension brings: the tree is only there once it has been
    // asked for, and a run reports each outcome on its own.
    void testTestController()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "tests", "alien-test-controller-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  const ctrl = vscode.tests.createTestController('alien', 'Alien Tests');\n"
            "  ctrl.resolveHandler = item => {\n"
            "    if (item)\n"
            "      return;\n"
            "    const suite = ctrl.createTestItem('suite', 'Suite');\n"
            "    const good = ctrl.createTestItem('good', 'Good',\n"
            "                                     vscode.Uri.file('/tmp/good.js'));\n"
            "    good.range = new vscode.Range(3, 0, 3, 4);\n"
            "    const bad = ctrl.createTestItem('bad', 'Bad');\n"
            "    suite.children.replace([good, bad]);\n"
            "    ctrl.items.replace([suite]);\n"
            "  };\n"
            "  ctrl.createRunProfile('Run', vscode.TestRunProfileKind.Run, request => {\n"
            "    const run = ctrl.createTestRun(request);\n"
            "    for (const item of request.include || []) {\n"
            "      run.started(item);\n"
            "      if (item.id === 'good')\n"
            "        run.passed(item, 12);\n"
            "      else\n"
            "        run.failed(item, new vscode.TestMessage('boom'), 34);\n"
            "    }\n"
            "    run.end();\n"
            "  }, true);\n"
            "  context.subscriptions.push(ctrl);\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy registered(&host, &ExtensionHost::testsChanged);
        QSignalSpy results(&host, &ExtensionHost::testResult);
        QSignalSpy finished(&host, &ExtensionHost::testRunFinished);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        QTRY_VERIFY_WITH_TIMEOUT(!registered.isEmpty() || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QCOMPARE(registered.first().at(0).toString(), QString("alien"));

        // Nobody has expanded anything yet, so asking is what makes the
        // extension look for its tests in the first place.
        QJsonArray items;
        bool answered = false;
        host.run().requestTests("alien", [&items, &answered](const QJsonArray &found) {
            items = found;
            answered = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(answered, 15000);
        QCOMPARE(items.size(), 1);
        const QJsonObject suite = items.first().toObject();
        QCOMPARE(suite.value("id").toString(), QString("suite"));
        const QJsonArray children = suite.value("children").toArray();
        QCOMPARE(children.size(), 2);
        const QJsonObject good = children.first().toObject();
        QCOMPARE(good.value("label").toString(), QString("Good"));
        QCOMPARE(good.value("line").toInt(), 3);
        QVERIFY(good.value("uri").toString().endsWith("/good.js"));

        // And a run says what happened to each test it was given, by itself.
        host.run().runTests("alien", {"good", "bad"}, false);
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 15000);
        auto outcome = [&results](const QString &test) {
            for (const QList<QVariant> &args : results)
                if (args.at(1).toString() == test
                    && args.at(2).value<TestState>() != TestState::Started)
                    return args;
            return QList<QVariant>();
        };
        QCOMPARE(outcome("good").value(2).value<TestState>(), TestState::Passed);
        QCOMPARE(outcome("good").value(4).toInt(), 12);
        QCOMPARE(outcome("bad").value(2).value<TestState>(), TestState::Failed);
        QCOMPARE(outcome("bad").value(3).toString(), QString("boom"));
        QCOMPARE(finished.first().at(0).toString(), QString("alien"));

        // A controller the host does not know starts nothing, and says so,
        // since no end of that run is coming.
        std::optional<bool> unknownStarted;
        host.run().runTests("nobody", {}, false,
                            [&unknownStarted](bool started) { unknownStarted = started; });
        QTRY_VERIFY_WITH_TIMEOUT(unknownStarted.has_value(), 15000);
        QVERIFY(!*unknownStarted);
    }

    // And the same tests in Qt Creator's Tests pane: the tree says what they
    // are called and where they are, the run says how they went.
    void testExtensionTestsInPane()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath file = root / "suite.txt";
        QVERIFY(file.writeFileContents("one\ntwo\nthree\nfour\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "pane", "alien-pane-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  const ctrl = vscode.tests.createTestController('alien.pane', 'Pane');\n"
                    "  const uri = vscode.Uri.file('%1');\n"
                    "  const suite = ctrl.createTestItem('suite', 'Arithmetic');\n"
                    "  const adds = ctrl.createTestItem('adds', 'adds', uri);\n"
                    "  adds.range = new vscode.Range(2, 0, 2, 5);\n"
                    "  const fails = ctrl.createTestItem('fails', 'fails');\n"
                    "  suite.children.replace([adds, fails]);\n"
                    "  ctrl.items.replace([suite]);\n"
                    "  ctrl.createRunProfile('Run', vscode.TestRunProfileKind.Run, request => {\n"
                    "    const run = ctrl.createTestRun(request);\n"
                    "    run.started(adds);\n"
                    "    run.passed(adds, 12);\n"
                    "    run.failed(fails, new vscode.TestMessage('two is not three'));\n"
                    "    run.appendOutput('all done\\n');\n"
                    "    run.end();\n"
                    "  }, true);\n"
                    "  context.subscriptions.push(ctrl);\n"
                    "}\n"
                    "module.exports = { activate };\n")
                .arg(file.path()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        AlienTestAdapter adapter(&host);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.isRunning(), 15000);

        // A suite lends its name to the tests underneath it.
        QTRY_COMPARE_WITH_TIMEOUT(adapter.tests("alien.pane").size(), 2, 15000);
        QStringList names;
        for (const AlienTestAdapter::Test &test : adapter.tests("alien.pane"))
            names.append(test.name);
        names.sort();
        QCOMPARE(names, QStringList({"Arithmetic::adds", "Arithmetic::fails"}));

        QList<Autotest::TestResult> reported;
        QObject collector;
        connect(Autotest::Internal::TestRunner::instance(),
                &Autotest::Internal::TestRunner::testResultReady,
                &collector, [&reported](const Autotest::TestResult &result) {
            reported.append(result);
        });

        const Result<> started = adapter.runAll(false);
        QVERIFY2(started.has_value(), qPrintable(started ? QString() : started.error()));
        // Qt Creator's own runs are held off while somebody else's is going.
        QVERIFY(Autotest::Internal::TestRunner::instance()->isTestRunning());

        QTRY_COMPARE_WITH_TIMEOUT(reported.size(), 3, 15000);

        // What the pane groups them under, and what it puts inside each test.
        QCOMPARE(reported.at(0).id(), QString("VSIX Extension Tests"));
        QCOMPARE(reported.at(0).name(), QString("Arithmetic::adds"));
        QCOMPARE(int(reported.at(0).result()), int(Autotest::ResultType::TestStart));

        QCOMPARE(reported.at(1).name(), QString("Arithmetic::adds"));
        QCOMPARE(int(reported.at(1).result()), int(Autotest::ResultType::Pass));
        QCOMPARE(reported.at(1).duration().value_or(QString()), QString("12"));
        // The location comes from the tree, in the editor's own counting.
        QCOMPARE(reported.at(1).fileName(), file);
        QCOMPARE(reported.at(1).line(), 3);

        QCOMPARE(reported.at(2).name(), QString("Arithmetic::fails"));
        QCOMPARE(int(reported.at(2).result()), int(Autotest::ResultType::Fail));
        QCOMPARE(reported.at(2).description(), QString("two is not three"));
        // A test the extension gave no range for has no line to point at.
        QCOMPARE(reported.at(2).line(), 0);

        // The extension ends the run, not the last outcome in it.
        QTRY_VERIFY_WITH_TIMEOUT(
            !Autotest::Internal::TestRunner::instance()->isTestRunning(), 15000);
    }

    // A folder an extension adds to the workspace is a project Qt Creator
    // opens, and dropping it again closes that project.
    void testWorkspaceFolderUpdate()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath folder = root / "added";
        QVERIFY(folder.ensureWritableDir());
        QVERIFY((folder / "main.txt").writeFileContents("hello").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "folders", "alien-folder-test",
            QString("const vscode = require('vscode');\n"
                    "const folder = {uri: vscode.Uri.file('%1')};\n"
                    "function activate(context) {\n"
                    "  vscode.window.showInformationMessage('outOfRange:'\n"
                    "      + vscode.workspace.updateWorkspaceFolders(7, null, folder));\n"
                    "  vscode.window.showInformationMessage('added:'\n"
                    "      + vscode.workspace.updateWorkspaceFolders(0, null, folder));\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand('alien.drop',\n"
                    "      () => vscode.window.showInformationMessage('dropped:'\n"
                    "          + vscode.workspace.updateWorkspaceFolders(0, 1))));\n"
                    "}\n"
                    "module.exports = { activate };\n")
                .arg(folder.path()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy shown(&host, &ExtensionHost::messageShown);
        QSignalSpy failed(&host, &ExtensionHost::activationFailed);
        host.activate(*manifest);

        auto saw = [&shown](const QString &text) {
            for (const QList<QVariant> &args : shown)
                if (args.first().toString() == text)
                    return true;
            return false;
        };

        // Asking beyond the end of the list is rejected, and the answer says so
        // rather than the folder quietly ending up somewhere else.
        QTRY_VERIFY_WITH_TIMEOUT(saw("added:true") || failed.size() >= 1, 15000);
        QVERIFY2(failed.isEmpty(),
                 qPrintable(failed.isEmpty() ? QString() : failed.first().at(1).toString()));
        QVERIFY(saw("outOfRange:false"));

        auto opened = [&folder] {
            return Utils::findOr(ProjectExplorer::ProjectManager::projects(), nullptr,
                                 [&folder](ProjectExplorer::Project *project) {
                                     return project->projectDirectory() == folder;
                                 });
        };
        QTRY_VERIFY_WITH_TIMEOUT(opened(), 15000);

        // And what the host knows about is what can be dropped again, so the
        // list it has has to be the one Qt Creator holds.
        host.setWorkspaceFolders(
            QJsonArray{QJsonObject{{"path", folder.path()}, {"name", folder.fileName()}}});
        host.executeCommand("alien.drop");
        QTRY_VERIFY_WITH_TIMEOUT(saw("dropped:true"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!opened(), 15000);
    }

    // Two projects side by side in one directory are two workspace folders
    // with one path. Dropping one closes one, dropping both closes each once.
    void testWorkspaceFoldersSharingADirectory()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath root = FilePath::fromString(dir.path());
        const FilePath shared = root / "shared";
        QVERIFY(shared.ensureWritableDir());

        const Result<VscodeManifest> manifest = writeMockExtension(
            root / "folders", "alien-shared-folder-test",
            "const vscode = require('vscode');\n"
            "function activate(context) {\n"
            "  context.subscriptions.push(vscode.commands.registerCommand('alien.drop',\n"
            "      count => vscode.window.showInformationMessage('dropped:'\n"
            "          + vscode.workspace.updateWorkspaceFolders(0, count))));\n"
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        const auto inShared = [&shared] {
            return Utils::filtered(ProjectExplorer::ProjectManager::projects(),
                                   [&shared](ProjectExplorer::Project *project) {
                                       return project->projectDirectory() == shared;
                                   });
        };
        const QScopeGuard close([&inShared] {
            ProjectExplorer::ProjectManager::removeProjects(inShared());
        });
        // The manager takes ownership, so removing a project deletes it.
        int opened = 0;
        const auto openBoth = [&shared, &opened] {
            for (int i = 0; i < 2; ++i) {
                ProjectExplorer::ProjectManager::addProject(
                    new TestProject(shared / QString("%1.txt").arg(++opened)));
            }
        };
        openBoth();
        QCOMPARE(inShared().size(), 2);

        ExtensionHost host(node);
        QSignalSpy shown(&host, &ExtensionHost::messageShown);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.drop"), 15000);
        const QJsonObject folder{{"path", shared.path()}, {"name", shared.fileName()}};
        const auto drop = [&](int count) {
            shown.clear();
            host.setWorkspaceFolders(QJsonArray{folder, folder});
            host.executeCommand("alien.drop", QJsonArray{count});
            QTRY_VERIFY_WITH_TIMEOUT(!shown.isEmpty(), 15000);
            QCOMPARE(shown.first().first().toString(), QString("dropped:true"));
        };

        drop(1);
        QTRY_COMPARE_WITH_TIMEOUT(inShared().size(), 1, 15000);

        openBoth();
        QCOMPARE(inShared().size(), 3);
        // Of three, the two the host was told about.
        drop(2);
        QTRY_COMPARE_WITH_TIMEOUT(inShared().size(), 1, 15000);
    }
};

QObject *createAlienHostTest()
{
    return new AlienHostTest;
}

} // namespace Alien::Internal

#include "alien_test.moc"
