// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "hostprotocols.h"

#include "alienconstants.h"
#include "alientr.h"
#include "alientreeview.h"
#include "codicons.h"
#include "extensionhost.h"
#include "hostconnection.h"
#include "webviewrenderer.h"

#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/fileutils.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/fileutils.h>
#include <utils/link.h>

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTextBlock>

using namespace Core;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace Alien::Internal {

// A progress nobody is going to end would run in the status bar for good.
void WindowProtocol::endAllProgress()
{
    qDeleteAll(std::exchange(m_progressWatchers, {}));
    m_progressDone.clear();
    for (QFutureInterface<void> *interface : std::exchange(m_progress, {})) {
        interface->reportFinished();
        delete interface;
    }
}

// What an item says about itself belongs in the list: two commands named "Run"
// are told apart by nothing else.
static QStringList quickPickLabels(const QJsonObject &object)
{
    const QJsonArray shown = object.value("shown").toArray();
    if (shown.isEmpty()) {
        QStringList items;
        for (const QJsonValue &item : object.value("items").toArray())
            items << item.toString();
        return items;
    }

    QStringList items;
    for (const QJsonValue &value : shown) {
        const QJsonObject item = value.toObject();
        QString text = item.value("label").toString();
        const QString description = item.value("description").toString();
        const QString detail = item.value("detail").toString();
        if (!description.isEmpty())
            text += " - " + description;
        if (!detail.isEmpty())
            text += " (" + detail + ')';
        items << text;
    }
    return items;
}

WindowProtocol::~WindowProtocol()
{
    qDeleteAll(m_treeFactories);
    endAllProgress();
}

void WindowProtocol::forgetHost()
{
    qDeleteAll(m_treeFactories);
    m_treeFactories.clear();
    endAllProgress();
    if (!std::exchange(m_statusProgress, {}).isEmpty())
        emit m_host.statusBarMessageChanged({});
    // Nobody is left to answer. The ids keep counting, so that a dialog still
    // open from before cannot answer a prompt of the next host.
    m_pendingPrompts.clear();
    m_multiPicks.clear();
    // The next host numbers its panels from the start again.
    for (const QJsonValue &panel : std::as_const(m_webviewPanels)) {
        if (m_webviewRenderer)
            m_webviewRenderer->disposePanel(panel.toObject().value("id").toString());
    }
    m_webviewPanels = {};
}

void WindowProtocol::installHandlers()
{
    connection()->setRequestHandler(
        "window/openExternal",
        [](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QString uri = params.toObject().value("uri").toString();
            // Strict: this comes from an extension as a URI, not from a user as
            // something to guess at, so a malformed one is an error rather than
            // a thing to fix up.
            const QUrl url(uri, QUrl::StrictMode);
            if (uri.isEmpty() || !url.isValid()) {
                respond({}, Tr::tr("Cannot open \"%1\": not a valid URL.").arg(uri));
                return;
            }
            respond(QDesktopServices::openUrl(url), {});
        });

    connection()->setRequestHandler(
        "window/showMessage",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString levelName = object.value("level").toString();
            const MessageLevel level = levelName == "error"  ? MessageLevel::Error
                                       : levelName == "warn" ? MessageLevel::Warning
                                                             : MessageLevel::Info;
            const QString message = object.value("message").toString();
            QString prefix;
            switch (level) {
            case MessageLevel::Info: prefix = Tr::tr("Info"); break;
            case MessageLevel::Warning: prefix = Tr::tr("Warning"); break;
            case MessageLevel::Error: prefix = Tr::tr("Error"); break;
            }
            MessageManager::writeFlashing(QString("Alien [%1]: %2").arg(prefix, message));
            emit m_host.messageShown(message);

            QStringList items;
            for (const QJsonValue &item : object.value("items").toArray())
                items << item.toString();
            if (items.isEmpty()) {
                // Nothing was asked, so there is nothing to answer.
                respond(QJsonValue(QJsonValue::Null), {});
                return;
            }
            // An extension offering choices is asking a question, and answering
            // "dismissed" every time means it can never be told yes.
            const int id = m_nextPromptId++;
            m_pendingPrompts.insert(id, respond);
            emit m_host.messageQuestionRequested(id, level, message, items,
                                          object.value("detail").toString(),
                                          object.value("modal").toBool());
        });

    // A document an extension provides itself has no file behind it, so it goes
    // into an editor holding the text, keyed by its URI so that showing the
    // same one twice reuses that editor.
    connection()->setRequestHandler(
        "window/showDocumentContents",
        [](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            QString title = object.value("title").toString();
            IEditor *editor = EditorManager::openEditorWithContents(
                Core::Constants::K_DEFAULT_TEXT_EDITOR_ID,
                &title,
                object.value("text").toString().toUtf8(),
                object.value("uri").toString());
            if (!editor) {
                respond({}, Tr::tr("Cannot show \"%1\".").arg(title));
                return;
            }
            if (auto textDocument = qobject_cast<TextDocument *>(editor->document()))
                textDocument->document()->setModified(false);
            respond(true, {});
        });

    connection()->setRequestHandler(
        "window/showTextDocument",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const FilePath filePath = m_host.fromHostPath(object.value("path").toString());
            const QJsonObject selection = object.value("selection").toObject();
            const Link link{filePath,
                            selection.value("line").toInt() + 1,
                            selection.value("character").toInt()};
            const EditorManager::OpenEditorFlags flags
                = object.value("preserveFocus").toBool()
                      ? EditorManager::DoNotChangeCurrentEditor
                      : EditorManager::NoFlags;
            // Where the line should end up: in the middle of the view, at the
            // top, or wherever it already is.
            const int revealType = object.value("revealType").toInt();
            IEditor *editor = EditorManager::openEditorAt(link, {}, flags);
            if (!editor) {
                respond({}, Tr::tr("Cannot open \"%1\".").arg(filePath.toUserOutput()));
                return;
            }
            // The editor that was opened, which need not be the current one.
            if (TextEditorWidget *widget = TextEditorWidget::fromEditor(editor)) {
                // vscode.TextEditorRevealType
                enum { InCenter = 1, InCenterIfOutsideViewport = 2, AtTop = 3 };
                if (revealType == InCenter || revealType == InCenterIfOutsideViewport) {
                    widget->centerCursor();
                } else if (revealType == AtTop) {
                    widget->gotoLine(link.target.line, link.target.column, false);
                    widget->setTopBlock(widget->textCursor().block());
                }
            }
            respond(true, {});
        });

    // Filters arrive as {"Images": ["png", "jpg"]}, Qt wants them as
    // "Images (*.png *.jpg)".
    const auto fileDialogFilter = [](const QJsonObject &filters) {
        QStringList parts;
        for (auto it = filters.begin(); it != filters.end(); ++it) {
            QStringList patterns;
            for (const QJsonValue &extension : it.value().toArray())
                patterns.append("*." + extension.toString());
            parts.append(QString("%1 (%2)").arg(it.key(), patterns.join(' ')));
        }
        return parts.join(";;");
    };

    // A modal dialog spins an event loop, which must not run inside the one
    // reading the host's output.
    const auto queued = [this](const HostConnection::RequestHandler &handler) {
        return [this, handler](const QJsonValue &params, const HostConnection::Responder &respond) {
            QMetaObject::invokeMethod(
                this, [handler, params, respond] { handler(params, respond); },
                Qt::QueuedConnection);
        };
    };

    connection()->setRequestHandler(
        "window/showOpenDialog",
        queued([this, fileDialogFilter](const QJsonValue &params,
                                        const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString caption = object.value("title").toString().isEmpty()
                                        ? Tr::tr("Open File")
                                        : object.value("title").toString();
            const FilePath dir = m_host.fromHostPath(object.value("defaultPath").toString());
            const QString filter = fileDialogFilter(object.value("filters").toObject());

            FilePaths chosen;
            if (object.value("canSelectFolders").toBool()) {
                const FilePath dirPath = Utils::FileUtils::getExistingDirectory(caption, dir);
                if (!dirPath.isEmpty())
                    chosen.append(dirPath);
            } else if (object.value("canSelectMany").toBool()) {
                chosen = Utils::FileUtils::getOpenFilePaths(caption, dir, filter);
            } else if (const FilePath file = Utils::FileUtils::getOpenFilePath(caption, dir, filter);
                       !file.isEmpty()) {
                chosen.append(file);
            }

            if (chosen.isEmpty()) {
                respond(QJsonValue(QJsonValue::Null), {}); // cancelled
                return;
            }
            QJsonArray paths;
            for (const FilePath &path : chosen)
                paths.append(m_host.toHostPath(path));
            respond(paths, {});
        }));

    connection()->setRequestHandler(
        "window/showSaveDialog",
        queued([this, fileDialogFilter](const QJsonValue &params,
                                        const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString caption = object.value("title").toString().isEmpty()
                                        ? Tr::tr("Save File")
                                        : object.value("title").toString();
            const FilePath file = Utils::FileUtils::getSaveFilePath(
                caption,
                m_host.fromHostPath(object.value("defaultPath").toString()),
                fileDialogFilter(object.value("filters").toObject()));
            if (file.isEmpty()) {
                respond(QJsonValue(QJsonValue::Null), {});
                return;
            }
            respond(m_host.toHostPath(file), {});
        }));

    // A list the extension fills in after showing it: it asks for a choice
    // before it knows the choices.
    connection()->setNotificationHandler("quickPick/hide", [this](const QJsonValue &params) {
        emit m_host.quickPickHidden(params.toObject().value("pickId").toString());
    });

    connection()->setNotificationHandler("quickPick/update", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit m_host.quickPickUpdated(object.value("pickId").toString(), quickPickLabels(object),
                              object.value("placeholder").toString());
    });

    connection()->setRequestHandler(
        "window/showQuickPick",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QStringList items = quickPickLabels(object);
            const int id = m_nextPromptId++;
            m_pendingPrompts.insert(id, respond);
            if (object.value("canPickMany").toBool())
                m_multiPicks.insert(id);
            emit m_host.quickPickRequested(id, items, object.value("placeholder").toString(),
                                    object.value("canPickMany").toBool(),
                                    object.value("pickId").toString());
        });

    connection()->setRequestHandler(
        "window/showInputBox",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const int id = m_nextPromptId++;
            m_pendingPrompts.insert(id, respond);
            emit m_host.inputBoxRequested(id, object.value("prompt").toString(),
                                   object.value("value").toString(),
                                   object.value("placeholder").toString(),
                                   object.value("password").toBool());
        });

    connection()->setNotificationHandler("statusbar/setMessage", [this](const QJsonValue &params) {
        emit m_host.statusBarMessageChanged(params.toObject().value("text").toString());
    });

    connection()->setNotificationHandler("statusbar/update", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit m_host.statusBarItemChanged(object.value("id").toString(), object);
    });

    connection()->setNotificationHandler("statusbar/remove", [this](const QJsonValue &params) {
        emit m_host.statusBarItemRemoved(params.toObject().value("id").toString());
    });

    // What an extension asked for that we do not implement yet. Said once per
    // member, so it reads as a list of what to build rather than noise.
    connection()->setNotificationHandler("api/unimplemented", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString extension = object.value("extension").toString();
        const QString member = object.value("member").toString();
        if (extension.isEmpty()) {
            MessageManager::writeSilently(
                Tr::tr("An extension used \"%1\", which is not implemented yet.").arg(member));
        } else {
            MessageManager::writeSilently(
                Tr::tr("The \"%1\" extension used \"%2\", which is not implemented yet.")
                    .arg(extension, member));
        }
        emit m_host.apiUnimplemented(extension, member);
    });

    connection()->setNotificationHandler("progress/start", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString id = object.value("id").toString();
        if (m_progress.contains(id))
            return;
        // A window progress is a line in the status bar and nothing more; only
        // a notification is a task with a bar and a stop button.
        enum { ProgressLocationWindow = 10 }; // vscode.ProgressLocation.Window
        if (object.value("location").toInt() == ProgressLocationWindow) {
            emit m_host.statusBarMessageChanged(stripCodicons(object.value("title").toString()));
            m_statusProgress.insert(id);
            return;
        }
        auto interface = new QFutureInterface<void>;
        interface->reportStarted();
        interface->setProgressRange(0, 100); // what report({increment}) counts in
        m_progress.insert(id, interface);
        Core::ProgressManager::addTask(interface->future(),
                                 stripCodicons(object.value("title").toString()),
                                 "Alien.Progress");
        // A task that said it can be stopped is watched for the user stopping
        // it, which is the only way the extension hears about the button.
        if (object.value("cancellable").toBool()) {
            auto watcher = new QFutureWatcher<void>(this);
            m_progressWatchers.insert(id, watcher);
            connect(watcher, &QFutureWatcherBase::canceled, this, [this, id] {
                if (connection())
                    connection()->sendRequest("progress/cancel", QJsonObject{{"id", id}}, {});
            });
            watcher->setFuture(interface->future());
        }
    });

    connection()->setNotificationHandler("progress/report", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString id = object.value("id").toString();
        QFutureInterface<void> *interface = m_progress.value(id);
        if (!interface)
            return;
        double &done = m_progressDone[id];
        done += object.value("increment").toDouble();
        interface->setProgressValueAndText(qRound(qBound(0.0, done, 100.0)),
                                           stripCodicons(object.value("message").toString()));
    });

    connection()->setNotificationHandler("progress/end", [this](const QJsonValue &params) {
        if (m_statusProgress.remove(params.toObject().value("id").toString())) {
            emit m_host.statusBarMessageChanged({});
            return;
        }
        const QString id = params.toObject().value("id").toString();
        delete m_progressWatchers.take(id);
        m_progressDone.remove(id);
        if (QFutureInterface<void> *interface = m_progress.take(id)) {
            interface->reportFinished();
            delete interface;
        }
    });

    connection()->setNotificationHandler("treeview/register", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString viewId = object.value("viewId").toString();
        // "Gerrit AI: Dashboard" rather than "gerritAI.dashboard": the sidebar
        // lists views from every source, so the container says whose it is.
        const QString name = object.value("name").toString();
        const QString container = object.value("container").toString();
        QString displayName = viewId;
        if (!name.isEmpty())
            displayName = container.isEmpty() ? name : QString("%1: %2").arg(container, name);
        if (!m_treeFactories.contains(viewId))
            m_treeFactories.insert(viewId, new AlienTreeViewFactory(&m_host, viewId, displayName));
        emit m_host.treeViewRegistered(viewId);
    });

    connection()->setNotificationHandler("menus/viewTitle", [this](const QJsonValue &params) {
        const QJsonObject views = params.toObject().value("views").toObject();
        for (auto it = views.begin(); it != views.end(); ++it) {
            const QJsonArray items = it->toArray();
            if (m_treeTitleMenus.value(it.key()) == items)
                continue;
            m_treeTitleMenus.insert(it.key(), items);
            emit m_host.treeTitleMenuChanged(it.key());
        }
    });

    connection()->setNotificationHandler("views/welcome", [this](const QJsonValue &params) {
        const QHash<QString, QString> before = m_viewWelcome;
        m_viewWelcome.clear();
        for (const QJsonValue &value : params.toObject().value("items").toArray()) {
            const QJsonObject item = value.toObject();
            m_viewWelcome.insert(item.value("view").toString(),
                                 item.value("contents").toString());
        }
        const QSet<QString> touched = QSet<QString>(before.keyBegin(), before.keyEnd())
                                      + QSet<QString>(m_viewWelcome.keyBegin(),
                                                      m_viewWelcome.keyEnd());
        for (const QString &viewId : touched) {
            if (before.value(viewId) != m_viewWelcome.value(viewId))
                emit m_host.viewWelcomeChanged(viewId);
        }
    });

    connection()->setNotificationHandler("treeview/refresh", [this](const QJsonValue &params) {
        emit m_host.treeViewRefreshed(params.toObject().value("viewId").toString());
    });

    connection()->setNotificationHandler("treeview/unregister", [this](const QJsonValue &params) {
        const QString viewId = params.toObject().value("viewId").toString();
        delete m_treeFactories.take(viewId); // unregisters from the navigation pool
    });

    connection()->setNotificationHandler("webview/create", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString id = object.value("id").toString();
        const QString viewType = object.value("viewType").toString();
        const QString title = object.value("title").toString();
        const QJsonObject options = object.value("options").toObject();
        if (m_webviewRenderer) {
            m_webviewRenderer->createPanel(id, viewType, title);
            m_webviewRenderer->setPanelOptions(id, options);
        }
        m_webviewPanels.append(
            QJsonObject{{"id", id}, {"viewType", viewType}, {"title", title}});
        emit m_host.webviewCreated(id, viewType, title);
    });

    connection()->setNotificationHandler("webview/setHtml", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString id = object.value("id").toString();
        const QString html = object.value("html").toString();
        if (m_webviewRenderer)
            m_webviewRenderer->setHtml(id, html);
        emit m_host.webviewHtmlChanged(id, html);
    });

    connection()->setNotificationHandler("webview/postMessage", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString id = object.value("id").toString();
        const QJsonValue message = object.value("message");
        if (m_webviewRenderer)
            m_webviewRenderer->postMessage(id, message);
        const QByteArray json = QJsonDocument(QJsonObject{{"message", message}})
                                    .toJson(QJsonDocument::Compact);
        emit m_host.webviewMessagePosted(id, QString::fromUtf8(json));
    });

    connection()->setNotificationHandler("webview/reveal", [this](const QJsonValue &params) {
        if (m_webviewRenderer)
            m_webviewRenderer->reveal(params.toObject().value("id").toString());
    });

    connection()->setNotificationHandler("webview/dispose", [this](const QJsonValue &params) {
        const QString id = params.toObject().value("id").toString();
        if (m_webviewRenderer)
            m_webviewRenderer->disposePanel(id);
        forgetWebviewPanel(id);
    });

    connection()->setNotificationHandler("output/append", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString channel = object.value("channel").toString();
        const QString value = object.value("value").toString();
        emit m_host.channelOutput(channel, value, object.value("line").toBool());
        qCDebug(logHost).noquote() << QString("<%1> %2").arg(channel, value);
    });

    connection()->setNotificationHandler("output/show", [this](const QJsonValue &params) {
        emit m_host.channelShowRequested(params.toObject().value("preserveFocus").toBool());
    });

    connection()->setNotificationHandler("output/clear", [this](const QJsonValue &) {
        emit m_host.channelClearRequested();
    });
}

