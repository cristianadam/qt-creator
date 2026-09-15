// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "hostprotocols.h"
#include "vscodemanifest.h"

#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/link.h>

#include <QRegularExpression>
#include <utils/result.h>

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>

#include <functional>

QT_BEGIN_NAMESPACE
class QJsonValue;
QT_END_NAMESPACE

namespace Core { class SecretAspect; }
namespace Utils { class FileSystemWatcher; }

namespace Alien::Internal {

class AlienClient;
class HostConnection;

// Whether a language contribution claims this file - by extension, by whole
// name, or by one of the globs it lists.
bool languageMatchesFile(const VscodeLanguage &language, const Utils::FilePath &filePath);

// What an extension would call the language of this file, taken from what Qt
// Creator already recognizes it as. Empty when nothing does.
QString builtinLanguageId(const Utils::FilePath &filePath);

// A VS Code glob as a regular expression, matched against a path relative to
// the search root.
QRegularExpression globExpression(const QString &glob);

// Which of window.show*Message() an extension called.
enum class MessageLevel { Info, Warning, Error };

// What a test run reported about one test.
enum class TestState { Enqueued, Started, Passed, Failed, Errored, Skipped };

// The Node.js VS Code extension host. A single shared Node process (see
// host/host.js) loads extensions and serves the "vscode" module API over
// JSON-RPC. This class owns that process, maps inbound API calls onto Qt
// Creator, and lets callers activate extensions and invoke the commands they
// register.
class ExtensionHost final : public QObject
{
    Q_OBJECT

public:
    explicit ExtensionHost(const Utils::FilePath &nodePath, QObject *parent = nullptr);
    ~ExtensionHost() override;

    // Takes the Node.js to run the host with. A running host is shut down for
    // that, and the return value tells whether it was changed.
    bool setNodePath(const Utils::FilePath &nodePath);

    // Ends the host process while the plugin is still whole. Left to the
    // destructor, the process would be killed from inside IPlugin's own
    // teardown, before the extensions could deactivate. Returns the process
    // while it still ends.
    QPointer<QObject> shutdown();

    bool isRunning() const;

    // Activates an on-disk extension in the host.
    void activate(const VscodeManifest &manifest);

    // Deactivates a running extension: runs its deactivate() hook, disposes what
    // it registered, and stops any language servers it started. Takes effect
    // without a restart.
    void deactivate(const QString &id);

#ifdef WITH_TESTS
    // Extracts one of the bundled test extensions, the one in
    // ":/alien/host/<directory>", together with the other bundled files it
    // needs, and activates it. Used to exercise the host without an installed
    // extension.
    Utils::Result<> activateBundledTestExtension(const QString &directory = "testextension",
                                                 const QStringList &alsoNeeded = {});
#endif // WITH_TESTS

    // For tests: whether a directory is being watched for an extension.
    bool watchesDirectory(const Utils::FilePath &directory) const;
    void addFileWatch(const QString &id, const QString &pattern, const QString &base);
    Utils::FileSystemWatcher *fileWatcher();
    void handleWatchedDirectory(const Utils::FilePath &directory);
    Core::SecretAspect *secretFor(const QString &extension, const QString &key);
    // Sets the configuration the host exposes through vscode.workspace
    // .getConfiguration(). Keys are dotted (e.g. "qt-qml.qmlls.customExePath").
    void setConfiguration(const QJsonObject &configuration);

    // Sets the folders exposed as vscode.workspace.workspaceFolders. Each entry
    // is {"path": <fs path>, "name": <display name>}.
    void setWorkspaceFolders(const QJsonArray &folders);

    // Clients this host started for an extension. They belong to
    // LanguageClientManager, so the plugin has to wait for them before its
    // library may go.
    QList<QPointer<AlienClient>> languageClients() const;

    // Paths crossing the protocol are the ones the host itself sees: it runs
    // on the device node lives on, which need not be the local machine, so
    // Qt Creator's own "/__qtc_devices__/..." rendering is meaningless there.
    QString toHostPath(const Utils::FilePath &path) const;
    Utils::FilePath fromHostPath(const QString &path) const;
    bool isOnHostDevice(const Utils::FilePath &path) const;

