// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "hostprotocols.h"

#include "aliencompletion.h"
#include "alienconstants.h"
#include "alienhover.h"
#include "alientr.h"
#include "extensionhost.h"
#include "hostconnection.h"

#include <debugger/debuggertooltipmanager.h>

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/find/searchresultwindow.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/messagemanager.h>

#include <languageclient/snippet.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/taskhub.h>

#include <texteditor/autocompleter.h>
#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/completionassistprovider.h>
#include <texteditor/codeassist/functionhintproposal.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/codeassist/iassistprovider.h>
#include <texteditor/codeassist/ifunctionhintproposalmodel.h>
#include <texteditor/fontsettings.h>
#include <texteditor/quickfix.h>
#include <texteditor/semantichighlighter.h>
#include <texteditor/tabsettings.h>
#include <texteditor/textdocument.h>
#include <texteditor/textdocumentlayout.h>
#include <texteditor/texteditor.h>
#include <texteditor/texteditorconstants.h>
#include <texteditor/textmark.h>
#include <texteditor/textsuggestion.h>

#include <utils/algorithm.h>
#include <utils/changeset.h>
#include <utils/link.h>
#include <utils/theme/theme.h>

#include <QTextBlock>

#include <QApplication>
#include <QDesktopServices>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QStyle>
#include <QTimer>

using namespace Core;
using namespace LanguageClient;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace Alien::Internal {

// What an extension asked its markup to look like. Only what a text format can
// carry: the rest of a decoration - a gutter icon, a note after the line - has
// nowhere to go here yet.
static QTextCharFormat decorationFormat(const QJsonObject &options)
{
    const auto colorOf = [](const QJsonValue &value) {
        // A theme colour is named, not given; anything else is a CSS colour.
        if (value.isObject())
            return QColor();
        return QColor::fromString(value.toString());
    };

    QTextCharFormat format;
    if (const QColor color = colorOf(options.value("backgroundColor")); color.isValid())
        format.setBackground(color);
    if (const QColor color = colorOf(options.value("color")); color.isValid())
        format.setForeground(color);
    if (options.value("fontWeight").toString() == "bold")
        format.setFontWeight(QFont::Bold);
    if (options.value("fontStyle").toString() == "italic")
        format.setFontItalic(true);
    if (options.value("textDecoration").toString().contains("underline")) {
        format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        if (const QColor color = colorOf(options.value("borderColor")); color.isValid())
            format.setUnderlineColor(color);
    }
    if (format.properties().isEmpty()) {
        // Nothing we can show would leave the range unmarked, which is worse
        // than approximating it.
        format.setBackground(Utils::creatorColor(Utils::Theme::TextColorLink).lighter(180));
    }
    return format;
}

EditorProtocol::~EditorProtocol()
{
    removeWhatWasShown();

    // A command left registered would outlive the action, and the plugin.
    for (auto it = m_editorContextActions.cbegin(); it != m_editorContextActions.cend(); ++it) {
        ActionManager::unregisterAction(
            it.value(), Id("Alien.EditorContext").withSuffix(QString('.' + it.key())));
    }
    for (auto it = m_projectTreeActions.cbegin(); it != m_projectTreeActions.cend(); ++it) {
        ActionManager::unregisterAction(
            it.value(), Id("Alien.ProjectTree").withSuffix(QString('.' + it.key())));
    }

    // Detach our providers before they are destroyed with the host.
    releaseProviders(std::nullopt);
    for (const QPointer<TextDocument> &document : std::as_const(m_assistDocuments)) {
        if (document && m_signatureProvider
            && document->functionHintAssistProvider() == m_signatureProvider) {
            document->setFunctionHintAssistProvider(nullptr);
        }
    }

    // Remove our hover handler from editors before deleting it.
    for (const QPointer<TextEditorWidget> &widget : std::as_const(m_hoverWidgets)) {
        if (widget && m_hoverHandler)
            widget->removeHoverHandler(m_hoverHandler);
    }
    delete m_hoverHandler;
}

static void stopProvidingFolding(TextDocument *document)
{
    QTextDocument *contents = document->document();
    for (QTextBlock block = contents->begin(); block != contents->end(); block = block.next())
        TextEditor::TextBlockUserData::setFoldingIndent(block, 0);
    document->setFoldingIndentExternallyProvided(false);
}

// What was put on a document has to come off it: the document outlives the
// host, and an extension's marks, colours and folds are not the editor's
// own. Folding matters most - left switched to externally provided, a
// document can never fold again once nobody is providing it.
void EditorProtocol::removeWhatWasShown()
{
    for (const QList<TextMark *> &marks : std::as_const(m_diagnosticMarks))
        qDeleteAll(marks);
    m_diagnosticMarks.clear();
    for (const Tasks &tasks : std::as_const(m_diagnosticTasks)) {
        for (const Task &task : tasks)
            TaskHub::removeTask(task);
    }
    m_diagnosticTasks.clear();
    for (const QList<TextMark *> &marks : std::as_const(m_codeLenses))
        qDeleteAll(marks);
    m_codeLenses.clear();
    for (const QList<TextMark *> &marks : std::as_const(m_comments))
        qDeleteAll(marks);
    m_comments.clear();
    for (const QList<TextMark *> &marks : std::as_const(m_colors))
        qDeleteAll(marks);
    m_colors.clear();
    for (const QList<TextMark *> &marks : std::as_const(m_inlineValues))
        qDeleteAll(marks);
    m_inlineValues.clear();
    for (const QPointer<TextEditorWidget> &widget : std::as_const(m_decoratedWidgets)) {
        if (!widget)
            continue;
        for (auto it = m_decorations.cbegin(); it != m_decorations.cend(); ++it)
            widget->setExtraSelections(Id::fromString(it.key()), {});
    }
    m_decorations.clear();
    m_decoratedWidgets.clear();
    QSet<FilePath> tagged;
    for (auto it = m_diagnosticTags.cbegin(); it != m_diagnosticTags.cend(); ++it)
        tagged.insert(it.key().second);
    m_diagnosticTags.clear();
    for (const FilePath &path : std::as_const(tagged))
        showDiagnosticTags(path); // with none left
    for (TextDocument *document : m_foldingDocuments + m_markerFoldedDocuments)
        stopProvidingFolding(document);
    for (TextDocument *document : std::as_const(m_semanticDocuments)) {
        if (document->syntaxHighlighter()) {
            TextEditor::SemanticHighlighter::setExtraAdditionalFormats(
                document->syntaxHighlighter(), {}, {});
        }
    }
    // The sets stay: they say which documents are connected, and the
    // connections stay until the document is closed.
    for (const QPointer<TextEditorWidget> &widget : std::as_const(m_inlineWidgets)) {
        if (widget)
            widget->clearSuggestion();
    }
}

void EditorProtocol::forgetHost()
{
    m_documentVersions.clear();
    m_pendingChanges.clear();
    m_documentSyncStarted = false;
    disconnect(m_selectionConnection);
    removeWhatWasShown();
    releaseProviders(std::nullopt);
    // Nobody provides anything any more. The next host announces its own.
    for (QSet<QString> *languageIds :
         {&m_completionLanguageIds, &m_hoverLanguageIds, &m_formattingLanguageIds,
          &m_rangeFormattingLanguageIds, &m_codeActionLanguageIds, &m_renameLanguageIds,
          &m_referenceLanguageIds, &m_symbolLanguageIds, &m_highlightLanguageIds,
          &m_linkLanguageIds, &m_signatureLanguageIds, &m_signatureTriggers,
          &m_definitionLanguageIds, &m_typeDefinitionLanguageIds, &m_declarationLanguageIds,
          &m_codeLensLanguageIds, &m_colorLanguageIds, &m_foldingLanguageIds,
          &m_semanticLanguageIds, &m_inlineLanguageIds, &m_onTypeLanguageIds,
          &m_onTypeTriggers, &m_evaluatableExpressionLanguageIds, &m_inlineValueLanguageIds}) {
        languageIds->clear();
    }
    m_hasWorkspaceSymbols = false;
    // Nobody would run what they offer.
    m_editorTitleItems.clear();
    m_editorContextItems.clear();
    for (const QHash<QString, QAction *> &actions : std::as_const(m_editorTitleActions)) {
        for (QAction *action : actions)
            action->setVisible(false);
    }
    for (QAction *action : std::as_const(m_editorContextActions))
        action->setVisible(false);
    for (QAction *action : std::as_const(m_projectTreeActions))
        action->setVisible(false);
}

void EditorProtocol::installHandlers()
{
    // An extension's own markup on the text: a finding, a review comment. The
    // format is remembered when the type is made, and applied to whatever
    // ranges the extension later hands over for a file.
    connection()->setNotificationHandler("decoration/register", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        m_decorations.insert(object.value("key").toString(),
                             decorationFormat(object.value("options").toObject()));
    });

    connection()->setNotificationHandler("decoration/unregister", [this](const QJsonValue &params) {
        const QString key = params.toObject().value("key").toString();
        m_decorations.remove(key);
        // Whatever it marked up goes with it.
        for (TextEditorWidget *widget : m_decoratedWidgets) {
            if (widget)
                widget->setExtraSelections(Id::fromString(key), {});
        }
    });

    connection()->setNotificationHandler("decoration/set", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString key = object.value("key").toString();
        const auto format = m_decorations.constFind(key);
        if (format == m_decorations.constEnd())
            return;
        const FilePath filePath = m_host.fromHostPath(object.value("path").toString());
        TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
        if (!document)
            return;

        const QString text = document->plainText();
        QList<QTextEdit::ExtraSelection> selections;
        for (const QJsonValue &value : object.value("ranges").toArray()) {
            const QJsonObject range = value.toObject();
            QTextCursor cursor(document->document());
            cursor.setPosition(offsetOf(text, range.value("start")));
            cursor.setPosition(offsetOf(text, range.value("end")), QTextCursor::KeepAnchor);
            selections.append({cursor, *format});
        }
        for (TextEditorWidget *widget : TextEditorWidget::textEditorWidgetsForDocument(document)) {
            widget->setExtraSelections(Id::fromString(key), selections);
            if (!m_decoratedWidgets.contains(widget))
                m_decoratedWidgets.append(widget);
        }
    });

    connection()->setRequestHandler(
        "editor/insertSnippet",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const FilePath filePath = m_host.fromHostPath(object.value("path").toString());
            const Link link{filePath,
                            object.value("line").toInt() + 1,
                            object.value("character").toInt()};
            if (!EditorManager::openEditorAt(link)) {
                respond({}, Tr::tr("Cannot open \"%1\".").arg(filePath.toUserOutput()));
                return;
            }
            BaseTextEditor *editor = BaseTextEditor::currentTextEditor();
            TextEditorWidget *widget = editor ? editor->editorWidget() : nullptr;
            if (!widget) {
                respond({}, Tr::tr("\"%1\" is not a text editor.").arg(filePath.toUserOutput()));
                return;
            }
            // The same parser the language client uses, so an extension's
            // snippet behaves like one that came from a language server:
            // placeholders become the fields the user tabs through.
            widget->insertCodeSnippet(widget->position(),
                                      object.value("snippet").toString(),
                                      &LanguageClient::parseSnippet);
            respond(true, {});
        });

    connection()->setNotificationHandler("menus/editorTitle", [this](const QJsonValue &params) {
        m_editorTitleItems.clear();
        for (const QJsonValue &item : params.toObject().value("items").toArray()) {
            const QJsonObject object = item.toObject();
            m_editorTitleItems.append({object.value("command").toString(),
                                       object.value("title").toString()});
        }
        ensureEditorFeatures();
        for (IEditor *editor : DocumentModel::editorsForOpenedDocuments()) {
            if (TextEditorWidget *widget = TextEditorWidget::fromEditor(editor))
                updateEditorTitleActions(widget);
        }
    });

    connection()->setNotificationHandler("menus/editorContext", [this](const QJsonValue &params) {
        m_editorContextItems.clear();
        for (const QJsonValue &item : params.toObject().value("items").toArray()) {
            const QJsonObject object = item.toObject();
            m_editorContextItems.append({object.value("command").toString(),
                                         object.value("title").toString()});
        }
        updateEditorContextActions();
    });

    connection()->setNotificationHandler("diagnostics/publish", [this](const QJsonValue &params) {
        publishDiagnostics(params);
    });

    // The set is what was sent, not what was ever sent: an extension being
    // switched off announces the languages that are left, and a feature nobody
    // provides any more has to stop being provided.
    auto registerLanguageFeature = [this](QSet<QString> &target) {
        return [this, &target](const QJsonValue &params) {
            const QSet<QString> before = target;
            target.clear();
            for (const QJsonValue &language : params.toObject().value("languageIds").toArray())
                target.insert(language.toString());
            for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
                attachEditorFeatures(editor);
            if (before == target)
                return;
            // What the languages that just lost their provider still show has
            // to be asked for again, and the answer will be nothing.
            for (const QString &languageId : before - target)
                releaseLanguageFeatures(languageId);
        };
    };

    connection()->setNotificationHandler(
        "completion/registerProvider",
        [this, registerCompletion = registerLanguageFeature(m_completionLanguageIds)](
            const QJsonValue &params) {
            registerCompletion(params);
            // Attach to documents that are already open.
            for (IDocument *document : DocumentModel::openedDocuments()) {
                if (auto textDocument = qobject_cast<TextDocument *>(document))
                    maybeAttachCompletion(textDocument);
            }
        });
    connection()->setNotificationHandler("hover/registerProvider",
                                         registerLanguageFeature(m_hoverLanguageIds));
    connection()->setNotificationHandler("definition/registerProvider",
                                         registerLanguageFeature(m_definitionLanguageIds));
    connection()->setNotificationHandler("typeDefinition/registerProvider",
                                         registerLanguageFeature(m_typeDefinitionLanguageIds));
    connection()->setNotificationHandler("declaration/registerProvider",
                                         registerLanguageFeature(m_declarationLanguageIds));
    connection()->setNotificationHandler("codeLens/registerProvider",
                                         registerLanguageFeature(m_codeLensLanguageIds));
    connection()->setNotificationHandler("color/registerProvider",
                                         registerLanguageFeature(m_colorLanguageIds));
    connection()->setNotificationHandler("folding/registerProvider",
                                         registerLanguageFeature(m_foldingLanguageIds));
    connection()->setNotificationHandler("semanticTokens/registerProvider",
                                         registerLanguageFeature(m_semanticLanguageIds));
    connection()->setNotificationHandler("inlineCompletion/registerProvider",
                                         registerLanguageFeature(m_inlineLanguageIds));
    connection()->setNotificationHandler(
        "evaluatableExpression/registerProvider",
        registerLanguageFeature(m_evaluatableExpressionLanguageIds));
    connection()->setNotificationHandler("inlineValues/registerProvider",
                                         registerLanguageFeature(m_inlineValueLanguageIds));

    // What the debugger evaluates when the user points at something. The
    // pointer is guarded rather than cleared on the way out: a host that is
    // gone claims nothing, and the next one takes the hook over.
    Debugger::Internal::debuggerToolTipExpressionProvider()
        = [host = QPointer<ExtensionHost>(&m_host)](
              const FilePath &file, int line, int column,
              const std::function<void(const QString &)> &answer) {
              return host && host->editor().provideToolTipExpression(file, line, column, answer);
          };
    const auto registerOnType = [this, registerLanguageFeature](const QJsonValue &params) {
        m_onTypeTriggers.clear();
        for (const QJsonValue &character : params.toObject().value("triggerCharacters").toArray())
            m_onTypeTriggers.insert(character.toString());
        registerLanguageFeature(m_onTypeLanguageIds)(params);
    };
    connection()->setNotificationHandler("onTypeFormatting/registerProvider", registerOnType);

    connection()->setNotificationHandler("document/setLanguage", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const FilePath filePath = m_host.fromHostPath(object.value("uri").toString());
        m_host.m_languageOverrides.insert(filePath, object.value("languageId").toString());
        for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
            attachEditorFeatures(editor);
    });

    connection()->setRequestHandler(
        "languages/known",
        [this](const QJsonValue &, const HostConnection::Responder &respond) {
            QSet<QString> ids;
            for (const auto &[extensionId, language] : m_host.m_declaredLanguages)
                ids.insert(language.id);
            for (const QString &id : m_host.m_languageOverrides)
                ids.insert(id);
            for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
                ids.insert(m_host.languageIdFor(editor->document()->filePath()));
            ids.insert("plaintext");
            QJsonArray array;
            for (const QString &id : ids)
                array.append(id);
            respond(QJsonObject{{"languageIds", array}}, {});
        });

    // A document the extension makes up rather than reads from disk, said again.
    connection()->setNotificationHandler(
        "document/replaceContents", [this](const QJsonValue &params) {
            const QJsonObject object = params.toObject();
            const FilePath path = m_host.fromHostPath(object.value("uri").toString());
            TextDocument *document = TextDocument::textDocumentForFilePath(path);
            if (!document)
                return;
            QTextCursor cursor(document->document());
            cursor.select(QTextCursor::Document);
            cursor.insertText(object.value("text").toString());
        });

    connection()->setNotificationHandler("codeLens/changed", [this](const QJsonValue &) {
        for (const FilePath &path : m_codeLenses.keys())
            refreshCodeLenses(path);
    });
    connection()->setNotificationHandler("comments/register", [this](const QJsonValue &) {
        m_hasCommentControllers = true;
        if (Core::IEditor *editor = Core::EditorManager::currentEditor())
            refreshComments(editor->document()->filePath());
    });
    connection()->setNotificationHandler("comments/changed", [this](const QJsonValue &params) {
        const QString uri = params.toObject().value("uri").toString();
        refreshComments(m_host.fromHostPath(uri));
        emit m_host.commentsChanged(uri);
    });
    connection()->setNotificationHandler("formatting/registerProvider",
                                         registerLanguageFeature(m_formattingLanguageIds));
    connection()->setNotificationHandler("rangeFormatting/registerProvider",
                                         registerLanguageFeature(m_rangeFormattingLanguageIds));
    connection()->setNotificationHandler("codeAction/registerProvider",
                                         registerLanguageFeature(m_codeActionLanguageIds));
    connection()->setNotificationHandler("rename/registerProvider",
                                         registerLanguageFeature(m_renameLanguageIds));
    connection()->setNotificationHandler("references/registerProvider",
                                         registerLanguageFeature(m_referenceLanguageIds));
    connection()->setNotificationHandler("symbols/registerProvider",
                                         registerLanguageFeature(m_symbolLanguageIds));
    connection()->setNotificationHandler("workspaceSymbols/registerProvider",
                                         [this](const QJsonValue &) {
                                             m_hasWorkspaceSymbols = true;
                                         });
    connection()->setNotificationHandler("highlights/registerProvider",
                                         registerLanguageFeature(m_highlightLanguageIds));
    connection()->setNotificationHandler("links/registerProvider",
                                         registerLanguageFeature(m_linkLanguageIds));
    connection()->setNotificationHandler(
        "signature/registerProvider", [this](const QJsonValue &params) {
            const QJsonObject object = params.toObject();
            for (const QJsonValue &id : object.value("languageIds").toArray())
                m_signatureLanguageIds.insert(id.toString());
            for (const QJsonValue &character : object.value("triggerCharacters").toArray())
                m_signatureTriggers.insert(character.toString());
            ensureEditorFeatures();
            for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
                attachEditorFeatures(editor);
        });
}

