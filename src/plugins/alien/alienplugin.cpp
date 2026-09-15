// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alienplugin.h"

#include "extensionhost.h"
#include "extensionregistry.h"
#include "alienclient.h"
#include "alienconstants.h"
#include "aliensettings.h"
#include "alientr.h"
#include "alienlocatorfilter.h"
#include "alientreeview.h"
#include "alienoutline.h"
#include "alienpanes.h"
#include "alientests.h"
#include "autowebviewrenderer.h"
#ifdef ALIEN_WITH_WEBENGINE
#include "webenginewebviewrenderer.h"
#endif
#include "aliencompletion.h"
#include "alienhover.h"
#include "alienmcp.h"
#include "codicons.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/find/searchresultwindow.h>
#include <coreplugin/secretaspect.h>
#include <coreplugin/idocument.h>
#include <coreplugin/icore.h>
#include <coreplugin/progressmanager/progressmanager.h>
#include <coreplugin/ioutputpane.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/navigationwidget.h>
#include <coreplugin/statusbarmanager.h>
#include <coreplugin/vcsmanager.h>

#include <mcp/server/toolregistry.h>

#include <extensionsystem/iplugin.h>

#include <languageclient/languageclientmanager.h>

#include <projectexplorer/kitmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/taskhub.h>

#include <texteditor/texteditor.h>

#include <utils/async.h>
#include <utils/fileutils.h>
#include <utils/futuresynchronizer.h>
#include <utils/qtcprocess.h>

#include <QMainWindow>
#include <QDockWidget>
#include <QGuiApplication>
#include <utils/infobar.h>
#include <utils/layoutbuilder.h>
#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/temporarydirectory.h>
#include <utils/unarchiver.h>

#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>

#include <functional>
#include <memory>

#ifdef WITH_TESTS
#include "alien_test.h"
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

// The shortcut an extension asks for, in Qt's spelling. VS Code writes
// modifiers in lower case and separates the two halves of a chord with a space,
// where QKeySequence wants a comma. On macOS, "cmd" is Qt's "Ctrl" and "ctrl"
// is Qt's "Meta".
QKeySequence keySequenceOf(const VscodeKeybinding &binding)
{
    QString key = binding.key;
    if (HostOsInfo::isMacHost() && !binding.macKey.isEmpty())
        key = binding.macKey;
    else if (HostOsInfo::isWindowsHost() && !binding.windowsKey.isEmpty())
        key = binding.windowsKey;
    else if (HostOsInfo::isLinuxHost() && !binding.linuxKey.isEmpty())
        key = binding.linuxKey;
    if (key.isEmpty())
        return {};

    // QKeySequence takes up to four, and reads "unknown" as nothing at all.
    // Each chord is parsed on its own: a comma is both a key an extension can
    // ask for and the separator QKeySequence splits chords on, and the joined
    // string is where the two collide.
    QList<QKeyCombination> chords;
    for (const QString &chord : key.split(' ', Qt::SkipEmptyParts)) {
        QStringList parts;
        for (const QString &part : chord.split('+', Qt::SkipEmptyParts)) {
            const QString lower = part.toLower();
            if (lower == "cmd" || lower == "meta" || lower == "win")
                parts << (HostOsInfo::isMacHost() ? "Ctrl" : "Meta");
            else if (lower == "ctrl" && HostOsInfo::isMacHost())
                parts << "Meta";
            else if (lower == "ctrl" || lower == "shift" || lower == "alt")
                parts << lower.at(0).toUpper() + lower.mid(1);
            else
                parts << part; // a key name QKeySequence knows: "F7", "O", "Escape"
        }
        const QKeySequence single(parts.join('+'));
        if (single.count() != 1)
            return {};
        chords << single[0];
        if (chords.size() == 4)
            break;
    }
    if (chords.isEmpty())
        return {};
    chords.resize(4, QKeyCombination::fromCombined(0));
    const QKeySequence sequence(chords[0], chords[1], chords[2], chords[3]);
    return sequence.toString().isEmpty() ? QKeySequence() : sequence;
}

