// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionhost.h"

#include "alienclient.h"
#include "aliencompletion.h"
#include "alienhover.h"
#include "alientr.h"
#include "alientreeview.h"
#include "hostconnection.h"
#include "webviewrenderer.h"

#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/idocument.h>
#include <coreplugin/messagemanager.h>

#include <languageclient/languageclientmanager.h>
#include <languageclient/languageclientsettings.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/textmark.h>

#include <utils/commandline.h>
#include <utils/link.h>
#include <utils/theme/theme.h>

#include <QTextBlock>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>

using namespace Core;
using namespace LanguageClient;
using namespace TextEditor;
using namespace Utils;

static Q_LOGGING_CATEGORY(logHost, "qtc.alien.host", QtWarningMsg)

namespace Alien::Internal {

// Translates a VS Code glob to a regular expression matched against a path
// relative to the search root. "**" spans directory separators, "*" and "?" do
// not, and "{a,b}" is an alternation.
static QRegularExpression globToRegularExpression(const QString &glob)
{
    QString pattern;
    for (int i = 0; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (c == '*') {
            const bool isDoubleStar = i + 1 < glob.size() && glob.at(i + 1) == '*';
            if (isDoubleStar && i + 2 < glob.size() && glob.at(i + 2) == '/') {
                pattern += "(?:.*/)?"; // "**/" also matches nothing at all
                i += 2;
            } else if (isDoubleStar) {
                pattern += ".*";
                ++i;
            } else {
                pattern += "[^/]*";
            }
        } else if (c == '?') {
            pattern += "[^/]";
        } else if (c == '{') {
            pattern += "(?:";
        } else if (c == '}') {
            pattern += ')';
        } else if (c == ',') {
            pattern += '|';
        } else {
            pattern += QRegularExpression::escape(c);
        }
    }
    return QRegularExpression('^' + pattern + '$');
}

static Result<> extractResource(const QString &resourcePath, const FilePath &dest)
{
    QFile resource(resourcePath);
    if (!resource.open(QIODevice::ReadOnly))
        return make_unexpected(Tr::tr("Cannot read bundled resource \"%1\".").arg(resourcePath));

    if (const Result<> dir = dest.parentDir().ensureWritableDir(); !dir)
        return dir;

    const Result<qint64> written = dest.writeFileContents(resource.readAll());
    if (!written)
        return make_unexpected(written.error());
    return {};
}

ExtensionHost::ExtensionHost(const FilePath &nodePath, QObject *parent)
    : QObject(parent)
    , m_nodePath(nodePath)
{}

ExtensionHost::~ExtensionHost()
{
    for (const QPointer<AlienClient> &client : std::as_const(m_lspClients)) {
        if (client)
            LanguageClientManager::shutdownClient(client);
    }
    for (const QList<TextMark *> &marks : std::as_const(m_diagnosticMarks))
        qDeleteAll(marks);

    // Detach our completion provider before it is destroyed with this object.
    for (const QPointer<TextDocument> &document : std::as_const(m_completionDocuments)) {
        if (document && document->completionAssistProvider() == m_completionProvider)
            document->setCompletionAssistProvider(nullptr);
    }

    // Remove our hover handler from editors before deleting it.
    for (const QPointer<TextEditorWidget> &widget : std::as_const(m_hoverWidgets)) {
        if (widget && m_hoverHandler)
            widget->removeHoverHandler(m_hoverHandler);
    }
    delete m_hoverHandler;

    qDeleteAll(m_treeFactories);

    if (!m_runtimeDir.isEmpty())
        m_runtimeDir.removeRecursively();
}

bool ExtensionHost::isRunning() const
{
    return m_connection && m_connection->isRunning();
}

Result<> ExtensionHost::ensureStarted()
{
    if (m_connection)
        return {};

    if (!m_nodePath.isExecutableFile())
        return make_unexpected(Tr::tr("Node.js was not found. Set its path in the settings."));

    // The runtime directory lives on the same device as node, so the host can
    // later be launched on a remote device without changing this code.
    const Result<FilePath> tmp = m_nodePath.tmpDir();
    if (!tmp)
        return make_unexpected(tmp.error());
    m_runtimeDir = *tmp / ("qtc-alien-" + QString::number(QCoreApplication::applicationPid()));
    if (const Result<> dir = m_runtimeDir.ensureWritableDir(); !dir)
        return dir;

    const FilePath hostJs = m_runtimeDir / "host.js";
    if (const Result<> extracted = extractResource(":/alien/host/host.js", hostJs); !extracted)
        return extracted;

    m_connection = new HostConnection(this);
    installHandlers();
    m_connection->setCommand({m_nodePath, {hostJs.toFSPathString()}});
    m_connection->setWorkingDirectory(m_runtimeDir);

    connect(m_connection, &HostConnection::started, this, [this] {
        // Push configuration and workspace folders before any extension
        // activates and reads them.
        m_connection->sendNotification("configuration/update",
                                       QJsonObject{{"config", m_configuration}});
        m_connection->sendNotification("workspace/updateFolders",
                                       QJsonObject{{"folders", m_workspaceFolders}});
        const QList<std::function<void()>> deferred = std::exchange(m_deferred, {});
        for (const std::function<void()> &action : deferred)
            action();
    });
    connect(m_connection, &HostConnection::errorOccurred, this, [](const QString &message) {
        MessageManager::writeFlashing(Tr::tr("Alien host error: %1").arg(message));
    });

    m_connection->start();
    return {};
}

void ExtensionHost::installHandlers()
{
    m_connection->setRequestHandler(
        "window/showMessage",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString level = object.value("level").toString();
            const QString message = object.value("message").toString();
            const QString prefix = level == "error" ? Tr::tr("Error")
                                   : level == "warn" ? Tr::tr("Warning")
                                                     : Tr::tr("Info");
            MessageManager::writeFlashing(QString("Alien [%1]: %2").arg(prefix, message));
            emit messageShown(message);
            // No message-box UI yet: report that no item was selected.
            respond(QJsonValue(QJsonValue::Null), {});
        });