void EditorProtocol::ensureDocumentSync()
{
    if (m_documentSyncStarted)
        return;
    m_documentSyncStarted = true;
    // A host that replaces a stopped one is told through the same connections.
    if (std::exchange(m_documentSignalsConnected, true)) {
        announceDocuments();
        return;
    }

    connect(EditorManager::instance(), &EditorManager::documentOpened,
            this, &EditorProtocol::onDocumentOpened);
    connect(EditorManager::instance(), &EditorManager::documentClosed,
            this, &EditorProtocol::onDocumentClosed);
    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, [this] {
                syncActiveEditor();
                reportEditorFocus();
                // The editor's own settings are part of what the host is told,
                // and they can change while it stays open.
                auto document = qobject_cast<TextDocument *>(EditorManager::currentDocument());
                if (document && !m_tabSettingsWatched.contains(document)) {
                    m_tabSettingsWatched.insert(document);
                    connect(document, &TextDocument::tabSettingsChanged, this,
                            [this] { syncActiveEditor(); });
                    connect(document, &QObject::destroyed, this, [this, document] {
                        m_tabSettingsWatched.remove(document);
                    });
                }
            });
    connect(qApp, &QApplication::focusChanged, this, [this] { reportEditorFocus(); });
    reportEditorFocus();

    // Files appearing in or disappearing from a project. The project model
    // reports only that its list changed, so what was added and what went is
    // the difference against what was there before - which is also what an
    // extension is told.
    const auto watchProject = [this](ProjectExplorer::Project *project) {
        m_projectFiles.insert(project, project->files(ProjectExplorer::Project::AllFiles));
        connect(project, &ProjectExplorer::Project::fileListChanged, this, [this, project] {
            const FilePaths before = m_projectFiles.value(project);
            const FilePaths now = project->files(ProjectExplorer::Project::AllFiles);
            m_projectFiles.insert(project, now);

            const QSet<FilePath> beforeSet(before.begin(), before.end());
            const QSet<FilePath> nowSet(now.begin(), now.end());
            const auto report = [this](const QString &method, const QSet<FilePath> &paths) {
                QJsonArray files;
                for (const FilePath &path : paths) {
                    if (m_host.isOnHostDevice(path))
                        files.append(m_host.toHostPath(path));
                }
                if (!files.isEmpty() && connection())
                    connection()->sendNotification(method, QJsonObject{{"files", files}});
            };
            report("files/didCreate", nowSet - beforeSet);
            report("files/didDelete", beforeSet - nowSet);
        });
    };
    for (ProjectExplorer::Project *project : ProjectExplorer::ProjectManager::projects())
        watchProject(project);
    connect(ProjectExplorer::ProjectManager::instance(),
            &ProjectExplorer::ProjectManager::projectAdded, this, watchProject);
    connect(ProjectExplorer::ProjectManager::instance(),
            &ProjectExplorer::ProjectManager::projectRemoved, this,
            [this](ProjectExplorer::Project *project) { m_projectFiles.remove(project); });

    // A file that changed its name is one an extension may be watching: it
    // keeps state per path, and never hearing about a rename leaves that state
    // pointing at a file that is not there any more.
    connect(DocumentManager::instance(), &DocumentManager::allDocumentsRenamed, this,
            [this](const FilePath &from, const FilePath &to) {
                if (connection() && m_host.isOnHostDevice(to)) {
                    connection()->sendNotification(
                        "files/didRename",
                        QJsonObject{{"from", m_host.toHostPath(from)},
                                    {"to", m_host.toHostPath(to)}});
                }
            });

    announceDocuments();
}

void EditorProtocol::announceDocuments()
{
    for (IDocument *document : DocumentModel::openedDocuments())
        onDocumentOpened(document);
    syncOtherEditors();
    syncActiveEditor();
}

void EditorProtocol::onDocumentOpened(IDocument *document)
{
    if (!connection())
        return;
    auto textDocument = qobject_cast<TextDocument *>(document);
    if (!textDocument) {
        syncOtherEditors();
        return;
    }

    const FilePath filePath = textDocument->filePath();
    if (filePath.isEmpty())
        return;

    // A document on another device is not reachable for the host, and its path
    // would collide with a same-named one on the host's own device.
    if (!m_host.isOnHostDevice(filePath))
        return;

    m_documentVersions.insert(filePath, 1);
    enum { EolLf = 1, EolCrlf = 2 }; // vscode.EndOfLine
    connection()->sendNotification("document/didOpen", QJsonObject{
        {"uri", m_host.toHostPath(filePath)},
        {"languageId", m_host.languageIdFor(filePath)},
        {"version", 1},
        // Whether it has unsaved changes, and how its lines end: an extension
        // writing back has to know both.
        {"isDirty", textDocument->isModified()},
        {"eol", textDocument->format().lineTerminationMode
                        == Utils::TextFileFormat::CRLFLineTerminator ? EolCrlf : EolLf},
        {"text", textDocument->plainText()},
    });

    maybeAttachCompletion(textDocument);

    if (m_syncedDocuments.contains(textDocument))
        return;
    m_syncedDocuments.insert(textDocument);
    connect(textDocument, &QObject::destroyed, this,
            [this, textDocument] { m_syncedDocuments.remove(textDocument); });

    // To the extension, a document under a new name is a different document.
    connect(textDocument, &IDocument::filePathChanged, this,
            [this, textDocument](const FilePath &oldPath) {
                m_pendingChanges.remove(oldPath);
                // The marks moved with the document.
                for (auto marks : {&m_colors, &m_codeLenses, &m_comments}) {
                    if (marks->contains(oldPath))
                        marks->insert(textDocument->filePath(), marks->take(oldPath));
                }
                if (m_documentVersions.remove(oldPath) && connection()) {
                    connection()->sendNotification(
                        "document/didClose", QJsonObject{{"uri", m_host.toHostPath(oldPath)}});
                }
                onDocumentOpened(textDocument);
            });

    // Saving is what a great many extensions wait for - to lint, to build, to
    // run what the file describes - and it was never passed on. Queued like the
    // change below, so that an extension acting on a save sees the text that
    // was saved rather than the one before it.
    connect(textDocument, &IDocument::aboutToSave, this,
            [this, textDocument](const FilePath &path, IDocument::SaveOption) {
                if (connection() && m_host.isOnHostDevice(path)) {
                    connection()->sendNotification("document/willSave",
                                                   QJsonObject{{"uri", m_host.toHostPath(path)}});
                }
            }, Qt::QueuedConnection);
    connect(textDocument, &IDocument::saved, this,
            [this](const FilePath &path, IDocument::SaveOption) {
                if (connection() && m_host.isOnHostDevice(path)) {
                    connection()->sendNotification("document/didSave",
                                                   QJsonObject{{"uri", m_host.toHostPath(path)}});
                }
            }, Qt::QueuedConnection);

    // Sent after the event loop: the signal arrives while the change is being
    // applied, and the text read at that moment can still be the half-done
    // one - an editor filled in one go reports itself empty. The changes of
    // one pass, a Replace All say, go out as the one range covering them all,
    // since the text sent along is the one after the last of them.
    connect(textDocument, &TextDocument::contentsChangedWithPosition, this,
            [this, textDocument](int position, int charsRemoved, int charsAdded) {
                if (!connection())
                    return;
                const FilePath path = textDocument->filePath();
                const auto pending = m_pendingChanges.constFind(path);
                if (pending == m_pendingChanges.cend()) {
                    m_pendingChanges.insert(path, {position, position + charsRemoved,
                                                   position + charsAdded});
                    QMetaObject::invokeMethod(this, [this, textDocument = QPointer(textDocument),
                                                     path] {
                        sendPendingChange(textDocument, path);
                    }, Qt::QueuedConnection);
                    return;
                }
                // In the text as it is now, the range covered so far ends at
                // newEnd, and everything after it is shifted by the same
                // amount against the text the host last saw.
                const PendingChange covered = *pending;
                const int shift = covered.newEnd - covered.oldEnd;
                const int end = std::max(covered.newEnd, position + charsRemoved);
                m_pendingChanges.insert(path, {std::min(covered.start, position),
                                               end - shift,
                                               end - charsRemoved + charsAdded});
            });
}