void WindowProtocol::requestTreeMenu(const QString &viewId, const QString &id, const QString &kind,
                                    const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    m_host.whenReady([this, viewId, id, kind, callback] {
        connection()->sendRequest(
            "treeview/menu", QJsonObject{{"viewId", viewId}, {"id", id}, {"kind", kind}},
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("items").toArray()
                                         : QJsonArray());
            });
    }, [callback] { callback({}); });
}

void WindowProtocol::runInWebview(
    const QString &id, const QString &script,
    const std::function<void(const QJsonValue &, const QString &)> &done)
{
    if (!m_webviewRenderer) {
        done({}, Tr::tr("This build has no webview backend."));
        return;
    }
    m_webviewRenderer->runScript(id, script, done);
}

void WindowProtocol::reportTreeSelection(const QString &viewId, const QStringList &ids)
{
    if (!connection())
        return;
    QJsonArray array;
    for (const QString &id : ids)
        array.append(id);
    connection()->sendNotification("treeview/selectionChanged",
                                   QJsonObject{{"viewId", viewId}, {"ids", array}});
}

void WindowProtocol::executeTreeItemCommand(const QString &viewId, const QString &id,
                                           const QString &command)
{
    if (!connection())
        return;
    m_host.whenReady([this, viewId, id, command] {
        connection()->sendRequest(
            "treeview/executeItemCommand",
            QJsonObject{{"viewId", viewId}, {"id", id}, {"command", command}},
            [command](const QJsonValue &, const QString &error) {
                if (!error.isEmpty()) {
                    MessageManager::writeFlashing(
                        Tr::tr("Command \"%1\" failed: %2").arg(command, error));
                }
            });
    });
}