    m_connection->setRequestHandler(
        "workspace/findFiles",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString include = object.value("include").toString();
            const QString exclude = object.value("exclude").toString();
            const int maxResults = object.value("maxResults").toInt();

            // A RelativePattern carries its own base; a plain string pattern is
            // searched under every workspace folder. Searching goes through
            // FilePath, so a remote workspace works the same way.
            FilePaths roots;
            const QString base = object.value("base").toString();
            if (!base.isEmpty()) {
                roots << FilePath::fromUserInput(base);
            } else {
                for (const QJsonValue &folder : m_workspaceFolders)
                    roots << FilePath::fromUserInput(folder.toObject().value("path").toString());
            }

            const QRegularExpression includeRe = globToRegularExpression(include);
            const QRegularExpression excludeRe
                = exclude.isEmpty() ? QRegularExpression() : globToRegularExpression(exclude);

            QJsonArray found;
            for (const FilePath &root : roots) {
                root.iterateDirectory(
                    [&](const FilePath &item) {
                        const QString relative = item.relativePathFromDir(root);
                        if (!includeRe.match(relative).hasMatch())
                            return IterationPolicy::Continue;
                        if (!exclude.isEmpty() && excludeRe.match(relative).hasMatch())
                            return IterationPolicy::Continue;
                        found.append(item.toFSPathString());
                        return maxResults > 0 && found.size() >= maxResults
                                   ? IterationPolicy::Stop : IterationPolicy::Continue;
                    },
                    FileFilter({}, DirFilterFlag::Files, DirIteratorFlag::Subdirectories));
                if (maxResults > 0 && found.size() >= maxResults)
                    break;
            }
            respond(found, {});
        });

    m_connection->setRequestHandler(
        "window/showQuickPick",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            QStringList items;
            for (const QJsonValue &item : object.value("items").toArray())
                items << item.toString();
            const int id = m_nextPromptId++;
            m_pendingPrompts.insert(id, respond);
            emit quickPickRequested(id, items, object.value("placeholder").toString());
        });

    m_connection->setRequestHandler(
        "window/showInputBox",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const int id = m_nextPromptId++;
            m_pendingPrompts.insert(id, respond);
            emit inputBoxRequested(id, object.value("prompt").toString(),
                                   object.value("value").toString(),
                                   object.value("placeholder").toString());
        });

    m_connection->setNotificationHandler("statusbar/setMessage", [this](const QJsonValue &params) {
        emit statusBarMessageChanged(params.toObject().value("text").toString());
    });

    m_connection->setNotificationHandler("statusbar/update", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit statusBarItemChanged(object.value("id").toString(),
                                  object.value("text").toString(),
                                  object.value("tooltip").toString(),
                                  object.value("alignment").toInt(),
                                  object.value("visible").toBool());
    });

    m_connection->setNotificationHandler("statusbar/remove", [this](const QJsonValue &params) {
        emit statusBarItemRemoved(params.toObject().value("id").toString());
    });

    m_connection->setNotificationHandler("treeview/register", [this](const QJsonValue &params) {
        const QString viewId = params.toObject().value("viewId").toString();
        if (!m_treeFactories.contains(viewId))
            m_treeFactories.insert(viewId, new AlienTreeViewFactory(this, viewId, viewId));
        emit treeViewRegistered(viewId);
    });

    m_connection->setNotificationHandler("treeview/refresh", [this](const QJsonValue &params) {
        emit treeViewRefreshed(params.toObject().value("viewId").toString());
    });

    m_connection->setNotificationHandler("treeview/unregister", [this](const QJsonValue &params) {
        const QString viewId = params.toObject().value("viewId").toString();
        delete m_treeFactories.take(viewId); // unregisters from the navigation pool
    });

    m_connection->setNotificationHandler("webview/create", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString id = object.value("id").toString();
        const QString viewType = object.value("viewType").toString();
        const QString title = object.value("title").toString();
        if (m_webviewRenderer)
            m_webviewRenderer->createPanel(id, viewType, title);
        emit webviewCreated(id, viewType, title);
    });

    m_connection->setNotificationHandler("webview/setHtml", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString id = object.value("id").toString();
        const QString html = object.value("html").toString();
        if (m_webviewRenderer)
            m_webviewRenderer->setHtml(id, html);
        emit webviewHtmlChanged(id, html);
    });

    m_connection->setNotificationHandler("webview/postMessage", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString id = object.value("id").toString();
        const QJsonValue message = object.value("message");
        if (m_webviewRenderer)
            m_webviewRenderer->postMessage(id, message);
        const QByteArray json = QJsonDocument(QJsonObject{{"message", message}})
                                    .toJson(QJsonDocument::Compact);
        emit webviewMessagePosted(id, QString::fromUtf8(json));
    });

    m_connection->setNotificationHandler("webview/reveal", [this](const QJsonValue &params) {
        if (m_webviewRenderer)
            m_webviewRenderer->reveal(params.toObject().value("id").toString());
    });

    m_connection->setNotificationHandler("webview/dispose", [this](const QJsonValue &params) {
        const QString id = params.toObject().value("id").toString();
        if (m_webviewRenderer)
            m_webviewRenderer->disposePanel(id);
        emit webviewDisposed(id);
    });

    m_connection->setNotificationHandler("output/append", [](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString text = QString("Alien <%1>: %2")
                                 .arg(object.value("channel").toString(),
                                      object.value("value").toString());
        MessageManager::writeSilently(text);
        qCDebug(logHost).noquote() << text; // also visible with qtc.alien.host logging
    });

    m_connection->setNotificationHandler("commands/register", [this](const QJsonValue &params) {
        const QString command = params.toObject().value("command").toString();
        if (!command.isEmpty() && !m_commands.contains(command)) {
            m_commands.append(command);
            emit commandsChanged();
        }
    });

    m_connection->setNotificationHandler("commands/unregister", [this](const QJsonValue &params) {
        const QString command = params.toObject().value("command").toString();
        if (m_commands.removeAll(command) > 0)
            emit commandsChanged();
    });

    m_connection->setRequestHandler(
        "languageclient/start",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString id = object.value("id").toString();
            const QString name = object.value("name").toString();

            const QJsonObject command = object.value("command").toObject();
            QStringList args;
            for (const QJsonValue &arg : command.value("args").toArray())
                args << arg.toString();
            const CommandLine commandLine(FilePath::fromUserInput(command.value("path").toString()),
                                          args);

            LanguageFilter filter;
            for (const QJsonValue &pattern : object.value("filePatterns").toArray())
                filter.filePattern << pattern.toString();

            const FilePath cwd = FilePath::fromUserInput(object.value("cwd").toString());
            const QJsonObject initOptions = object.value("initializationOptions").toObject();

            if (auto existing = m_lspClients.take(id))
                LanguageClientManager::shutdownClient(existing);

            m_lspClients.insert(
                id, new AlienClient(name.isEmpty() ? id : name, commandLine, filter, cwd, initOptions));
            emit languageClientStarted(id);
            respond(QJsonValue(QJsonValue::Null), {});
        });

    m_connection->setNotificationHandler("languageclient/stop", [this](const QJsonValue &params) {
        const QString id = params.toObject().value("id").toString();
        if (auto client = m_lspClients.take(id))
            LanguageClientManager::shutdownClient(client);
    });

    m_connection->setNotificationHandler("diagnostics/publish", [this](const QJsonValue &params) {
        publishDiagnostics(params);
    });

    m_connection->setNotificationHandler("completion/registerProvider",
                                         [this](const QJsonValue &params) {
        for (const QJsonValue &language : params.toObject().value("languageIds").toArray())
            m_completionLanguageIds.insert(language.toString());
        // Attach to documents that are already open.
        for (IDocument *document : DocumentModel::openedDocuments()) {
            if (auto textDocument = qobject_cast<TextDocument *>(document))
                maybeAttachCompletion(textDocument);
        }
    });

    auto registerLanguageFeature = [this](QSet<QString> &target) {
        return [this, &target](const QJsonValue &params) {
            for (const QJsonValue &language : params.toObject().value("languageIds").toArray())
                target.insert(language.toString());
            for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
                attachEditorFeatures(editor);
        };
    };
    m_connection->setNotificationHandler("hover/registerProvider",
                                         registerLanguageFeature(m_hoverLanguageIds));
    m_connection->setNotificationHandler("definition/registerProvider",
                                         registerLanguageFeature(m_definitionLanguageIds));

    m_connection->setNotificationHandler("log", [](const QJsonValue &params) {
        qCDebug(logHost).noquote() << params.toObject().value("message").toString();
    });

    m_connection->setNotificationHandler("host/ready", [](const QJsonValue &params) {
        qCDebug(logHost) << "Host ready, node" << params.toObject().value("node").toString();
    });
}

