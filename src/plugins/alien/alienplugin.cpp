// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionhost.h"
#include "extensionregistry.h"
#include "alienclient.h"
#include "alienconstants.h"
#include "aliensettings.h"
#include "alientr.h"
#include "alienlocatorfilter.h"
#include "alientreeview.h"
#include "alienoutline.h"
#include "autowebviewrenderer.h"
#ifdef ALIEN_WITH_WEBENGINE
#include "webenginewebviewrenderer.h"
#endif
#include "aliencompletion.h"
#include "alienmcp.h"
#include "codicons.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/find/searchresultwindow.h>
#include <coreplugin/secretaspect.h>
#include <coreplugin/idocument.h>
#include <coreplugin/icore.h>
#include <coreplugin/progressmanager/progressmanager.h>
#include <coreplugin/ioutputpane.h>
#include <coreplugin/outputwindow.h>
#include <coreplugin/terminal/searchableterminal.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/statusbarmanager.h>
#include <coreplugin/vcsmanager.h>

#include <mcp/server/toolregistry.h>

#include <extensionsystem/iplugin.h>

#include <languageclient/languageclientmanager.h>

#include <projectexplorer/kitmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/taskhub.h>

#include <texteditor/codeassist/assistproposaliteminterface.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/genericproposalmodel.h>
#include <texteditor/autocompleter.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditor.h>
#include <texteditor/textmark.h>

#include <utils/fileutils.h>

#include <QClipboard>
#include <QMainWindow>
#include <QDockWidget>
#include <QGuiApplication>
#include <utils/infobar.h>
#include <utils/layoutbuilder.h>
#include <utils/mimeutils.h>
#include <utils/stylehelper.h>
#include <utils/utilsicons.h>
#include <utils/theme/theme.h>
#include <utils/temporarydirectory.h>
#include <utils/unarchiver.h>

#include <QEventLoop>
#include <QFontInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QDialog>
#include <QTimer>
#include <QLabel>
#include <QMouseEvent>
#include <QLineEdit>
#include <QPointer>
#include <QScopeGuard>
#include <QSet>

#include <functional>
#include <memory>

#ifdef WITH_TESTS
#include <texteditor/textdocument.h>
#include <texteditor/textdocumentlayout.h>
#include <texteditor/textsuggestion.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSignalSpy>
#include <QSpinBox>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QMenu>
#include <QTextDocument>
#include <QToolBar>

#include "vscodemanifest.h"
#endif

using namespace Core;
using namespace Utils;

namespace Alien::Internal {

// The folder an extension is told about when no project is open. A lone file's
// own directory is rarely what a workspace means to one: it looks there for the
// things that describe a project - a .qdocconf, a manifest, a config - and they
// sit at the top of the checkout. So the checkout is the answer where there is
// one.
static FilePath workspaceFolderFor(const FilePath &documentDirectory)
{
    if (documentDirectory.isEmpty())
        return {};
    const FilePath topLevel = Core::VcsManager::findTopLevelForDirectory(documentDirectory);
    return topLevel.isEmpty() ? documentDirectory : topLevel;
}

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

    // The colours a theme knows by name, which is how an extension asks for
    // "this is a warning" without naming a colour of its own.
    void setColors(const QString &foreground, const QString &background)
    {
        const auto themed = [](const QString &name) -> QColor {
            if (name.contains("errorBackground") || name.contains("errorForeground"))
                return Utils::creatorColor(Utils::Theme::TextColorError);
            if (name.contains("warningBackground") || name.contains("warningForeground"))
                return Utils::creatorColor(Utils::Theme::IconsWarningColor);
            if (name.isEmpty())
                return {};
            return Utils::creatorColor(Utils::Theme::TextColorNormal);
        };
        const QColor front = themed(background.isEmpty() ? foreground : background);
        QPalette colors = m_text->palette();
        if (front.isValid())
            colors.setColor(QPalette::WindowText, front);
        else
            colors = QPalette();
        m_text->setPalette(colors);
    }

    // What clicking it runs. An item with a command is something the user is
    // meant to press, so it says so with the cursor and answers a click.
    void setCommand(const QString &command, const std::function<void(const QString &)> &run)
    {
        m_command = command;
        m_run = run;
        setCursor(command.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    }

    void setContent(const QString &markedUpText)
    {
        QIcon icon = firstCodicon(markedUpText);
        // A button written as icon markup alone is only ever seen as its icon.
        // Without one for that name it would be an empty patch of status bar
        // that still answers a click, so it gets the same stand-in the tree
        // view's toolbar uses.
        if (icon.isNull() && isIconOnly(markedUpText))
            icon = Utils::Icons::TOOLBAR_EXTENSION.icon();
        const int size = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_icon->setPixmap(icon.pixmap(size, size));
        m_icon->setVisible(!icon.isNull());
        m_text->setText(stripCodicons(markedUpText));
    }

    // An item the extension left blank is one it does not want seen. Asked
    // before the status bar is up, so it cannot go by what is on screen.
    bool hasContent() const
    {
        return !m_text->text().isEmpty() || !m_icon->pixmap().isNull();
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) final
    {
        if (event->button() == Qt::LeftButton && !m_command.isEmpty() && m_run)
            m_run(m_command);
        QWidget::mouseReleaseEvent(event);
    }

private:
    QLabel *m_icon = new QLabel;
    QLabel *m_text = new QLabel;
    QString m_command;
    std::function<void(const QString &)> m_run;
};

// The shortcut an extension asks for, in Qt's spelling. VS Code writes
// modifiers in lower case and separates the two halves of a chord with a space,
// where QKeySequence wants a comma; "cmd" is Qt's "Ctrl" on the platform where
// it exists at all.
static QKeySequence keySequenceOf(const VscodeKeybinding &binding)
{
    QString key = binding.key;
    if (HostOsInfo::isMacHost() && !binding.mac.isEmpty())
        key = binding.mac;
    else if (HostOsInfo::isWindowsHost() && !binding.windows.isEmpty())
        key = binding.windows;
    else if (HostOsInfo::isLinuxHost() && !binding.linux.isEmpty())
        key = binding.linux;
    if (key.isEmpty())
        return {};

    QStringList chords;
    for (const QString &chord : key.split(' ', Qt::SkipEmptyParts)) {
        QStringList parts;
        for (const QString &part : chord.split('+', Qt::SkipEmptyParts)) {
            const QString lower = part.toLower();
            if (lower == "cmd" || lower == "meta" || lower == "win")
                parts << (HostOsInfo::isMacHost() ? "Ctrl" : "Meta");
            else if (lower == "ctrl" || lower == "shift" || lower == "alt")
                parts << lower.at(0).toUpper() + lower.mid(1);
            else
                parts << part; // a key name QKeySequence knows: "F7", "O", "Escape"
        }
        chords << parts.join('+');
    }
    // QKeySequence takes up to four, and reads "unknown" as nothing at all.
    const QKeySequence sequence(chords.mid(0, 4).join(','));
    return sequence.toString().isEmpty() ? QKeySequence() : sequence;
}

// Whether one of the events an extension waits for has happened. An extension
// that names none, or asks for "*", starts with the host; the rest wait, which
// is the difference between a handful of extensions running and all of them.
// An event naming something we do not offer - a debug adapter, a task type -
// cannot fire, so an extension that names only those keeps waiting.
static bool activationEventFired(const VscodeManifest &manifest,
                                 const QStringList &openLanguageIds,
                                 const FilePaths &workspaceFolders,
                                 const QSet<QString> &commandRequested,
                                 const QSet<QString> &kindRequested)
{
    if (manifest.activationEvents.isEmpty())
        return true;
    if (commandRequested.contains(manifest.qualifiedId()))
        return true;

    for (const QString &event : manifest.activationEvents) {
        const QString argument = event.section(':', 1);
        if (event == "*" || event == "onStartupFinished")
            return true;
        // A view of ours exists only once the extension has registered it, so
        // waiting for one to be shown would wait forever.
        if (event.startsWith("onView"))
            return true;
        // A debug session of a type it knows, or a task of a type it provides:
        // both are asked for before the extension that answers them is up.
        if (event.startsWith("onDebugResolve:") || event.startsWith("onTaskType:")) {
            if (kindRequested.contains(argument))
                return true;
        } else if (event == "onDebugInitialConfigurations"
                   || event == "onDebugDynamicConfigurations"
                   || event.startsWith("onDebugDynamicConfigurations:")) {
            for (const VscodeDebugger &debugger : manifest.debuggers) {
                if (kindRequested.contains(debugger.type))
                    return true;
            }
        } else if (event.startsWith("onLanguage:")) {
            if (openLanguageIds.contains(argument))
                return true;
        } else if (event.startsWith("workspaceContains:")) {
            for (const FilePath &folder : workspaceFolders) {
                if (!argument.contains('*') && !argument.contains('?')) {
                    if ((folder / argument).exists())
                        return true;
                    continue;
                }
                // The whole pattern decides, not just its last segment:
                // "*/pom.xml" is one directory down, not any depth.
                bool found = false;
                const QRegularExpression expression = globExpression(argument);
                folder.iterateDirectory(
                    [&found, &folder, &expression](const FilePath &item) {
                        if (!expression.match(item.relativePathFromDir(folder)).hasMatch())
                            return IterationPolicy::Continue;
                        found = true;
                        return IterationPolicy::Stop;
                    },
                    FileFilter({argument.split('/').last()}, DirFilterFlag::Files,
                               DirIteratorFlag::Subdirectories));
                if (found)
                    return true;
            }
        }
    }
    return false;
}

// Where the output channels an extension writes to go. In one pane of their
// own rather than in General Messages, which they drown: one extension logging
// its kits fills the panel Qt Creator reports its own trouble in. The channel
// is prefixed, so the pane's filter picks one out.
class AlienOutputPane final : public IOutputPane
{
public:
    explicit AlienOutputPane(QObject *parent)
        : IOutputPane(parent)
    {
        setId("Alien.Output");
        setDisplayName(Tr::tr("VSIX Extensions"));
        setPriorityInStatusBar(-40);

        m_widget = new Core::OutputWindow(Context("Alien.Output"), "Alien.Output.Zoom");
        m_widget->setReadOnly(true);

        connect(this, &IOutputPane::zoomInRequested, m_widget, &Core::OutputWindow::zoomIn);
        connect(this, &IOutputPane::zoomOutRequested, m_widget, &Core::OutputWindow::zoomOut);
        connect(this, &IOutputPane::resetZoomRequested, m_widget, &Core::OutputWindow::resetZoom);
        connect(this, &IOutputPane::fontChanged, m_widget, &Core::OutputWindow::setBaseFont);
        connect(this, &IOutputPane::wheelZoomEnabledChanged,
                m_widget, &Core::OutputWindow::setWheelZoomEnabled);

        setupFilterUi("Alien.Output.Filter", "Alien::Internal::AlienOutputPane");
        setFilteringEnabled(true);
        setupContext("Alien.Output", m_widget);
    }

