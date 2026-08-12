// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vscodemanifest.h"

#include <utils/filepath.h>
#include <utils/result.h>

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

namespace Core { class IDocument; class IEditor; class INavigationWidgetFactory; }
namespace TextEditor { class TextDocument; class TextEditorWidget; class TextMark; }

namespace Alien::Internal {

class AlienClient;
class AlienCompletionAssistProvider;
class AlienHoverHandler;
class HostConnection;
class WebviewRenderer;

// The Node.js VS Code extension host.
//
// A single shared Node process (see host/host.js) loads extensions and serves
// the "vscode" module API over JSON-RPC. This class owns that process, maps
// inbound API calls onto Qt Creator, and lets callers activate extensions and
// invoke the commands they register.
class ExtensionHost final : public QObject
{
    Q_OBJECT

public:
    explicit ExtensionHost(const Utils::FilePath &nodePath, QObject *parent = nullptr);
    ~ExtensionHost() override;

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
    void requestTreeChildren(const QString &viewId, const QString &id,
                             const std::function<void(const QJsonArray &)> &callback);

    // Extracts and activates a bundled extension that creates a webview panel.
    Utils::Result<> activateBundledWebviewTestExtension();

    // The backend that displays webview panels; the core plugin sets a
    // litehtml one. Without a renderer, webview panels degrade to no-ops.
    void setWebviewRenderer(WebviewRenderer *renderer);

    // Delivers a webview -> extension message (used by a JS renderer, or tests).
    void deliverWebviewMessage(const QString &id, const QJsonValue &message);

    // Sets the configuration the host exposes through vscode.workspace
    // .getConfiguration(). Keys are dotted (e.g. "qt-qml.qmlls.customExePath").
    void setConfiguration(const QJsonObject &configuration);

    // Sets the folders exposed as vscode.workspace.workspaceFolders. Each entry
    // is {"path": <fs path>, "name": <display name>}.
    void setWorkspaceFolders(const QJsonArray &folders);

    // Paths crossing the protocol are the ones the host itself sees: it runs
    // on the device node lives on, which need not be the local machine, so
    // Qt Creator's own "/__qtc_devices__/..." rendering is meaningless there.
    QString toHostPath(const Utils::FilePath &path) const;
    Utils::FilePath fromHostPath(const QString &path) const;
    bool isOnHostDevice(const Utils::FilePath &path) const;

    QStringList registeredCommands() const { return m_commands; }
    void executeCommand(const QString &command, const QJsonArray &arguments = {});

    // Answers for a pending window.showQuickPick / showInputBox request. The
    // UI lives outside this class (the plugin, or a test); index < 0 or
    // accepted == false means cancelled.
    void resolveQuickPick(int id, int index);
    void resolveInputBox(int id, const QString &value, bool accepted);

    // Asks the in-host providers at a position. Used by the assist/hover glue
    // and by tests.
    void requestCompletion(const Utils::FilePath &uri, int line, int character,
                           const std::function<void(const QJsonArray &)> &callback);
    void requestHover(const Utils::FilePath &uri, int line, int character,
                      const std::function<void(const QString &)> &callback);
    void requestDefinition(const Utils::FilePath &uri, int line, int character,
                           const std::function<void(const QJsonArray &)> &callback);

    // The LSP client started on behalf of an extension, keyed by the id the
    // host assigned it (see the "languageclient/start" handler).
    AlienClient *languageClient(const QString &id) const;

signals:
    void commandsChanged();
    void stopped();
    void activationFailed(const QString &id, const QString &error);
    void languageClientStarted(const QString &id);
    void messageShown(const QString &text);
    void diagnosticsPublished(const QString &uri, int count);
    void quickPickRequested(int id, const QStringList &items, const QString &placeholder);
    void inputBoxRequested(int id, const QString &prompt, const QString &value,
                           const QString &placeholder);
    void statusBarMessageChanged(const QString &text);
    void statusBarItemChanged(const QString &id, const QString &text, const QString &tooltip,
                              int alignment, bool visible);
    void statusBarItemRemoved(const QString &id);
    void treeViewRegistered(const QString &viewId);
    void treeViewRefreshed(const QString &viewId);
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
    QString languageIdFor(const Utils::FilePath &filePath) const;

    // Diagnostics (host -> Creator editor marks).
    void publishDiagnostics(const QJsonValue &params);

    // Completion (Creator editor -> host providers).
    AlienCompletionAssistProvider *completionProvider();
    void maybeAttachCompletion(TextEditor::TextDocument *document);

    // Hover + go-to-definition (Creator editor -> host providers).
    void ensureEditorFeatures();
    void attachEditorFeatures(Core::IEditor *editor);
    AlienHoverHandler *hoverHandler();

    Utils::FilePath m_nodePath;
    HostConnection *m_connection = nullptr;
    Utils::FilePath m_runtimeDir;
    QStringList m_commands;
    QList<std::function<void()>> m_deferred;
    QHash<QString, QPointer<AlienClient>> m_lspClients;

    bool m_documentSyncStarted = false;
    // Follows the current editor's caret; re-made whenever the editor changes.
    QMetaObject::Connection m_selectionConnection;
    QHash<QString, QString> m_languageBySuffix; // "qml" -> "qml"
    QHash<Utils::FilePath, int> m_documentVersions;

    // Keyed by "<collection>\n<uri>"; the marks currently shown for that set.
    QHash<QString, QList<TextEditor::TextMark *>> m_diagnosticMarks;

    AlienCompletionAssistProvider *m_completionProvider = nullptr;
    QSet<QString> m_completionLanguageIds;
    QList<QPointer<TextEditor::TextDocument>> m_completionDocuments;

    bool m_editorFeaturesStarted = false;
    AlienHoverHandler *m_hoverHandler = nullptr;
    QSet<QString> m_hoverLanguageIds;
    QSet<QString> m_definitionLanguageIds;
    QList<QPointer<TextEditor::TextEditorWidget>> m_hoverWidgets;

    int m_nextPromptId = 1;
    QHash<int, std::function<void(const QJsonValue &, const QString &)>> m_pendingPrompts;

    QHash<QString, Core::INavigationWidgetFactory *> m_treeFactories;

    WebviewRenderer *m_webviewRenderer = nullptr;
    QJsonObject m_configuration;
    QJsonArray m_workspaceFolders;
};

} // namespace Alien::Internal
