// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vscodemanifest.h"

#include <debugger/dap/dapstartdata.h>
#include <projectexplorer/task.h>

#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/link.h>

#include <QRegularExpression>
#include <utils/result.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QFutureInterface>
#include <QFutureWatcher>
#include <QStringList>

#include <functional>

QT_BEGIN_NAMESPACE
class QJsonValue;
QT_END_NAMESPACE

namespace Core {
class SecretAspect;
class IDocument;
class IEditor;
class INavigationWidgetFactory;
class SearchResult;
} // namespace Core
namespace Utils { class FileSystemWatcher; }
namespace ProjectExplorer { class Node; class Project; class RunControl; }

namespace TextEditor {
class CompletionAssistProvider;
class IAssistProvider;
class TextDocument;
class TextEditorWidget;
class TextMark;
} // namespace TextEditor

namespace Alien::Internal {

class AlienClient;
class AlienCompletionAssistProvider;
class AlienHoverHandler;
class AlienFormatter;
class HostConnection;
class WebviewRenderer;

// Whether a language contribution claims this file - by extension, by whole
// name, or by one of the globs it lists.
bool languageMatchesFile(const VscodeLanguage &language, const Utils::FilePath &filePath);

// What an extension would call the language of this file, taken from what Qt
// Creator already recognizes it as. Empty when nothing does.
QString builtinLanguageId(const Utils::FilePath &filePath);

// The Node.js VS Code extension host.
//
// A single shared Node process (see host/host.js) loads extensions and serves
// the "vscode" module API over JSON-RPC. This class owns that process, maps
// inbound API calls onto Qt Creator, and lets callers activate extensions and
// invoke the commands they register.
// A VS Code glob as a regular expression, matched against a path relative to
// the search root.
QRegularExpression globExpression(const QString &glob);

class ExtensionHost final : public QObject
{
    Q_OBJECT

public:
    explicit ExtensionHost(const Utils::FilePath &nodePath, QObject *parent = nullptr);
    ~ExtensionHost() override;

    // Ends the host process while the plugin is still whole. Left to the
    // destructor, the process would be killed and waited for from inside
    // IPlugin's own teardown, which is not a place to run an event loop in.
    void shutdown();

    bool isRunning() const;

    // Activates an on-disk extension in the host.
    void activate(const VscodeManifest &manifest);

    // Deactivates a running extension: runs its deactivate() hook, disposes what
    // it registered, and stops any language servers it started. Takes effect
    // without a restart.
    void deactivate(const QString &id);

    // Extracts the bundled test extension to a temporary directory and
    // activates it. Used to exercise the host without an installed extension.
    Utils::Result<> activateBundledTestExtension();

    // Extracts and activates a bundled extension that starts a mock language
    // server through vscode-languageclient, exercising the interception path.
    Utils::Result<> activateBundledLspTestExtension();

    // Extracts and activates a bundled extension that mirrors opened documents
    // through the vscode.workspace API, exercising document sync.
    Utils::Result<> activateBundledDocSyncTestExtension();

    // Extracts and activates a bundled extension that publishes a diagnostic
    // through vscode.languages for opened documents.
    Utils::Result<> activateBundledDiagnosticsTestExtension();

    // Extracts and activates a bundled extension that registers a completion
    // provider through vscode.languages.
    Utils::Result<> activateBundledCompletionTestExtension();

    // Extracts and activates a bundled extension that registers hover and
    // definition providers through vscode.languages.
    Utils::Result<> activateBundledHoverDefinitionTestExtension();

    // Extracts and activates a bundled extension that uses window.showQuickPick.
    Utils::Result<> activateBundledQuickPickTestExtension();

    // Extracts and activates a bundled extension that uses the status bar.
    Utils::Result<> activateBundledStatusBarTestExtension();

    // Extracts and activates a bundled extension that registers a tree view.
    Utils::Result<> activateBundledTreeViewTestExtension();

