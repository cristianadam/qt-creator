// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <debugger/dap/dapstartdata.h>
#include <projectexplorer/task.h>

#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/link.h>
#include <utils/result.h>

#include <QFutureInterface>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTextCharFormat>

#include <functional>
#include <optional>

QT_BEGIN_NAMESPACE
class QAction;
class QJsonValue;
class QTextCursor;
QT_END_NAMESPACE

namespace Core {
class IDocument;
class IEditor;
class INavigationWidgetFactory;
class SearchResult;
} // namespace Core
namespace ProjectExplorer { class Node; class Project; class RunControl; }

namespace TextEditor {
class CompletionAssistProvider;
class Formatter;
class IAssistProvider;
class TextDocument;
class TextEditorWidget;
class TextMark;
} // namespace TextEditor

namespace Alien::Internal {

class AlienCompletionAssistProvider;
class AlienHoverHandler;
class ExtensionHost;
class HostConnection;
class WebviewRenderer;

Q_DECLARE_LOGGING_CATEGORY(logHost)

int offsetOf(const QString &text, const QJsonValue &position);
QList<QJsonObject> editsBackToFront(const QString &text, const QJsonArray &edits);
Utils::Result<> applyTextEdits(const Utils::FilePath &filePath, const QJsonArray &edits);

// The parts of ExtensionHost that serve one area of the API each. A part
// registers the handlers for its protocol namespaces on the host's connection,
// keeps the state they build up, and takes it down again when the host goes.
class HostProtocol : public QObject
{
protected:
    explicit HostProtocol(ExtensionHost &host) : m_host(host) {}

    // Null while no host process runs.
    HostConnection *connection() const;

    ExtensionHost &m_host;
};

// What the vscode.window API shows: questions and quick picks, the status bar,
// progress, output channels, tree views and webviews.
class WindowProtocol final : public HostProtocol
{
public:
    explicit WindowProtocol(ExtensionHost &host) : HostProtocol(host) {}
    ~WindowProtocol() final;

    void installHandlers();
    void forgetHost();

    // Fetches the children of a tree node (empty id = roots) from the in-host
    // provider. Used by the tree view widget and by tests.
    void requestTreeMenu(const QString &viewId, const QString &id, const QString &kind,
                         const std::function<void(const QJsonArray &)> &callback);
    // Every button a view has, each saying whether its clause holds right now.
    // The host announces them when the view registers and again whenever a
    // context key moves, so a toolbar follows the state of the extension.
    QJsonArray treeTitleMenu(const QString &viewId) const { return m_treeTitleMenus.value(viewId); }
    // What a view shows while it has nothing in it, empty when it says nothing.
    QString viewWelcome(const QString &viewId) const { return m_viewWelcome.value(viewId); }
    void reportTreeSelection(const QString &viewId, const QStringList &ids);
    void executeTreeItemCommand(const QString &viewId, const QString &id,
                                const QString &command);
    void requestTreeChildren(const QString &viewId, const QString &id,
                             const std::function<void(const QJsonArray &)> &callback);

    // The webview panels extensions have open, and running script in one.
    QJsonArray webviewPanels() const;
    void runInWebview(const QString &id, const QString &script,
                      const std::function<void(const QJsonValue &, const QString &)> &done);
    // The backend that displays webview panels; the core plugin sets a
    // litehtml one. Without a renderer, webview panels degrade to no-ops.
    void setWebviewRenderer(WebviewRenderer *renderer);
    // Delivers a webview -> extension message (used by a JS renderer, or tests).
    void deliverWebviewMessage(const QString &id, const QJsonValue &message);
    void forgetWebviewPanel(const QString &id);

    // Answers for a pending window.showQuickPick / showInputBox request. The
    // UI lives outside this class (the plugin, or a test); index < 0 or
    // accepted == false means cancelled.
    void resolveQuickPick(int id, const QList<int> &indexes);
    void resolveMessageQuestion(int id, int index);
    void resolveInputBox(int id, const QString &value, bool accepted);

private:
    void endAllProgress();