void EditorProtocol::sendPendingChange(TextDocument *textDocument, const FilePath &path)
{
    if (!m_pendingChanges.contains(path))
        return;
    const PendingChange change = m_pendingChanges.take(path);
    if (!textDocument || !connection() || !m_documentVersions.contains(path))
        return;
    const int version = m_documentVersions.value(path) + 1;
    m_documentVersions.insert(path, version);
    // Where the change was, as well as the text after it. An extension is
    // told what the user did - a keystroke is a keystroke, not a replacement
    // of the whole file - while the text stays the authority on what the
    // document now says, so the two cannot drift apart.
    connection()->sendNotification("document/didChange", QJsonObject{
        {"uri", m_host.toHostPath(path)},
        {"version", version},
        {"text", textDocument->plainText()},
        {"position", change.start},
        {"charsRemoved", change.oldEnd - change.start},
        {"charsAdded", change.newEnd - change.start},
    });
}

void EditorProtocol::onDocumentClosed(IDocument *document)
{
    auto textDocument = qobject_cast<TextDocument *>(document);
    // The connections went with the document, the file opened again needs new ones.
    for (QSet<TextDocument *> *documents : {&m_onTypeDocuments, &m_markerFoldedDocuments,
                                            &m_semanticDocuments, &m_foldingDocuments,
                                            &m_colorDocuments, &m_codeLensDocuments}) {
        documents->remove(textDocument);
    }

    if (!connection())
        return;
    if (!textDocument) {
        syncOtherEditors();
        return;
    }
    const FilePath filePath = textDocument->filePath();
    m_documentVersions.remove(filePath);
    connection()->sendNotification("document/didClose",
                                   QJsonObject{{"uri", m_host.toHostPath(filePath)}});
}

void EditorProtocol::syncActiveEditor()
{
    if (!connection())
        return;
    IDocument *document = EditorManager::currentDocument();
    auto textDocument = qobject_cast<TextDocument *>(document);
    const QJsonValue uri = textDocument ? QJsonValue(m_host.toHostPath(textDocument->filePath()))
                                        : QJsonValue(QJsonValue::Null);
    // The file the user is looking at, whatever editor shows it. A binary file
    // or an image has no text document and is open all the same, and what an
    // extension reads to find out is the active tab.
    const FilePath activePath = document ? document->filePath() : FilePath();
    const QJsonValue tabUri = !activePath.isEmpty() && m_host.isOnHostDevice(activePath)
                                  ? QJsonValue(m_host.toHostPath(activePath))
                                  : QJsonValue(QJsonValue::Null);
    // What the editor is set to, which is what an extension formats to.
    const TextEditor::TabSettingsData tabs = textDocument ? textDocument->tabSettings()
                                                          : TextEditor::TabSettingsData();
    connection()->sendNotification(
        "editor/didChangeActive",
        QJsonObject{{"uri", uri},
                    {"tabUri", tabUri},
                    {"tabSize", tabs.m_indentSize},
                    {"insertSpaces",
                     tabs.m_tabPolicy == TextEditor::TabSettingsData::SpacesOnlyTabPolicy}});

    // What is on screen, which an extension walks to decorate or inspect every
    // editor the user can see. An empty list means it decorates nothing.
    QJsonArray visible;
    for (IEditor *editor : EditorManager::visibleEditors()) {
        auto visibleDocument = qobject_cast<TextDocument *>(editor->document());
        if (visibleDocument && !visibleDocument->filePath().isEmpty()
            && m_host.isOnHostDevice(visibleDocument->filePath())) {
            visible.append(m_host.toHostPath(visibleDocument->filePath()));
        }
    }
    connection()->sendNotification("editor/didChangeVisible", QJsonObject{{"uris", visible}});

    // Follow the caret of whatever is now current. Extensions that keep a view
    // aligned with the cursor - a preview scrolling along with the editor -
    // need this; without it their scroll sync is silently inert.
    disconnect(m_selectionConnection);
    BaseTextEditor *editor = BaseTextEditor::currentTextEditor();
    TextEditorWidget *widget = editor ? editor->editorWidget() : nullptr;
    if (!widget)
        return;
    m_selectionConnection = connect(widget, &TextEditorWidget::cursorPositionChanged,
                                    this, [this, widget] { sendSelection(widget); });
    sendSelection(widget); // start out in sync, not at the next keystroke
}

void EditorProtocol::syncOtherEditors()
{
    if (!connection())
        return;
    QJsonArray uris;
    for (IDocument *document : DocumentModel::openedDocuments()) {
        if (qobject_cast<TextDocument *>(document))
            continue;
        const FilePath filePath = document->filePath();
        if (filePath.isEmpty() || !m_host.isOnHostDevice(filePath))
            continue;
        uris.append(m_host.toHostPath(filePath));
    }
    connection()->sendNotification("editor/didChangeOtherEditors",
                                   QJsonObject{{"uris", uris}});
}

void EditorProtocol::sendSelection(TextEditorWidget *widget)
{
    if (!connection())
        return;
    const TextDocument *document = widget->textDocument();
    if (!document || document->filePath().isEmpty())
        return;

    const QTextCursor cursor = widget->textCursor();
    QTextCursor anchor = cursor;
    anchor.setPosition(cursor.anchor());

    connection()->sendNotification("editor/didChangeSelection", QJsonObject{
        {"uri", m_host.toHostPath(document->filePath())},
        {"anchorLine", anchor.blockNumber()},
        {"anchorCharacter", anchor.positionInBlock()},
        {"activeLine", cursor.blockNumber()},
        {"activeCharacter", cursor.positionInBlock()},
    });
}

void EditorProtocol::publishDiagnostics(const QJsonValue &params)
{
    const QJsonObject object = params.toObject();
    const QString uri = object.value("uri").toString();
    const QString collection = object.value("collection").toString();
    const QJsonArray diagnostics = object.value("diagnostics").toArray();
    const QString key = collection + '\n' + uri;

    // Replace the previous marks for this collection/uri.
    qDeleteAll(m_diagnosticMarks.take(key));
    for (const Task &task : m_diagnosticTasks.take(key))
        TaskHub::removeTask(task);

    const FilePath filePath = m_host.fromHostPath(uri);
    m_diagnosticTags.remove(qMakePair(collection, filePath));
    if (!diagnostics.isEmpty())
        m_diagnosticTags.insert(qMakePair(collection, filePath), diagnostics);
    const TextMarkCategory category{Tr::tr("Alien"), "Alien.Diagnostics"};

    QList<TextMark *> marks;
    Tasks tasks;
    for (const QJsonValue &value : diagnostics) {
        const QJsonObject diagnostic = value.toObject();
        const QJsonObject start = diagnostic.value("range").toObject().value("start").toObject();
        const int line = start.value("line").toInt() + 1;
        const QString message = diagnostic.value("message").toString();

        QString tooltip = message;
        // Where else the diagnostic points - the other end of a duplicate
        // definition, the declaration a call does not match.
        for (const QJsonValue &value : diagnostic.value("relatedInformation").toArray()) {
            const QJsonObject related = value.toObject();
            const QJsonObject at = related.value("range").toObject().value("start").toObject();
            tooltip += QString("\n%1:%2: %3")
                           .arg(m_host.fromHostPath(related.value("uri").toString()).fileName())
                           .arg(at.value("line").toInt() + 1)
                           .arg(related.value("message").toString());
        }

        auto mark = new TextMark(filePath, line, category);
        mark->setToolTip(tooltip);
        mark->setLineAnnotation(message);

        enum { SeverityError = 0, SeverityWarning = 1 }; // vscode.DiagnosticSeverity
        Task::TaskType type = Task::Unknown;
        switch (diagnostic.value("severity").toInt()) {
        case SeverityError:
            mark->setColor(Theme::CodeModel_Error_TextMarkColor);
            type = Task::Error;
            break;
        case SeverityWarning:
            mark->setColor(Theme::CodeModel_Warning_TextMarkColor);
            type = Task::Warning;
            break;
        default:
            mark->setColor(Theme::CodeModel_Info_TextMarkColor);
            break;
        }
        marks.append(mark);

        // Also list it in Issues: a mark alone is only visible in an open
        // editor, and an extension diagnoses files the user has not opened.
        Task task(type, message, filePath, line, Constants::TASK_CATEGORY_DIAGNOSTICS);
        // The mark above is the one in the editor, with the fuller tooltip.
        task.preventTextMarkCreation();
        TaskHub::addTask(task);
        tasks.append(task);
    }

    showDiagnosticTags(filePath);

    if (!marks.isEmpty())
        m_diagnosticMarks.insert(key, marks);
    if (!tasks.isEmpty())
        m_diagnosticTasks.insert(key, tasks);

    emit m_host.diagnosticsPublished(uri, int(diagnostics.size()));
}

// Unnecessary code is worth showing as such rather than as one more warning in
// the margin, and so is code that still works but should not be used.
void EditorProtocol::showDiagnosticTags(const FilePath &filePath)
{
    TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
    if (!document)
        return;

    QList<QTextEdit::ExtraSelection> selections;
    const QString text = document->plainText();
    for (auto it = m_diagnosticTags.cbegin(); it != m_diagnosticTags.cend(); ++it) {
        if (it.key().second != filePath)
            continue;
        for (const QJsonValue &value : it.value()) {
            const QJsonObject diagnostic = value.toObject();
            QTextCharFormat format;
            bool tagged = false;
            enum { TagUnnecessary = 1, TagDeprecated = 2 }; // vscode.DiagnosticTag
            for (const QJsonValue &tag : diagnostic.value("tags").toArray()) {
                if (tag.toInt() == TagUnnecessary) {
                    format.setForeground(Utils::creatorColor(Theme::TextColorDisabled));
                    tagged = true;
                } else if (tag.toInt() == TagDeprecated) {
                    format.setFontStrikeOut(true);
                    tagged = true;
                }
            }
            if (!tagged)
                continue;
            const QJsonObject range = diagnostic.value("range").toObject();
            QTextCursor cursor(document->document());
            cursor.setPosition(offsetOf(text, range.value("start")));
            cursor.setPosition(offsetOf(text, range.value("end")), QTextCursor::KeepAnchor);
            selections.append({cursor, format});
        }
    }
    for (TextEditorWidget *widget : TextEditorWidget::textEditorWidgetsForDocument(document))
        widget->setExtraSelections(Id("Alien.DiagnosticTags"), selections);
}

void EditorProtocol::resolveCompletion(const QString &id,
                                      const std::function<void(const QJsonObject &)> &callback)
{
    if (!connection() || id.isEmpty())
        return;
    connection()->sendRequest("completion/resolve", QJsonObject{{"id", id}},
                              [callback](const QJsonValue &result, const QString &error) {
                                  if (error.isEmpty() && result.isObject())
                                      callback(result.toObject());
                              });
}

AlienCompletionAssistProvider *EditorProtocol::completionProvider()
{
    if (!m_completionProvider)
        m_completionProvider = new AlienCompletionAssistProvider(&m_host);
    return m_completionProvider;
}

void EditorProtocol::maybeAttachCompletion(TextDocument *document)
{
    if (!document || !m_completionLanguageIds.contains(m_host.languageIdFor(document->filePath())))
        return;
    CompletionAssistProvider *previous = document->completionAssistProvider();
    if (previous == completionProvider())
        return;

    document->setCompletionAssistProvider(completionProvider());
    m_completionDocuments.removeIf([document](const auto &entry) {
        return entry.first == document;
    });
    m_completionDocuments.append({document, previous});
}

void EditorProtocol::requestCompletion(const FilePath &uri, int line, int character,
                                      const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", m_host.toHostPath(uri)},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    m_host.whenReady([this, params, callback] {
        connection()->sendRequest(
            "completion/provide", params,
            [callback](const QJsonValue &result, const QString &error) {
                if (!error.isEmpty())
                    callback({});
                else
                    callback(result.toObject().value("items").toArray());
            });
    }, [callback] { callback({}); });
}

void EditorProtocol::requestHover(const FilePath &uri, int line, int character,
                                 const std::function<void(const QString &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", m_host.toHostPath(uri)},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    m_host.whenReady([this, params, callback] {
        connection()->sendRequest(
            "hover/provide", params, [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("contents").toString() : QString());
            });
    }, [callback] { callback({}); });
}

void EditorProtocol::requestEvaluatableExpression(
    const FilePath &uri, int line, int character,
    const std::function<void(const QString &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", m_host.toHostPath(uri)},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    m_host.whenReady([this, params, callback] {
        connection()->sendRequest(
            "evaluatableExpression/provide", params,
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("expression").toString()
                                         : QString());
            });
    }, [callback] { callback({}); });
}

void EditorProtocol::requestInlineValues(const FilePath &uri, int startLine, int endLine,
                                        int frameId, int stoppedLine,
                                        const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", m_host.toHostPath(uri)},
        {"startLine", startLine},
        {"endLine", endLine},
        {"frameId", frameId},
        {"stoppedLine", stoppedLine},
    };
    m_host.whenReady([this, params, callback] {
        connection()->sendRequest(
            "inlineValues/provide", params,
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toArray() : QJsonArray());
            });
    }, [callback] { callback({}); });
}