    // Fetches the children of a tree node (empty id = roots) from the in-host
    // provider. Used by the tree view widget and by tests.
    void requestTreeMenu(const QString &viewId, const QString &id, const QString &kind,
                         const std::function<void(const QJsonArray &)> &callback);
    // What an extension offers at a menu location for one file, as the clauses
    // about that file allow. Used by the project tree and by tests.
    void requestResourceMenu(const QString &location, const Utils::FilePath &path, bool isFolder,
                             const std::function<void(const QJsonArray &)> &callback);
    // The buttons of a view are needed the moment it is built, so they are
    // fetched when the view registers, well before anyone opens it.
    QJsonArray treeTitleMenu(const QString &viewId) const { return m_treeTitleMenus.value(viewId); }
    // The webview panels extensions have open, and running script in one.
    QJsonArray webviewPanels() const { return m_webviewPanels; }
    void runInWebview(const QString &id, const QString &script,
                      const std::function<void(const QJsonValue &, const QString &)> &done);

    // The terminals extensions offer to set up, and opening one.
    QJsonArray terminalProfiles() const { return m_terminalProfiles; }
    // What was typed into a terminal an extension drives, and how big it is.
    void sendTerminalInput(int id, const QString &text);
    void setTerminalDimensions(int id, int columns, int rows);
    void openTerminalProfile(const QString &id);

    // How many debug sessions started for an extension are running.
    int debugSessionCount() const { return int(m_debugSessions.size()); }

    void reportTreeSelection(const QString &viewId, const QStringList &ids);
    void executeTreeItemCommand(const QString &viewId, const QString &id,
                                const QString &command);
    void requestTreeChildren(const QString &viewId, const QString &id,
                             const std::function<void(const QJsonArray &)> &callback);

    // Extracts and activates a bundled extension that creates a webview panel.
    Utils::Result<> activateBundledWebviewTestExtension();

    // The backend that displays webview panels; the core plugin sets a
    // litehtml one. Without a renderer, webview panels degrade to no-ops.
    void setWebviewRenderer(WebviewRenderer *renderer);