void WindowProtocol::requestTreeChildren(const QString &viewId, const QString &id,
                                        const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    QJsonObject params{{"viewId", viewId}};
    if (!id.isEmpty())
        params.insert("id", id);
    m_host.whenReady([this, params, callback] {
        connection()->sendRequest(
            "treeview/getChildren", params,
            [callback](const QJsonValue &result, const QString &error) {
                if (!error.isEmpty())
                    callback({});
                else
                    callback(result.toObject().value("nodes").toArray());
            });
    }, [callback] { callback({}); });
}

QJsonArray WindowProtocol::webviewPanels() const
{
    QJsonArray panels;
    for (const QJsonValue &value : m_webviewPanels) {
        QJsonObject panel = value.toObject();
        // Whether a page can be driven is decided by the backend the panel
        // ended up on, not by what the extension asked for: a panel that
        // declared no script is moved to an engine as soon as its page turns
        // out to have one, and a caller going by the declaration leaves a page
        // alone that it could have read.
        panel["scripts"] = m_webviewRenderer
                           && m_webviewRenderer->runsScripts(panel.value("id").toString());
        panels.append(panel);
    }
    return panels;
}

void WindowProtocol::setWebviewRenderer(WebviewRenderer *renderer)
{
    m_webviewRenderer = renderer;
    if (!renderer)
        return;
    renderer->onMessage = [this](const QString &id, const QJsonValue &message) {
        deliverWebviewMessage(id, message);
    };
    renderer->onDisposed = [this](const QString &id) {
        forgetWebviewPanel(id);
        if (connection())
            connection()->sendNotification("webview/onDidDispose", QJsonObject{{"id", id}});
    };
}