    ~AlienOutputPane() final { delete m_widget; }

    void append(const QString &channel, const QString &text, bool newLine)
    {
        m_widget->appendMessage(QString("[%1] %2").arg(channel, text) + (newLine ? "\n" : ""),
                                Utils::GeneralMessageFormat);
    }

    QWidget *outputWidget(QWidget *parent) final
    {
        m_widget->setParent(parent);
        return m_widget;
    }

    const QList<Core::OutputWindow *> outputWindows() const final { return {m_widget}; }

    void clearContents() final { m_widget->clear(); }
    bool canFocus() const final { return true; }
    bool hasFocus() const final { return m_widget->window()->focusWidget() == m_widget; }
    void setFocus() final { m_widget->setFocus(); }
    bool canNext() const final { return false; }
    bool canPrevious() const final { return false; }
    void goToNext() final {}
    void goToPrev() final {}
    bool canNavigate() const final { return false; }

    // The filter UI is set up above, so these are what makes it do anything.
    bool hasFilterContext() const final { return true; }

    void updateFilter() final
    {
        m_widget->updateFilterProperties(filterText(), filterCaseSensitivity(),
                                         filterUsesRegexp(), filterIsInverted(),
                                         beforeContext(), afterContext());
    }

private:
    Core::OutputWindow *m_widget = nullptr;
};

// One terminal an extension drives itself. The text in it is what the
// extension wrote, and what is typed goes back to it - there is no process
// here, which is the whole point: a debug adapter's console, a REPL an
// extension implements, a log it wants to look like a terminal.
class ExtensionTerminal final : public Core::SearchableTerminal
{
    Q_OBJECT

public:
    ExtensionTerminal(int id, ExtensionHost *host, QWidget *parent = nullptr)
        : Core::SearchableTerminal(parent)
        , m_id(id)
        , m_host(host)
    {
        // The colours a terminal needs are the theme's own; a view with none
        // set draws with invalid ones.
        std::array<QColor, 20> colors;
        for (int i = 0; i < 16; ++i)
            colors[i] = Utils::creatorColor(Utils::Theme::Color(Utils::Theme::TerminalAnsi0 + i));
        colors[size_t(WidgetColorIdx::Background)]
            = Utils::creatorColor(Utils::Theme::TerminalBackground);
        colors[size_t(WidgetColorIdx::Foreground)]
            = Utils::creatorColor(Utils::Theme::TerminalForeground);
        colors[size_t(WidgetColorIdx::Selection)]
            = Utils::creatorColor(Utils::Theme::TerminalSelection);
        colors[size_t(WidgetColorIdx::FindMatch)]
            = Utils::creatorColor(Utils::Theme::TerminalFindMatch);
        setColors(colors);
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    }

private:
    qint64 writeToPty(const QByteArray &data) final
    {
        if (m_host)
            m_host->sendTerminalInput(m_id, QString::fromUtf8(data));
        return data.size();
    }

    bool resizePty(QSize size) final
    {
        if (m_host)
            m_host->setTerminalDimensions(m_id, size.width(), size.height());
        return true;
    }

    const int m_id;
    QPointer<ExtensionHost> m_host;
};

// Where those terminals live. One tab each, as a terminal is somewhere one
// looks rather than a line in a log: the escape codes an extension writes -
// cortex-debug's console colours its server output - are what a terminal is
// for, and an output window would show them as litter.
class AlienTerminalPane final : public IOutputPane
{
public:
    explicit AlienTerminalPane(QObject *parent)
        : IOutputPane(parent)
    {
        setId("Alien.Terminals");
        setDisplayName(Tr::tr("VSIX Extension Terminals"));
        setPriorityInStatusBar(-45);

        m_tabs = new QTabWidget;
        m_tabs->setTabsClosable(true);
        m_tabs->setDocumentMode(true);
        connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
            if (QWidget *page = m_tabs->widget(index)) {
                m_tabs->removeTab(index);
                delete page;
            }
        });
    }

    ~AlienTerminalPane() final { delete m_tabs; }

    void open(int id, const QString &name, ExtensionHost *host)
    {
        close(id);
        auto terminal = new ExtensionTerminal(id, host);
        m_terminals.insert(id, terminal);
        m_tabs->addTab(terminal, name.isEmpty() ? Tr::tr("Terminal") : name);
    }

    void write(int id, const QString &text)
    {
        if (ExtensionTerminal *terminal = m_terminals.value(id))
            terminal->writeToTerminal(text.toUtf8(), true);
    }

    void rename(int id, const QString &name)
    {
        if (ExtensionTerminal *terminal = m_terminals.value(id)) {
            const int index = m_tabs->indexOf(terminal);
            if (index >= 0)
                m_tabs->setTabText(index, name);
        }
    }

    void close(int id)
    {
        if (ExtensionTerminal *terminal = m_terminals.take(id)) {
            const int index = m_tabs->indexOf(terminal);
            if (index >= 0)
                m_tabs->removeTab(index);
            delete terminal;
        }
    }

    void show(int id)
    {
        if (ExtensionTerminal *terminal = m_terminals.value(id)) {
            m_tabs->setCurrentWidget(terminal);
            popup(IOutputPane::ModeSwitch);
        }
    }

    QWidget *outputWidget(QWidget *parent) final
    {
        m_tabs->setParent(parent);
        return m_tabs;
    }

    void clearContents() final
    {
        if (auto terminal = qobject_cast<ExtensionTerminal *>(m_tabs->currentWidget()))
            terminal->restart();
    }

    bool canFocus() const final { return true; }
    bool hasFocus() const final
    {
        return m_tabs->window()->focusWidget() == m_tabs->currentWidget();
    }
    void setFocus() final { if (QWidget *page = m_tabs->currentWidget()) page->setFocus(); }
    bool canNext() const final { return false; }
    bool canPrevious() const final { return false; }
    void goToNext() final {}
    void goToPrev() final {}
    bool canNavigate() const final { return false; }

private:
    QTabWidget *m_tabs = nullptr;
    QHash<int, ExtensionTerminal *> m_terminals;
};

// Said where the user asked from, because a command that quietly does nothing
// is indistinguishable from one that did its work silently.
static void reportCommandNotRun(const QString &id, const QString &reason)
{
    MessageManager::writeFlashing(
        Tr::tr("The command \"%1\" did not run: %2").arg(id, reason));
}