    // Delivers a webview -> extension message (used by a JS renderer, or tests).
    void deliverWebviewMessage(const QString &id, const QJsonValue &message);
    // For tests: which languages an extension offered to format.
    QSet<QString> formattingLanguageIds() const { return m_formattingLanguageIds; }
    QSet<QString> codeActionLanguageIds() const { return m_codeActionLanguageIds; }
    void requestCodeActions(const Utils::FilePath &filePath, const QPair<int, int> &start,
                            const QPair<int, int> &end,
                            const std::function<void(const QJsonArray &)> &callback);
    void applyChanges(const QJsonArray &changes);
    QSet<QString> renameLanguageIds() const { return m_renameLanguageIds; }
    QSet<QString> referenceLanguageIds() const { return m_referenceLanguageIds; }
    QSet<QString> symbolLanguageIds() const { return m_symbolLanguageIds; }
    bool providesSymbolsFor(const Utils::FilePath &filePath) const;
    QSet<QString> highlightLanguageIds() const { return m_highlightLanguageIds; }
    void requestHighlights(const Utils::FilePath &filePath, int line, int character,
                           const std::function<void(const QJsonArray &)> &callback);
    void highlightOccurrences(TextEditor::TextEditorWidget *widget);
    QSet<QString> linkLanguageIds() const { return m_linkLanguageIds; }
    QSet<QString> signatureLanguageIds() const { return m_signatureLanguageIds; }
    bool hasWorkspaceSymbols() const { return m_hasWorkspaceSymbols; }
    // For tests: whether a directory is being watched for an extension.
    bool watchesDirectory(const Utils::FilePath &directory) const;
    void addFileWatch(const QString &id, const QString &pattern, const QString &base);
    Utils::FileSystemWatcher *fileWatcher();
    void handleWatchedDirectory(const Utils::FilePath &directory);
    QSet<QString> codeLensLanguageIds() const { return m_codeLensLanguageIds; }
    void refreshCodeLenses(const Utils::FilePath &filePath);
    void requestTasks(const std::function<void(const QJsonArray &)> &callback);
    void runTask(const QString &id);
    void requestWorkspaceSymbols(const QString &query,
                                 const std::function<void(const QJsonArray &)> &callback);
    void requestSignatureHelp(const Utils::FilePath &filePath, int line, int character,
                              const std::function<void(const QJsonObject &)> &callback);
    QSet<QString> signatureTriggers() const { return m_signatureTriggers; }
    TextEditor::CompletionAssistProvider *signatureProvider();
    Core::SecretAspect *secretFor(const QString &extension, const QString &key);
    void requestDocumentLinks(const Utils::FilePath &filePath,
                              const std::function<void(const QJsonArray &)> &callback);
    void followDocumentLink(TextEditor::TextEditorWidget *widget, const QTextCursor &cursor,
                            const Utils::LinkHandler &callback, bool resolveTarget);
    void requestDocumentSymbols(const Utils::FilePath &filePath,
                                const std::function<void(const QJsonArray &)> &callback);
    void requestReferences(const Utils::FilePath &filePath, int line, int character,
                           const std::function<void(const QJsonArray &)> &callback);
    void findUsages(const Utils::FilePath &filePath, int line, int character,
                    const QString &word,
                    const std::function<void(Core::SearchResult *)> &reportSearch = {});
    void requestRenameEdits(const Utils::FilePath &filePath, int line, int character,
                            const QString &newName,
                            const std::function<void(const QJsonArray &)> &callback);
    void askAndRename(const Utils::FilePath &filePath, int line, int character,
                      const QString &oldName);
    TextEditor::IAssistProvider *quickFixProvider();
    QSet<QString> rangeFormattingLanguageIds() const { return m_rangeFormattingLanguageIds; }
    bool formatsRangesFor(const Utils::FilePath &filePath) const;
    void requestRangeFormatting(const Utils::FilePath &filePath, int tabSize, bool insertSpaces,
                                const QPair<int, int> &start, const QPair<int, int> &end,
                                const std::function<void(const QJsonArray &)> &callback);
    void requestFormatting(const Utils::FilePath &filePath, int tabSize, bool insertSpaces,
                           const std::function<void(const QJsonArray &)> &callback);

    // Sets the configuration the host exposes through vscode.workspace
    // .getConfiguration(). Keys are dotted (e.g. "qt-qml.qmlls.customExePath").
    void setConfiguration(const QJsonObject &configuration);

    // Sets the folders exposed as vscode.workspace.workspaceFolders. Each entry
    // is {"path": <fs path>, "name": <display name>}.
    void setWorkspaceFolders(const QJsonArray &folders);

    // Paths crossing the protocol are the ones the host itself sees: it runs
    // on the device node lives on, which need not be the local machine, so
    // Qt Creator's own "/__qtc_devices__/..." rendering is meaningless there.
    // Clients this host started for an extension. They belong to
    // LanguageClientManager, so the plugin has to wait for them before its
    // library may go.
    QList<QPointer<AlienClient>> languageClients() const;

    QString toHostPath(const Utils::FilePath &path) const;
    Utils::FilePath fromHostPath(const QString &path) const;
    bool isOnHostDevice(const Utils::FilePath &path) const;

    QStringList registeredCommands() const { return m_commands; }
    // What a view shows while it has nothing in it, empty when it says nothing.
    QString viewWelcome(const QString &viewId) const
    { return m_viewWelcome.value(viewId); }
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

    // Asks an extension what its debugger for this type is: the configuration
    // its providers fill in, and how its adapter is started, if it names one.
    void requestDebugConfigurations(const QString &type,
                                    const std::function<void(const QJsonArray &)> &callback);
    void requestDebugAdapter(const QString &type, const QJsonObject &configuration,
                             const std::function<void(const QJsonObject &)> &callback);

    // The language id a file counts as, from what the manifests declare.
    QString languageIdFor(const Utils::FilePath &filePath) const;