// Whether one of the events an extension waits for has happened. An extension
// that names none, or asks for "*", starts with the host; the rest wait, which
// is the difference between a handful of extensions running and all of them.
// An event naming something we do not offer - a debug adapter, a task type -
// cannot fire, so an extension that names only those keeps waiting.
// A glob in "workspaceContains:" walks the folder. Given workspaceContains,
// the walk is the caller's: a question not answered there yet is entered
// without an answer, and counts as not fired until one is filled in.
static bool folderContains(const FilePath &folder, const QString &pattern,
                           const std::function<bool()> &canceled = {})
{
    // The whole pattern decides, not just its last segment: "*/pom.xml" is
    // one directory down, not any depth.
    bool found = false;
    const QRegularExpression expression = globExpression(pattern);
    // A name filter knows no "{a,b}", and would take the braces literally.
    const QString name = pattern.section('/', -1);
    const QStringList nameFilters = name.contains('{') ? QStringList() : QStringList{name};
    folder.iterateDirectory(
        [&](const FilePath &item) {
            if (canceled && canceled())
                return IterationPolicy::Stop;
            if (!expression.match(item.relativePathFromDir(folder)).hasMatch())
                return IterationPolicy::Continue;
            found = true;
            return IterationPolicy::Stop;
        },
        FileFilter(nameFilters, DirFilterFlag::Files,
                   DirIteratorFlag::Subdirectories));
    return found;
}

bool activationEventFired(const VscodeManifest &manifest,
                          const QStringList &openLanguageIds,
                          const FilePaths &workspaceFolders,
                          const QSet<QString> &commandRequested,
                          const QSet<QString> &kindRequested,
                          WorkspaceContainsCache *workspaceContains)
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
                if (workspaceContains) {
                    const std::pair<FilePath, QString> key{folder, argument};
                    if (!workspaceContains->contains(key))
                        workspaceContains->insert(key, std::nullopt);
                    if (workspaceContains->value(key).value_or(false))
                        return true;
                    continue;
                }
                if (folderContains(folder, argument))
                    return true;
            }
        }
    }
    return false;
}

// Where an extension is installed is named by its package.json, so each part
// must stay a single plain path component.
Result<QString> installFolderName(const VscodeManifest &manifest)
{
    static const QRegularExpression plain("^[A-Za-z0-9][A-Za-z0-9._+-]*$");
    for (const QString &part : {manifest.publisher, manifest.name, manifest.version}) {
        if (!part.isEmpty() && (!plain.match(part).hasMatch() || part.contains("..")))
            return ResultError(Tr::tr("\"%1\" is not a valid identifier.").arg(part));
    }
    return manifest.qualifiedId()
           + (manifest.version.isEmpty() ? QString() : '-' + manifest.version);
}

// Said where the user asked from, because a command that quietly does nothing
// is indistinguishable from one that did its work silently.
void reportCommandNotRun(const QString &id, const QString &reason)
{
    MessageManager::writeFlashing(
        Tr::tr("The command \"%1\" did not run: %2").arg(id, reason));
}

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

#ifdef WITH_TESTS
        ActionBuilder runTest(this, Constants::RUN_TEST_EXTENSION_ACTION_ID);
        runTest.setText(Tr::tr("Run Alien Test Extension"));
        runTest.addOnTriggered(this, [this] {
            if (const Result<> result = host()->activateBundledTestExtension(); !result)
                MessageManager::writeFlashing(result.error());
        });