void WindowProtocol::forgetWebviewPanel(const QString &id)
{
    for (qsizetype i = 0; i < m_webviewPanels.size(); ++i) {
        if (m_webviewPanels.at(i).toObject().value("id").toString() == id) {
            m_webviewPanels.removeAt(i);
            break;
        }
    }
    emit m_host.webviewDisposed(id);
}

void WindowProtocol::deliverWebviewMessage(const QString &id, const QJsonValue &message)
{
    if (connection())
        connection()->sendNotification("webview/onMessage",
                                       QJsonObject{{"id", id}, {"message", message}});
}

void WindowProtocol::resolveQuickPick(int id, const QList<int> &indexes)
{
    const auto respond = m_pendingPrompts.take(id);
    if (!respond)
        return;
    // Picking many answers with the list, however long; picking one answers
    // with the index itself, and -1 for a dialog that was dismissed.
    if (m_multiPicks.remove(id)) {
        QJsonArray chosen;
        for (int index : indexes) {
            if (index >= 0)
                chosen.append(index);
        }
        respond(chosen, {});
        return;
    }
    respond(QJsonValue(indexes.isEmpty() ? -1 : indexes.first()), {});
}

void WindowProtocol::resolveMessageQuestion(int id, int index)
{
    if (const auto respond = m_pendingPrompts.take(id)) {
        respond(index >= 0 ? QJsonValue(index) : QJsonValue(QJsonValue::Null), {});
    }
}

void WindowProtocol::resolveInputBox(int id, const QString &value, bool accepted)
{
    if (const auto respond = m_pendingPrompts.take(id))
        respond(accepted ? QJsonValue(value) : QJsonValue(QJsonValue::Null), {});
}

} // namespace Alien::Internal