void ExtensionHost::whenReady(const std::function<void()> &action)
{
    if (isRunning())
        action();
    else
        m_deferred.append(action);
}

void ExtensionHost::activate(const VscodeManifest &manifest)
{
    if (const Result<> started = ensureStarted(); !started) {
        emit activationFailed(manifest.qualifiedId(), started.error());
        MessageManager::writeFlashing(
            Tr::tr("Cannot activate \"%1\": %2").arg(manifest.qualifiedId(), started.error()));
        return;
    }

    const QString id = manifest.qualifiedId();

    // Pass the language -> file-extension map so the host can resolve a
    // vscode-languageclient documentSelector (language ids) to file patterns,
    // and remember it here to tag synced documents with a languageId.
    QJsonArray languages;
    for (const VscodeLanguage &language : manifest.languages) {
        QJsonArray extensions;
        for (const QString &extension : language.extensions) {
            extensions.append(extension);
            const QString suffix = extension.startsWith('.') ? extension.mid(1) : extension;
            m_languageBySuffix.insert(suffix.toLower(), language.id);
        }
        languages.append(QJsonObject{{"id", language.id}, {"extensions", extensions}});
    }

    const QJsonObject params{
        {"id", id},
        {"path", manifest.rootDir.toFSPathString()},
        {"main", manifest.mainPath().toFSPathString()},
        {"languages", languages},
        {"packageJSON", manifest.rawPackageJson},
    };
    whenReady([this, id, params] {
        m_connection->sendRequest(
            "activate", params, [this, id](const QJsonValue &, const QString &error) {
                if (!error.isEmpty()) {
                    emit activationFailed(id, error);
                    MessageManager::writeFlashing(
                        Tr::tr("Activation of \"%1\" failed: %2").arg(id, error));
                }
            });
        ensureDocumentSync();
        ensureEditorFeatures();
    });
}