    QStringList registeredCommands() const { return m_commands; }
    // The subset the command palette may offer, as the manifests allow.
    QStringList paletteCommands() const { return m_paletteCommands; }

    // Starts the extension contributing a command none of the running ones
    // has, and reports whether the command is there afterwards. Set by
    // whoever knows which extensions there are; unset, nothing is woken.
    std::function<void(const QString &command, const std::function<void(bool)> &done)>
        wakeCommandOwner;

    // Starts an extension by id, for when one asks another to. Same contract
    // as wakeCommandOwner.
    std::function<void(const QString &extensionId, const std::function<void(bool)> &done)>
        startExtension;

    // Every enabled extension, so that one can be found before it runs.
    void setKnownExtensions(const QList<VscodeManifest> &manifests);
    // The adapter an extension names in its manifest, which is how most of
    // them declare one. Empty when this type has none.
    QJsonObject manifestAdapterFor(const QString &type) const;

    // The language id a file counts as, from what the manifests declare.
    QString languageIdFor(const Utils::FilePath &filePath) const;

    // Where settings an extension writes itself are kept, and the two ways in:
    // through a running host, which then hears about the change, or straight to
    // the file when nothing is running.
    static Utils::FilePath writtenConfigurationFile();
    static Utils::Result<> writeConfigurationFile(const QJsonObject &values);
    // Everything written over an extension's own defaults: what the user put
    // into the extensions directory by hand, under what an extension wrote.
    static QJsonObject configurationOverrides(QStringList *errors = nullptr);
    Utils::Result<> writeConfiguration(const QJsonObject &values);
    Utils::Result<> removeConfiguration(const QString &key);
    // The same for a setting an extension wrote for one folder only, which
    // belongs to that folder and must not answer for the next project.
    static Utils::FilePath folderConfigurationFile(const Utils::FilePath &folder);
    Utils::Result<> writeFolderConfiguration(const Utils::FilePath &folder,
                                             const QString &key, const QJsonValue &value);
    Utils::Result<> removeFolderConfiguration(const Utils::FilePath &folder,
                                              const QString &key);
    // A clause the command only runs under, which is how a manifest binds a key
    // to one place and not the whole editor.
    void executeCommand(const QString &command, const QJsonArray &arguments = {},
                        const QString &when = {});

    // The LSP client started on behalf of an extension, keyed by the id the
    // host assigned it (see the "languageclient/start" handler).
    AlienClient *languageClient(const QString &id) const;

    // The parts that serve one area of the API each.
    EditorProtocol &editor() { return m_editor; }
    WindowProtocol &window() { return m_window; }
    RunProtocol &run() { return m_run; }

signals:
    void commandsChanged();
    void debugTypeRequested(const QString &type);
    void terminalProfilesChanged();
    void configurationWritten();
    void hostFailed(const QString &message);
    void channelOutput(const QString &channel, const QString &text, bool newLine);
    void channelShowRequested(bool preserveFocus);
    void channelClearRequested();
    void taskStarted(const QString &name, const Utils::CommandLine &command);
    void terminalOpened(const QString &name, const Utils::CommandLine &command);
    // A terminal the extension drives itself: it says what is in it, and takes
    // back what is typed.
    void extensionTerminalOpened(int id, const QString &name);
    void extensionTerminalOutput(int id, const QString &text);
    void extensionTerminalRenamed(int id, const QString &name);
    void extensionTerminalClosed(int id);
    void extensionTerminalShowRequested(int id);
    void stopped();
    void activated(const QString &id);
    void activationFailed(const QString &id, const QString &error);
    void languageClientStarted(const QString &id);
    void messageShown(const QString &text);
    void diagnosticsPublished(const QString &uri, int count);
    // pickId is set for a list the extension can still change; quickPickUpdated
    // then names the same one.
    void quickPickRequested(int id, const QStringList &items, const QString &placeholder,
                            bool canPickMany, const QString &pickId = {});
    void quickPickUpdated(const QString &pickId, const QStringList &items,
                          const QString &placeholder);
    void quickPickHidden(const QString &pickId);
    // An extension asked something and offered these answers.
    void messageQuestionRequested(int id, MessageLevel level, const QString &message,
                                  const QStringList &items, const QString &detail, bool modal);
    void inputBoxRequested(int id, const QString &prompt, const QString &value,
                           const QString &placeholder, bool password);
    void statusBarMessageChanged(const QString &text);
    void statusBarItemChanged(const QString &id, const QJsonObject &item);
    void statusBarItemRemoved(const QString &id);
    void treeViewRegistered(const QString &viewId);
    void treeViewRefreshed(const QString &viewId);
    void treeTitleMenuChanged(const QString &viewId);
    void viewWelcomeChanged(const QString &viewId);
    void webviewCreated(const QString &id, const QString &viewType, const QString &title);
    void webviewHtmlChanged(const QString &id, const QString &html);
    void webviewMessagePosted(const QString &id, const QString &messageJson);
    void webviewDisposed(const QString &id);
    void commentsChanged(const QString &uri);
    void sourceControlsChanged(const QString &id);
    void testsChanged(const QString &controllerId);
    // What a run says about one test, with the message a failure came with and
    // the milliseconds it took, -1 when the extension gave none.
    void testResult(const QString &controllerId, const QString &testId, TestState state,
                    const QString &message, int duration);
    void testRunFinished(const QString &controllerId);
    void testOutput(const QString &controllerId, const QString &output);
    // A part of the API an extension reached for and the host does not have.
    // The extension is empty when it was touched after activation, where the
    // host no longer knows whose code is running.
    void apiUnimplemented(const QString &extension, const QString &member);

private:
    friend class EditorProtocol;
    friend class HostProtocol;
    friend class RunProtocol;
    friend class WindowProtocol;