    QHash<QString, QString> m_viewWelcome; // view id -> contents
    QSet<int> m_multiPicks; // prompts that answer with a list
    QSet<QString> m_statusProgress;
    QJsonArray m_webviewPanels;
    int m_nextPromptId = 1;
    QHash<int, std::function<void(const QJsonValue &, const QString &)>> m_pendingPrompts;
    QHash<QString, Core::INavigationWidgetFactory *> m_treeFactories;
    QHash<QString, QJsonArray> m_treeTitleMenus;
    QHash<QString, QFutureInterface<void> *> m_progress; // id -> its progress bar
    QHash<QString, QFutureWatcher<void> *> m_progressWatchers; // the cancellable ones
    QHash<QString, double> m_progressDone; // increments add up, unrounded
    WebviewRenderer *m_webviewRenderer = nullptr;
};

// What runs: terminals, tasks, debug sessions, tests and source control.
class RunProtocol final : public HostProtocol
{
public:
    explicit RunProtocol(ExtensionHost &host) : HostProtocol(host) {}

    void installHandlers();
    void forgetHost();

    // The terminals extensions offer to set up, and opening one.
    QJsonArray terminalProfiles() const { return m_terminalProfiles; }
    // What was typed into a terminal an extension drives, and how big it is.
    void sendTerminalInput(int id, const QString &text);
    void closeTerminal(int id);
    void setTerminalDimensions(int id, int columns, int rows);
    void openTerminalProfile(const QString &id);
    bool hasTerminalLinkProviders() const { return m_hasTerminalLinkProviders; }
    void requestTerminalLinks(const QString &line,
                              const std::function<void(const QJsonArray &)> &callback);
    void handleTerminalLink(const QString &id);

    void requestTasks(const std::function<void(const QJsonArray &)> &callback);
    void runTask(const QString &id);

    // How many debug sessions started for an extension are running.
    int debugSessionCount() const { return int(m_debugSessions.size()); }
    // Asks an extension what its debugger for this type is: the configuration
    // its providers fill in, and how its adapter is started, if it names one.
    void requestDebugConfigurations(const QString &type,
                                    const std::function<void(const QJsonArray &)> &callback);
    void requestDebugAdapter(const QString &type, const QJsonObject &configuration,
                             const std::function<void(const QJsonObject &)> &callback);
    std::shared_ptr<Debugger::Internal::DapSessionChannel> debugChannel(int sessionId) const
    { return m_debugChannels.value(sessionId); }
    void requestFrameVariables(
        int sessionId, int frameId,
        const std::function<void(const QHash<QString, QString> &)> &callback);

    void requestSourceControls(const std::function<void(const QJsonArray &)> &callback);
    void requestTests(const QString &controllerId,
                      const std::function<void(const QJsonArray &)> &callback);
    void runTests(const QString &controllerId, const QStringList &itemIds, bool debug,
                  const std::function<void(bool)> &started = {});

private:
    void startTerminal(int id, const Utils::CommandLine &command);

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
        // Driven by the extension rather than by a process of its own.
        bool pty = false;
    };
    QHash<int, Terminal> m_terminals;
    int m_nextTerminalId = 1;
    int m_nextTaskId = 1;
    QJsonArray m_terminalProfiles;
    QHash<int, QPointer<ProjectExplorer::RunControl>> m_debugSessions;
    QHash<int, std::shared_ptr<Debugger::Internal::DapSessionChannel>> m_debugChannels;
    int m_nextDebugSessionId = 1;
    bool m_hasTerminalLinkProviders = false;
};

// What extensions put into the editor: the documents they see, the language
// features they provide, and what they mark in the text.
class EditorProtocol final : public HostProtocol
{
public:
    explicit EditorProtocol(ExtensionHost &host) : HostProtocol(host) {}
    ~EditorProtocol() final;

    void installHandlers();
    void forgetHost();

    // Document sync (Creator -> host vscode.workspace).
    void ensureDocumentSync();
    void announceDocuments();
    void ensureEditorFeatures();

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
    QSet<QString> codeLensLanguageIds() const { return m_codeLensLanguageIds; }
    void refreshCodeLenses(const Utils::FilePath &filePath);
    // What the extensions say about the lines on screen now that the program
    // stopped in this frame, or nothing at all now that it runs again
    // (frameId below zero). Both counts are the adapter's, from one.
    void showInlineValues(int sessionId, int frameId, const Utils::FilePath &file, int line);
    bool provideToolTipExpression(const Utils::FilePath &file, int line, int column,
                                  const std::function<void(const QString &)> &answer);
    void refreshComments(const Utils::FilePath &filePath);
    void requestComments(const Utils::FilePath &filePath,
                         const std::function<void(const QJsonArray &)> &callback);
    void requestWorkspaceSymbols(const QString &query,
                                 const std::function<void(const QJsonArray &)> &callback);
    void requestSignatureHelp(const Utils::FilePath &filePath, int line, int character,
                              const std::function<void(const QJsonObject &)> &callback);
    QSet<QString> signatureTriggers() const { return m_signatureTriggers; }
    TextEditor::CompletionAssistProvider *signatureProvider();
    void requestDocumentLinks(const Utils::FilePath &filePath,
                              const std::function<void(const QJsonArray &)> &callback);
    void followDocumentLink(TextEditor::TextEditorWidget *widget, const QTextCursor &cursor,
                            const Utils::LinkHandler &callback, bool resolveTarget);
    void followLink(TextEditor::TextEditorWidget *widget, const QTextCursor &cursor,
                    const Utils::LinkHandler &callback, bool resolveTarget);
    void followDefinition(TextEditor::TextEditorWidget *widget, const QTextCursor &cursor,
                          const Utils::LinkHandler &callback);
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
    // What an extension offers at a menu location for one file, as the clauses
    // about that file allow. Used by the project tree and by tests.
    void requestResourceMenu(const QString &location, const Utils::FilePath &path, bool isFolder,
                             const std::function<void(const QJsonArray &)> &callback);