#ifdef WITH_TESTS
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

        const Result<> result = host.activateBundledDocSyncTestExtension();
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

        const Result<> result = host.activateBundledDocSyncTestExtension();
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

        const Result<> result = host.activateBundledDocSyncTestExtension();
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
        for (TextEditor::TextMark *mark : document->marks())
            tooltips << mark->toolTip();
        QVERIFY2(tooltips.filter("foo.txt:2: first seen here").size() == 1,
                 qPrintable(tooltips.join('|')));

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
        QJsonArray received;
        auto poll = [&] {
            host.requestCompletion(file, 0, 1, [&](const QJsonArray &items) {
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
                    host.resolveQuickPick(id, {1});
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
                    host.resolveQuickPick(id, {1});
                });

        QSignalSpy spy(&host, &ExtensionHost::messageShown);
        const Result<> result = host.activateBundledQuickPickTestExtension();
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
                                                 {"title", "%cmd.title%"}}}}}},
        };
        QVERIFY((root / "package.json").writeFileContents(QJsonDocument(pkg).toJson()));
        const QJsonObject nls{{"ext.name", "Translated Name"}, {"cmd.title", "Translated Title"}};
        QVERIFY((root / "package.nls.json").writeFileContents(QJsonDocument(nls).toJson()));

        const Result<VscodeManifest> manifest
            = VscodeManifest::fromPackageJson(root / "package.json");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        QCOMPARE(manifest->displayName, QString("Translated Name"));
        QCOMPARE(manifest->commands.value(0).title, QString("Translated Title"));
        // An unknown key keeps its placeholder rather than turning into nothing.
        QCOMPARE(manifest->description, QString("%ext.missing%"));
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

        const Result<> result = host.activateBundledTreeViewTestExtension();
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));

        QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 10000);
        QCOMPARE(spy.first().first().toString(), QString("alienExplorer"));

        // Root level.
        QStringList rootLabels;
        QString rootAId;
        QString rootAIcon;
        auto rootPoll = [&] {
            host.requestTreeChildren("alienExplorer", {}, [&](const QJsonArray &nodes) {
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
            host.requestTreeChildren("alienWatched", {}, [&](const QJsonArray &nodes) {
                for (const QJsonValue &n : nodes) {
                    if (n.toObject().value("label").toString() == "Root B")
                        watchedId = n.toObject().value("id").toString();
                }
            });
            return !watchedId.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(watchedPoll(), 15000);
        host.reportTreeSelection("alienWatched", {watchedId});
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.first().first().toString(), QString("picked:Root B"));

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

        // A menu entry gated on a context key the extension has not set yet is
        // not offered ...
        QStringList titles;
        auto menuTitles = [&host, &rootAId, &titles] {
            host.requestTreeMenu("alienExplorer", rootAId, "view/item/context",
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

        // ... and is once setContext says so.
        host.executeCommand("alien.tree.setReady");
        QTRY_COMPARE_WITH_TIMEOUT(menuTitles(), QStringList({"Always", "Gated"}), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(host.paletteCommands(),
                                  QStringList({"alien.tree.gated", "alien.tree.setReady"}),
                                  15000);
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
        if (auto filter = widget->findChild<QLineEdit *>("extensionsFilterEdit"))
            filterText = filter->text();

        QTRY_VERIFY_WITH_TIMEOUT(sawDialog, 15000);
        answer.stop();

        // The list was narrowed to whoever declares the setting, and that
        // extension's settings opened on it.
        QCOMPARE(filterText, QString("alien.owns"));
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
        QTRY_VERIFY_WITH_TIMEOUT(host.formattingLanguageIds().contains("alienbuiltin"), 15000);

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
        QTRY_VERIFY_WITH_TIMEOUT(host.codeLensLanguageIds().contains("alienlens"), 15000);

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
                    "}\n"
                    "module.exports = { activate };\n").arg(watched.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy activated(&host, &ExtensionHost::activated);
        QSignalSpy output(&host, &ExtensionHost::channelOutput);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(activated.size() == 1, 15000);
        // The watch is in place before anything happens in the directory.
        QTRY_VERIFY_WITH_TIMEOUT(host.watchesDirectory(watched), 15000);

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

        // A file the pattern does not name is not reported.
        QVERIFY((watched / "other.txt").writeFileContents("x").has_value());
        QVERIFY(marker.removeFile());
        QTRY_VERIFY_WITH_TIMEOUT(lines().contains("deleted=" + marker.toFSPathString()), 15000);
        QVERIFY(!reported.filter("other.txt").size());
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
        host.requestTasks([&tasks](const QJsonArray &found) { tasks = found; });
        QTRY_VERIFY_WITH_TIMEOUT(!tasks.isEmpty(), 15000);
        QCOMPARE(tasks.size(), 1);
        const QJsonObject task = tasks.first().toObject();
        QCOMPARE(task.value("name").toString(), QString("Build it"));
        QCOMPARE(task.value("source").toString(), QString("Aliens"));
        QCOMPARE(task.value("detail").toString(), QString("runs true"));
        QVERIFY(!task.value("id").toString().isEmpty());

        // And running the one that was listed reaches the extension, which
        // sees its own task start.
        host.runTask(task.value("id").toString());
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
        QTRY_VERIFY_WITH_TIMEOUT(host.rangeFormattingLanguageIds().contains("alienrange"), 15000);
        QVERIFY(host.formatsRangesFor(source));

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // Only the second line is asked about, and only it is changed: the
        // point of formatting a selection is that the rest is left alone.
        QJsonArray edits;
        const auto ask = [&host, &edits, &source] {
            host.requestRangeFormatting(source, 4, true, {1, 0}, {1, 8},
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
        QTRY_VERIFY_WITH_TIMEOUT(host.typeDefinitionLanguageIds().contains("alientype"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonArray locations;
        const auto ask = [&host, &locations, &source] {
            host.requestTypeDefinition(source, 0, 1,
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
            host.requestDefinition(source, 0, 1,
                                   [&definition](const QJsonArray &r) { definition = r; });
            return !definition.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(askDefinition(), 15000);
        QCOMPARE(FilePath::fromUserInput(
                     definition.first().toObject().value("uri").toString()), source);

        Core::EditorManager::closeEditors({editor}, false);
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
        QVERIFY(host.hasWorkspaceSymbols());

        // Everything, and then only what the query matches: the extension does
        // the matching, because only it knows what its names look like.
        QJsonArray all;
        host.requestWorkspaceSymbols({}, [&all](const QJsonArray &r) { all = r; });
        QTRY_VERIFY_WITH_TIMEOUT(all.size() == 2, 15000);
        QCOMPARE(all.first().toObject().value("name").toString(), QString("alpha"));
        QCOMPARE(all.first().toObject().value("container").toString(), QString("Things"));
        QCOMPARE(all.first().toObject().value("path").toString(), source.toFSPathString());
        QCOMPARE(all.first().toObject().value("range").toObject().value("start").toObject()
                     .value("character").toInt(), 6);

        QJsonArray filtered;
        host.requestWorkspaceSymbols("bet", [&filtered](const QJsonArray &r) { filtered = r; });
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
        QTRY_VERIFY_WITH_TIMEOUT(host.signatureLanguageIds().contains("aliensig"), 15000);
        // The characters that open the hint came with the registration.
        QVERIFY(host.signatureTriggers().contains("("));
        QVERIFY(host.signatureTriggers().contains(","));

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonObject help;
        const auto ask = [&host, &help, &source] {
            host.requestSignatureHelp(source, 0, 6,
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
        QTRY_VERIFY_WITH_TIMEOUT(host.linkLanguageIds().contains("alienlink"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        TextEditor::TextEditorWidget *widget = TextEditor::TextEditorWidget::fromEditor(editor);
        QVERIFY(widget);

        QJsonArray links;
        const auto ask = [&host, &links, &source] {
            host.requestDocumentLinks(source,
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
        host.followDocumentLink(widget, cursor,
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
        host.followDocumentLink(widget, cursor,
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
        QTRY_VERIFY_WITH_TIMEOUT(host.highlightLanguageIds().contains("alienhl"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        TextEditor::TextEditorWidget *widget = TextEditor::TextEditorWidget::fromEditor(editor);
        QVERIFY(widget);

        QJsonArray ranges;
        const auto ask = [&host, &ranges, &source] {
            host.requestHighlights(source, 0, 1,
                                   [&ranges](const QJsonArray &result) { ranges = result; });
            return !ranges.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        QCOMPARE(ranges.size(), 2);

        // And they are marked in the editor, the way its own occurrences are.
        host.highlightOccurrences(widget);
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
        QTRY_VERIFY_WITH_TIMEOUT(host.symbolLanguageIds().contains("aliensym"), 15000);
        // The outline offers itself for this file, and for no other.
        QVERIFY(host.providesSymbolsFor(source));
        QVERIFY(!host.providesSymbolsFor(FilePath::fromString(dir.path()) / "other.txt"));

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonArray symbols;
        const auto ask = [&host, &symbols, &source] {
            host.requestDocumentSymbols(source,
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
        QTRY_VERIFY_WITH_TIMEOUT(host.referenceLanguageIds().contains("alienref"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonArray found;
        const auto ask = [&host, &found, &source] {
            host.requestReferences(source, 0, 0,
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
        host.findUsages(source, 0, 0, "thing",
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

        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "ren", "alien-rename-test",
            "const vscode = require('vscode');\n"
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
            "      return edit;\n"
            "    }\n"
            "  }));\n"
            "}\n"
            "module.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienname"}, {"extensions", QJsonArray{".alienname"}}}}}});
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.renameLanguageIds().contains("alienname"), 15000);

        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);
        auto document = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(document);

        // Every place the extension knows of, renamed at once. Asking again is
        // how we wait for the document to have reached the host.
        QJsonArray changes;
        const auto ask = [&host, &changes, &source] {
            host.requestRenameEdits(source, 0, 0, "new",
                                    [&changes](const QJsonArray &result) { changes = result; });
            return !changes.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        host.applyChanges(changes);
        QCOMPARE(document->plainText(), QString("new\nnew\n"));

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
        QTRY_VERIFY_WITH_TIMEOUT(host.codeActionLanguageIds().contains("alienfix"), 15000);

        // The provider is asked about a document the host knows, which is one
        // Qt Creator has open: opening it is what tells the host about it.
        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        // What the editor asks for when the user asks for a quick fix. Asking
        // again is how we wait for the document to have reached the host: the
        // answer to a later request proves the earlier notification arrived.
        QJsonArray offered;
        const auto ask = [&host, &offered, &source] {
            host.requestCodeActions(source, {0, 0}, {0, 5},
                                    [&offered](const QJsonArray &actions) { offered = actions; });
            return !offered.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered.first().toObject().value("title").toString(), QString("Make it right"));

        // Applying it is applying the edits it came with.
        host.applyChanges(offered.first().toObject().value("changes").toArray());
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
        QTRY_VERIFY_WITH_TIMEOUT(host.codeActionLanguageIds().contains("alienfix"), 15000);
        Core::IEditor *editor = Core::EditorManager::openEditor(source);
        QVERIFY(editor);

        QJsonArray offered;
        const auto ask = [&host, &offered, &source] {
            host.requestCodeActions(source, {0, 0}, {0, 5},
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
            "      return [new vscode.CompletionItem('Symbol')];\n"
            "    },\n"
            "    resolveCompletionItem(item) {\n"
            "      item.documentation = 'The one symbol.';\n"
            "      item.additionalTextEdits = [vscode.TextEdit.insert(\n"
            "          new vscode.Position(0, 0), 'import Symbol\\n')];\n"
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
            host.requestCompletion(file, 1, 4, [&items](const QJsonArray &r) { items = r; });
            return !items.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(ask(), 15000);
        // The item as offered knows nothing yet.
        QCOMPARE(items.first().toObject().value("documentation").toString(), QString());
        QVERIFY(items.first().toObject().value("additionalTextEdits").toArray().isEmpty());
        QVERIFY(!items.first().toObject().value("id").toString().isEmpty());

        std::unique_ptr<TextEditor::IAssistProposal> proposal(
            createCompletionProposal(widget->position(), items, &host));
        TextEditor::GenericProposalModelPtr model
            = qSharedPointerCast<TextEditor::GenericProposalModel>(proposal->model());
        model->proposalItem(0)->apply(widget, widget->position());

        // Taking it is what asks, and the import arrives with the answer.
        QTRY_COMPARE_WITH_TIMEOUT(widget->document()->toPlainText(),
                                  QString("import Symbol\nhead\nuse Symbol"), 15000);
        EditorManager::closeAllEditors(false);
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
            host.requestHover(source, 0, 1, [&hover](const QString &text) { hover = text; });
            return hover.contains("first");
        };
        QTRY_VERIFY_WITH_TIMEOUT(askHover(), 15000);
        QVERIFY2(hover.contains("second"), qPrintable(hover));

        QJsonArray locations;
        const auto askDefinition = [&host, &locations, &source] {
            host.requestDefinition(source, 0, 1,
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
            host.requestTreeChildren("alienSub", {}, [&rowId](const QJsonArray &nodes) {
                if (!nodes.isEmpty())
                    rowId = nodes.first().toObject().value("id").toString();
            });
            return !rowId.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(rows(), 15000);

        QJsonArray menu;
        const auto askMenu = [&host, &menu, &rowId] {
            host.requestTreeMenu("alienSub", rowId, "view/item/context",
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
            host.requestDocumentSymbols(source,
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
                    host.resolveQuickPick(id, {1});
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
        host.requestDebugAdapter("alienlang", {}, [](const QJsonObject &) {});
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
                    "  \"brackets\": [[\"{\", \"}\"]],\n"
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
        const QStringList corpusKeys{"ctrl+shift+v", "shift+alt+u", "delete", "enter", "f7",
                                     "shift+f1", "ctrl+k ctrl+shift+c", "ctrl+k v", "F2",
                                     "ctrl+shift+alt+s", "shift+ctrl+f5"};
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
                    host.resolveQuickPick(id, {0});
                });
        QString questionDetail;
        bool questionModal = false;
        connect(&host, &ExtensionHost::messageQuestionRequested, &host,
                [&](int id, const QString &, const QString &, const QStringList &,
                    const QString &detail, bool modal) {
                    questionDetail = detail;
                    questionModal = modal;
                    host.resolveMessageQuestion(id, 0);
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
                    "{\"autoClosingPairs\": [[\"(\", \")\"]],\n"
                    " \"folding\": {\"markers\": {\"start\": \"^//#region\",\n"
                    "                             \"end\": \"^//#endregion\"}}}\n").has_value());

        const FilePath source = root / "code.alienlc";
        QVERIFY(source.writeFileContents("//#region\ninside\n//#endregion\n").has_value());

        const Result<VscodeManifest> manifest = writeMockExtension(
            extension, "alien-lc",
            "function activate() {}\nmodule.exports = { activate };\n",
            QJsonArray{"*"},
            QJsonObject{{"languages", QJsonArray{QJsonObject{
                {"id", "alienlc"},
                {"extensions", QJsonArray{".alienlc"}},
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

        // Typing the opening character brings its closing one.
        int skipped = 0;
        QTRY_VERIFY_WITH_TIMEOUT(
            widget->autoCompleter()->insertMatchingBrace(widget->textCursor(), "(", QChar(),
                                                         false, &skipped) == QString(")"), 15000);

        // The region the markers name folds as one block.
        TextEditor::TextDocument *document = widget->textDocument();
        const QTextBlock inside = document->document()->findBlockByNumber(1);
        QVERIFY(inside.isValid());
        QCOMPARE(TextEditor::TextBlockUserData::foldingIndent(inside), 1);
        QVERIFY(document->isFoldingIndentExternallyProvided());

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
            host.requestDebugConfigurations(
                "aliendbg", [&configurations](const QJsonArray &r) { configurations = r; });
            return !configurations.isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(askConfigurations(), 15000);
        QCOMPARE(configurations.first().toObject().value("name").toString(), QString("Launch It"));

        // What its tasks are for, and who reads their output.
        QJsonArray tasks;
        const auto askTasks = [&host, &tasks] {
            host.requestTasks([&tasks](const QJsonArray &r) { tasks = r; });
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
        QTRY_COMPARE_WITH_TIMEOUT(host.terminalProfiles().size(), 2, 15000);
        const QJsonObject profile = host.terminalProfiles().first().toObject();
        QCOMPARE(profile.value("id").toString(), QString("alien.shell"));
        QCOMPARE(profile.value("title").toString(), QString("Alien Build Shell"));

        // Opening it asks the extension what to run, and a terminal appears
        // running the shell it named.
        QSignalSpy opened(&host, &ExtensionHost::terminalOpened);
        host.openTerminalProfile("alien.shell");
        QTRY_VERIFY_WITH_TIMEOUT(!opened.isEmpty(), 15000);
        QCOMPARE(opened.first().first().toString(), QString("Alien Build Shell"));
        QCOMPARE(opened.first().at(1).value<Utils::CommandLine>().executable().fileName(),
                 QString("echo"));

        // A profile that names no shell opens the user's own, rather than
        // waiting for a command that is never coming.
        QSignalSpy plain(&host, &ExtensionHost::terminalOpened);
        host.openTerminalProfile("alien.default");
        QTRY_VERIFY_WITH_TIMEOUT(!plain.isEmpty(), 15000);
        QCOMPARE(plain.first().first().toString(), QString("Alien Plain Shell"));
        QVERIFY(!plain.first().at(1).value<Utils::CommandLine>().executable().isEmpty());
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
                    host.resolveQuickPick(id, {int(items.indexOf(
                        items.filter("beta").value(0)))});
                });
        connect(&host, &ExtensionHost::inputBoxRequested, &host,
                [&host](int id, const QString &, const QString &value, const QString &, bool) {
                    // Answered with what was suggested, as pressing Return does.
                    host.resolveInputBox(id, value, true);
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
        QCOMPARE(host.debugSessionCount(), 0);

        // And one the extension filled in is debugged.
        host.executeCommand("alien.dbg.start");
        QTRY_VERIFY_WITH_TIMEOUT(!said("started:").isEmpty(), 20000);
        QCOMPARE(said("started:"), QString("started:true:Alien Debug"));
        QCOMPARE(host.debugSessionCount(), 1);

        // Stopping it is reported back, so the extension hears it ended rather
        // than believing it is still debugging.
        host.executeCommand("alien.dbg.stop");
        QTRY_COMPARE_WITH_TIMEOUT(host.debugSessionCount(), 0, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!said("terminated:").isEmpty(), 20000);
        QCOMPARE(said("terminated:"), QString("terminated:Alien Debug"));
    }

    // Where the adapter comes from: what the extension hands over itself, and
    // what only its manifest names. Both end at a real adapter process, so the
    // session ending is what proves it was reached rather than merely named.
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
        QTRY_COMPARE_WITH_TIMEOUT(host.debugSessionCount(), 0, 20000);

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
        QTRY_COMPARE_WITH_TIMEOUT(host.debugSessionCount(), 0, 20000);
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
            "    close: () => {},\n"
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
        host.sendTerminalInput(id, "hello\n");
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 15000);
        QCOMPARE(messages.first().at(0).toString(), QString("typed:hello"));
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
        QCOMPARE(host.debugSessionCount(), 0);
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
        QTRY_COMPARE_WITH_TIMEOUT(host.debugSessionCount(), 0, 20000);
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
        QTRY_VERIFY_WITH_TIMEOUT(host.formattingLanguageIds().contains("alienlang"), 15000);

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
                    host.resolveQuickPick(id, {int(items.indexOf("second"))});
                });
        connect(&host, &ExtensionHost::inputBoxRequested, &host,
                [&host](int id, const QString &, const QString &, const QString &) {
                    host.resolveInputBox(id, "an answer", true);
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

        if (Core::IEditor *editor = Utils::findOrDefault(
                Core::EditorManager::visibleEditors(), [&source](Core::IEditor *candidate) {
                    return candidate->document()->filePath() == source;
                })) {
            Core::EditorManager::closeEditors({editor}, false);
        }
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
            "  context.subscriptions.push(channel);\n"
            "}\n"
            "module.exports = { activate };\n");
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
                              "roundtrip=/a b/c"}));
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
                [&pending](int id, const QString &, const QString &, const QStringList &) {
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
        host.resolveMessageQuestion(pending, 1);
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
        const FilePath added = root / "added.txt";
        QVERIFY((root / "proj.creator").writeFileContents("[General]\n").has_value());
        QVERIFY((root / "proj.files").writeFileContents("first.txt\n").has_value());
        QVERIFY((root / "proj.config").writeFileContents("").has_value());
        QVERIFY((root / "proj.includes").writeFileContents("").has_value());
        QVERIFY((root / "first.txt").writeFileContents("one").has_value());

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
            "}\n"
            "module.exports = { activate };\n");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.files.ready"), 15000);

        const ProjectExplorer::OpenProjectResult opened
            = ProjectExplorer::ProjectExplorerPlugin::openProject(root / "proj.creator");
        if (!opened && opened.errorMessage().contains("No plugin can open project type")) {
            // A test run loads the plugin under test and what it depends on, so
            // the manager for this project type may not be there. Run with
            // "-load GenericProjectManager" to have this covered.
            QSKIP("no project manager for a generic project in this run");
        }
        QVERIFY2(bool(opened), qPrintable(opened.errorMessage()));
        ProjectExplorer::Project *project = opened.project();
        QVERIFY(project);
        QTRY_VERIFY_WITH_TIMEOUT(
            project->files(ProjectExplorer::Project::AllFiles).contains(root / "first.txt"),
            15000);

        // A file appearing in the project is one an extension is told about.
        QVERIFY(added.writeFileContents("two").has_value());
        QVERIFY((root / "proj.files").writeFileContents("first.txt\nadded.txt\n").has_value());
        auto saw = [&spy] {
            for (const QList<QVariant> &args : spy) {
                if (args.first().toString() == "created:added.txt")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(saw(), 30000);

        ProjectExplorer::ProjectManager::closeAllProjects();
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
        connect(&host, &ExtensionHost::messageQuestionRequested, &host,
                [&host, &offered](int id, const QString &level, const QString &,
                                  const QStringList &items) {
                    offered = QStringList{level} + items;
                    host.resolveMessageQuestion(id, 1); // "Later"
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
        QCOMPARE(offered, QStringList({"warn", "Configure", "Later"}));

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
            host.requestTreeChildren("alienCreated", {}, [&labels](const QJsonArray &nodes) {
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
                    { "command": "alien.keys.nothing", "key": "" }
                ] } })").has_value());

        const Result<VscodeManifest> manifest
            = VscodeManifest::fromPackageJson(root / "package.json");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));
        QCOMPARE(manifest->keybindings.size(), 3);

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
        // Nothing asked for stays nothing, rather than becoming a shortcut that
        // answers to no key.
        QVERIFY(sequenceFor("alien.keys.nothing").isEmpty());
    }

    void testTerminal()
    {
        const FilePath node = FilePath("node").searchInPath();
        if (!node.isExecutableFile())
            QSKIP("node.js not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FilePath marker = FilePath::fromString(dir.path()) / "terminal.txt";
        const Result<VscodeManifest> manifest = writeMockExtension(
            FilePath::fromString(dir.path()) / "term", "alien-terminal-test",
            QString("const vscode = require('vscode');\n"
                    "function activate(context) {\n"
                    "  context.subscriptions.push(vscode.commands.registerCommand("
                    "'alien.term.run', () => {\n"
                    "    const terminal = vscode.window.createTerminal('alien');\n"
                    "    terminal.sendText('echo ran > %1');\n"
                    "    terminal.show();\n"
                    "  }));\n"
                    "}\n"
                    "module.exports = { activate };\n").arg(marker.toFSPathString()));
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        ExtensionHost host(node);
        QSignalSpy opened(&host, &ExtensionHost::terminalOpened);
        host.activate(*manifest);
        QTRY_VERIFY_WITH_TIMEOUT(host.registeredCommands().contains("alien.term.run"), 15000);
        host.executeCommand("alien.term.run");

        // What the extension sends is what the terminal is opened with ...
        QTRY_VERIFY_WITH_TIMEOUT(!opened.isEmpty(), 15000);
        QCOMPARE(opened.first().first().toString(), QString("alien"));
        QVERIFY(opened.first().last().value<CommandLine>().arguments().contains("echo ran"));

        // ... and it runs, which is the point of asking for a terminal.
        QTRY_VERIFY_WITH_TIMEOUT(marker.exists(), 20000);
        QCOMPARE(marker.fileContents().value_or(QByteArray()).trimmed(), QByteArray("ran"));
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
        host.requestDebugAdapter("alienlang", {{"request", "launch"}},
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
        QTRY_VERIFY_WITH_TIMEOUT(!host.viewWelcome("alien.welcome.view").isEmpty(), 15000);
        QVERIFY(host.viewWelcome("alien.welcome.view").contains("Nothing here yet."));
        QVERIFY(host.viewWelcome("alien.welcome.view").contains("command:alien.welcome.setup"));

        // Once the clause it is guarded by holds, the view has nothing to say.
        host.executeCommand("alien.welcome.ready");
        QTRY_VERIFY_WITH_TIMEOUT(host.viewWelcome("alien.welcome.view").isEmpty(), 15000);

        // A view nobody contributed anything for says nothing, and answering
        // the ping took everything that was on its way with it.
        host.executeCommand("alien.welcome.ping");
        QTRY_VERIFY_WITH_TIMEOUT(Utils::anyOf(spy, [](const QList<QVariant> &args) {
            return args.first().toString() == "ping";
        }), 15000);
        QVERIFY(host.viewWelcome("alien.other.view").isEmpty());
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
            host.requestResourceMenu("explorer/context", path, isFolder,
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
        for (const Result<VscodeManifest> *m : {&neighbour, &asker})
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
                    host.resolveQuickPick(id, canPickMany ? QList<int>{0, 2} : QList<int>{1});
                });
        bool askedForSecret = false;
        connect(&host, &ExtensionHost::inputBoxRequested, &host,
                [&host, &askedForSecret](int id, const QString &, const QString &,
                                         const QString &, bool password) {
                    askedForSecret = password;
                    host.resolveInputBox(id, "hunter2", true);
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
        // The extension heard both that a tab appeared and that another became
        // the active one.
        QCOMPARE(said(), QString("tabs:1:one.txt+two.txt:two.txt:uri:otruextrue"));
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
        QCOMPARE(said(), QString("enums:1,-1,2,2,2,3,3:back:true"));
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

    // What the extension rewrites the moment a particular character is typed -
    // closing a brace, ending a statement - which is what Qt Creator does for
    // languages it knows itself.
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
                    "alien.sections": { "type": "array", "default": [{"label": "One"}] }
                } } } })";
        QVERIFY((root / "package.json").writeFileContents(packageJson).has_value());

        const Result<VscodeManifest> manifest
            = VscodeManifest::fromPackageJson(root / "package.json");
        QVERIFY2(manifest.has_value(), qPrintable(manifest ? QString() : manifest.error()));

        QCOMPARE(manifest->configurationSettings.size(), 4);
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
};
#endif

class AlienPlugin;
static AlienPlugin *thePlugin = nullptr;

class AlienPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Alien.json")

public:
    void initialize() final
    {
        thePlugin = this;
        m_settingsPage = setupAlienSettings();
        registerMcpTools();

        ProjectExplorer::TaskHub::addCategory(
            {Constants::TASK_CATEGORY_DIAGNOSTICS,
             Tr::tr("VSIX Extensions"),
             Tr::tr("Diagnostics reported by VSIX extensions.")});

        ActionBuilder rescan(this, Constants::RESCAN_ACTION_ID);
        rescan.setText(Tr::tr("Rescan VSIX Extensions"));
        rescan.addOnTriggered(this, [this] { reload(); });

        ActionBuilder runTest(this, Constants::RUN_TEST_EXTENSION_ACTION_ID);
        runTest.setText(Tr::tr("Run Alien Test Extension"));
        runTest.addOnTriggered(this, [this] {
            if (const Result<> result = host()->activateBundledTestExtension(); !result)
                MessageManager::writeFlashing(result.error());
        });

        m_locatorFilter = std::make_unique<AlienLocatorFilter>(
            [this] { return extensionCommands(); },
            [this](const QString &id) { runExtensionCommand(id); });

        m_symbolFilter = std::make_unique<AlienSymbolLocatorFilter>(
            [this](const QString &query,
                   const std::function<void(const QList<AlienSymbol> &)> &report) {
                if (!m_host || !m_host->hasWorkspaceSymbols()) {
                    report({});
                    return;
                }
                m_host->requestWorkspaceSymbols(query, [report](const QJsonArray &found) {
                    QList<AlienSymbol> symbols;
                    for (const QJsonValue &value : found) {
                        const QJsonObject object = value.toObject();
                        const QJsonObject start = object.value("range").toObject()
                                                      .value("start").toObject();
                        AlienSymbol symbol;
                        symbol.name = object.value("name").toString();
                        symbol.container = object.value("container").toString();
                        symbol.filePath = FilePath::fromUserInput(
                            object.value("path").toString());
                        symbol.line = start.value("line").toInt();
                        symbol.character = start.value("character").toInt();
                        symbols.append(symbol);
                    }
                    report(symbols);
                });
            });

        m_taskFilter = std::make_unique<AlienTaskLocatorFilter>(
            [this](const std::function<void(const QList<AlienTask> &)> &report) {
                if (!m_host) {
                    report({});
                    return;
                }
                m_host->requestTasks([report](const QJsonArray &found) {
                    QList<AlienTask> tasks;
                    for (const QJsonValue &value : found) {
                        const QJsonObject object = value.toObject();
                        AlienTask task;
                        task.id = object.value("id").toString();
                        task.name = object.value("name").toString();
                        task.source = object.value("source").toString();
                        task.detail = object.value("detail").toString();
                        tasks.append(task);
                    }
                    report(tasks);
                });
            },
            [this](const QString &id) {
                if (m_host)
                    m_host->runTask(id);
            });

        ActionBuilder execute(this, Constants::EXECUTE_COMMAND_ACTION_ID);
        execute.setText(Tr::tr("Execute Alien Extension Command..."));
        execute.addOnTriggered(this, [this] { executeCommand(); });

        ActionBuilder install(this, Constants::INSTALL_VSIX_ACTION_ID);
        install.setText(Tr::tr("Install Extension (.vsix)..."));
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

    // The status bar reparents what it is given, so these widgets belong to
    // Qt Creator while their class lives in this library. They have to go
    // before it does.
    void removeStatusBarWidgets()
    {
        if (m_statusMessage)
            StatusBarManager::destroyStatusBarWidget(m_statusMessage);
        m_statusMessage = nullptr;
        for (StatusBarItem *item : std::as_const(m_statusItems))
            StatusBarManager::destroyStatusBarWidget(item);
        m_statusItems.clear();
    }

    ShutdownFlag aboutToShutdown() final
    {
        removeStatusBarWidgets();

        // The action manager keeps a command per action, so an action that goes
        // with the library leaves a command pointing into it - listed under
        // Preferences > Keyboard, and dispatched to on the next shortcut.
        for (const Id &id : m_keyActions.keys())
            removeKeybinding(id);

        ProjectExplorer::TaskHub::removeCategory(Constants::TASK_CATEGORY_DIAGNOSTICS);

        // The host is a child process of ours, and killing it means waiting for
        // it. Doing that here, rather than leaving it to the destructor, keeps
        // the wait out of IPlugin's teardown.
        if (m_host)
            m_host->shutdown();

        // The clients belong to LanguageClientManager, not to this plugin, and
        // shutting one down only sends it the request. Reporting a synchronous
        // shutdown here lets this library be unloaded while they are still
        // alive - with their vtables and their EditorManager connections in it,
        // so the next opened document jumps into freed code. Wait for them to
        // be gone instead; the client's own 20s timer bounds the wait.
        QList<QPointer<AlienClient>> alive;
        const auto keep = [&alive](const QPointer<AlienClient> &client) {
            if (client && !alive.contains(client))
                alive.append(client);
        };
        for (const QPointer<AlienClient> &client : std::as_const(m_clients))
            keep(client);
        // The host starts clients of its own, for extensions that bring a
        // language server; those have to be waited for just the same.
        if (m_host) {
            for (const QPointer<AlienClient> &client : m_host->languageClients())
                keep(client);
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

            m_host->wakeCommandOwner = [this](const QString &command,
                                              const std::function<void(bool)> &done) {
                wakeCommandOwner(command, [done](const Result<> &woken) { done(bool(woken)); });
            };
            m_host->startExtension = [this](const QString &extensionId,
                                            const std::function<void(bool)> &done) {
                wakeExtension(extensionId, [done](const Result<> &r) { done(bool(r)); });
            };
            m_host->setKnownExtensions(enabledExtensions());

            connect(m_host, &ExtensionHost::channelOutput, this,
                    [this](const QString &channel, const QString &text, bool newLine) {
                        outputPane()->append(channel, text, newLine);
                    });
            connect(m_host, &ExtensionHost::extensionTerminalOpened, this,
                    [this](int id, const QString &name) {
                        terminalPane()->open(id, name, m_host);
                    });
            connect(m_host, &ExtensionHost::extensionTerminalOutput, this,
                    [this](int id, const QString &text) { terminalPane()->write(id, text); });
            connect(m_host, &ExtensionHost::extensionTerminalRenamed, this,
                    [this](int id, const QString &name) { terminalPane()->rename(id, name); });
            connect(m_host, &ExtensionHost::extensionTerminalClosed, this,
                    [this](int id) { terminalPane()->close(id); });
            connect(m_host, &ExtensionHost::extensionTerminalShowRequested, this,
                    [this](int id) { terminalPane()->show(id); });
            connect(m_host, &ExtensionHost::channelShowRequested, this,
                    [this](bool preserveFocus) {
                        // An extension that logs as it works asks to be seen
                        // without being switched to; taking the mode from the
                        // user on every line is not what it asked for.
                        outputPane()->popup(preserveFocus
                                                ? IOutputPane::NoModeSwitch
                                                : IOutputPane::ModeSwitch | IOutputPane::WithFocus);
                    });
            connect(m_host, &ExtensionHost::channelClearRequested, this,
                    [this] { outputPane()->clearContents(); });

            // These answer when the user does, not by waiting here for it: the
            // request arrives while the host process is delivering output, and
            // a dialog run from there stops us reading it - and lets the
            // process be destroyed underneath the emission that started it.
            connect(m_host, &ExtensionHost::messageQuestionRequested, this,
                    [this](int id, const QString &level, const QString &message,
                           const QStringList &items, const QString &detail, bool modal) {
                        auto box = new QMessageBox(ICore::dialogParent());
                        box->setAttribute(Qt::WA_DeleteOnClose);
                        box->setIcon(level == "error" ? QMessageBox::Critical
                                     : level == "warn" ? QMessageBox::Warning
                                                       : QMessageBox::Information);
                        box->setText(stripCodicons(message));
                        // The second line of the question, and whether it is
                        // one the user has to answer before going on.
                        box->setInformativeText(stripCodicons(detail));
                        box->setWindowModality(modal ? Qt::ApplicationModal
                                                     : Qt::NonModal);
                        // In the order the extension offered them, so the first
                        // one is the one it means as the answer.
                        QList<QAbstractButton *> buttons;
                        for (const QString &item : items) {
                            buttons.append(box->addButton(stripCodicons(item),
                                                          QMessageBox::ActionRole));
                        }
                        box->addButton(QMessageBox::Cancel);
                        connect(box, &QDialog::finished, this, [this, id, box, buttons] {
                            if (QTC_GUARD(m_host))
                                m_host->resolveMessageQuestion(
                                    id, int(buttons.indexOf(box->clickedButton())));
                        });
                        box->open();
                    });

            connect(m_host, &ExtensionHost::quickPickHidden, this,
                    [this](const QString &pickId) {
                        // A list the extension has taken back; leaving it up
                        // would leave the user a dialog nobody answers.
                        if (QInputDialog *dialog = m_livePicks.take(pickId))
                            dialog->reject();
                    });

            connect(m_host, &ExtensionHost::quickPickUpdated, this,
                    [this](const QString &pickId, const QStringList &items,
                           const QString &placeholder) {
                        // The extension showed the list before it had one.
                        QInputDialog *dialog = m_livePicks.value(pickId);
                        if (!dialog)
                            return;
                        const QStringList shown = Utils::transform(items, &stripCodicons);
                        dialog->setComboBoxItems(shown);
                        if (!placeholder.isEmpty())
                            dialog->setLabelText(placeholder);
                    });

            connect(m_host, &ExtensionHost::quickPickRequested, this,
                    [this](int id, const QStringList &items, const QString &placeholder,
                           bool canPickMany, const QString &pickId) {
                        if (canPickMany) {
                            showMultiPick(id, items, placeholder);
                            return;
                        }
                        // Quick pick entries carry icon markup as well; the
                        // answer is an index, so stripping it changes nothing
                        // for the extension.
                        const QStringList shown = Utils::transform(items, [](const QString &item) {
                            return stripCodicons(item);
                        });
                        auto dialog = new QInputDialog(ICore::dialogParent());
                        dialog->setAttribute(Qt::WA_DeleteOnClose);
                        dialog->setWindowTitle(Tr::tr("Select"));
                        // Remembered while it is up, so a list that arrives
                        // later reaches the dialog waiting for it.
                        if (!pickId.isEmpty()) {
                            m_livePicks.insert(pickId, dialog);
                            connect(dialog, &QObject::destroyed, this,
                                    [this, pickId] { m_livePicks.remove(pickId); });
                        }
                        dialog->setLabelText(placeholder.isEmpty() ? Tr::tr("Select an item:")
                                                                   : placeholder);
                        dialog->setComboBoxItems(shown);
                        dialog->setComboBoxEditable(false);
                        connect(dialog, &QDialog::finished, this,
                                [this, id, dialog, shown](int result) {
                                    const int index = result == QDialog::Accepted
                                                          ? int(shown.indexOf(dialog->textValue()))
                                                          : -1;
                                    if (QTC_GUARD(m_host))
                                        m_host->resolveQuickPick(id, {index});
                                });
                        dialog->open();
                    });

            connect(m_host, &ExtensionHost::inputBoxRequested, this,
                    [this](int id, const QString &prompt, const QString &value,
                           const QString &placeholder, bool password) {
                        auto dialog = new QInputDialog(ICore::dialogParent());
                        dialog->setAttribute(Qt::WA_DeleteOnClose);
                        dialog->setWindowTitle(prompt.isEmpty() ? Tr::tr("Input") : prompt);
                        dialog->setLabelText(placeholder);
                        dialog->setTextValue(value);
                        if (password)
                            dialog->setTextEchoMode(QLineEdit::Password);
                        connect(dialog, &QDialog::finished, this,
                                [this, id, dialog](int result) {
                                    if (QTC_GUARD(m_host))
                                        m_host->resolveInputBox(id, dialog->textValue(),
                                                                result == QDialog::Accepted);
                                });
                        dialog->open();
                    });

            connect(m_host, &ExtensionHost::stopped, this, [this] {
                // Nothing runs in a host that is gone, so the next activation
                // trigger starts a fresh one and puts the extensions back.
                m_activeIds.clear();
                removeStatusBarWidgets();
                MessageManager::writeFlashing(
                    Tr::tr("The extension host stopped. It restarts with the next "
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
                    [this](const QString &id, const QJsonObject &entry) {
                        const int alignment = entry.value("alignment").toInt();
                        StatusBarItem *item = m_statusItems.value(id);
                        if (!item) {
                            item = new StatusBarItem;
                            item->setObjectName("alienStatusItem." + id);
                            StatusBarManager::addStatusBarWidget(
                                item, alignment == 2 ? StatusBarManager::RightCorner
                                                     : StatusBarManager::First);
                            m_statusItems.insert(id, item);
                        }
                        item->setContent(entry.value("text").toString());
                        item->setToolTip(stripCodicons(entry.value("tooltip").toString()));
                        item->setCommand(entry.value("command").toString(),
                                         [this](const QString &id) {
                                             runExtensionCommand(id);
                                         });
                        // An item saying something is wrong says so in colour.
                        item->setColors(entry.value("color").toString(),
                                        entry.value("backgroundColor").toString());
                        item->setVisible(entry.value("visible").toBool()
                                         && item->hasContent());
                    });

            connect(m_host, &ExtensionHost::statusBarItemRemoved, this, [this](const QString &id) {
                if (StatusBarItem *item = m_statusItems.take(id))
                    StatusBarManager::destroyStatusBarWidget(item);
            });

            setupAlienOutline(m_host);

            m_webviewRenderer = std::make_unique<AutoWebviewRenderer>();
            m_host->setWebviewRenderer(m_webviewRenderer.get());

            connect(m_host, &ExtensionHost::configurationWritten, this, [this] {
                m_host->setConfiguration(buildConfiguration(m_activatable));
            });

            connect(m_host, &ExtensionHost::terminalProfilesChanged, this,
                    [this] { registerTerminalProfiles(); });
            connect(m_host, &ExtensionHost::debugTypeRequested, this,
                    [this](const QString &type) {
                        if (m_pendingKindActivations.contains(type))
                            return;
                        m_pendingKindActivations.insert(type);
                        syncActivation();
                    });
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
            Tr::tr("Install Extension"), {}, Tr::tr("Extensions (*.vsix)"));
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
        if (const Result<> r = target.parentDir().ensureWritableDir(); !r) {
            MessageManager::writeFlashing(Tr::tr("Cannot install to \"%1\": %2")
                                              .arg(target.toUserOutput(), r.error()));
            return;
        }
        target.removeRecursively();
        if (const Result<> r = extracted.copyRecursively(target); !r) {
            MessageManager::writeFlashing(Tr::tr("Cannot install to \"%1\": %2")
                                              .arg(target.toUserOutput(), r.error()));
            return;
        }

        MessageManager::writeFlashing(
            Tr::tr("Installed extension \"%1\".").arg(manifest->qualifiedId()));
        reload();
    }

    // What these extensions are and are not, said once before any of them
    // runs: they are other people's software, on their terms, and some of
    // them are licensed for use with the editor they were written for and
    // nothing else. Mirrors what the extension manager asks about its own
    // third-party sources.
    void askToRunExtensions()
    {
        const char kAccepted[] = "Alien.LegalNotice";
        InfoBar *infoBar = ICore::popupInfoBar();
        if (!infoBar->canInfoBeAdded(kAccepted))
            return;

        InfoBarEntry info(
            kAccepted,
            Tr::tr("VSIX extensions are created and owned by third parties, not by "
                   "The Qt Company, and Qt Creator runs them as they are.\n"
                   "\n"
                   "You acknowledge that you install and run extensions at your own "
                   "discretion and risk. They come without warranties of any kind and "
                   "may be subject to license terms of their own - including terms that "
                   "permit their use only with a particular product, which Qt Creator "
                   "cannot check for you.\n"
                   "\n"
                   "You can turn this off again in Preferences > VSIX Extensions."),
            InfoBarEntry::GlobalSuppression::Disabled);
        info.setTitle(Tr::tr("Run VSIX Extensions?"));
        info.setInfoType(InfoLabelType::Information);
        info.addCustomButton(
            Tr::tr("Run Extensions"),
            [this] {
                settings().legalNoticeAccepted.setValue(true);
                settings().writeSettings();
                reload();
            },
            {},
            InfoBarEntry::ButtonAction::SuppressPersistently);
        info.addCustomButton(
            Tr::tr("Do Not Run"),
            [this] {
                settings().enable.setValue(false);
                settings().writeSettings();
                reload();
            },
            {},
            InfoBarEntry::ButtonAction::SuppressPersistently);
        infoBar->addInfo(info);
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

        // Turning the support on is not yet consent to run someone else's code.
        if (!settings().legalNoticeAccepted()) {
            deactivateAll();
            askToRunExtensions();
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
            MessageManager::writeSilently(Tr::tr("VSIX extension: %1").arg(error));

        for (const VscodeManifest &manifest : manifests) {
            MessageManager::writeSilently(
                Tr::tr("Discovered extension \"%1\" (%2 languages, %3 commands)%4.")
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

        registerKeybindings();

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
        // Waiting for a project or a document to name a folder, since an
        // extension like qt-qml only starts its server for one - unless a
        // command of it was asked for by name, which has to run either way.
        if (folders.isEmpty() && m_pendingCommandActivations.isEmpty())
            return;

        const QList<VscodeManifest> desired = extensionsToActivate();
        QSet<QString> enabledIds;
        for (const VscodeManifest &manifest : enabledExtensions())
            enabledIds.insert(manifest.qualifiedId());

        // Deactivate extensions the user turned off (or that disappeared). An
        // extension that has started stays started, as it does in the editor
        // these come from: it holds state the user can see.
        for (const QString &id : m_activeIds.values()) {
            if (!enabledIds.contains(id)) {
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
            host()->setKnownExtensions(enabledExtensions());
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

    // The commands the running extensions offer for the palette, paired with
    // the titles their manifests give them - the host only knows the ids.
    QList<AlienCommand> extensionCommands() const
    {
        QStringList ids = m_host ? m_host->paletteCommands() : QStringList();
        // An extension waiting for one of its commands has to be offered them,
        // or the wait never ends. Its manifest says which; the palette rules
        // that need the host are applied once it runs.
        for (const VscodeManifest &manifest : enabledExtensions()) {
            if (m_activeIds.contains(manifest.qualifiedId()))
                continue;
            for (const VscodeCommand &command : manifest.commands) {
                if (!ids.contains(command.command)
                    && !manifest.paletteHiddenCommands.contains(command.command)) {
                    ids.append(command.command);
                }
            }
        }

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
    // A shortcut an extension asks for becomes an action of ours, so it works,
    // shows up under Preferences > Keyboard, and can be rebound there like any
    // other. Running it wakes the extension if it is still waiting.
    // A terminal an extension knows how to set up becomes an action of ours, so
    // it can be found, run and given a shortcut like anything else.
    void registerTerminalProfiles()
    {
        QSet<Id> wanted;
        for (const QJsonValue &value : m_host->terminalProfiles()) {
            const QJsonObject profile = value.toObject();
            const QString profileId = profile.value("id").toString();
            if (profileId.isEmpty())
                continue;
            const Id id = Id("Alien.TerminalProfile.").withSuffix(profileId);
            wanted.insert(id);
            if (m_terminalProfileActions.contains(id))
                continue;

            const QString title = profile.value("title").toString();
            auto action = new QAction(title.isEmpty() ? profileId : title, this);
            connect(action, &QAction::triggered, this, [this, profileId] {
                if (m_host)
                    m_host->openTerminalProfile(profileId);
            });
            Command *registered = ActionManager::registerAction(action, id);
            registered->setDescription(action->text());
            m_terminalProfileActions.insert(id, action);
        }

        for (const Id &id : m_terminalProfileActions.keys()) {
            if (wanted.contains(id))
                continue;
            QAction *action = m_terminalProfileActions.take(id);
            ActionManager::unregisterAction(action, id);
            delete action;
        }
    }

    void registerKeybindings()
    {
        const QList<VscodeManifest> enabled = enabledExtensions();
        QSet<Id> wanted;
        for (const VscodeManifest &manifest : enabled) {
            for (const VscodeKeybinding &binding : manifest.keybindings) {
                const QKeySequence sequence = keySequenceOf(binding);
                if (sequence.isEmpty())
                    continue;
                const Id id = Id("Alien.").withSuffix(binding.command);
                wanted.insert(id);
                if (m_keyActions.contains(id))
                    continue;

                const auto it = std::find_if(manifest.commands.begin(), manifest.commands.end(),
                                             [&binding](const VscodeCommand &c) {
                                                 return c.command == binding.command;
                                             });
                const QString title = it == manifest.commands.end()
                                          ? binding.command
                                          : (it->category.isEmpty()
                                                 ? it->title
                                                 : it->category + ": " + it->title);

                auto action = new QAction(title, this);
                // What the manifest hands the command, and the clause that has
                // to hold for the key to do anything at all: a shortcut bound
                // only inside the extension's own view must not take over the
                // editor's.
                connect(action, &QAction::triggered, this,
                        [this, command = binding.command, when = binding.when,
                         arguments = binding.arguments] {
                            runExtensionCommand(command,
                                                arguments.isUndefined() ? QJsonArray()
                                                                        : QJsonArray{arguments},
                                                when);
                        });
                Command *registered = ActionManager::registerAction(action, id);
                registered->setDefaultKeySequence(sequence);
                registered->setDescription(title);
                m_keyActions.insert(id, action);
            }
        }

        // Drop the ones whose extension is gone, so a shortcut never outlives
        // what it was for.
        for (const Id &id : m_keyActions.keys()) {
            if (!wanted.contains(id))
                removeKeybinding(id);
        }
    }

    void removeKeybinding(const Id &id)
    {
        QAction *action = m_keyActions.take(id);
        ActionManager::unregisterAction(action, id);
        delete action;
    }

    AlienOutputPane *outputPane()
    {
        if (!m_outputPane)
            m_outputPane = new AlienOutputPane(this);
        return m_outputPane;
    }

    AlienTerminalPane *terminalPane()
    {
        if (!m_terminalPane)
            m_terminalPane = new AlienTerminalPane(this);
        return m_terminalPane;
    }

    VscodeManifest commandOwner(const QString &id) const
    {
        return Utils::findOrDefault(enabledExtensions(), [&id](const VscodeManifest &m) {
            return Utils::anyOf(m.commands, [&id](const VscodeCommand &c) {
                return c.command == id;
            });
        });
    }

    // Starts the extension a command belongs to, and says whether the command
    // turned up. An extension that activates without registering what it said
    // it has - or that fails to activate at all - would leave the caller
    // waiting forever, so both of those answer too.
    // Starts an extension that is installed and enabled but has not run yet,
    // and reports once it has - or once it is clear that it will not.
    void wakeExtension(const QString &extensionId,
                       const std::function<void(const Result<> &)> &done)
    {
        if (!m_host) {
            done(ResultError(Tr::tr("The extension host is not running.")));
            return;
        }
        if (m_activeIds.contains(extensionId)) {
            done(Result<>{});
            return;
        }
        const bool known = Utils::anyOf(enabledExtensions(), [&](const VscodeManifest &m) {
            return m.qualifiedId() == extensionId;
        });
        if (!known) {
            done(ResultError(Tr::tr("\"%1\" is not installed or not enabled.").arg(extensionId)));
            return;
        }
        const auto waiting = std::make_shared<QList<QMetaObject::Connection>>();
        const auto finish = [this, waiting, done](const Result<> &result) {
            for (const QMetaObject::Connection &connection : *waiting)
                disconnect(connection);
            waiting->clear();
            done(result);
        };
        waiting->append(connect(m_host, &ExtensionHost::activated, this,
                                [finish, extensionId](const QString &activated) {
                                    if (activated == extensionId)
                                        finish(Result<>{});
                                }));
        waiting->append(connect(m_host, &ExtensionHost::activationFailed, this,
                                [finish, extensionId](const QString &failed,
                                                      const QString &error) {
                                    if (failed == extensionId)
                                        finish(ResultError(error));
                                }));
        m_pendingCommandActivations.insert(extensionId);
        syncActivation();
    }

    void wakeCommandOwner(const QString &id, const std::function<void(const Result<> &)> &done)
    {
        const VscodeManifest owner = commandOwner(id);
        if (!m_host || owner.qualifiedId().isEmpty()) {
            done(ResultError(Tr::tr("No extension offers it.")));
            return;
        }
        const auto waiting = std::make_shared<QList<QMetaObject::Connection>>();
        const auto finish = [this, waiting, done](const Result<> &result) {
            for (const QMetaObject::Connection &connection : *waiting)
                disconnect(connection);
            waiting->clear();
            done(result);
        };

        waiting->append(connect(m_host, &ExtensionHost::commandsChanged, this,
                                [this, id, finish] {
                                    if (m_host && m_host->registeredCommands().contains(id))
                                        finish(Result<>{});
                                }));
        waiting->append(connect(m_host, &ExtensionHost::activationFailed, this,
                                [finish, owner](const QString &failed, const QString &error) {
                                    if (failed == owner.qualifiedId())
                                        finish(ResultError(error));
                                }));
        // Once it has activated, whatever it was going to register is
        // registered: if the command is still not there, it is not coming.
        waiting->append(connect(m_host, &ExtensionHost::activated, this,
                                [this, id, finish, owner](const QString &activated) {
                                    if (activated != owner.qualifiedId())
                                        return;
                                    if (m_host && m_host->registeredCommands().contains(id))
                                        return; // the other handler answers
                                    finish(ResultError(Tr::tr("\"%1\" does not offer it.")
                                                           .arg(owner.qualifiedId())));
                                }));

        m_pendingCommandActivations.insert(owner.qualifiedId());
        syncActivation();
    }

    // Picking several at once. QInputDialog has one item and no more, so this
    // is a list the user selects in, answering with every row that was picked.
    void showMultiPick(int id, const QStringList &items, const QString &placeholder)
    {
        auto dialog = new QDialog(ICore::dialogParent());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(Tr::tr("Select"));

        auto list = new QListWidget;
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        for (const QString &item : items)
            list->addItem(stripCodicons(item));

        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

        using namespace Layouting;
        Column {
            new QLabel(placeholder.isEmpty() ? Tr::tr("Select items:") : placeholder),
            list,
            buttons,
        }.attachTo(dialog);

        connect(dialog, &QDialog::finished, this, [this, id, list](int result) {
            QList<int> chosen;
            if (result == QDialog::Accepted) {
                for (const QModelIndex &index : list->selectionModel()->selectedRows())
                    chosen.append(index.row());
                std::sort(chosen.begin(), chosen.end());
            }
            if (QTC_GUARD(m_host))
                m_host->resolveQuickPick(id, chosen);
        });
        dialog->open();
    }

    // Runs a command, waking the extension that contributes it if it has been
    // waiting for exactly that.
    void runExtensionCommand(const QString &id, const QJsonArray &arguments = {},
                             const QString &when = {})
    {
        if (!m_host)
            return;
        if (m_host->registeredCommands().contains(id)
            || commandOwner(id).qualifiedId().isEmpty()) {
            // Running, or a built-in the host answers.
            m_host->executeCommand(id, arguments, when);
            return;
        }
        wakeCommandOwner(id, [this, id, arguments, when](const Result<> &woken) {
            if (!m_host)
                return;
            if (woken)
                m_host->executeCommand(id, arguments, when);
            else
                reportCommandNotRun(id, woken.error());
        });
    }

    QList<VscodeManifest> enabledExtensions() const
    {
        const QStringList ids = settings().enabledExtensions.ids();
        const QSet<QString> enabled(ids.begin(), ids.end());
        return Utils::filtered(m_activatable, [&](const VscodeManifest &manifest) {
            return enabled.contains(manifest.qualifiedId());
        });
    }

    QList<VscodeManifest> allEnabledForActivation() const { return enabledExtensions(); }

    // The folders an extension's "workspaceContains:" is asked about.
    FilePaths workspaceFolderPaths() const
    {
        FilePaths folders;
        for (ProjectExplorer::Project *project : ProjectExplorer::ProjectManager::projects())
            folders.append(project->projectDirectory());
        if (folders.isEmpty()) {
            if (IDocument *document = EditorManager::currentDocument())
                folders.append(workspaceFolderFor(document->filePath().parentDir()));
        }
        return folders;
    }

    // The languages of what is open, as the manifests define them: an extension
    // waiting for one of them has to be told before anything is running, so
    // this reads the manifests rather than the host.
    QStringList openLanguageIds() const
    {
        QStringList ids;
        for (IDocument *document : DocumentModel::openedDocuments()) {
            for (const VscodeManifest &manifest : m_activatable) {
                for (const VscodeLanguage &language : manifest.languages) {
                    if (languageMatchesFile(language, document->filePath())
                        && !ids.contains(language.id)) {
                        ids.append(language.id);
                    }
                }
            }
            // The languages nobody contributes because the editor is expected
            // to have them already, which is what most extensions wait for.
            const QString known = builtinLanguageId(document->filePath());
            if (!known.isEmpty() && !ids.contains(known))
                ids.append(known);
        }
        return ids;
    }

    QList<VscodeManifest> extensionsToActivate() const
    {
        const QStringList languages = openLanguageIds();
        const FilePaths folders = workspaceFolderPaths();
        return Utils::filtered(enabledExtensions(), [&](const VscodeManifest &manifest) {
            return activationEventFired(manifest, languages, folders,
                                        m_pendingCommandActivations, m_pendingKindActivations);
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
        // Without a project, extensions that work per workspace folder still
        // need one; the checkout the open file belongs to is what a user would
        // have opened.
        if (folders.isEmpty()) {
            if (IDocument *document = EditorManager::currentDocument()) {
                const FilePath dir = workspaceFolderFor(document->filePath().parentDir());
                if (!dir.isEmpty() && host->isOnHostDevice(dir))
                    folders.append(QJsonObject{{"path", host->toHostPath(dir)},
                                               {"name", dir.fileName()}});
            }
        }
        return folders;
    }

    // What has been written over an extension's own defaults, and only that:
    // the defaults reach the host with the manifest, and an extension asks
    // which of the two it is looking at.
    QJsonObject buildConfiguration(const QList<VscodeManifest> &manifests) const
    {
        QSet<QString> declared;
        for (const VscodeManifest &manifest : manifests) {
            const QJsonObject defaults = manifest.configurationDefaults;
            for (auto it = defaults.begin(); it != defaults.end(); ++it)
                declared.insert(it.key());
        }

        QJsonObject config;
        const FilePaths overrides = {settings().extensionsDir() / "settings.json",
                                     ExtensionHost::writtenConfigurationFile()};
        for (const FilePath &overrideFile : overrides) {
            if (const Result<QByteArray> contents = overrideFile.fileContents()) {
                const QJsonObject user = QJsonDocument::fromJson(*contents).object();
                for (auto it = user.begin(); it != user.end(); ++it) {
                    // A stale key from an extension that is gone would be a
                    // value nothing declares, which nothing can read either.
                    if (declared.contains(it.key()))
                        config.insert(it.key(), it.value());
                }
            }
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

    friend ExtensionHost *runningExtensionHost();

    QList<QPointer<AlienClient>> m_clients;
    QPointer<ExtensionHost> m_host;
    QPointer<QLabel> m_statusMessage;
    QHash<QString, StatusBarItem *> m_statusItems;
    QList<VscodeManifest> m_activatable;
    QSet<QString> m_activeIds; // extensions currently running in the host
    bool m_activationTriggersConnected = false;
    QSet<QString> m_pendingCommandActivations; // asked for by name, not yet running
    QSet<QString> m_pendingKindActivations; // a debug or task type asked for
    QHash<Id, QAction *> m_terminalProfileActions;
    QHash<QString, QInputDialog *> m_livePicks;
    QHash<Id, QAction *> m_keyActions; // one per contributed shortcut
    QPointer<AlienOutputPane> m_outputPane;
    QPointer<AlienTerminalPane> m_terminalPane;
    std::unique_ptr<Core::IOptionsPage> m_settingsPage;
    std::unique_ptr<AutoWebviewRenderer> m_webviewRenderer;
    std::unique_ptr<AlienLocatorFilter> m_locatorFilter;
    std::unique_ptr<AlienSymbolLocatorFilter> m_symbolFilter;
    std::unique_ptr<AlienTaskLocatorFilter> m_taskFilter;
};

ExtensionHost *runningExtensionHost()
{
    return thePlugin ? thePlugin->m_host.data() : nullptr;
}

} // namespace Alien::Internal

#include "alienplugin.moc"