    Utils::Result<> ensureStarted();
    void installHandlers();
    void sendKnownExtensions();
    void whenReady(const std::function<void()> &action,
                   const std::function<void()> &failed = {});
    void forgetHost();
    void stopClient(AlienClient *client);

    Utils::FilePath m_nodePath;
    HostConnection *m_connection = nullptr;
    // One watch an extension asked for.
    class FileWatch
    {
    public:
        QString id;
        QRegularExpression matcher;
        bool absolute = false; // matched against the whole path, not one below a base
        Utils::FilePaths bases;
        Utils::FilePaths roots;
    };
    // What a file was when its directory was last read, enough to tell a file
    // that changed from one that only sits next to one.
    class FileStamp
    {
    public:
        bool operator==(const FileStamp &other) const = default;
        QDateTime modified;
        qint64 size = 0;
    };
    QJsonArray workspaceFoldersWithSettings() const;
    using DirectoryStamps = QHash<Utils::FilePath, FileStamp>;
    static DirectoryStamps directoryStamps(const Utils::FilePath &directory);
    bool watchMatches(const FileWatch &watch, const Utils::FilePath &file) const;
    void watchFilesIn(const Utils::FilePath &directory);
    QSet<Utils::FilePath> m_rewrittenDirectories; // read again once the writing stops
    QHash<QString, FileWatch> m_watches;
    QHash<Utils::FilePath, DirectoryStamps> m_watchedContents;
    Utils::FileSystemWatcher *m_fileWatcher = nullptr;
    QHash<QString, Core::SecretAspect *> m_secrets;
    Utils::FilePath m_runtimeDir;
    QStringList m_commands;
    QStringList m_paletteCommands;
    struct Deferred
    {
        std::function<void()> run;
        std::function<void()> failed;
    };
    QList<Deferred> m_deferred;
    QHash<QString, QPointer<AlienClient>> m_lspClients;
    QList<QPointer<AlienClient>> m_stoppingClients; // asked to stop, not gone yet

    // What activated extensions claim, by extension id, in activation order.
    QList<std::pair<QString, VscodeLanguage>> m_declaredLanguages;
    QHash<Utils::FilePath, QString> m_languageOverrides;
    QList<VscodeManifest> m_knownExtensions;
    QJsonObject m_configuration;
    QJsonArray m_workspaceFolders;

    // Last, so that they go first: their cleanup still reaches the host.
    EditorProtocol m_editor{*this};
    WindowProtocol m_window{*this};
    RunProtocol m_run{*this};
};

// The host the plugin is running, if any.
ExtensionHost *runningExtensionHost();

} // namespace Alien::Internal