void EditorProtocol::requestTypeDefinition(const FilePath &uri, int line, int character,
                                          const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", m_host.toHostPath(uri)},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    m_host.whenReady([this, params, callback] {
        connection()->sendRequest(
            "typeDefinition/provide", params,
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("locations").toArray()
                                         : QJsonArray());
            });
    }, [callback] { callback({}); });
}

void EditorProtocol::requestDeclaration(const FilePath &uri, int line, int character,
                                       const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", m_host.toHostPath(uri)},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    m_host.whenReady([this, params, callback] {
        connection()->sendRequest(
            "declaration/provide", params,
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("locations").toArray()
                                         : QJsonArray());
            });
    }, [callback] { callback({}); });
}

void EditorProtocol::requestDefinition(const FilePath &uri, int line, int character,
                                      const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", m_host.toHostPath(uri)},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    m_host.whenReady([this, params, callback] {
        connection()->sendRequest(
            "definition/provide", params,
            [callback](const QJsonValue &result, const QString &error) {
                if (!error.isEmpty())
                    callback({});
                else
                    callback(result.toObject().value("locations").toArray());
            });
    }, [callback] { callback({}); });
}

// Formatting the document with what an extension would do to it. Qt Creator
// asks through this whenever the user formats, and on save where that is turned
// on, so the extension's formatter is the one the editor already has.
class AlienFormatter final : public TextEditor::Formatter
{
public:
    AlienFormatter(ExtensionHost *host, TextEditor::TextDocument *document)
        : m_host(host)
        , m_document(document)
    {}

    void format(const QTextCursor &cursor,
                const TextEditor::TabSettingsData &tabSettings,
                const TextEditor::FormatCallback &callback) final
    {
        if (!m_host || !m_document) {
            callback({});
            return;
        }
        const int tabSize = tabSettings.m_indentSize;
        const bool spaces
            = tabSettings.m_tabPolicy == TextEditor::TabSettingsData::SpacesOnlyTabPolicy;
        const auto applied = [document = QPointer<TextEditor::TextDocument>(m_document),
                              revision = m_document->document()->revision(),
                              callback](const QJsonArray &edits) {
                if (!document) // closed while the extension was thinking
                    return;
                // Edited meanwhile: the ranges are for the text that was sent.
                if (document->document()->revision() != revision) {
                    callback({});
                    return;
                }
                const QString text = document->plainText();
                Utils::ChangeSet changes;
                for (const QJsonValue &value : edits) {
                    const QJsonObject edit = value.toObject();
                    const QJsonObject range = edit.value("range").toObject();
                    changes.replace(offsetOf(text, range.value("start")),
                                    offsetOf(text, range.value("end")),
                                    edit.value("newText").toString());
                }
                callback(changes);
        };

        // What the user asked to have formatted. A selection goes to the
        // provider that formats a range, where the extension offered one;
        // otherwise the whole document is the answer to either question.
        if (m_mode == FormatMode::Range && cursor.hasSelection()
            && m_host->editor().formatsRangesFor(m_document->filePath())) {
            QTextCursor start = cursor;
            start.setPosition(cursor.selectionStart());
            QTextCursor end = cursor;
            end.setPosition(cursor.selectionEnd());
            m_host->editor().requestRangeFormatting(m_document->filePath(), tabSize, spaces,
                                           {start.blockNumber(), start.positionInBlock()},
                                           {end.blockNumber(), end.positionInBlock()}, applied);
            return;
        }
        m_host->editor().requestFormatting(m_document->filePath(), tabSize, spaces, applied);
    }

    void setMode(FormatMode mode) final { m_mode = mode; }

private:
    QPointer<ExtensionHost> m_host;
    QPointer<TextEditor::TextDocument> m_document;
    FormatMode m_mode = FormatMode::Range;
};

bool EditorProtocol::formatsRangesFor(const FilePath &filePath) const
{
    return m_rangeFormattingLanguageIds.contains(m_host.languageIdFor(filePath));
}