    // What the extension only works out for the item actually taken - its
    // documentation, and the edits that have to go with it.
    void resolveCompletion(const QString &id,
                           const std::function<void(const QJsonObject &)> &callback);

    // Where settings an extension writes itself are kept, and the two ways in:
    // through a running host, which then hears about the change, or straight to
    // the file when nothing is running.
    static Utils::FilePath writtenConfigurationFile();
    static Utils::Result<> writeConfigurationFile(const QJsonObject &values);
    Utils::Result<> writeConfiguration(const QJsonObject &values);
    Utils::Result<> removeConfiguration(const QString &key);
    // A clause the command only runs under, which is how a manifest binds a key
    // to one place and not the whole editor.
    void executeCommand(const QString &command, const QJsonArray &arguments = {},
                        const QString &when = {});

    // Answers for a pending window.showQuickPick / showInputBox request. The
    // UI lives outside this class (the plugin, or a test); index < 0 or
    // accepted == false means cancelled.
    // One index, or several when the extension asked to pick many.
    void resolveQuickPick(int id, const QList<int> &indexes);
    void resolveMessageQuestion(int id, int index);
    void resolveInputBox(int id, const QString &value, bool accepted);

    // Asks the in-host providers at a position. Used by the assist/hover glue
    // and by tests.
    void requestCompletion(const Utils::FilePath &uri, int line, int character,
                           const std::function<void(const QJsonArray &)> &callback);
    void requestHover(const Utils::FilePath &uri, int line, int character,
                      const std::function<void(const QString &)> &callback);
    QSet<QString> typeDefinitionLanguageIds() const { return m_typeDefinitionLanguageIds; }
    void requestTypeDefinition(const Utils::FilePath &uri, int line, int character,
                               const std::function<void(const QJsonArray &)> &callback);
    void requestDefinition(const Utils::FilePath &uri, int line, int character,
                           const std::function<void(const QJsonArray &)> &callback);

    // The LSP client started on behalf of an extension, keyed by the id the
    // host assigned it (see the "languageclient/start" handler).
    AlienClient *languageClient(const QString &id) const;

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
    void messageQuestionRequested(int id, const QString &level, const QString &message,
                                  const QStringList &items, const QString &detail, bool modal);
    void inputBoxRequested(int id, const QString &prompt, const QString &value,
                           const QString &placeholder, bool password);
    void statusBarMessageChanged(const QString &text);
    void statusBarItemChanged(const QString &id, const QJsonObject &item);
    void statusBarItemRemoved(const QString &id);
    void treeViewRegistered(const QString &viewId);
    void treeViewRefreshed(const QString &viewId);
    void viewWelcomeChanged(const QString &viewId);
    void webviewCreated(const QString &id, const QString &viewType, const QString &title);
    void webviewHtmlChanged(const QString &id, const QString &html);
    void webviewMessagePosted(const QString &id, const QString &messageJson);
    void webviewDisposed(const QString &id);

private:
    Utils::Result<> ensureStarted();
    void installHandlers();
    void whenReady(const std::function<void()> &action);

    // Document sync (Creator -> host vscode.workspace).
    void ensureDocumentSync();
    void onDocumentOpened(Core::IDocument *document);
    void onDocumentClosed(Core::IDocument *document);
    void syncActiveEditor();
    void sendSelection(TextEditor::TextEditorWidget *widget);


    // Diagnostics (host -> Creator editor marks).
    void publishDiagnostics(const QJsonValue &params);

    // Completion (Creator editor -> host providers).
    AlienCompletionAssistProvider *completionProvider();
    void maybeAttachCompletion(TextEditor::TextDocument *document);