    // What the extension only works out for the item actually taken - its
    // documentation, and the edits that have to go with it.
    void resolveCompletion(const QString &id,
                           const std::function<void(const QJsonObject &)> &callback);

    // Asks the in-host providers at a position. Used by the assist/hover glue
    // and by tests.
    void requestCompletion(const Utils::FilePath &uri, int line, int character,
                           const std::function<void(const QJsonArray &)> &callback);
    void requestHover(const Utils::FilePath &uri, int line, int character,
                      const std::function<void(const QString &)> &callback);
    // What the debugger is to evaluate at a position, empty when no extension
    // claims it. Both counts are the extension's, from zero.
    void requestEvaluatableExpression(const Utils::FilePath &uri, int line, int character,
                                      const std::function<void(const QString &)> &callback);
    // What the extensions have to say about the lines from startLine to
    // endLine while the program stands still in this frame.
    void requestInlineValues(const Utils::FilePath &uri, int startLine, int endLine, int frameId,
                             int stoppedLine,
                             const std::function<void(const QJsonArray &)> &callback);
    QSet<QString> typeDefinitionLanguageIds() const { return m_typeDefinitionLanguageIds; }
    void requestTypeDefinition(const Utils::FilePath &uri, int line, int character,
                               const std::function<void(const QJsonArray &)> &callback);
    void requestDefinition(const Utils::FilePath &uri, int line, int character,
                           const std::function<void(const QJsonArray &)> &callback);
    QSet<QString> declarationLanguageIds() const { return m_declarationLanguageIds; }
    void requestDeclaration(const Utils::FilePath &uri, int line, int character,
                            const std::function<void(const QJsonArray &)> &callback);

private:
    friend class ExtensionHost;
    friend class AlienHostTest;

    void onDocumentOpened(Core::IDocument *document);
    void onDocumentClosed(Core::IDocument *document);
    void sendPendingChange(TextEditor::TextDocument *textDocument, const Utils::FilePath &path);
    void syncActiveEditor();
    void syncOtherEditors();
    void sendSelection(TextEditor::TextEditorWidget *widget);

    // Diagnostics (host -> Creator editor marks).
    void publishDiagnostics(const QJsonValue &params);

    // Completion (Creator editor -> host providers).
    AlienCompletionAssistProvider *completionProvider();
    void maybeAttachCompletion(TextEditor::TextDocument *document);

    // Hover + go-to-definition (Creator editor -> host providers).
    void attachEditorFeatures(Core::IEditor *editor);
    bool firstRequestConnect(TextEditor::TextEditorWidget *widget, const char *request);
    void updateEditorTitleActions(TextEditor::TextEditorWidget *widget);
    void updateEditorContextActions();
    void updateProjectTreeActions(ProjectExplorer::Node *node);
    void refreshColors(const Utils::FilePath &filePath);
    void refreshFolding(const Utils::FilePath &filePath);
    void refreshSemanticTokens(const Utils::FilePath &filePath);
    void refreshInlineCompletion(TextEditor::TextEditorWidget *widget);
    void releaseLanguageFeatures(const QString &languageId);
    void releaseProviders(const std::optional<QString> &languageId);
    void removeWhatWasShown();
    void showDiagnosticTags(const Utils::FilePath &filePath);
    void reportEditorFocus();
    void applyLanguageConfiguration(TextEditor::TextEditorWidget *widget);
    void refreshMarkerFolding(const Utils::FilePath &filePath);
    void requestOnTypeFormatting(TextEditor::TextDocument *document, int line,
                                 int character, const QString &typed);
    AlienHoverHandler *hoverHandler();