void ExtensionHost::deactivate(const QString &id)
{
    // Stop the language servers this extension started, even if it did not
    // dispose them itself. Their key is "<extensionId>:<clientId>".
    const QString prefix = id + ':';
    for (const QString &key : m_lspClients.keys()) {
        if (key.startsWith(prefix)) {
            if (auto client = m_lspClients.take(key))
                LanguageClientManager::shutdownClient(client);
        }
    }

    if (!isRunning())
        return;

    m_connection->sendRequest("deactivate", QJsonObject{{"id", id}},
                              [](const QJsonValue &, const QString &) {});
}

Result<> ExtensionHost::activateBundledTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath dir = m_runtimeDir / "testextension";
    const FilePath packageJson = dir / "package.json";
    const FilePath extensionJs = dir / "extension.js";

    if (const Result<> r = extractResource(":/alien/host/testextension/package.json", packageJson); !r)
        return r;
    if (const Result<> r = extractResource(":/alien/host/testextension/extension.js", extensionJs); !r)
        return r;

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

Result<> ExtensionHost::activateBundledLspTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath extDir = m_runtimeDir / "lsptestextension";
    const FilePath packageJson = extDir / "package.json";

    const QList<std::pair<QString, FilePath>> resources = {
        {":/alien/host/lsptestextension/package.json", packageJson},
        {":/alien/host/lsptestextension/extension.js", extDir / "extension.js"},
        {":/alien/host/mockserver/server.js", m_runtimeDir / "mockserver" / "server.js"},
    };
    for (const auto &[resource, dest] : resources) {
        if (const Result<> r = extractResource(resource, dest); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

Result<> ExtensionHost::activateBundledDocSyncTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath extDir = m_runtimeDir / "docsynctestextension";
    const FilePath packageJson = extDir / "package.json";

    const QList<std::pair<QString, FilePath>> resources = {
        {":/alien/host/docsynctestextension/package.json", packageJson},
        {":/alien/host/docsynctestextension/extension.js", extDir / "extension.js"},
    };
    for (const auto &[resource, dest] : resources) {
        if (const Result<> r = extractResource(resource, dest); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

Result<> ExtensionHost::activateBundledDiagnosticsTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath extDir = m_runtimeDir / "diagnosticstestextension";
    const FilePath packageJson = extDir / "package.json";

    const QList<std::pair<QString, FilePath>> resources = {
        {":/alien/host/diagnosticstestextension/package.json", packageJson},
        {":/alien/host/diagnosticstestextension/extension.js", extDir / "extension.js"},
    };
    for (const auto &[resource, dest] : resources) {
        if (const Result<> r = extractResource(resource, dest); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

Result<> ExtensionHost::activateBundledCompletionTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath extDir = m_runtimeDir / "completiontestextension";
    const FilePath packageJson = extDir / "package.json";

    const QList<std::pair<QString, FilePath>> resources = {
        {":/alien/host/completiontestextension/package.json", packageJson},
        {":/alien/host/completiontestextension/extension.js", extDir / "extension.js"},
    };
    for (const auto &[resource, dest] : resources) {
        if (const Result<> r = extractResource(resource, dest); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

Result<> ExtensionHost::activateBundledHoverDefinitionTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath extDir = m_runtimeDir / "hoverdeftestextension";
    const FilePath packageJson = extDir / "package.json";

    const QList<std::pair<QString, FilePath>> resources = {
        {":/alien/host/hoverdeftestextension/package.json", packageJson},
        {":/alien/host/hoverdeftestextension/extension.js", extDir / "extension.js"},
    };
    for (const auto &[resource, dest] : resources) {
        if (const Result<> r = extractResource(resource, dest); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

Result<> ExtensionHost::activateBundledQuickPickTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath extDir = m_runtimeDir / "quickpicktestextension";
    const FilePath packageJson = extDir / "package.json";

    const QList<std::pair<QString, FilePath>> resources = {
        {":/alien/host/quickpicktestextension/package.json", packageJson},
        {":/alien/host/quickpicktestextension/extension.js", extDir / "extension.js"},
    };
    for (const auto &[resource, dest] : resources) {
        if (const Result<> r = extractResource(resource, dest); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

Result<> ExtensionHost::activateBundledStatusBarTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath extDir = m_runtimeDir / "statusbartestextension";
    const FilePath packageJson = extDir / "package.json";

    const QList<std::pair<QString, FilePath>> resources = {
        {":/alien/host/statusbartestextension/package.json", packageJson},
        {":/alien/host/statusbartestextension/extension.js", extDir / "extension.js"},
    };
    for (const auto &[resource, dest] : resources) {
        if (const Result<> r = extractResource(resource, dest); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

Result<> ExtensionHost::activateBundledTreeViewTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath extDir = m_runtimeDir / "treeviewtestextension";
    const FilePath packageJson = extDir / "package.json";

    const QList<std::pair<QString, FilePath>> resources = {
        {":/alien/host/treeviewtestextension/package.json", packageJson},
        {":/alien/host/treeviewtestextension/extension.js", extDir / "extension.js"},
    };
    for (const auto &[resource, dest] : resources) {
        if (const Result<> r = extractResource(resource, dest); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

void ExtensionHost::requestTreeChildren(const QString &viewId, const QString &id,
                                        const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    QJsonObject params{{"viewId", viewId}};
    if (!id.isEmpty())
        params.insert("id", id);
    whenReady([this, params, callback] {
        m_connection->sendRequest(
            "treeview/getChildren", params,
            [callback](const QJsonValue &result, const QString &error) {
                if (!error.isEmpty())
                    callback({});
                else
                    callback(result.toObject().value("nodes").toArray());
            });
    });
}

Result<> ExtensionHost::activateBundledWebviewTestExtension()
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath extDir = m_runtimeDir / "webviewtestextension";
    const FilePath packageJson = extDir / "package.json";

    const QList<std::pair<QString, FilePath>> resources = {
        {":/alien/host/webviewtestextension/package.json", packageJson},
        {":/alien/host/webviewtestextension/extension.js", extDir / "extension.js"},
    };
    for (const auto &[resource, dest] : resources) {
        if (const Result<> r = extractResource(resource, dest); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}

void ExtensionHost::setWebviewRenderer(WebviewRenderer *renderer)
{
    m_webviewRenderer = renderer;
    if (!renderer)
        return;
    renderer->onMessage = [this](const QString &id, const QJsonValue &message) {
        deliverWebviewMessage(id, message);
    };
    renderer->onDisposed = [this](const QString &id) {
        if (m_connection)
            m_connection->sendNotification("webview/onDidDispose", QJsonObject{{"id", id}});
    };
}

void ExtensionHost::deliverWebviewMessage(const QString &id, const QJsonValue &message)
{
    if (m_connection)
        m_connection->sendNotification("webview/onMessage",
                                       QJsonObject{{"id", id}, {"message", message}});
}

void ExtensionHost::setConfiguration(const QJsonObject &configuration)
{
    m_configuration = configuration;
    if (m_connection && m_connection->isRunning())
        m_connection->sendNotification("configuration/update", QJsonObject{{"config", configuration}});
}

void ExtensionHost::setWorkspaceFolders(const QJsonArray &folders)
{
    if (folders == m_workspaceFolders)
        return;
    m_workspaceFolders = folders;
    if (m_connection && m_connection->isRunning())
        m_connection->sendNotification("workspace/updateFolders", QJsonObject{{"folders", folders}});
}

AlienClient *ExtensionHost::languageClient(const QString &id) const
{
    return m_lspClients.value(id);
}

QString ExtensionHost::languageIdFor(const FilePath &filePath) const
{
    return m_languageBySuffix.value(filePath.suffix().toLower(), "plaintext");
}

void ExtensionHost::ensureDocumentSync()
{
    if (m_documentSyncStarted)
        return;
    m_documentSyncStarted = true;

    connect(EditorManager::instance(), &EditorManager::documentOpened,
            this, &ExtensionHost::onDocumentOpened);
    connect(EditorManager::instance(), &EditorManager::documentClosed,
            this, &ExtensionHost::onDocumentClosed);
    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, [this] { syncActiveEditor(); });

    for (IDocument *document : DocumentModel::openedDocuments())
        onDocumentOpened(document);
    syncActiveEditor();
}

void ExtensionHost::onDocumentOpened(IDocument *document)
{
    auto textDocument = qobject_cast<TextDocument *>(document);
    if (!textDocument)
        return;

    const FilePath filePath = textDocument->filePath();
    if (filePath.isEmpty())
        return;

    m_documentVersions.insert(filePath, 1);
    m_connection->sendNotification("document/didOpen", QJsonObject{
        {"uri", filePath.toFSPathString()},
        {"languageId", languageIdFor(filePath)},
        {"version", 1},
        {"text", textDocument->plainText()},
    });

    maybeAttachCompletion(textDocument);

    connect(textDocument, &TextDocument::contentsChangedWithPosition, this,
            [this, textDocument] {
                const FilePath path = textDocument->filePath();
                const int version = m_documentVersions.value(path) + 1;
                m_documentVersions.insert(path, version);
                m_connection->sendNotification("document/didChange", QJsonObject{
                    {"uri", path.toFSPathString()},
                    {"version", version},
                    {"text", textDocument->plainText()},
                });
            });
}

void ExtensionHost::onDocumentClosed(IDocument *document)
{
    auto textDocument = qobject_cast<TextDocument *>(document);
    if (!textDocument)
        return;
    const FilePath filePath = textDocument->filePath();
    m_documentVersions.remove(filePath);
    m_connection->sendNotification("document/didClose",
                                   QJsonObject{{"uri", filePath.toFSPathString()}});
}

void ExtensionHost::syncActiveEditor()
{
    IDocument *document = EditorManager::currentDocument();
    auto textDocument = qobject_cast<TextDocument *>(document);
    const QJsonValue uri = textDocument ? QJsonValue(textDocument->filePath().toFSPathString())
                                        : QJsonValue(QJsonValue::Null);
    m_connection->sendNotification("editor/didChangeActive", QJsonObject{{"uri", uri}});

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

void ExtensionHost::sendSelection(TextEditorWidget *widget)
{
    const TextDocument *document = widget->textDocument();
    if (!document || document->filePath().isEmpty())
        return;

    const QTextCursor cursor = widget->textCursor();
    QTextCursor anchor = cursor;
    anchor.setPosition(cursor.anchor());

    m_connection->sendNotification("editor/didChangeSelection", QJsonObject{
        {"uri", document->filePath().toFSPathString()},
        {"anchorLine", anchor.blockNumber()},
        {"anchorCharacter", anchor.positionInBlock()},
        {"activeLine", cursor.blockNumber()},
        {"activeCharacter", cursor.positionInBlock()},
    });
}

void ExtensionHost::publishDiagnostics(const QJsonValue &params)
{
    const QJsonObject object = params.toObject();
    const QString uri = object.value("uri").toString();
    const QString collection = object.value("collection").toString();
    const QJsonArray diagnostics = object.value("diagnostics").toArray();
    const QString key = collection + '\n' + uri;

    // Replace the previous marks for this collection/uri.
    qDeleteAll(m_diagnosticMarks.take(key));

    const FilePath filePath = FilePath::fromUserInput(uri);
    const TextMarkCategory category{Tr::tr("Alien"), "Alien.Diagnostics"};

    QList<TextMark *> marks;
    for (const QJsonValue &value : diagnostics) {
        const QJsonObject diagnostic = value.toObject();
        const QJsonObject start = diagnostic.value("range").toObject().value("start").toObject();
        const int line = start.value("line").toInt() + 1;

        auto mark = new TextMark(filePath, line, category);
        mark->setToolTip(diagnostic.value("message").toString());
        mark->setLineAnnotation(diagnostic.value("message").toString());

        switch (diagnostic.value("severity").toInt()) {
        case 0: mark->setColor(Theme::CodeModel_Error_TextMarkColor); break;
        case 1: mark->setColor(Theme::CodeModel_Warning_TextMarkColor); break;
        default: mark->setColor(Theme::CodeModel_Info_TextMarkColor); break;
        }
        marks.append(mark);
    }

    if (!marks.isEmpty())
        m_diagnosticMarks.insert(key, marks);

    emit diagnosticsPublished(uri, int(diagnostics.size()));
}

AlienCompletionAssistProvider *ExtensionHost::completionProvider()
{
    if (!m_completionProvider)
        m_completionProvider = new AlienCompletionAssistProvider(this);
    return m_completionProvider;
}

void ExtensionHost::maybeAttachCompletion(TextDocument *document)
{
    if (!document || !m_completionLanguageIds.contains(languageIdFor(document->filePath())))
        return;
    if (document->completionAssistProvider() == completionProvider())
        return;

    document->setCompletionAssistProvider(completionProvider());
    m_completionDocuments.append(document);
}

void ExtensionHost::requestCompletion(const FilePath &uri, int line, int character,
                                      const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", uri.toFSPathString()},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    whenReady([this, params, callback] {
        m_connection->sendRequest(
            "completion/provide", params,
            [callback](const QJsonValue &result, const QString &error) {
                if (!error.isEmpty())
                    callback({});
                else
                    callback(result.toObject().value("items").toArray());
            });
    });
}

void ExtensionHost::requestHover(const FilePath &uri, int line, int character,
                                 const std::function<void(const QString &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", uri.toFSPathString()},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    whenReady([this, params, callback] {
        m_connection->sendRequest(
            "hover/provide", params, [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("contents").toString() : QString());
            });
    });
}

void ExtensionHost::requestDefinition(const FilePath &uri, int line, int character,
                                      const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", uri.toFSPathString()},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    whenReady([this, params, callback] {
        m_connection->sendRequest(
            "definition/provide", params,
            [callback](const QJsonValue &result, const QString &error) {
                if (!error.isEmpty())
                    callback({});
                else
                    callback(result.toObject().value("locations").toArray());
            });
    });
}

AlienHoverHandler *ExtensionHost::hoverHandler()
{
    if (!m_hoverHandler)
        m_hoverHandler = new AlienHoverHandler(this);
    return m_hoverHandler;
}

void ExtensionHost::ensureEditorFeatures()
{
    if (m_editorFeaturesStarted)
        return;
    m_editorFeaturesStarted = true;

    connect(EditorManager::instance(), &EditorManager::editorOpened,
            this, &ExtensionHost::attachEditorFeatures);
    for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
        attachEditorFeatures(editor);
}

void ExtensionHost::attachEditorFeatures(IEditor *editor)
{
    TextEditorWidget *widget = TextEditorWidget::fromEditor(editor);
    if (!widget)
        return;
    const QString languageId = languageIdFor(widget->textDocument()->filePath());

    if (m_hoverLanguageIds.contains(languageId) && !m_hoverWidgets.contains(widget)) {
        widget->addHoverHandler(hoverHandler());
        m_hoverWidgets.append(widget);
    }

    if (m_definitionLanguageIds.contains(languageId)) {
        widget->setOptionalActions(widget->optionalActions()
                                   | TextEditor::OptionalActions::FollowSymbolUnderCursor);
        connect(widget, &TextEditorWidget::requestLinkAt, this,
                [this, widget](const QTextCursor &cursor, const Utils::LinkHandler &callback,
                               bool resolveTarget) {
                    Q_UNUSED(resolveTarget)
                    const FilePath path = widget->textDocument()->filePath();
                    const int line = cursor.blockNumber();
                    const int character = cursor.positionInBlock();

                    // Source range to highlight: the word under the cursor.
                    QTextCursor wordCursor = cursor;
                    wordCursor.select(QTextCursor::WordUnderCursor);

                    requestDefinition(path, line, character,
                        [callback, start = wordCursor.selectionStart(),
                         end = wordCursor.selectionEnd()](const QJsonArray &locations) {
                            if (locations.isEmpty()) {
                                callback(Utils::Link());
                                return;
                            }
                            const QJsonObject location = locations.first().toObject();
                            const QJsonObject startPos
                                = location.value("range").toObject().value("start").toObject();
                            Utils::Link link(
                                FilePath::fromUserInput(location.value("uri").toString()),
                                startPos.value("line").toInt() + 1,
                                startPos.value("character").toInt());
                            link.linkTextStart = start;
                            link.linkTextEnd = end;
                            callback(link);
                        });
                });
    }
}

void ExtensionHost::resolveQuickPick(int id, int index)
{
    if (const auto respond = m_pendingPrompts.take(id))
        respond(QJsonValue(index), {});
}

void ExtensionHost::resolveInputBox(int id, const QString &value, bool accepted)
{
    if (const auto respond = m_pendingPrompts.take(id))
        respond(accepted ? QJsonValue(value) : QJsonValue(QJsonValue::Null), {});
}

void ExtensionHost::executeCommand(const QString &command)
{
    if (!m_connection)
        return;
    whenReady([this, command] {
        m_connection->sendRequest(
            "executeCommand",
            QJsonObject{{"command", command}, {"args", QJsonArray{}}},
            [command](const QJsonValue &, const QString &error) {
                if (!error.isEmpty()) {
                    MessageManager::writeFlashing(
                        Tr::tr("Command \"%1\" failed: %2").arg(command, error));
                }
            });
    });
}

} // namespace Alien::Internal