    // Hover + go-to-definition (Creator editor -> host providers).
    void ensureEditorFeatures();
    void attachEditorFeatures(Core::IEditor *editor);
    void updateEditorTitleActions(TextEditor::TextEditorWidget *widget);
    void updateEditorContextActions();
    void updateProjectTreeActions(ProjectExplorer::Node *node);
    void refreshColors(const Utils::FilePath &filePath);
    void refreshFolding(const Utils::FilePath &filePath);
    void refreshSemanticTokens(const Utils::FilePath &filePath);
    void refreshInlineCompletion(TextEditor::TextEditorWidget *widget);
    void releaseLanguageFeatures(const QString &languageId);
    void showDiagnosticTags(const Utils::FilePath &filePath);
    void reportEditorFocus();
    void startTerminal(int id, const Utils::CommandLine &command);
    void applyLanguageConfiguration(TextEditor::TextEditorWidget *widget);
    void refreshMarkerFolding(const Utils::FilePath &filePath);
    void requestOnTypeFormatting(TextEditor::TextDocument *document, int line,
                                 int character, const QString &typed);
    AlienHoverHandler *hoverHandler();

    Utils::FilePath m_nodePath;
    HostConnection *m_connection = nullptr;
    QHash<QString, QTextCharFormat> m_decorations;
    QList<QPointer<TextEditor::TextEditorWidget>> m_decoratedWidgets;
    // One watch an extension asked for.
    class FileWatch
    {
    public:
        QString id;
        QRegularExpression matcher;
        bool recursive = false;
        Utils::FilePaths roots;
    };
    QHash<QString, FileWatch> m_watches;
    QHash<Utils::FilePath, QSet<Utils::FilePath>> m_watchedContents;
    Utils::FileSystemWatcher *m_fileWatcher = nullptr;
    QHash<QString, Core::SecretAspect *> m_secrets;
    Utils::FilePath m_runtimeDir;
    QStringList m_commands;
    QStringList m_paletteCommands;
    QHash<int, QPointer<ProjectExplorer::RunControl>> m_runningTasks;
    // A terminal an extension asked for, and what it was told about it.
    class Terminal
    {
    public:
        QString name;
        Utils::FilePath workingDirectory;
        Utils::EnvironmentItems environmentChanges;
        Utils::FilePath shell; // the one the extension asked for, if it did
        QStringList shellArguments;
        bool started = false;
        // Driven by the extension rather than by a process of its own.
        bool pty = false;
    };
    QHash<int, Terminal> m_terminals;
    QHash<ProjectExplorer::Project *, Utils::FilePaths> m_projectFiles;
    int m_nextTerminalId = 1;
    int m_nextTaskId = 1;
    QList<std::function<void()>> m_deferred;
    QHash<QString, QPointer<AlienClient>> m_lspClients;

    bool m_documentSyncStarted = false;
    // Follows the current editor's caret; re-made whenever the editor changes.
    QMetaObject::Connection m_selectionConnection;
    QList<VscodeLanguage> m_declaredLanguages; // what activated extensions claim
    QHash<QString, QString> m_viewWelcome; // view id -> contents
    QSet<int> m_multiPicks; // prompts that answer with a list
    // "explorer/context" entries, one action per command and container.
    QHash<QString, QAction *> m_projectTreeActions;
    QString m_projectTreeResource; // the file those entries were offered for
    QHash<Utils::FilePath, int> m_documentVersions;

    // Keyed by "<collection>\n<uri>"; the marks currently shown for that set.
    QHash<QString, QList<TextEditor::TextMark *>> m_diagnosticMarks;
    QHash<QString, ProjectExplorer::Tasks> m_diagnosticTasks;

    AlienCompletionAssistProvider *m_completionProvider = nullptr;
    QSet<QString> m_completionLanguageIds;
    QList<QPointer<TextEditor::TextDocument>> m_completionDocuments;