    QHash<QString, QTextCharFormat> m_decorations;
    QList<QPointer<TextEditor::TextEditorWidget>> m_decoratedWidgets;
    QHash<ProjectExplorer::Project *, Utils::FilePaths> m_projectFiles;

    bool m_documentSyncStarted = false; // the running host knows the open documents
    bool m_documentSignalsConnected = false;
    QSet<TextEditor::TextDocument *> m_syncedDocuments;
    // Follows the current editor's caret; re-made whenever the editor changes.
    QMetaObject::Connection m_selectionConnection;
    // "explorer/context" entries, one action per command and container.
    QHash<QString, QAction *> m_projectTreeActions;
    QString m_projectTreeResource; // the file those entries were offered for
    int m_projectTreeRequest = 0; // only the latest answer is for the current node
    QHash<Utils::FilePath, int> m_documentVersions;
    // Changes not yet sent, as one range: from start to oldEnd in the text the
    // host last saw, from start to newEnd in the text now.
    struct PendingChange
    {
        int start = 0;
        int oldEnd = 0;
        int newEnd = 0;
    };
    QHash<Utils::FilePath, PendingChange> m_pendingChanges;

    // Keyed by "<collection>\n<uri>"; the marks currently shown for that set.
    QHash<QString, QList<TextEditor::TextMark *>> m_diagnosticMarks;
    QHash<QString, ProjectExplorer::Tasks> m_diagnosticTasks;

    AlienCompletionAssistProvider *m_completionProvider = nullptr;
    QSet<QString> m_completionLanguageIds;
    // The documents ours was set on, and what they had before, which comes
    // back when ours is no longer wanted.
    QList<std::pair<QPointer<TextEditor::TextDocument>,
                    QPointer<TextEditor::CompletionAssistProvider>>> m_completionDocuments;
    QList<std::pair<QPointer<TextEditor::TextDocument>,
                    QPointer<TextEditor::IAssistProvider>>> m_quickFixDocuments;
    QList<std::pair<QPointer<TextEditor::TextDocument>, TextEditor::Formatter *>>
        m_formattedDocuments;

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
    // Where the signature provider is set.
    QList<QPointer<TextEditor::TextDocument>> m_assistDocuments;
    QSet<QString> m_definitionLanguageIds;
    QSet<QString> m_typeDefinitionLanguageIds;
    QSet<QString> m_declarationLanguageIds;
    QSet<QString> m_codeLensLanguageIds;
    QSet<QString> m_colorLanguageIds;
    QSet<QString> m_foldingLanguageIds;
    QSet<QString> m_semanticLanguageIds;
    QSet<QString> m_inlineLanguageIds;
    QSet<QString> m_onTypeLanguageIds;
    QSet<QString> m_onTypeTriggers;
    QSet<TextEditor::TextDocument *> m_onTypeDocuments;
    QHash<QPair<QString, Utils::FilePath>, QJsonArray> m_diagnosticTags;
    QSet<TextEditor::TextDocument *> m_markerFoldedDocuments;
    QHash<QString, QString> m_tokenTypeAliases;
    QSet<QObject *> m_tabSettingsWatched;
    bool m_editorFocused = false;
    bool m_editorReadOnly = false;
    QList<QPointer<TextEditor::TextEditorWidget>> m_inlineWidgets;
    QSet<TextEditor::TextDocument *> m_semanticDocuments;
    QSet<TextEditor::TextDocument *> m_foldingDocuments;
    QHash<Utils::FilePath, QList<TextEditor::TextMark *>> m_colors;
    QSet<TextEditor::TextDocument *> m_colorDocuments;
    QHash<Utils::FilePath, QList<TextEditor::TextMark *>> m_codeLenses;
    QSet<TextEditor::TextDocument *> m_codeLensDocuments;
    QHash<Utils::FilePath, QList<TextEditor::TextMark *>> m_comments;
    QSet<QString> m_evaluatableExpressionLanguageIds;
    QSet<QString> m_inlineValueLanguageIds;
    // What the current stop is annotated with. There is one stop at a time,
    // and the annotations go away with it.
    QHash<Utils::FilePath, QList<TextEditor::TextMark *>> m_inlineValues;
    int m_inlineValueStop = 0; // which stop the values are for
    bool m_hasCommentControllers = false;
    QList<QPointer<TextEditor::TextEditorWidget>> m_hoverWidgets;
    QHash<TextEditor::TextEditorWidget *, QSet<QByteArray>> m_connectedRequests;
};

} // namespace Alien::Internal