void EditorProtocol::requestRangeFormatting(const FilePath &filePath, int tabSize,
                                           bool insertSpaces, const QPair<int, int> &start,
                                           const QPair<int, int> &end,
                                           const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "rangeFormatting/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)},
                    {"tabSize", tabSize},
                    {"insertSpaces", insertSpaces},
                    {"startLine", start.first},
                    {"startCharacter", start.second},
                    {"endLine", end.first},
                    {"endCharacter", end.second}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

void EditorProtocol::requestFormatting(const FilePath &filePath, int tabSize, bool insertSpaces,
                                     const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "formatting/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)},
                    {"tabSize", tabSize},
                    {"insertSpaces", insertSpaces}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// One thing an extension offered to do here. The edits came with it, so
// performing it is applying them - nothing has to be asked again.
class AlienQuickFixOperation final : public TextEditor::QuickFixOperation
{
public:
    AlienQuickFixOperation(const QJsonObject &action, ExtensionHost *host)
        : m_changes(action.value("changes").toArray())
        , m_command(action.value("command").toString())
        , m_arguments(action.value("arguments").toArray())
        , m_host(host)
    {
        setDescription(action.value("title").toString());
    }

    // An action either edits or runs a command, and may do both.
    void perform() final
    {
        if (!m_host)
            return;
        if (!m_changes.isEmpty())
            m_host->editor().applyChanges(m_changes);
        if (!m_command.isEmpty())
            m_host->executeCommand(m_command, m_arguments);
    }

private:
    QJsonArray m_changes;
    QString m_command;
    QJsonArray m_arguments;
    QPointer<ExtensionHost> m_host;
};

// Asks the extension what it would offer, and hands the answer back when it
// arrives: the editor is not kept waiting on a language it does not know.
class AlienQuickFixProcessor final : public AlienAssistProcessor
{
public:
    explicit AlienQuickFixProcessor(ExtensionHost *host) : m_host(host) {}

    TextEditor::IAssistProposal *perform() final
    {
        if (!m_host)
            return nullptr;
        QTextCursor cursor = interface()->cursor();
        if (!cursor.hasSelection())
            cursor.select(QTextCursor::WordUnderCursor);
        if (!cursor.hasSelection())
            cursor.select(QTextCursor::LineUnderCursor);

        QTextCursor start = cursor;
        start.setPosition(cursor.selectionStart());
        QTextCursor end = cursor;
        end.setPosition(cursor.selectionEnd());
        return request([&] {
            m_host->editor().requestCodeActions(
                interface()->filePath(),
                {start.blockNumber(), start.positionInBlock()},
                {end.blockNumber(), end.positionInBlock()},
                guarded([this](const QJsonArray &actions) {
                    TextEditor::QuickFixOperations operations;
                    for (const QJsonValue &value : actions) {
                        const QJsonObject action = value.toObject();
                        operations << new AlienQuickFixOperation(action, m_host);
                    }
                    deliver(operations.isEmpty() ? nullptr
                                                 : TextEditor::GenericProposal::createProposal(
                                                       interface(), operations));
                }));
        });
    }

private:
    QPointer<ExtensionHost> m_host;
};

class AlienQuickFixProvider final : public TextEditor::IAssistProvider
{
public:
    explicit AlienQuickFixProvider(ExtensionHost *host)
        : IAssistProvider(host)
        , m_host(host)
    {}

    TextEditor::IAssistProcessor *createProcessor(const TextEditor::AssistInterface *) const final
    {
        return new AlienQuickFixProcessor(m_host);
    }

private:
    QPointer<ExtensionHost> m_host;
};

void EditorProtocol::requestCodeActions(const FilePath &filePath, const QPair<int, int> &start,
                                      const QPair<int, int> &end,
                                      const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "codeAction/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)},
                    {"startLine", start.first},
                    {"startCharacter", start.second},
                    {"endLine", end.first},
                    {"endCharacter", end.second}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// The edits of one workspace edit, applied where the file is open and on disk
// where it is not, as an edit an extension asks for itself is.
void EditorProtocol::applyChanges(const QJsonArray &changes)
{
    for (const QJsonValue &value : changes) {
        const QJsonObject change = value.toObject();
        const FilePath filePath = m_host.fromHostPath(change.value("path").toString());
        if (const Result<> done = applyTextEdits(filePath, change.value("edits").toArray());
            !done) {
            MessageManager::writeFlashing(done.error());
        }
    }
}

TextEditor::IAssistProvider *EditorProtocol::quickFixProvider()
{
    if (!m_quickFixProvider)
        m_quickFixProvider = new AlienQuickFixProvider(&m_host);
    return m_quickFixProvider;
}

void EditorProtocol::requestRenameEdits(const FilePath &filePath, int line, int character,
                                      const QString &newName,
                                      const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "rename/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)},
                    {"line", line},
                    {"character", character},
                    {"newName", newName}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// The line a result sits on, so the search pane shows the text and not just the
// place. An open document knows it best; otherwise it comes off disk.
static QString lineTextOf(const FilePath &filePath, int line)
{
    if (auto document = TextDocument::textDocumentForFilePath(filePath)) {
        const QTextBlock block = document->document()->findBlockByNumber(line);
        if (block.isValid())
            return block.text();
    }
    const Result<QByteArray> contents = filePath.fileContents();
    if (!contents)
        return {};
    const QStringList lines = QString::fromUtf8(*contents).split('\n');
    return line >= 0 && line < lines.size() ? lines.at(line) : QString();
}

void EditorProtocol::requestDocumentLinks(
    const FilePath &filePath, const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "links/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// A link an extension put on the text. Hovering asks for it so the editor can
// underline it; following it opens what it points at - a file in the editor, a
// page in the browser.
void EditorProtocol::followDocumentLink(TextEditorWidget *widget, const QTextCursor &cursor,
                                       const Utils::LinkHandler &callback, bool resolveTarget)
{
    const FilePath filePath = widget->textDocument()->filePath();
    const int line = cursor.blockNumber();
    const int character = cursor.positionInBlock();

    requestDocumentLinks(
        filePath, [this, callback, line, character, resolveTarget,
                   pointer = QPointer<TextEditorWidget>(widget)](const QJsonArray &links) {
            if (!pointer) {
                callback(Utils::Link());
                return;
            }
            for (const QJsonValue &value : links) {
                const QJsonObject link = value.toObject();
                const QJsonObject range = link.value("range").toObject();
                const QJsonObject start = range.value("start").toObject();
                const QJsonObject end = range.value("end").toObject();
                if (start.value("line").toInt() != line || end.value("line").toInt() != line)
                    continue;
                if (character < start.value("character").toInt()
                    || character > end.value("character").toInt()) {
                    continue;
                }

                const QString text = pointer->textDocument()->plainText();
                Utils::Link answer;
                answer.linkTextStart = offsetOf(text, range.value("start"));
                answer.linkTextEnd = offsetOf(text, range.value("end"));

                const QString scheme = link.value("scheme").toString();
                if (scheme == "file" || scheme.isEmpty()) {
                    answer.targetFilePath = m_host.fromHostPath(link.value("path").toString());
                } else {
                    // Nothing here can open it, so it opens where it belongs.
                    // The link still points at itself, so the editor underlines
                    // it and following it stays put.
                    if (resolveTarget)
                        QDesktopServices::openUrl(QUrl(link.value("target").toString()));
                    answer.targetFilePath = pointer->textDocument()->filePath();
                    answer.target.line = line + 1;
                    answer.target.column = start.value("character").toInt();
                }
                callback(answer);
                return;
            }
            callback(Utils::Link());
        });
}

// A link the extension put on the text comes first, the symbol under it is
// what is followed where there is none.
void EditorProtocol::followLink(TextEditorWidget *widget, const QTextCursor &cursor,
                                const Utils::LinkHandler &callback, bool resolveTarget)
{
    const QString languageId = m_host.languageIdFor(widget->textDocument()->filePath());
    const bool symbols = m_definitionLanguageIds.contains(languageId)
                         || m_declarationLanguageIds.contains(languageId);
    if (!m_linkLanguageIds.contains(languageId)) {
        if (symbols)
            followDefinition(widget, cursor, callback);
        else
            callback(Utils::Link());
        return;
    }
    followDocumentLink(widget, cursor,
                       [this, widget = QPointer(widget), cursor, callback,
                        symbols](const Utils::Link &link) {
                           if (link.hasValidTarget() || !symbols || !widget)
                               callback(link);
                           else
                               followDefinition(widget, cursor, callback);
                       },
                       resolveTarget);
}

void EditorProtocol::followDefinition(TextEditorWidget *widget, const QTextCursor &cursor,
                                      const Utils::LinkHandler &callback)
{
    const FilePath path = widget->textDocument()->filePath();
    const int line = cursor.blockNumber();
    const int character = cursor.positionInBlock();

    // Source range to highlight: the word under the cursor.
    QTextCursor wordCursor = cursor;
    wordCursor.select(QTextCursor::WordUnderCursor);

    const auto linkTo = [this, start = wordCursor.selectionStart(),
                         end = wordCursor.selectionEnd()](const QJsonArray &locations) {
        if (locations.isEmpty())
            return Utils::Link();
        const QJsonObject location = locations.first().toObject();
        const QJsonObject startPos = location.value("range").toObject().value("start").toObject();
        Utils::Link link(m_host.fromHostPath(location.value("uri").toString()),
                         startPos.value("line").toInt() + 1,
                         startPos.value("character").toInt());
        link.linkTextStart = start;
        link.linkTextEnd = end;
        return link;
    };

    requestDefinition(path, line, character,
                      [this, path, line, character, callback, linkTo](const QJsonArray &locations) {
                          // Where a symbol is declared is the nearest thing to a
                          // definition for an extension that offers only the one,
                          // and the editor has a single Follow Symbol for both.
                          if (!locations.isEmpty()
                              || !m_declarationLanguageIds.contains(m_host.languageIdFor(path))) {
                              callback(linkTo(locations));
                              return;
                          }
                          requestDeclaration(path, line, character,
                                             [callback, linkTo](const QJsonArray &declared) {
                                                 callback(linkTo(declared));
                                             });
                      });
}

void EditorProtocol::requestHighlights(const FilePath &filePath, int line, int character,
                                     const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "highlights/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)},
                    {"line", line},
                    {"character", character}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// Marks what the extension says is the same thing as what is under the cursor,
// in the colour the editor uses for its own occurrences.
void EditorProtocol::highlightOccurrences(TextEditorWidget *widget)
{
    const QTextCursor cursor = widget->textCursor();
    requestHighlights(
        widget->textDocument()->filePath(), cursor.blockNumber(), cursor.positionInBlock(),
        [this, pointer = QPointer<TextEditorWidget>(widget)](const QJsonArray &ranges) {
            if (!pointer)
                return;
            const QString text = pointer->textDocument()->plainText();
            const QTextCharFormat format = pointer->textDocument()->fontSettings()
                                               .toTextCharFormat(TextEditor::C_OCCURRENCES);
            QList<QTextEdit::ExtraSelection> selections;
            for (const QJsonValue &value : ranges) {
                const QJsonObject range = value.toObject();
                QTextCursor selection(pointer->document());
                selection.setPosition(offsetOf(text, range.value("start")));
                selection.setPosition(offsetOf(text, range.value("end")),
                                      QTextCursor::KeepAnchor);
                selections.append({selection, format});
            }
            pointer->setExtraSelections(TextEditorWidget::CodeSemanticsSelection, selections);
        });
}

bool EditorProtocol::providesSymbolsFor(const FilePath &filePath) const
{
    return m_symbolLanguageIds.contains(m_host.languageIdFor(filePath));
}

void EditorProtocol::requestDocumentSymbols(
    const FilePath &filePath, const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "symbols/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

void EditorProtocol::requestReferences(const FilePath &filePath, int line, int character,
                                     const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "references/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)},
                    {"line", line},
                    {"character", character}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// Where the extension says the word is used, in the pane Qt Creator shows every
// other search in.
void EditorProtocol::findUsages(const FilePath &filePath, int line, int character,
                               const QString &word,
                               const std::function<void(Core::SearchResult *)> &reportSearch)
{
    requestReferences(filePath, line, character, [this, word, reportSearch](
                                                    const QJsonArray &locations) {
        Core::SearchResult *search = Core::SearchResultWindow::instance()->startNewSearch(
            Tr::tr("VSIX Extension Usages:"), {}, word);
        Utils::SearchResultItems items;
        for (const QJsonValue &value : locations) {
            const QJsonObject location = value.toObject();
            const FilePath path = m_host.fromHostPath(location.value("path").toString());
            const QJsonObject range = location.value("range").toObject();
            const QJsonObject start = range.value("start").toObject();
            const QJsonObject end = range.value("end").toObject();

            Utils::SearchResultItem item;
            item.setFilePath(path);
            item.setMainRange(start.value("line").toInt() + 1,
                              start.value("character").toInt(),
                              qMax(0, end.value("character").toInt()
                                          - start.value("character").toInt()));
            item.setLineText(lineTextOf(path, start.value("line").toInt()));
            item.setUseTextEditorFont(true);
            items.append(item);
        }
        search->addResults(items, Core::SearchResult::AddOrdered);
        connect(search, &Core::SearchResult::activated, search,
                [](const Utils::SearchResultItem &item) {
                    EditorManager::openEditorAtSearchResult(item);
                });
        search->finishSearch(false);
        if (search->isInteractive())
            search->popup();
        if (reportSearch)
            reportSearch(search);
    });
}

// The new name comes from the user, and the edits from the extension. Asked
// without waiting here for the answer: this runs while the host is delivering,
// and a dialog run from there stops us reading it.
void EditorProtocol::askAndRename(const FilePath &filePath, int line, int character,
                                 const QString &oldName)
{
    auto dialog = new QInputDialog(ICore::dialogParent());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(Tr::tr("Rename Symbol"));
    dialog->setLabelText(Tr::tr("New name:"));
    dialog->setTextValue(oldName);
    connect(dialog, &QDialog::finished, this,
            [this, dialog, filePath, line, character](int result) {
                const QString newName = dialog->textValue();
                if (result != QDialog::Accepted || newName.isEmpty())
                    return;
                requestRenameEdits(filePath, line, character, newName,
                                   [this](const QJsonArray &changes) {
                                       if (changes.isEmpty()) {
                                           MessageManager::writeFlashing(
                                               Tr::tr("The extension had nothing to rename "
                                                      "here."));
                                           return;
                                       }
                                       applyChanges(changes);
                                   });
            });
    dialog->open();
}

// What an extension says the call being typed takes. Only what the hint shows:
// the signatures, which of them applies, and the argument the cursor is in.
class AlienFunctionHintModel final : public TextEditor::IFunctionHintProposalModel
{
public:
    AlienFunctionHintModel(const QStringList &signatures, int activeArgument)
        : m_signatures(signatures)
        , m_activeArgument(activeArgument)
    {}

    void reset() final {}
    int size() const final { return m_signatures.size(); }
    QString text(int index) const final
    {
        return index >= 0 && index < m_signatures.size() ? m_signatures.at(index) : QString();
    }
    int activeArgument(const QString &) const final { return m_activeArgument; }

private:
    QStringList m_signatures;
    int m_activeArgument = 0;
};

class AlienSignatureProcessor final : public AlienAssistProcessor
{
public:
    explicit AlienSignatureProcessor(ExtensionHost *host) : m_host(host) {}

    TextEditor::IAssistProposal *perform() final
    {
        if (!m_host)
            return nullptr;
        const QTextCursor cursor = interface()->cursor();
        return request([&] {
            m_host->editor().requestSignatureHelp(
                interface()->filePath(), cursor.blockNumber(), cursor.positionInBlock(),
                guarded([this, position = interface()->position()](const QJsonObject &help) {
                    const QJsonArray signatures = help.value("signatures").toArray();
                    if (signatures.isEmpty()) {
                        deliver(nullptr);
                        return;
                    }
                    QStringList labels;
                    for (const QJsonValue &value : signatures)
                        labels.append(value.toObject().value("label").toString());
                    // The one the extension says applies comes first, because the
                    // hint shows the first and lets the user page through.
                    const int active = qBound(0, help.value("activeSignature").toInt(),
                                              int(labels.size()) - 1);
                    labels.move(active, 0);
                    deliver(new TextEditor::FunctionHintProposal(
                        position,
                        TextEditor::FunctionHintProposalModelPtr(new AlienFunctionHintModel(
                            labels, help.value("activeParameter").toInt()))));
                }));
        });
    }

private:
    QPointer<ExtensionHost> m_host;
};

class AlienSignatureProvider final : public TextEditor::CompletionAssistProvider
{
public:
    explicit AlienSignatureProvider(ExtensionHost *host)
        : CompletionAssistProvider(host)
        , m_host(host)
    {}

    TextEditor::IAssistProcessor *createProcessor(const TextEditor::AssistInterface *) const final
    {
        return new AlienSignatureProcessor(m_host);
    }

    // What opens the hint: the characters the extension said it triggers on,
    // which for a call is nearly always the bracket that starts one.
    int activationCharSequenceLength() const final { return 1; }
    bool isActivationCharSequence(const QString &sequence) const final
    {
        return m_host && m_host->editor().signatureTriggers().contains(sequence);
    }

private:
    QPointer<ExtensionHost> m_host;
};

// One thing an extension said about a line. VS Code puts these above the line;
// the annotation at the end of it is where Qt Creator shows what it has to say
// about one, and it can be clicked the same way.
class AlienCodeLensMark final : public TextEditor::TextMark
{
public:
    AlienCodeLensMark(TextEditor::TextDocument *document, int line, const QString &title,
                      const QString &tooltip, const QString &command, const QJsonArray &arguments,
                      ExtensionHost *host)
        : TextEditor::TextMark(document, line + 1,
                               {Tr::tr("VSIX Extension"), Constants::TEXT_MARK_CATEGORY})
        , m_command(command)
        , m_arguments(arguments)
        , m_host(host)
    {
        setPriority(TextEditor::TextMark::LowPriority);
        setLineAnnotation(title);
        setToolTip(tooltip.isEmpty() ? Tr::tr("Runs \"%1\".").arg(command) : tooltip);
    }

    void clicked() final
    {
        if (m_host && !m_command.isEmpty())
            m_host->executeCommand(m_command, m_arguments);
    }

private:
    QString m_command;
    QJsonArray m_arguments;
    QPointer<ExtensionHost> m_host;
};

// What an extension says a variable holds, while the program stands still.
// VS Code writes this inside the line, past the code; the annotation at the
// end of the line is where Qt Creator has room for it.
class AlienInlineValueMark final : public TextEditor::TextMark
{
public:
    AlienInlineValueMark(TextEditor::TextDocument *document, int line, const QString &text)
        : TextEditor::TextMark(document, line + 1,
                               {Tr::tr("VSIX Extension"), Constants::TEXT_MARK_CATEGORY})
    {
        setPriority(TextEditor::TextMark::LowPriority);
        setLineAnnotation(text);
        setToolTip(text);
    }
};

// A comment thread on a line: a review remark, a merge request's discussion.
// VS Code opens a widget inside the editor for it. There is no such thing here,
// so the line is marked and the thread is what the mark says about it.
class AlienCommentMark final : public TextEditor::TextMark
{
public:
    AlienCommentMark(TextEditor::TextDocument *document, int line, const QString &annotation,
                     const QString &thread)
        : TextEditor::TextMark(document, line + 1,
                               {Tr::tr("VSIX Extension"), Constants::TEXT_MARK_CATEGORY})
    {
        setPriority(TextEditor::TextMark::NormalPriority);
        setLineAnnotation(annotation);
        setToolTip(thread);
        setColor(Utils::Theme::CodeModel_Info_TextMarkColor);
    }
};

// A colour an extension found in a line. VS Code paints a swatch in front of
// the literal; Qt Creator has no room inside a line, so it goes in the margin
// where the editor already marks lines, in the colour that was reported.
class AlienColorMark final : public TextEditor::TextMark
{
public:
    AlienColorMark(TextEditor::TextDocument *document, int line, const QColor &color)
        : TextEditor::TextMark(document, line + 1,
                               {Tr::tr("VSIX Extension"), Constants::TEXT_MARK_CATEGORY})
        , m_color(color)
    {
        setPriority(TextEditor::TextMark::LowPriority);
        setIcon(swatch(color));
        setToolTip(color.name(QColor::HexArgb));
    }

    QColor annotationColor() const final { return m_color; }

private:
    static QIcon swatch(const QColor &color)
    {
        const int side = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        QPixmap pixmap(side, side);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(color);
        painter.setPen(Utils::creatorColor(Utils::Theme::TextColorDisabled));
        painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
        return QIcon(pixmap);
    }

    QColor m_color;
};

// What the extension would rewrite now that this character has been typed -
// closing a brace, ending a statement. Qt Creator does the same for languages
// it knows itself; this is that, for a language only the extension knows.
void EditorProtocol::requestOnTypeFormatting(TextDocument *document, int line, int character,
                                            const QString &typed)
{
    if (!connection() || !document)
        return;
    const FilePath filePath = document->filePath();
    // The extension formats the text it knows, which has to include the
    // character just typed.
    sendPendingChange(document, filePath);
    const TextEditor::TabSettingsData tabs = document->tabSettings();
    const int revision = document->document()->revision();
    connection()->sendRequest(
        "onTypeFormatting/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)},
                    {"line", line},
                    {"character", character},
                    {"ch", typed},
                    {"tabSize", tabs.m_indentSize},
                    {"insertSpaces",
                     tabs.m_tabPolicy == TextEditor::TabSettingsData::SpacesOnlyTabPolicy}},
        [filePath, revision](const QJsonValue &result, const QString &error) {
            const QJsonArray edits = result.toArray();
            if (!error.isEmpty() || edits.isEmpty())
                return;
            TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
            // Typed on meanwhile: the ranges are for the text that was sent.
            if (!document || document->document()->revision() != revision)
                return;
            QTextDocument *contents = document->document();
            const QString text = contents->toPlainText();
            QTextCursor cursor(contents);
            // One edit block, so the whole reformatting is one undo step - as
            // it is when the editor indents a line itself.
            cursor.beginEditBlock();
            for (const QJsonObject &edit : editsBackToFront(text, edits)) {
                const QJsonObject range = edit.value("range").toObject();
                cursor.setPosition(offsetOf(text, range.value("start")));
                cursor.setPosition(offsetOf(text, range.value("end")), QTextCursor::KeepAnchor);
                cursor.insertText(edit.value("newText").toString());
            }
            cursor.endEditBlock();
        });
}

// A language nobody provides for any more: whatever an extension put on those
// documents comes off, rather than waiting for the next edit to notice.
void EditorProtocol::releaseLanguageFeatures(const QString &languageId)
{
    for (IEditor *editor : DocumentModel::editorsForOpenedDocuments()) {
        TextEditorWidget *widget = TextEditorWidget::fromEditor(editor);
        if (!widget)
            continue;
        TextDocument *document = widget->textDocument();
        const FilePath path = document->filePath();
        if (m_host.languageIdFor(path) != languageId)
            continue;

        if (!m_foldingLanguageIds.contains(languageId) && m_foldingDocuments.contains(document))
            stopProvidingFolding(document);
        if (!m_semanticLanguageIds.contains(languageId) && document->syntaxHighlighter()) {
            TextEditor::SemanticHighlighter::setExtraAdditionalFormats(
                document->syntaxHighlighter(), {}, {});
        }
        if (!m_colorLanguageIds.contains(languageId))
            qDeleteAll(m_colors.take(path));
        if (!m_inlineLanguageIds.contains(languageId))
            widget->clearSuggestion();
    }
    releaseProviders(languageId);
}

// Puts back what our providers replaced, on the documents of a language that
// lost its provider, or on all of them without a language. A formatter cannot
// be put back: the document deleted the one it had when it took ours.
void EditorProtocol::releaseProviders(const std::optional<QString> &languageId)
{
    const auto released = [this, &languageId](TextDocument *document,
                                              const QList<const QSet<QString> *> &providing) {
        if (!document)
            return true;
        if (!languageId)
            return true;
        if (m_host.languageIdFor(document->filePath()) != *languageId)
            return false;
        return Utils::allOf(providing, [&languageId](const QSet<QString> *languageIds) {
            return !languageIds->contains(*languageId);
        });
    };

    m_completionDocuments.removeIf([&](const auto &entry) {
        TextDocument *document = entry.first;
        if (!released(document, {&m_completionLanguageIds}))
            return false;
        if (document && document->completionAssistProvider() == m_completionProvider)
            document->setCompletionAssistProvider(entry.second);
        return true;
    });
    m_quickFixDocuments.removeIf([&](const auto &entry) {
        TextDocument *document = entry.first;
        if (!released(document, {&m_codeActionLanguageIds}))
            return false;
        if (document && document->quickFixAssistProvider() == m_quickFixProvider)
            document->setQuickFixAssistProvider(entry.second);
        return true;
    });
    m_formattedDocuments.removeIf([&](const auto &entry) {
        TextDocument *document = entry.first;
        if (!released(document, {&m_formattingLanguageIds, &m_rangeFormattingLanguageIds}))
            return false;
        if (document && document->formatter() == entry.second)
            document->setFormatter(nullptr);
        return true;
    });
}

void EditorProtocol::refreshInlineCompletion(TextEditorWidget *widget)
{
    if (!connection() || !widget)
        return;
    const FilePath filePath = widget->textDocument()->filePath();
    if (!m_inlineLanguageIds.contains(m_host.languageIdFor(filePath)))
        return;
    const QTextCursor cursor = widget->textCursor();
    const int line = cursor.blockNumber();
    const int character = cursor.positionInBlock();
    connection()->sendRequest(
        "inlineCompletion/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)}, {"line", line}, {"character", character}},
        [this, widget = QPointer<TextEditorWidget>(widget)](const QJsonValue &result,
                                                            const QString &error) {
            if (!widget || !error.isEmpty())
                return;
            QList<TextEditor::TextSuggestion::Data> suggestions;
            for (const QJsonValue &value : result.toArray()) {
                const QJsonObject item = value.toObject();
                const QJsonObject range = item.value("range").toObject();
                const QJsonObject start = range.value("start").toObject();
                const QJsonObject end = range.value("end").toObject();
                // Qt Creator counts lines from one where the protocol counts
                // from zero; the column is the same on both sides.
                const Utils::Text::Position from{start.value("line").toInt() + 1,
                                                 start.value("character").toInt()};
                const Utils::Text::Position to{end.value("line").toInt() + 1,
                                               end.value("character").toInt()};
                const Utils::Text::Position at{item.value("line").toInt() + 1,
                                               item.value("character").toInt()};
                suggestions.append({Utils::Text::Range{from, to}, at,
                                    item.value("text").toString()});
            }
            if (suggestions.isEmpty())
                return;
            widget->insertSuggestion(std::make_unique<TextEditor::CyclicSuggestion>(
                suggestions, widget->document()));
        });
}

// What the server calls each piece of the text, in the colours Qt Creator
// already uses for those things - so an extension's highlighting looks like
// the editor rather than like the extension.
static TextEditor::TextStyle styleForTokenType(const QString &type)
{
    static const QHash<QString, TextEditor::TextStyle> styles = {
        {"namespace", TextEditor::C_TYPE}, {"type", TextEditor::C_TYPE},
        {"class", TextEditor::C_TYPE}, {"enum", TextEditor::C_TYPE},
        {"interface", TextEditor::C_TYPE}, {"struct", TextEditor::C_TYPE},
        {"typeParameter", TextEditor::C_TYPE}, {"decorator", TextEditor::C_TYPE},
        {"parameter", TextEditor::C_PARAMETER},
        {"variable", TextEditor::C_LOCAL},
        {"property", TextEditor::C_FIELD}, {"enumMember", TextEditor::C_FIELD},
        {"event", TextEditor::C_FIELD},
        {"function", TextEditor::C_FUNCTION}, {"method", TextEditor::C_FUNCTION},
        {"macro", TextEditor::C_PREPROCESSOR},
        {"keyword", TextEditor::C_KEYWORD}, {"modifier", TextEditor::C_KEYWORD},
        {"comment", TextEditor::C_COMMENT},
        {"string", TextEditor::C_STRING}, {"regexp", TextEditor::C_STRING},
        {"number", TextEditor::C_NUMBER},
        {"operator", TextEditor::C_OPERATOR},
    };
    return styles.value(type, TextEditor::C_TEXT);
}

void EditorProtocol::refreshSemanticTokens(const FilePath &filePath)
{
    if (!connection() || !m_semanticLanguageIds.contains(m_host.languageIdFor(filePath)))
        return;
    connection()->sendRequest(
        "semanticTokens/provide", QJsonObject{{"uri", m_host.toHostPath(filePath)}},
        [this, filePath](const QJsonValue &result, const QString &error) {
            if (!error.isEmpty())
                return;
            TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
            if (!document || !document->syntaxHighlighter())
                return;
            const TextEditor::FontSettingsData fonts = TextEditor::globalFontSettings().data();
            QHash<int, QTextCharFormat> formats;
            QHash<QString, int> kinds; // one kind per style-and-modifiers combination
            TextEditor::HighlightingResults results;
            for (const QJsonValue &value : result.toArray()) {
                const QJsonObject token = value.toObject();
                TextEditor::TextStyles styles;
                styles.mainStyle = styleForTokenType(
                    m_tokenTypeAliases.value(token.value("type").toString(),
                                             token.value("type").toString()));
                styles.mixinStyles.initializeElements();
                QStringList used;
                for (const QJsonValue &modifier : token.value("modifiers").toArray()) {
                    const QString name = modifier.toString();
                    // Only what this editor draws differently: the rest of the
                    // protocol's modifiers have nothing to show for them here.
                    if (name == "declaration")
                        styles.mixinStyles.push_back(TextEditor::C_DECLARATION);
                    else if (name == "definition")
                        styles.mixinStyles.push_back(TextEditor::C_FUNCTION_DEFINITION);
                    else if (name == "static")
                        styles.mixinStyles.push_back(TextEditor::C_STATIC_MEMBER);
                    else
                        continue;
                    used.append(name);
                }
                const QString key = QString::number(int(styles.mainStyle)) + used.join(',');
                const auto it = kinds.constFind(key);
                const int kind = it != kinds.constEnd() ? *it : kinds.size();
                if (it == kinds.constEnd()) {
                    kinds.insert(key, kind);
                    formats.insert(kind, fonts.toTextCharFormat(styles));
                }
                // The results are 1-based where the protocol counts from zero.
                results.append(TextEditor::HighlightingResult(token.value("line").toInt() + 1,
                                                              token.value("character").toInt() + 1,
                                                              token.value("length").toInt(),
                                                              kind));
            }
            TextEditor::SemanticHighlighter::setExtraAdditionalFormats(
                document->syntaxHighlighter(), results, formats);
        });
}

// Where the text folds, as the extension's own server sees it. Qt Creator
// otherwise folds a language it does not know by indentation, which is a
// guess; this replaces the guess for as long as an extension answers.
void EditorProtocol::refreshFolding(const FilePath &filePath)
{
    if (!connection() || !m_foldingLanguageIds.contains(m_host.languageIdFor(filePath)))
        return;
    connection()->sendRequest(
        "folding/provide", QJsonObject{{"uri", m_host.toHostPath(filePath)}},
        [filePath](const QJsonValue &result, const QString &error) {
            TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
            if (!document)
                return;
            if (!error.isEmpty()) {
                document->setFoldingIndentExternallyProvided(false);
                return;
            }
            const QJsonArray ranges = result.toArray();
            document->setFoldingIndentExternallyProvided(!ranges.isEmpty());
            QTextDocument *contents = document->document();
            for (QTextBlock block = contents->begin(); block != contents->end();
                 block = block.next()) {
                TextEditor::TextBlockUserData::setFoldingIndent(block, 0);
                TextEditor::TextBlockUserData::setFoldingStartIncluded(block, false);
                TextEditor::TextBlockUserData::setFoldingEndIncluded(block, false);
            }
            for (const QJsonValue &value : ranges) {
                const QJsonObject range = value.toObject();
                // The fold begins on the line after the one that opens it, the
                // way Qt Creator counts a foldable region.
                const QTextBlock start = contents->findBlockByNumber(
                    range.value("start").toInt() + 1);
                const QTextBlock end = contents->findBlockByNumber(range.value("end").toInt() + 1);
                for (QTextBlock block = start; block.isValid() && block != end;
                     block = block.next()) {
                    TextEditor::TextBlockUserData::changeFoldingIndent(block, 1);
                }
            }
        });
}

void EditorProtocol::refreshColors(const FilePath &filePath)
{
    if (!connection() || !m_colorLanguageIds.contains(m_host.languageIdFor(filePath)))
        return;
    connection()->sendRequest(
        "color/provide", QJsonObject{{"uri", m_host.toHostPath(filePath)}},
        [this, filePath](const QJsonValue &result, const QString &error) {
            qDeleteAll(m_colors.take(filePath));
            if (!error.isEmpty())
                return;
            TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
            if (!document)
                return;
            QList<TextEditor::TextMark *> marks;
            for (const QJsonValue &value : result.toArray()) {
                const QJsonObject item = value.toObject();
                // The API reports each channel from 0 to 1.
                const QColor color = QColor::fromRgbF(item.value("red").toDouble(),
                                                      item.value("green").toDouble(),
                                                      item.value("blue").toDouble(),
                                                      item.value("alpha").toDouble(1.0));
                marks.append(new AlienColorMark(document, item.value("line").toInt(), color));
            }
            if (marks.isEmpty())
                return;
            m_colors.insert(filePath, marks);
        });
}

void EditorProtocol::refreshCodeLenses(const FilePath &filePath)
{
    if (!connection() || !m_codeLensLanguageIds.contains(m_host.languageIdFor(filePath)))
        return;
    connection()->sendRequest(
        "codeLens/provide", QJsonObject{{"uri", m_host.toHostPath(filePath)}},
        [this, filePath](const QJsonValue &result, const QString &error) {
            qDeleteAll(m_codeLenses.take(filePath));
            if (!error.isEmpty())
                return;
            TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
            if (!document)
                return;
            QList<TextEditor::TextMark *> marks;
            for (const QJsonValue &value : result.toArray()) {
                const QJsonObject lens = value.toObject();
                marks.append(new AlienCodeLensMark(document,
                                                   lens.value("line").toInt(),
                                                   lens.value("title").toString(),
                                                   lens.value("tooltip").toString(),
                                                   lens.value("command").toString(),
                                                   lens.value("arguments").toArray(),
                                                   &m_host));
            }
            if (marks.isEmpty())
                return;
            m_codeLenses.insert(filePath, marks);
        });
}

bool EditorProtocol::provideToolTipExpression(const FilePath &file, int line, int column,
                                             const std::function<void(const QString &)> &answer)
{
    if (!connection() || !m_evaluatableExpressionLanguageIds.contains(m_host.languageIdFor(file)))
        return false;
    // The debugger counts the line from one and, having just added one to it,
    // the column too. The extension counts both from zero.
    requestEvaluatableExpression(file, line - 1, column - 1, answer);
    return true;
}

// Where the user is looking, which is what the extension is asked about: a
// value computed for a line nobody can see costs a round trip for nothing.
static TextEditor::TextEditorWidget *widgetShowing(TextDocument *document)
{
    for (IEditor *editor : DocumentModel::editorsForDocument(document)) {
        if (auto textEditor = qobject_cast<BaseTextEditor *>(editor))
            return textEditor->editorWidget();
    }
    return nullptr;
}

void EditorProtocol::showInlineValues(int sessionId, int frameId, const FilePath &file, int line)
{
    const int stop = ++m_inlineValueStop;
    for (const QList<TextMark *> &marks : std::as_const(m_inlineValues))
        qDeleteAll(marks);
    m_inlineValues.clear();
    if (frameId < 0 || !connection())
        return;
    if (!m_inlineValueLanguageIds.contains(m_host.languageIdFor(file)))
        return;
    TextDocument *document = TextDocument::textDocumentForFilePath(file);
    if (!document)
        return;
    TextEditor::TextEditorWidget *widget = widgetShowing(document);
    if (!widget)
        return;
    const int firstLine = widget->firstVisibleBlockNumber();
    const int lastLine = widget->lastVisibleBlockNumber();

    const auto withVariables = [this, sessionId, frameId, file, line, firstLine, lastLine,
                                stop](const QHash<QString, QString> &variables) {
        requestInlineValues(file, firstLine, lastLine, frameId, line,
                            [this, sessionId, frameId, file, variables,
                             stop](const QJsonArray &values) {
            const auto annotations = std::make_shared<QHash<int, QStringList>>();
            // One more than the answers waited for, so that the marks are only
            // made once every evaluation has been asked for.
            const auto pending = std::make_shared<int>(1);
            const auto show = [host = QPointer<EditorProtocol>(this), file, annotations, pending,
                               stop] {
                // The program went on since.
                if (--*pending != 0 || !host || host->m_inlineValueStop != stop)
                    return;
                TextDocument *document = TextDocument::textDocumentForFilePath(file);
                if (!document)
                    return;
                QList<TextMark *> marks;
                for (auto it = annotations->cbegin(); it != annotations->cend(); ++it)
                    marks.append(new AlienInlineValueMark(document, it.key(), it.value().join(", ")));
                if (!marks.isEmpty()) {
                    qDeleteAll(host->m_inlineValues.take(file));
                    host->m_inlineValues.insert(file, marks);
                }
            };
            const std::shared_ptr<Debugger::Internal::DapSessionChannel> channel
                = m_host.run().debugChannel(sessionId);
            for (const QJsonValue &entry : values) {
                const QJsonObject value = entry.toObject();
                const int valueLine = value.value("line").toInt();
                const QString kind = value.value("kind").toString();
                const QString text = value.value("text").toString();
                if (text.isEmpty())
                    continue;
                if (kind == "text") {
                    (*annotations)[valueLine].append(text);
                } else if (kind == "variable") {
                    QString found = variables.value(text);
                    // A case-insensitive lookup is what a language that does
                    // not tell "Count" from "count" asks for.
                    if (found.isEmpty() && !value.value("caseSensitive").toBool(true)) {
                        for (auto it = variables.cbegin(); it != variables.cend(); ++it) {
                            if (it.key().compare(text, Qt::CaseInsensitive) == 0) {
                                found = it.value();
                                break;
                            }
                        }
                    }
                    if (!found.isEmpty())
                        (*annotations)[valueLine].append(text + " = " + found);
                } else if (channel && channel->send) {
                    ++*pending;
                    channel->send("evaluate",
                                  QJsonObject{{"expression", text},
                                              {"frameId", frameId},
                                              {"context", "watch"}},
                                  [annotations, valueLine, text, show](
                                      const Result<QJsonObject> &body) {
                                      const QString result
                                          = body ? body->value("result").toString() : QString();
                                      if (!result.isEmpty())
                                          (*annotations)[valueLine].append(text + " = " + result);
                                      show();
                                  });
                }
            }
            show();
        });
    };
    m_host.run().requestFrameVariables(sessionId, frameId, withVariables);
}

void EditorProtocol::requestComments(const FilePath &filePath,
                                    const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "comments/provide", QJsonObject{{"uri", m_host.toHostPath(filePath)}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject().value("threads").toArray()
                                     : QJsonArray());
        });
}

void EditorProtocol::refreshComments(const FilePath &filePath)
{
    if (!m_hasCommentControllers || filePath.isEmpty())
        return;
    requestComments(filePath, [this, filePath](const QJsonArray &threads) {
        qDeleteAll(m_comments.take(filePath));
        TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
        if (!document)
            return;
        QList<TextEditor::TextMark *> marks;
        for (const QJsonValue &value : threads) {
            const QJsonObject thread = value.toObject();
            const QJsonArray comments = thread.value("comments").toArray();
            QStringList lines;
            for (const QJsonValue &entry : comments) {
                const QJsonObject comment = entry.toObject();
                const QString author = comment.value("author").toString();
                const QString body = comment.value("body").toString();
                lines.append(author.isEmpty() ? body : QString("%1: %2").arg(author, body));
            }
            const QString label = thread.value("label").toString();
            const QString annotation
                = label.isEmpty()
                      ? Tr::tr("%n comment(s)", nullptr, int(comments.size()))
                      : label;
            marks.append(new AlienCommentMark(document, thread.value("line").toInt(),
                                              annotation, lines.join('\n')));
        }
        if (marks.isEmpty())
            return;
        m_comments.insert(filePath, marks);
    });
}

void EditorProtocol::requestWorkspaceSymbols(
    const QString &query, const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection() || !m_hasWorkspaceSymbols) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "workspaceSymbols/provide", QJsonObject{{"query", query}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

void EditorProtocol::requestSignatureHelp(
    const FilePath &filePath, int line, int character,
    const std::function<void(const QJsonObject &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "signature/provide",
        QJsonObject{{"uri", m_host.toHostPath(filePath)}, {"line", line}, {"character", character}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject() : QJsonObject());
        });
}

TextEditor::CompletionAssistProvider *EditorProtocol::signatureProvider()
{
    if (!m_signatureProvider)
        m_signatureProvider = new AlienSignatureProvider(&m_host);
    return m_signatureProvider;
}

AlienHoverHandler *EditorProtocol::hoverHandler()
{
    if (!m_hoverHandler)
        m_hoverHandler = new AlienHoverHandler(&m_host);
    return m_hoverHandler;
}

void EditorProtocol::ensureEditorFeatures()
{
    if (m_editorFeaturesStarted)
        return;
    m_editorFeaturesStarted = true;

    connect(EditorManager::instance(), &EditorManager::editorOpened,
            this, &EditorProtocol::attachEditorFeatures);
    // Asked for when the selection changes rather than when the menu opens:
    // the answer comes from the host, and a menu is built the moment it is
    // shown.
    connect(ProjectExplorer::ProjectTree::instance(),
            &ProjectExplorer::ProjectTree::currentNodeChanged,
            this, &EditorProtocol::updateProjectTreeActions);
    for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
        attachEditorFeatures(editor);
}

void EditorProtocol::updateEditorTitleActions(TextEditorWidget *widget)
{
    if (!m_editorTitleActions.contains(widget)) {
        connect(widget, &QObject::destroyed, this,
                [this, widget] { m_editorTitleActions.remove(widget); });
    }
    QHash<QString, QAction *> &actions = m_editorTitleActions[widget];
    for (QAction *action : std::as_const(actions))
        action->setVisible(false);

    for (const auto &[command, title] : std::as_const(m_editorTitleItems)) {
        QAction *&action = actions[command];
        if (!action) {
            action = new QAction(title, widget);
            connect(action, &QAction::triggered, this,
                    [this, command] { m_host.executeCommand(command); });
            widget->insertExtraToolBarAction(TextEditorWidget::Right, action);
        }
        action->setText(title);
        action->setVisible(true);
    }
}

// Whether the editor has the keyboard, and whether what is in it can be
// changed: both gate what an extension contributes to the editor.
void EditorProtocol::reportEditorFocus()
{
    if (!connection())
        return;
    TextEditorWidget *widget = TextEditorWidget::currentTextEditorWidget();
    const bool focused = widget && widget->hasFocus();
    IDocument *document = EditorManager::currentDocument();
    const bool readOnly = document && document->isFileReadOnly();
    if (focused == m_editorFocused && readOnly == m_editorReadOnly)
        return;
    m_editorFocused = focused;
    m_editorReadOnly = readOnly;
    connection()->sendNotification("editor/didChangeFocus",
                                   QJsonObject{{"focused", focused}, {"readOnly", readOnly}});
}

void EditorProtocol::updateEditorContextActions()
{
    ActionContainer *container
        = ActionManager::actionContainer(TextEditor::Constants::M_STANDARDCONTEXTMENU);
    if (!container)
        return;

    for (QAction *action : std::as_const(m_editorContextActions))
        action->setVisible(false);

    for (const auto &[command, title] : std::as_const(m_editorContextItems)) {
        QAction *&action = m_editorContextActions[command];
        if (!action) {
            action = new QAction(title, this);
            connect(action, &QAction::triggered, this,
                    [this, command] { m_host.executeCommand(command); });
            container->addAction(ActionManager::registerAction(
                action, Id("Alien.EditorContext").withSuffix(QString('.' + command))));
        }
        action->setText(title);
        action->setVisible(true);
    }
}

// What an extension offers for the file picked in the project tree. The clause
// is about that file, so the answer has to be asked for per node; it is asked
// for when the selection changes, which is before the menu is opened.
void EditorProtocol::updateProjectTreeActions(ProjectExplorer::Node *node)
{
    ActionContainer *fileMenu
        = ActionManager::actionContainer(ProjectExplorer::Constants::M_FILECONTEXT);
    ActionContainer *folderMenu
        = ActionManager::actionContainer(ProjectExplorer::Constants::M_FOLDERCONTEXT);
    if (!fileMenu || !folderMenu)
        return;

    for (QAction *action : std::as_const(m_projectTreeActions))
        action->setVisible(false);
    const int request = ++m_projectTreeRequest;
    if (!connection() || !node)
        return;

    const FilePath path = node->filePath();
    const bool isFolder = node->asFolderNode() != nullptr;
    if (!m_host.isOnHostDevice(path))
        return;

    requestResourceMenu(
        "explorer/context", path, isFolder,
        [this, request, isFolder, path, fileMenu, folderMenu](const QJsonArray &items) {
            if (request != m_projectTreeRequest)
                return;
            ActionContainer *container = isFolder ? folderMenu : fileMenu;
            for (const QJsonValue &value : items) {
                const QJsonObject item = value.toObject();
                const QString command = item.value("command").toString();
                // A command offered for both a file and a folder needs an
                // action in each container; one action cannot be in two.
                const QString key = (isFolder ? "folder|" : "file|") + command;
                QAction *&action = m_projectTreeActions[key];
                if (!action) {
                    action = new QAction(item.value("title").toString(), this);
                    connect(action, &QAction::triggered, this, [this, command] {
                        // The command is about the file it was offered for.
                        m_host.executeCommand(command,
                                       QJsonArray{QJsonObject{{"$uri", m_projectTreeResource}}});
                    });
                    container->addAction(ActionManager::registerAction(
                        action, Id("Alien.ProjectTree").withSuffix(QString('.' + key)),
                        Core::Context(ProjectExplorer::Constants::C_PROJECT_TREE)));
                }
                action->setText(item.value("title").toString());
                action->setVisible(true);
            }
            m_projectTreeResource = m_host.toHostPath(path);
        });
}

void EditorProtocol::requestResourceMenu(const QString &location, const FilePath &path,
                                        bool isFolder,
                                        const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "menus/forResource",
        QJsonObject{{"location", location},
                    {"path", m_host.toHostPath(path)},
                    {"isFolder", isFolder},
                    {"languageId", m_host.languageIdFor(path)}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject().value("items").toArray() : QJsonArray());
        });
}

// The pairs the extension named, rather than the ones a C-like language has.
class AlienAutoCompleter final : public TextEditor::AutoCompleter
{
public:
    AlienAutoCompleter(const QList<QPair<QString, QString>> &closing,
                       const QList<QPair<QString, QString>> &surrounding,
                       const std::function<bool()> &isDeclared)
        : m_closing(closing)
        , m_surrounding(surrounding)
        , m_isDeclared(isDeclared)
    {}

    // Once the language has lost its extension, this does as little as the
    // completer it replaced.
    bool contextAllowsAutoBrackets(const QTextCursor &, const QString &text) const final
    {
        return m_isDeclared() && Utils::anyOf(m_closing, [&text](const auto &pair) {
            return text == pair.first || text == pair.second;
        });
    }

    bool contextAllowsAutoQuotes(const QTextCursor &, const QString &text) const final
    {
        return m_isDeclared() && Utils::anyOf(m_surrounding, [&text](const auto &pair) {
            return text == pair.first && pair.first == pair.second;
        });
    }

    QString insertMatchingBrace(const QTextCursor &, const QString &text, QChar lookAhead,
                                bool skipChars, int *skippedChars) const final
    {
        return complete(m_closing, text, lookAhead, skipChars, skippedChars);
    }

    QString insertMatchingQuote(const QTextCursor &, const QString &text, QChar lookAhead,
                                bool skipChars, int *skippedChars) const final
    {
        return complete(m_surrounding, text, lookAhead, skipChars, skippedChars);
    }

private:
    // A closing character typed over the same one is not inserted twice.
    static QString complete(const QList<QPair<QString, QString>> &pairs, const QString &text,
                            QChar lookAhead, bool skipChars, int *skippedChars)
    {
        if (skipChars && text == lookAhead
            && Utils::anyOf(pairs, [&text](const auto &pair) { return text == pair.second; })) {
            ++*skippedChars;
            return {};
        }
        for (const auto &[open, close] : pairs) {
            if (text == open)
                return close;
        }
        return {};
    }

    QList<QPair<QString, QString>> m_closing;
    QList<QPair<QString, QString>> m_surrounding;
    std::function<bool()> m_isDeclared;
};

// How the extension says the language is written, given to the editor: what a
// comment looks like, which characters come in pairs, and what marks a region.
void EditorProtocol::applyLanguageConfiguration(TextEditorWidget *widget)
{
    TextDocument *document = widget->textDocument();
    const QString languageId = m_host.languageIdFor(document->filePath());
    const VscodeLanguage *language = nullptr;
    for (const auto &[extensionId, candidate] : m_host.m_declaredLanguages) {
        if (candidate.id == languageId) {
            language = &candidate;
            break;
        }
    }
    if (!language)
        return;

    const QList<QPair<QString, QString>> pairs = language->autoClosingPairs.isEmpty()
                                                     ? language->brackets
                                                     : language->autoClosingPairs;
    // Only where the editor has no completer of its own: one that knows the
    // language does more than a list of pairs can.
    if ((!pairs.isEmpty() || !language->surroundingPairs.isEmpty())
        && document->id() == Core::Constants::K_DEFAULT_TEXT_EDITOR_ID) {
        const auto isDeclared = [host = QPointer<ExtensionHost>(&m_host), languageId] {
            return host && Utils::anyOf(host->m_declaredLanguages, [&](const auto &declared) {
                return declared.second.id == languageId;
            });
        };
        widget->setAutoCompleter(
            new AlienAutoCompleter(pairs, language->surroundingPairs, isDeclared));
        widget->updateCompletionSettings();
    }

    if (!language->foldingStartMarker.isEmpty() && !language->foldingEndMarker.isEmpty()) {
        if (!m_markerFoldedDocuments.contains(document)) {
            m_markerFoldedDocuments.insert(document);
            // Every line is matched again, which is too much for every keystroke.
            auto timer = new QTimer(document);
            timer->setSingleShot(true);
            timer->setInterval(300);
            connect(timer, &QTimer::timeout, this,
                    [this, document] { refreshMarkerFolding(document->filePath()); });
            connect(document, &TextDocument::contentsChanged, timer,
                    qOverload<>(&QTimer::start));
        }
        refreshMarkerFolding(document->filePath());
    }
}

// Regions the extension marks off by hand, which fold like any other block.
void EditorProtocol::refreshMarkerFolding(const FilePath &filePath)
{
    TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
    if (!document)
        return;
    const QString languageId = m_host.languageIdFor(filePath);
    const VscodeLanguage *language = nullptr;
    for (const auto &[extensionId, candidate] : m_host.m_declaredLanguages) {
        if (candidate.id == languageId) {
            language = &candidate;
            break;
        }
    }
    if (!language || language->foldingStartMarker.isEmpty()) {
        // Nobody marks the regions any longer, the editor's own folding is back.
        if (m_markerFoldedDocuments.contains(document) && !m_foldingDocuments.contains(document)
            && document->isFoldingIndentExternallyProvided()) {
            stopProvidingFolding(document);
        }
        return;
    }

    const QRegularExpression start(language->foldingStartMarker);
    const QRegularExpression end(language->foldingEndMarker);
    int depth = 0;
    for (QTextBlock block = document->document()->begin(); block.isValid();
         block = block.next()) {
        const QString text = block.text();
        const bool opens = start.match(text).hasMatch();
        const bool closes = !opens && end.match(text).hasMatch();
        if (closes && depth > 0)
            --depth;
        TextEditor::TextBlockUserData::setFoldingIndent(block, depth);
        if (opens)
            ++depth;
    }
    document->setFoldingIndentExternallyProvided(true);
}

// The features are attached again whenever an extension registers a provider,
// and a request answered twice would open two rename dialogs.
bool EditorProtocol::firstRequestConnect(TextEditorWidget *widget, const char *request)
{
    QSet<QByteArray> &connected = m_connectedRequests[widget];
    if (connected.isEmpty()) {
        connect(widget, &QObject::destroyed, this,
                [this, widget] { m_connectedRequests.remove(widget); });
    }
    if (connected.contains(request))
        return false;
    connected.insert(request);
    return true;
}

void EditorProtocol::attachEditorFeatures(IEditor *editor)
{
    TextEditorWidget *widget = TextEditorWidget::fromEditor(editor);
    if (!widget)
        return;
    updateEditorTitleActions(widget);
    const QString languageId = m_host.languageIdFor(widget->textDocument()->filePath());

    if (m_hoverLanguageIds.contains(languageId) && !m_hoverWidgets.contains(widget)) {
        widget->addHoverHandler(hoverHandler());
        m_hoverWidgets.append(widget);
    }

    if (m_signatureLanguageIds.contains(languageId)) {
        widget->textDocument()->setFunctionHintAssistProvider(signatureProvider());
        if (!m_assistDocuments.contains(widget->textDocument()))
            m_assistDocuments.append(widget->textDocument());
    }

    if (m_linkLanguageIds.contains(languageId)) {
        widget->setOptionalActions(widget->optionalActions()
                                   | TextEditor::OptionalActions::JumpToFileUnderCursor);
    }
    if (m_definitionLanguageIds.contains(languageId)
        || m_declarationLanguageIds.contains(languageId)) {
        widget->setOptionalActions(widget->optionalActions()
                                   | TextEditor::OptionalActions::FollowSymbolUnderCursor);
    }
    // One handler for links and symbols alike: the editor takes the first
    // answer, and a second one would open a second editor.
    if ((m_linkLanguageIds.contains(languageId) || m_definitionLanguageIds.contains(languageId)
         || m_declarationLanguageIds.contains(languageId))
        && firstRequestConnect(widget, "link")) {
        connect(widget, &TextEditorWidget::requestLinkAt, this,
                [this, widget](const QTextCursor &cursor, const Utils::LinkHandler &callback,
                               bool resolveTarget) {
                    followLink(widget, cursor, callback, resolveTarget);
                });
    }

    if (m_highlightLanguageIds.contains(languageId) && !m_highlightWidgets.contains(widget)) {
        m_highlightWidgets.append(widget);
        // Not on every keystroke: the cursor passes through a lot of places on
        // its way, and each one would be a round trip.
        auto timer = new QTimer(widget);
        timer->setSingleShot(true);
        timer->setInterval(300);
        connect(timer, &QTimer::timeout, this, [this, widget] {
            if (widget)
                highlightOccurrences(widget);
        });
        connect(widget, &TextEditorWidget::cursorPositionChanged, timer,
                qOverload<>(&QTimer::start));
    }

    if (m_referenceLanguageIds.contains(languageId) && firstRequestConnect(widget, "usages")) {
        widget->setOptionalActions(widget->optionalActions()
                                   | TextEditor::OptionalActions::FindUsage);
        connect(widget, &TextEditorWidget::requestUsages, this,
                [this, widget](const QTextCursor &cursor) {
                    QTextCursor word = cursor;
                    if (!word.hasSelection())
                        word.select(QTextCursor::WordUnderCursor);
                    findUsages(widget->textDocument()->filePath(), cursor.blockNumber(),
                               cursor.positionInBlock(), word.selectedText());
                });
    }

    if (m_renameLanguageIds.contains(languageId) && firstRequestConnect(widget, "rename")) {
        widget->setOptionalActions(widget->optionalActions()
                                   | TextEditor::OptionalActions::RenameSymbol);
        connect(widget, &TextEditorWidget::requestRename, this,
                [this, widget](const QTextCursor &cursor) {
                    QTextCursor word = cursor;
                    if (!word.hasSelection())
                        word.select(QTextCursor::WordUnderCursor);
                    askAndRename(widget->textDocument()->filePath(), cursor.blockNumber(),
                                 cursor.positionInBlock(), word.selectedText());
                });
    }

    if (m_codeActionLanguageIds.contains(languageId)) {
        TextDocument *document = widget->textDocument();
        IAssistProvider *previous = document->quickFixAssistProvider();
        if (previous != quickFixProvider()) {
            document->setQuickFixAssistProvider(quickFixProvider());
            m_quickFixDocuments.removeIf([document](const auto &entry) {
                return entry.first == document;
            });
            m_quickFixDocuments.append({document, previous});
        }
    }

    if (m_formattingLanguageIds.contains(languageId)
        || m_rangeFormattingLanguageIds.contains(languageId)) {
        // Ownership goes to the document, and it replaces whatever formatter
        // was there - the extension claimed this language.
        TextEditor::TextDocument *document = widget->textDocument();
        const bool ours = Utils::anyOf(m_formattedDocuments, [document](const auto &entry) {
            return entry.first == document && document->formatter() == entry.second;
        });
        if (!ours) {
            auto formatter = new AlienFormatter(&m_host, document);
            document->setFormatter(formatter);
            m_formattedDocuments.removeIf([document](const auto &entry) {
                return entry.first == document;
            });
            m_formattedDocuments.append({document, formatter});
        }
        // Range mode where a selection can be formatted on its own, which is
        // what makes Format Selection do less than Format Document.
        document->setFormatterMode(m_rangeFormattingLanguageIds.contains(languageId)
                                       ? TextEditor::Formatter::FormatMode::Range
                                       : TextEditor::Formatter::FormatMode::FullDocument);
    }

    applyLanguageConfiguration(widget);

    if (m_onTypeLanguageIds.contains(languageId)) {
        TextDocument *document = widget->textDocument();
        if (!m_onTypeDocuments.contains(document)) {
            m_onTypeDocuments.insert(document);
            connect(document, &TextDocument::contentsChangedWithPosition, this,
                    [this, document](int position, int charsRemoved, int charsAdded) {
                        // One character, freshly typed, and one this extension
                        // asked to hear about.
                        if (charsRemoved != 0 || charsAdded != 1)
                            return;
                        const QString typed = document->document()->characterAt(position);
                        if (!m_onTypeTriggers.contains(typed))
                            return;
                        const QTextBlock block = document->document()->findBlock(position);
                        requestOnTypeFormatting(document, block.blockNumber(),
                                                position - block.position() + 1, typed);
                    });
        }
    }

    if (m_inlineLanguageIds.contains(languageId) && !m_inlineWidgets.contains(widget)) {
        m_inlineWidgets.append(widget);
        // Asked for after a pause in typing, not on every keystroke: each ask
        // is a round trip, and a suggestion for text that has moved on is
        // worse than none.
        auto timer = new QTimer(widget);
        timer->setSingleShot(true);
        timer->setInterval(400);
        connect(timer, &QTimer::timeout, this, [this, widget] {
            if (widget)
                refreshInlineCompletion(widget);
        });
        connect(widget->textDocument(), &TextEditor::TextDocument::contentsChanged, timer,
                qOverload<>(&QTimer::start));
    }

    // The refreshes below are queued behind the queued "document/didChange",
    // so the extension answers for the text the user sees.
    if (m_semanticLanguageIds.contains(languageId)) {
        TextDocument *document = widget->textDocument();
        refreshSemanticTokens(document->filePath());
        if (!m_semanticDocuments.contains(document)) {
            m_semanticDocuments.insert(document);
            connect(document, &TextEditor::TextDocument::contentsChanged, this,
                    [this, document = QPointer<TextDocument>(document)] {
                        if (document) // closed while this was queued
                            refreshSemanticTokens(document->filePath());
                    },
                    Qt::QueuedConnection);
        }
    }

    if (m_foldingLanguageIds.contains(languageId)) {
        TextDocument *document = widget->textDocument();
        refreshFolding(document->filePath());
        if (!m_foldingDocuments.contains(document)) {
            m_foldingDocuments.insert(document);
            connect(document, &TextEditor::TextDocument::contentsChanged, this,
                    [this, document = QPointer<TextDocument>(document)] {
                        if (document) // closed while this was queued
                            refreshFolding(document->filePath());
                    },
                    Qt::QueuedConnection);
        }
    }

    if (m_colorLanguageIds.contains(languageId)) {
        TextDocument *document = widget->textDocument();
        refreshColors(document->filePath());
        if (!m_colorDocuments.contains(document)) {
            m_colorDocuments.insert(document);
            connect(document, &TextEditor::TextDocument::contentsChanged, this,
                    [this, document = QPointer<TextDocument>(document)] {
                        if (document) // closed while this was queued
                            refreshColors(document->filePath());
                    },
                    Qt::QueuedConnection);
        }
    }

    if (m_hasCommentControllers)
        refreshComments(widget->textDocument()->filePath());

    if (m_codeLensLanguageIds.contains(languageId)) {
        TextDocument *document = widget->textDocument();
        refreshCodeLenses(document->filePath());
        // What an extension says about a line depends on what the line says.
        // Connected once per document, since a file can be open twice.
        if (!m_codeLensDocuments.contains(document)) {
            m_codeLensDocuments.insert(document);
            connect(document, &TextEditor::TextDocument::contentsChanged, this,
                    [this, document = QPointer<TextDocument>(document)] {
                        if (document) // closed while this was queued
                            refreshCodeLenses(document->filePath());
                    },
                    Qt::QueuedConnection);
        }
    }

    if (m_typeDefinitionLanguageIds.contains(languageId) && firstRequestConnect(widget, "type")) {
        widget->setOptionalActions(widget->optionalActions()
                                   | TextEditor::OptionalActions::FollowTypeUnderCursor);
        connect(widget, &TextEditorWidget::requestTypeAt, this,
                [this, widget](const QTextCursor &cursor, const Utils::LinkHandler &callback,
                               bool resolveTarget) {
                    Q_UNUSED(resolveTarget)
                    QTextCursor word = cursor;
                    word.select(QTextCursor::WordUnderCursor);
                    requestTypeDefinition(
                        widget->textDocument()->filePath(), cursor.blockNumber(),
                        cursor.positionInBlock(),
                        [this, callback, start = word.selectionStart(),
                         end = word.selectionEnd()](const QJsonArray &locations) {
                            if (locations.isEmpty()) {
                                callback(Utils::Link());
                                return;
                            }
                            const QJsonObject location = locations.first().toObject();
                            const QJsonObject at
                                = location.value("range").toObject().value("start").toObject();
                            Utils::Link link(m_host.fromHostPath(location.value("uri").toString()),
                                             at.value("line").toInt() + 1,
                                             at.value("character").toInt());
                            link.linkTextStart = start;
                            link.linkTextEnd = end;
                            callback(link);
                        });
                });
    }
}

} // namespace Alien::Internal