    bool m_editorFeaturesStarted = false;
    // The buttons contributed to "editor/title", and the one action per editor
    // each of them has; an action stays once made and is only hidden again, as
    // a toolbar takes actions but does not give them back.
    QList<QPair<QString, QString>> m_editorTitleItems; // command, title
    QHash<TextEditor::TextEditorWidget *, QHash<QString, QAction *>> m_editorTitleActions;
    // The same for "editor/context", where one action serves every editor
    // because the menu is built from a shared container each time it opens.
    QList<QPair<QString, QString>> m_editorContextItems;
    QHash<QString, QAction *> m_editorContextActions;
    AlienHoverHandler *m_hoverHandler = nullptr;
    QSet<QString> m_hoverLanguageIds;
    QSet<QString> m_formattingLanguageIds;
    QSet<QString> m_rangeFormattingLanguageIds;
    QSet<QString> m_codeActionLanguageIds;
    QSet<QString> m_renameLanguageIds;
    QSet<QString> m_referenceLanguageIds;
    QSet<QString> m_symbolLanguageIds;
    QSet<QString> m_highlightLanguageIds;
    QSet<QString> m_linkLanguageIds;
    QSet<QString> m_signatureLanguageIds;
    QSet<QString> m_signatureTriggers;
    bool m_hasWorkspaceSymbols = false;
    TextEditor::CompletionAssistProvider *m_signatureProvider = nullptr;
    QList<QPointer<TextEditor::TextEditorWidget>> m_highlightWidgets;
    TextEditor::IAssistProvider *m_quickFixProvider = nullptr;
    QSet<QString> m_definitionLanguageIds;
    QSet<QString> m_typeDefinitionLanguageIds;
    QSet<QString> m_codeLensLanguageIds;
    QSet<QString> m_colorLanguageIds;
    QSet<QString> m_foldingLanguageIds;
    QSet<QString> m_semanticLanguageIds;
    QSet<QString> m_inlineLanguageIds;
    QSet<QString> m_onTypeLanguageIds;
    QSet<QString> m_onTypeTriggers;
    QSet<Utils::FilePath> m_onTypeDocuments;
    QHash<Utils::FilePath, QString> m_languageOverrides;
    QHash<QPair<QString, Utils::FilePath>, QJsonArray> m_diagnosticTags;
    QSet<Utils::FilePath> m_markerFoldedDocuments;
    QHash<QString, QString> m_tokenTypeAliases;
    QSet<QString> m_statusProgress;
    QSet<QObject *> m_tabSettingsWatched;
    QJsonArray m_terminalProfiles;
    QJsonArray m_webviewPanels;
    QHash<int, ProjectExplorer::RunControl *> m_debugSessions;
    QHash<int, std::shared_ptr<Debugger::Internal::DapSessionChannel>> m_debugChannels;
    int m_nextDebugSessionId = 1;
    QList<VscodeManifest> m_knownExtensions;
    bool m_editorFocused = false;
    bool m_editorReadOnly = false;
    QList<QPointer<TextEditor::TextEditorWidget>> m_inlineWidgets;
    QSet<Utils::FilePath> m_semanticDocuments;
    QSet<Utils::FilePath> m_foldingDocuments;
    QHash<Utils::FilePath, QList<TextEditor::TextMark *>> m_colors;
    QSet<Utils::FilePath> m_colorDocuments;
    QHash<Utils::FilePath, QList<TextEditor::TextMark *>> m_codeLenses;
    QSet<Utils::FilePath> m_codeLensDocuments;
    QList<QPointer<TextEditor::TextEditorWidget>> m_hoverWidgets;

    int m_nextPromptId = 1;
    QHash<int, std::function<void(const QJsonValue &, const QString &)>> m_pendingPrompts;

    QHash<QString, Core::INavigationWidgetFactory *> m_treeFactories;
    QHash<QString, QJsonArray> m_treeTitleMenus;
    QHash<QString, QFutureInterface<void> *> m_progress; // id -> its progress bar
    QHash<QString, QFutureWatcher<void> *> m_progressWatchers; // the cancellable ones

    WebviewRenderer *m_webviewRenderer = nullptr;
    QJsonObject m_configuration;
    QJsonArray m_workspaceFolders;
};

// The host the plugin is running, if any.
ExtensionHost *runningExtensionHost();

} // namespace Alien::Internal
