// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vscodemanifest.h"

#include <utils/filepath.h>
#include <utils/result.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>

#include <functional>

QT_BEGIN_NAMESPACE
class QJsonArray;
QT_END_NAMESPACE

namespace Core { class IDocument; class IEditor; }
namespace TextEditor { class TextDocument; class TextEditorWidget; class TextMark; }

namespace Alien::Internal {

class AlienClient;
class AlienCompletionAssistProvider;
class AlienHoverHandler;
class HostConnection;

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

    QStringList registeredCommands() const { return m_commands; }
    void executeCommand(const QString &command);

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
    void activationFailed(const QString &id, const QString &error);
    void languageClientStarted(const QString &id);
    void messageShown(const QString &text);
    void diagnosticsPublished(const QString &uri, int count);

private:
    Utils::Result<> ensureStarted();
    void installHandlers();
    void whenReady(const std::function<void()> &action);

    // Document sync (Creator -> host vscode.workspace).
    void ensureDocumentSync();
    void onDocumentOpened(Core::IDocument *document);
    void onDocumentClosed(Core::IDocument *document);
    void syncActiveEditor();
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
};

} // namespace Alien::Internal