#endif

        ActionBuilder runTests(this, Constants::RUN_EXTENSION_TESTS_ACTION_ID);
        runTests.setText(Tr::tr("Run VSIX Extension Tests"));
        runTests.addOnTriggered(this, [this] {
            host();
            if (const Result<> result = m_testAdapter->runAll(false); !result)
                MessageManager::writeFlashing(result.error());
        });

        m_locatorFilter = std::make_unique<AlienLocatorFilter>(
            [this] { return extensionCommands(); },
            [this](const QString &id) { runExtensionCommand(id); });

        m_symbolFilter = std::make_unique<AlienSymbolLocatorFilter>(
            [this](const QString &query,
                   const std::function<void(const QList<AlienSymbol> &)> &report) {
                if (!m_host || !m_host->editor().hasWorkspaceSymbols()) {
                    report({});
                    return;
                }
                const auto collect = [report, host = m_host](const QJsonArray &found) {
                    QList<AlienSymbol> symbols;
                    for (const QJsonValue &value : found) {
                        const QJsonObject object = value.toObject();
                        const QJsonObject start = object.value("range").toObject()
                                                      .value("start").toObject();
                        AlienSymbol symbol;
                        symbol.name = object.value("name").toString();
                        symbol.container = object.value("container").toString();
                        const QString path = object.value("path").toString();
                        symbol.filePath = host ? host->fromHostPath(path) : FilePath();
                        symbol.line = start.value("line").toInt();
                        symbol.character = start.value("character").toInt();
                        symbols.append(symbol);
                    }
                    report(symbols);
                };
                m_host->editor().requestWorkspaceSymbols(query, collect);
            });

        m_taskFilter = std::make_unique<AlienTaskLocatorFilter>(
            [this](const std::function<void(const QList<AlienTask> &)> &report) {
                if (!m_host) {
                    report({});
                    return;
                }
                m_host->run().requestTasks([report](const QJsonArray &found) {
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
                    m_host->run().runTask(id);
            });

        ActionBuilder execute(this, Constants::EXECUTE_COMMAND_ACTION_ID);
        execute.setText(Tr::tr("Execute Alien Extension Command..."));
        execute.addOnTriggered(this, [this] { executeCommand(); });

        ActionBuilder install(this, Constants::INSTALL_VSIX_ACTION_ID);
        install.setText(Tr::tr("Install Extension (.vsix)..."));
        install.addOnTriggered(this, [this] { installVsix(); });

#ifdef WITH_TESTS
        addTestCreator(createAlienHostTest);
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
        for (auto it = m_terminalProfileActions.cbegin(); it != m_terminalProfileActions.cend();
             ++it) {
            ActionManager::unregisterAction(it.value(), it.key());
            delete it.value();
        }
        m_terminalProfileActions.clear();

        ProjectExplorer::TaskHub::removeCategory(Constants::TASK_CATEGORY_DIAGNOSTICS);

        // Stopped here rather than in the destructor, the host has the time
        // to deactivate the extensions, and is waited for below.
        const QPointer<QObject> ending = m_host ? m_host->shutdown() : nullptr;

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
        for (const QPointer<AlienClient> &client : m_clients + m_stoppingClients)
            keep(client);
        // The host starts clients of its own, for extensions that bring a
        // language server; those have to be waited for just the same.
        if (m_host) {
            for (const QPointer<AlienClient> &client : m_host->languageClients())
                keep(client);
        }
        if (alive.isEmpty() && !ending)
            return SynchronousShutdown;

        const auto remaining = std::make_shared<int>(alive.size() + (ending ? 1 : 0));
        const auto gone = [this, remaining] {
            if (--*remaining == 0)
                emit asynchronousShutdownFinished();
        };
        if (ending)
            connect(ending, &QObject::destroyed, this, gone);
        for (const QPointer<AlienClient> &client : std::as_const(alive)) {
            connect(client, &QObject::destroyed, this, gone);
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
                    [this](int id, MessageLevel level, const QString &message,
                           const QStringList &items, const QString &detail, bool modal) {
                        auto box = new QMessageBox(ICore::dialogParent());
                        box->setAttribute(Qt::WA_DeleteOnClose);
                        switch (level) {
                        case MessageLevel::Info: box->setIcon(QMessageBox::Information); break;
                        case MessageLevel::Warning: box->setIcon(QMessageBox::Warning); break;
                        case MessageLevel::Error: box->setIcon(QMessageBox::Critical); break;
                        }
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
                                m_host->window().resolveMessageQuestion(
                                    id, int(buttons.indexOf(box->clickedButton())));
                        });
                        box->show();
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
                                    [this, pickId, dialog] {
                                        // Shown again, the entry is the new dialog's.
                                        if (m_livePicks.value(pickId) == dialog)
                                            m_livePicks.remove(pickId);
                                    });
                        }
                        dialog->setLabelText(placeholder.isEmpty() ? Tr::tr("Select an item:")
                                                                   : placeholder);
                        dialog->setComboBoxItems(shown);
                        dialog->setComboBoxEditable(false);
                        // The list may have been replaced since, the index is
                        // into the one on screen. The row, not its text: two
                        // entries can read the same once the icons are gone.
                        connect(dialog, &QDialog::finished, this, [this, id, dialog](int result) {
                            const auto combo = dialog->findChild<QComboBox *>();
                            const int index = result == QDialog::Accepted && combo
                                                  ? combo->currentIndex()
                                                  : -1;
                            if (QTC_GUARD(m_host))
                                m_host->window().resolveQuickPick(id, {index});
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
                                        m_host->window().resolveInputBox(id, dialog->textValue(),
                                                                result == QDialog::Accepted);
                                });
                        dialog->open();
                    });

            connect(m_host, &ExtensionHost::activationFailed, this,
                    [this](const QString &id) {
                        m_activeIds.remove(id);
                        m_failedIds.insert(id);
                    });
            connect(m_host, &ExtensionHost::activated, this,
                    [this](const QString &id) {
                        if (m_activeIds.contains(id)) // not deactivated meanwhile
                            m_activatedIds.insert(id);
                    });

            connect(m_host, &ExtensionHost::stopped, this, [this] {
                // Nothing runs in a host that is gone, so the next activation
                // trigger starts a fresh one and puts the extensions back.
                m_activeIds.clear();
                m_activatedIds.clear();
                removeStatusBarWidgets();
                MessageManager::writeFlashing(
                    Tr::tr("The extension host stopped. It restarts with the next "
                           "activation, or on \"Rescan VSIX Extensions\"."));
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
                        enum { AlignmentRight = 2 }; // vscode.StatusBarAlignment.Right
                        const int alignment = entry.value("alignment").toInt();
                        StatusBarItem *item = m_statusItems.value(id);
                        if (!item) {
                            item = new StatusBarItem;
                            item->setObjectName("alienStatusItem." + id);
                            StatusBarManager::addStatusBarWidget(
                                item, alignment == AlignmentRight ? StatusBarManager::RightCorner
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
            m_testAdapter = std::make_unique<AlienTestAdapter>(m_host);

            m_webviewRenderer = std::make_unique<AutoWebviewRenderer>();
            m_host->window().setWebviewRenderer(m_webviewRenderer.get());

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

        const auto temp = std::make_shared<Utils::TemporaryDirectory>("alien-vsix");
        auto unarchiver = new Unarchiver;
        unarchiver->setParent(this);
        unarchiver->setArchive(vsix);
        unarchiver->setDestination(temp->path());
        connect(unarchiver, &Unarchiver::done, this, [this, unarchiver, vsix, temp] {
            unarchiver->deleteLater();
            if (const Result<> r = unarchiver->result(); !r) {
                MessageManager::writeFlashing(
                    Tr::tr("Cannot unpack \"%1\": %2").arg(vsix.toUserOutput(), r.error()));
                return;
            }
            installExtracted(vsix, temp->path() / "extension");
        });
        unarchiver->start();
    }

    void installExtracted(const FilePath &vsix, const FilePath &extracted)
    {
        const Result<VscodeManifest> manifest
            = VscodeManifest::fromPackageJson(extracted / "package.json");
        if (!manifest) {
            MessageManager::writeFlashing(
                Tr::tr("\"%1\" is not a valid extension: %2")
                    .arg(vsix.toUserOutput(), manifest.error()));
            return;
        }

        const Result<QString> folderName = installFolderName(*manifest);
        if (!folderName) {
            MessageManager::writeFlashing(
                Tr::tr("\"%1\" is not a valid extension: %2")
                    .arg(vsix.toUserOutput(), folderName.error()));
            return;
        }
        const FilePath target = settings().extensionsDir() / *folderName;
        if (!QTC_GUARD(target.parentDir() == settings().extensionsDir()))
            return;
        if (const Result<> r = target.parentDir().ensureWritableDir(); !r) {
            MessageManager::writeFlashing(Tr::tr("Cannot install to \"%1\": %2")
                                              .arg(target.toUserOutput(), r.error()));
            return;
        }
        if (target.exists()) {
            if (const Result<> r = target.removeRecursively(); !r) {
                MessageManager::writeFlashing(Tr::tr("Cannot install to \"%1\": %2")
                                                  .arg(target.toUserOutput(), r.error()));
                return;
            }
        }
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
            InfoBarEntry::ButtonAction::Hide);
        infoBar->addInfo(info);
    }

    void reload()
    {
        // Still waited for on shutdown, until they are gone.
        m_stoppingClients.removeIf([](const QPointer<AlienClient> &client) { return !client; });
        for (const QPointer<AlienClient> &client : std::exchange(m_clients, {})) {
            if (client) {
                LanguageClient::LanguageClientManager::shutdownClient(client);
                m_stoppingClients.append(client);
            }
        }

        // The host that was shut down for it runs none of the extensions.
        if (m_host && m_host->setNodePath(settings().nodeJsPath())) {
            m_activeIds.clear();
            m_activatedIds.clear();
            removeStatusBarWidgets();
        }
        // Also what stopping the old host failed.
        m_failedIds.clear();

        if (!settings().enable()) {
            stopAll();
            return;
        }

        // Turning the support on is not yet consent to run someone else's code.
        if (!settings().legalNoticeAccepted()) {
            stopAll();
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
            stopAll();
            return;
        }

        QStringList errors;
        const QList<VscodeManifest> manifests
            = ExtensionRegistry::scan(settings().extensionsDir(), &errors);

        for (const QString &error : errors)
            MessageManager::writeSilently(Tr::tr("VSIX extension: %1").arg(error));

        QSet<QString> servedDirectly;
        for (const VscodeManifest &manifest : manifests) {
            MessageManager::writeSilently(
                Tr::tr("Discovered extension \"%1\" (%2 languages, %3 commands)%4.")
                    .arg(manifest.qualifiedId())
                    .arg(manifest.languages.size())
                    .arg(manifest.commands.size())
                    .arg(manifest.hasDebuggers ? Tr::tr(", debugger") : QString()));

            // Nothing of an extension runs before the user enabled it.
            if (!settings().enabledExtensions.isEnabled(manifest.qualifiedId()))
                continue;
            if (const std::optional<CommandLine> command = resolveServerCommand(manifest)) {
                m_clients << new AlienClient(manifest, *command);
                servedDirectly.insert(manifest.qualifiedId());
            }
        }

        // Activate extensions with a JS entry point in the host. Defer until a
        // workspace folder exists (a project or an open document): extensions
        // like qt-qml only start their language server for a folder, so they
        // must see one at activation time. One already run as a language
        // server would run twice.
        m_activatable = Utils::filtered(manifests, [&servedDirectly](const VscodeManifest &m) {
            return !m.main.isEmpty() && !servedDirectly.contains(m.qualifiedId());
        });

        registerKeybindings();

        if (!m_activationTriggersConnected) {
            m_activationTriggersConnected = true;
            connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
                    this, [this] { syncActivation(); });
            connect(ProjectExplorer::ProjectManager::instance(),
                    &ProjectExplorer::ProjectManager::projectAdded, this,
                    [this] {
                        forgetWorkspaceContains();
                        syncActivation();
                    });
        }
        syncActivation();
    }

    // Reconciles the set of extensions running in the host with the desired set
    // (enabled and activatable), once a workspace folder is available. Newly
    // disabled extensions are deactivated, newly enabled ones are activated.
    void syncActivation()
    {
        if (!settings().enable() || !settings().legalNoticeAccepted())
            return;
        const QJsonArray folders = workspaceFolders(host());
        // Waiting for a project or a document to name a folder, since an
        // extension like qt-qml only starts its server for one - unless a
        // command of it was asked for by name, which has to run either way.
        if (folders.isEmpty() && m_pendingCommandActivations.isEmpty())
            return;

        const QList<VscodeManifest> toActivate = extensionsToActivate();
        searchWorkspaceContains();
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
                m_activatedIds.remove(id);
            }
        }

        if (!toActivate.isEmpty()) {
            host()->setConfiguration(buildConfiguration(m_activatable));
            host()->setKnownExtensions(enabledExtensions());
            host()->setWorkspaceFolders(folders);
            activateInDependencyOrder(toActivate, enabledExtensions());
        }
    }

    // Nothing is to run, and nothing is to start again on the next trigger.
    void stopAll()
    {
        deactivateAll();
        m_activatable.clear();
        m_pendingCommandActivations.clear();
        m_pendingKindActivations.clear();
        forgetWorkspaceContains();
        registerKeybindings();
    }

    // The globs extensionsToActivate() could not answer yet: a whole tree,
    // build directories and all, maybe on a device, so not in the GUI thread.
    void searchWorkspaceContains()
    {
        for (auto it = m_workspaceContains.cbegin(); it != m_workspaceContains.cend(); ++it) {
            if (it.value() || m_workspaceSearches.contains(it.key()))
                continue;
            const std::pair<FilePath, QString> key = it.key();
            const QFuture<bool> search = Utils::asyncRun([key](QPromise<bool> &promise) {
                promise.addResult(folderContains(key.first, key.second, [&promise] {
                    return promise.isCanceled();
                }));
            });
            m_workspaceSearches.insert(key, search);
            const int generation = m_workspaceGeneration;
            Utils::futureSynchronizer()->addFuture(
                Utils::onResultReady(search, this, [this, key, generation](bool found) {
                    if (generation != m_workspaceGeneration)
                        return; // the folders changed since
                    m_workspaceSearches.remove(key);
                    m_workspaceContains.insert(key, found);
                    if (found)
                        syncActivation();
                }));
        }
    }

    void forgetWorkspaceContains()
    {
        for (QFuture<bool> &search : m_workspaceSearches)
            search.cancel();
        m_workspaceSearches.clear();
        m_workspaceContains.clear();
        ++m_workspaceGeneration;
    }

    void deactivateAll()
    {
        if (!m_host)
            return;
        for (const QString &id : m_activeIds.values())
            m_host->deactivate(id);
        m_activeIds.clear();
        m_activatedIds.clear();
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

    // A terminal an extension knows how to set up becomes an action of ours, so
    // it can be found, run and given a shortcut like anything else.
    void registerTerminalProfiles()
    {
        QSet<Id> wanted;
        for (const QJsonValue &value : m_host->run().terminalProfiles()) {
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
                    m_host->run().openTerminalProfile(profileId);
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
            QHash<QString, int> seen;
            for (const VscodeKeybinding &binding : manifest.keybindings) {
                const QKeySequence sequence = keySequenceOf(binding);
                if (sequence.isEmpty())
                    continue;
                // A command can be bound more than once, with other keys,
                // clauses or arguments. The first keeps the plain id, which
                // is what a shortcut the user changed is stored under.
                const int count = seen[binding.command]++;
                Id id = Id("Alien.").withSuffix(binding.command);
                if (count > 0)
                    id = id.withSuffix(QString('#' + QString::number(count + 1)));
                wanted.insert(id);
                // An updated extension can bind it differently.
                const QString signature = QString::fromUtf8(
                    QJsonDocument(QJsonArray{sequence.toString(), binding.when, binding.arguments})
                        .toJson(QJsonDocument::Compact));
                if (QAction *existing = m_keyActions.value(id)) {
                    if (existing->data().toString() == signature)
                        continue;
                    removeKeybinding(id);
                }

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
                action->setData(signature);
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

    // Starts an extension that is installed and enabled but has not run yet,
    // and reports once it has - or once it is clear that it will not.
    void wakeExtension(const QString &extensionId,
                       const std::function<void(const Result<> &)> &done)
    {
        if (!m_host) {
            done(ResultError(Tr::tr("The extension host is not running.")));
            return;
        }
        if (m_activatedIds.contains(extensionId)) {
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
        if (m_activeIds.contains(extensionId))
            return; // under way already
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
        // Whatever it was going to register while activating is registered,
        // and nothing would end the wait.
        if (m_activatedIds.contains(owner.qualifiedId())) {
            done(ResultError(Tr::tr("\"%1\" does not offer it.").arg(owner.qualifiedId())));
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
                m_host->window().resolveQuickPick(id, chosen);
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

    // The ones not running yet whose activation event has fired.
    QList<VscodeManifest> extensionsToActivate() const
    {
        const QStringList languages = openLanguageIds();
        const FilePaths folders = workspaceFolderPaths();
        return Utils::filtered(enabledExtensions(), [&](const VscodeManifest &manifest) {
            const QString id = manifest.qualifiedId();
            return !m_activeIds.contains(id)
                   && (!m_failedIds.contains(id) || m_pendingCommandActivations.contains(id))
                   && activationEventFired(manifest, languages, folders,
                                           m_pendingCommandActivations,
                                           m_pendingKindActivations, &m_workspaceContains);
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
            for (const VscodeSetting &setting : manifest.configurationSettings)
                declared.insert(setting.key);
        }

        QStringList errors;
        const QJsonObject overrides = ExtensionHost::configurationOverrides(&errors);
        for (const QString &error : errors)
            MessageManager::writeFlashing(Tr::tr("VSIX extension settings: %1").arg(error));

        QJsonObject config;
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            // A stale key from an extension that is gone would be a value
            // nothing declares, which nothing can read either.
            if (declared.contains(it.key()))
                config.insert(it.key(), it.value());
        }
        return config;
    }

    // A dependency runs only when it is enabled itself, as everything else.
    // One that is not installed at all, such as one built into VS Code, is
    // left out as before.
    void activateInDependencyOrder(const QList<VscodeManifest> &roots,
                                   const QList<VscodeManifest> &enabled)
    {
        QHash<QString, VscodeManifest> byId;
        for (const VscodeManifest &manifest : enabled)
            byId.insert(manifest.qualifiedId(), manifest);
        QSet<QString> visiting;

        const std::function<bool(const VscodeManifest &)> activate =
            [&](const VscodeManifest &manifest) {
                const QString id = manifest.qualifiedId();
                if (m_activeIds.contains(id) || visiting.contains(id))
                    return true;
                visiting.insert(id);
                // The host never hears of it, so whoever waits for it to
                // start is told here.
                const auto fail = [this, &id](const QString &error) {
                    m_failedIds.insert(id);
                    m_pendingCommandActivations.remove(id);
                    emit m_host->activationFailed(id, error);
                    return false;
                };
                for (const QString &dep : manifest.extensionDependencies) {
                    if (byId.contains(dep)) {
                        if (!activate(byId.value(dep)))
                            return fail(Tr::tr("\"%1\" needs \"%2\", which did not start.")
                                            .arg(id, dep));
                    } else if (Utils::anyOf(m_activatable, [&dep](const VscodeManifest &m) {
                                   return m.qualifiedId() == dep;
                               })) {
                        const QString error = Tr::tr("\"%1\" needs \"%2\", which is not "
                                                     "enabled in Preferences > VSIX Extensions.")
                                                  .arg(id, dep);
                        MessageManager::writeFlashing(error);
                        return fail(error);
                    }
                }
                m_activeIds.insert(id);
                m_pendingCommandActivations.remove(id);
                host()->activate(manifest);
                return true;
            };
        for (const VscodeManifest &manifest : roots)
            activate(manifest);
    }

    friend ExtensionHost *runningExtensionHost();

    QList<QPointer<AlienClient>> m_clients;
    QList<QPointer<AlienClient>> m_stoppingClients;
    QPointer<ExtensionHost> m_host;
    QPointer<QLabel> m_statusMessage;
    QHash<QString, StatusBarItem *> m_statusItems;
    QList<VscodeManifest> m_activatable;
    QSet<QString> m_activeIds; // extensions currently running in the host
    QSet<QString> m_activatedIds; // the ones of them that are done activating
    // Tried and failed, not tried again on every editor switch until the
    // settings change or the extensions are rescanned. A command still asks.
    QSet<QString> m_failedIds;
    bool m_activationTriggersConnected = false;
    mutable WorkspaceContainsCache m_workspaceContains;
    QHash<std::pair<FilePath, QString>, QFuture<bool>> m_workspaceSearches;
    int m_workspaceGeneration = 0;
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
    std::unique_ptr<AlienTestAdapter> m_testAdapter;
};

ExtensionHost *runningExtensionHost()
{
    return thePlugin ? thePlugin->m_host.data() : nullptr;
}

} // namespace Alien::Internal

#include "alienplugin.moc"
