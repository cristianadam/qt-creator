// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionhost.h"

#include "alienclient.h"
#include "alienconstants.h"
#include "aliensettings.h"
#include "alientr.h"
#include "hostconnection.h"

#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/fileutils.h>
#include <coreplugin/secretaspect.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/ioutputpane.h>
#include <coreplugin/locator/locatormanager.h>
#include <coreplugin/messagemanager.h>

#include <extensionsystem/pluginmanager.h>

#include <languageclient/languageclientmanager.h>
#include <languageclient/languageclientsettings.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/texteditorconstants.h>

#include <utils/appinfo.h>
#include <utils/async.h>
#include <utils/filesystemwatcher.h>
#include <utils/commandline.h>
#include <utils/fileutils.h>
#include <utils/futuresynchronizer.h>
#include <utils/mimeutils.h>
#include <utils/qtcassert.h>

#include <QFile>
#include <utils/stringutils.h>
#include <QClipboard>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimer>

using namespace Core;
using namespace LanguageClient;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace Alien::Internal {

Q_LOGGING_CATEGORY(logHost, "qtc.alien.host", QtWarningMsg)

// Translates a VS Code glob to a regular expression matched against a path
// relative to the search root. "**" spans directory separators, "*" and "?" do
// not, and "{a,b}" is an alternation.
QRegularExpression globExpression(const QString &glob)
{
    QString pattern;
    int braces = 0;
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
            ++braces;
        } else if (c == '}' && braces > 0) {
            pattern += ')';
            --braces;
        } else if (c == ',' && braces > 0) {
            // Only inside a group: a comma elsewhere is part of a name.
            pattern += '|';
        } else {
            pattern += QRegularExpression::escape(c);
        }
    }
    return QRegularExpression('^' + pattern + '$');
}

// The offset a VS Code position (0-based line and character) names in text.
int offsetOf(const QString &text, const QJsonValue &position)
{
    const QJsonObject object = position.toObject();
    const int line = object.value("line").toInt();
    const int character = object.value("character").toInt();

    int offset = 0;
    for (int i = 0; i < line; ++i) {
        const int next = text.indexOf('\n', offset);
        if (next < 0)
            return text.size();
        offset = next + 1;
    }
    const int lineEnd = text.indexOf('\n', offset);
    const int available = (lineEnd < 0 ? text.size() : lineEnd) - offset;
    return offset + std::min(character, available);
}

// Edits address the text they were computed against, so applying one must not
// move the next one: sort by position and work backwards. At the same start,
// the wider edit goes first, so that an insertion there is not swallowed, and
// the later one in the list does, so that insertions end up in list order.
QList<QJsonObject> editsBackToFront(const QString &text, const QJsonArray &edits)
{
    struct Edit
    {
        int start;
        int end;
        int index;
        QJsonObject edit;
    };
    QList<Edit> sorted;
    for (const QJsonValue &value : edits) {
        const QJsonObject edit = value.toObject();
        const QJsonObject range = edit.value("range").toObject();
        sorted.append({offsetOf(text, range.value("start")), offsetOf(text, range.value("end")),
                       int(sorted.size()), edit});
    }
    Utils::sort(sorted, [](const Edit &a, const Edit &b) {
        return std::tie(a.start, a.end, a.index) > std::tie(b.start, b.end, b.index);
    });
    return Utils::transform(sorted, &Edit::edit);
}

// An edit lands in the open editor when there is one, so that the user sees
// it and can undo it in one step, otherwise it is written to the file.
Result<> applyTextEdits(const FilePath &filePath, const QJsonArray &edits)
{
    if (auto document = TextDocument::textDocumentForFilePath(filePath)) {
        QTextDocument *contents = document->document();
        const QString text = contents->toPlainText();
        QTextCursor cursor(contents);
        cursor.beginEditBlock();
        for (const QJsonObject &edit : editsBackToFront(text, edits)) {
            const QJsonObject range = edit.value("range").toObject();
            cursor.setPosition(offsetOf(text, range.value("start")));
            cursor.setPosition(offsetOf(text, range.value("end")), QTextCursor::KeepAnchor);
            cursor.insertText(edit.value("newText").toString());
        }
        cursor.endEditBlock();
        return {};
    }

    const Result<QByteArray> contents = filePath.fileContents();
    if (!contents)
        return ResultError(contents.error());
    QString text = QString::fromUtf8(*contents);
    for (const QJsonObject &edit : editsBackToFront(text, edits)) {
        const QJsonObject range = edit.value("range").toObject();
        const int start = offsetOf(text, range.value("start"));
        const int end = offsetOf(text, range.value("end"));
        text.replace(start, end - start, edit.value("newText").toString());
    }
    if (const Result<qint64> written = filePath.writeFileContents(text.toUtf8()); !written)
        return ResultError(written.error());
    return {};
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

HostConnection *HostProtocol::connection() const
{
    return m_host.m_connection;
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
    if (!m_runtimeDir.isEmpty())
        m_runtimeDir.removeRecursively();
}

bool ExtensionHost::setNodePath(const FilePath &nodePath)
{
    if (nodePath == m_nodePath)
        return false;
    shutdown();
    // The runtime directory is on node's device, and a new one is made there.
    if (!m_runtimeDir.isEmpty() && !nodePath.isSameDevice(m_nodePath))
        std::exchange(m_runtimeDir, {}).removeRecursively();
    m_nodePath = nodePath;
    return true;
}

QPointer<QObject> ExtensionHost::shutdown()
{
    if (!m_connection)
        return {};
    // Take it out of reach first: stopping is not the host dying under us, and
    // nothing that reacts to that should run now.
    HostConnection *connection = std::exchange(m_connection, nullptr);
    connection->disconnect(this);
    const QPointer<QObject> ending = connection->stop();
    delete connection;
    forgetHost();
    if (ExtensionSystem::PluginManager::isShuttingDown())
        return ending;
    emit commandsChanged();
    emit stopped();
    return ending;
}

// What a host built up goes with it, whether it was stopped or died.
void ExtensionHost::forgetHost()
{
    // Who waits for an answer gets one, as for the requests already sent.
    const QList<Deferred> deferred = std::exchange(m_deferred, {});
    for (const Deferred &action : deferred) {
        if (action.failed)
            action.failed();
    }
    m_editor.forgetHost();
    // The servers are ours, not node's, and nothing would stop them any more.
    for (const QPointer<AlienClient> &client : std::exchange(m_lspClients, {}))
        stopClient(client);
    m_commands.clear();
    m_paletteCommands.clear();
    m_window.forgetHost();
    m_run.forgetHost();
    // The ids start again with the next host.
    m_watches.clear();
    m_watchedContents.clear();
    m_rewrittenDirectories.clear();
    delete std::exchange(m_fileWatcher, nullptr);
}

// Stopping takes a while, and languageClients() has to know until it is over.
void ExtensionHost::stopClient(AlienClient *client)
{
    if (!client)
        return;
    LanguageClientManager::shutdownClient(client);
    m_stoppingClients.removeIf([](const QPointer<AlienClient> &client) { return !client; });
    m_stoppingClients.append(client);
}

bool ExtensionHost::isRunning() const
{
    return m_connection && m_connection->isRunning();
}

QList<QPointer<AlienClient>> ExtensionHost::languageClients() const
{
    QList<QPointer<AlienClient>> clients;
    for (const QPointer<AlienClient> &client : m_lspClients.values() + m_stoppingClients) {
        if (client)
            clients.append(client);
    }
    return clients;
}

FilePath ExtensionHost::writtenConfigurationFile()
{
    return ICore::userResourcePath("alien/settings.json");
}

// The user writes the file in the extensions directory by hand, so comments
// are allowed in it the way VS Code allows them, and a typo in it names itself
// rather than leaving every override in it silently unread.
QJsonObject ExtensionHost::configurationOverrides(QStringList *errors)
{
    QJsonObject values;
    const FilePaths files = {settings().extensionsDir() / "settings.json",
                             writtenConfigurationFile()};
    for (const FilePath &file : files) {
        const Result<QByteArray> contents = file.fileContents();
        if (!contents)
            continue;
        QJsonParseError error;
        const QJsonDocument document
            = QJsonDocument::fromJson(Utils::cleanJson(*contents), &error);
        if (!document.isObject()) {
            if (errors) {
                *errors << QString("%1: %2").arg(file.toUserOutput(),
                                                 error.error != QJsonParseError::NoError
                                                     ? error.errorString()
                                                     : Tr::tr("Not a JSON object."));
            }
            continue;
        }
        const QJsonObject written = document.object();
        for (auto it = written.begin(); it != written.end(); ++it)
            values.insert(it.key(), it.value());
    }
    return values;
}

// What is already in a file that is about to be rewritten. One that is there
// but does not parse is not overwritten with only the new part.
static Result<QJsonObject> readJsonObject(const FilePath &file)
{
    if (!file.exists())
        return QJsonObject();
    const Result<QByteArray> contents = file.fileContents();
    if (!contents)
        return make_unexpected(contents.error());
    QJsonParseError error;
    const QJsonDocument document
        = QJsonDocument::fromJson(Utils::cleanJson(*contents), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return make_unexpected(Tr::tr("Cannot update \"%1\": %2")
                                   .arg(file.toUserOutput(),
                                        error.error != QJsonParseError::NoError
                                            ? error.errorString()
                                            : Tr::tr("Not a JSON object.")));
    }
    return document.object();
}

Result<> ExtensionHost::writeConfigurationFile(const QJsonObject &values)
{
    const FilePath file = writtenConfigurationFile();
    Result<QJsonObject> existing = readJsonObject(file);
    if (!existing)
        return make_unexpected(existing.error());
    QJsonObject written = *existing;
    for (auto it = values.begin(); it != values.end(); ++it)
        written.insert(it.key(), it.value());

    if (const Result<> dir = file.parentDir().ensureWritableDir(); !dir)
        return dir;
    const Result<qint64> saved
        = file.writeFileContents(QJsonDocument(written).toJson(QJsonDocument::Indented));
    if (!saved)
        return make_unexpected(saved.error());
    return {};
}

// Taking a setting out, which is not the same as writing a null over it: what
// the extension declared as its default has to apply again.
Result<> ExtensionHost::removeConfiguration(const QString &key)
{
    const FilePath file = writtenConfigurationFile();
    Result<QJsonObject> existing = readJsonObject(file);
    if (!existing)
        return make_unexpected(existing.error());
    QJsonObject written = *existing;
    if (!written.contains(key))
        return {};
    written.remove(key);
    if (const Result<> dir = file.parentDir().ensureWritableDir(); !dir)
        return dir;
    const Result<qint64> saved
        = file.writeFileContents(QJsonDocument(written).toJson(QJsonDocument::Indented));
    if (!saved)
        return make_unexpected(saved.error());
    emit configurationWritten();
    return {};
}

Result<> ExtensionHost::writeConfiguration(const QJsonObject &values)
{
    if (const Result<> written = writeConfigurationFile(values); !written)
        return written;
    emit configurationWritten();
    return {};
}

// A setting an extension wrote for one folder is kept here rather than in the
// project's own .vscode/settings.json: that file is what the user wrote there,
// and an editor that is not VS Code rewriting it, comments and all, is not
// what it is for.
FilePath ExtensionHost::folderConfigurationFile(const FilePath &folder)
{
    // The same path on another device is another folder.
    const QByteArray hash = QCryptographicHash::hash(folder.toUrlishString().toUtf8(),
                                                     QCryptographicHash::Sha1);
    return ICore::userResourcePath("alien/folders")
           / QString::fromLatin1(hash.toHex().left(12) + ".json");
}

static Result<QJsonObject> folderConfiguration(const FilePath &folder)
{
    return readJsonObject(ExtensionHost::folderConfigurationFile(folder));
}

static Result<> writeFolderConfigurationFile(const FilePath &folder, const QJsonObject &values)
{
    const FilePath file = ExtensionHost::folderConfigurationFile(folder);
    if (const Result<> dir = file.parentDir().ensureWritableDir(); !dir)
        return dir;
    const Result<qint64> saved
        = file.writeFileContents(QJsonDocument(values).toJson(QJsonDocument::Indented));
    if (!saved)
        return make_unexpected(saved.error());
    return {};
}

Result<> ExtensionHost::writeFolderConfiguration(const FilePath &folder, const QString &key,
                                                 const QJsonValue &value)
{
    Result<QJsonObject> existing = folderConfiguration(folder);
    if (!existing)
        return make_unexpected(existing.error());
    QJsonObject values = *existing;
    values.insert(key, value);
    if (const Result<> written = writeFolderConfigurationFile(folder, values); !written)
        return written;
    emit configurationWritten();
    return {};
}

Result<> ExtensionHost::removeFolderConfiguration(const FilePath &folder, const QString &key)
{
    Result<QJsonObject> existing = folderConfiguration(folder);
    if (!existing)
        return make_unexpected(existing.error());
    QJsonObject values = *existing;
    if (!values.contains(key))
        return {};
    values.remove(key);
    if (const Result<> written = writeFolderConfigurationFile(folder, values); !written)
        return written;
    emit configurationWritten();
    return {};
}

QString ExtensionHost::toHostPath(const FilePath &path) const
{
    return path.path();
}

FilePath ExtensionHost::fromHostPath(const QString &path) const
{
    return m_nodePath.withNewPath(path);
}

bool ExtensionHost::isOnHostDevice(const FilePath &path) const
{
    return path.isSameDevice(m_nodePath);
}

Result<> ExtensionHost::ensureStarted()
{
    if (m_connection)
        return {};

    if (!m_nodePath.isExecutableFile())
        return make_unexpected(Tr::tr("Node.js was not found. Set its path in the settings."));

    // The runtime directory lives on the same device as node, so the host can
    // later be launched on a remote device without changing this code.
    // A fresh name, not a guessable one another user could have put there.
    if (m_runtimeDir.isEmpty() || !m_runtimeDir.isDir()) {
        const Result<FilePath> tmp = m_nodePath.tmpDir();
        if (!tmp)
            return make_unexpected(tmp.error());
        const Result<FilePath> dir = (*tmp / "qtc-alien-XXXXXX").createTempDir();
        if (!dir)
            return make_unexpected(dir.error());
        m_runtimeDir = *dir;
    }

    const FilePath hostJs = m_runtimeDir / "host.js";
    if (const Result<> extracted = extractResource(":/alien/host/host.js", hostJs); !extracted)
        return extracted;

    // Extensions read this next to the application they run in, to find out
    // what that application is; one that is missing fails an activation whole.
    const QJsonObject product{
        {"nameShort", "Qt Creator"},
        {"nameLong", "Qt Creator"},
        {"applicationName", "qtcreator"},
        {"dataFolderName", ".qtcreator"},
        {"serverApplicationName", "qtcreator-server"},
        {"serverDataFolderName", ".qtcreator-server"},
        {"quality", "stable"},
        {"version", Utils::appInfo().displayVersion},
        {"commit", Utils::appInfo().revision},
    };
    const FilePath productJson = m_runtimeDir / "product.json";
    if (const Result<qint64> written = productJson.writeFileContents(
            QJsonDocument(product).toJson(QJsonDocument::Indented));
        !written) {
        return ResultError(written.error());
    }

    m_connection = new HostConnection(this);
    installHandlers();
    m_connection->setCommand({m_nodePath, {toHostPath(hostJs)}});
    m_connection->setWorkingDirectory(m_runtimeDir);

    connect(m_connection, &HostConnection::started, this, [this] {
        // Push configuration and workspace folders before any extension
        // activates and reads them.
        m_connection->sendNotification("configuration/update",
                                       QJsonObject{{"config", m_configuration}});
        m_connection->sendNotification("workspace/updateFolders",
                                       QJsonObject{{"folders", workspaceFoldersWithSettings()}});
        sendKnownExtensions();
        const QList<Deferred> deferred = std::exchange(m_deferred, {});
        for (const Deferred &action : deferred)
            action.run();
    });
    connect(m_connection, &HostConnection::errorOccurred, this, [](const QString &message) {
        MessageManager::writeFlashing(Tr::tr("Alien host error: %1").arg(message));
    });
    connect(m_connection, &HostConnection::finished, this, [this] {
        // Drop the dead connection: kept around, it would make ensureStarted()
        // report a running host forever, and everything that waits for one
        // would queue up behind a process that is not coming back.
        m_connection->deleteLater();
        m_connection = nullptr;
        forgetHost();
        emit commandsChanged();
        emit stopped();
    });

    m_connection->start();
    return {};
}

// One of a workspace edit's file operations. What the options ask for is what
// an extension relies on when it offers the same refactoring twice. A rename
// goes to target.
static Result<> applyFileOperation(const QString &kind, const FilePath &filePath,
                                   const FilePath &target, const QJsonObject &change)
{
    const bool overwrite = change.value("overwrite").toBool();
    const bool ignoreIfExists = change.value("ignoreIfExists").toBool();
    const bool ignoreIfNotExists = change.value("ignoreIfNotExists").toBool();

    if (kind == "create") {
        if (filePath.exists()) {
            if (ignoreIfExists)
                return {};
            if (!overwrite)
                return ResultError(Tr::tr("\"%1\" exists already.").arg(filePath.toUserOutput()));
        }
        if (const Result<> dir = filePath.parentDir().ensureWritableDir(); !dir)
            return dir;
        const Result<qint64> written = filePath.writeFileContents({});
        return written ? Result<>{} : ResultError(written.error());
    }

    if (kind == "delete") {
        if (!filePath.exists()) {
            return ignoreIfNotExists
                       ? Result<>{}
                       : ResultError(Tr::tr("\"%1\" does not exist.").arg(filePath.toUserOutput()));
        }
        if (filePath.isDir()) {
            return change.value("recursive").toBool()
                       ? filePath.removeRecursively()
                       : ResultError(Tr::tr("\"%1\" is a directory.").arg(filePath.toUserOutput()));
        }
        return filePath.removeFile();
    }

    if (kind == "rename") {
        if (target.isEmpty())
            return ResultError(Tr::tr("No new name was given for \"%1\".")
                                   .arg(filePath.toUserOutput()));
        if (target.exists()) {
            if (ignoreIfExists)
                return {};
            if (!overwrite)
                return ResultError(Tr::tr("\"%1\" exists already.").arg(target.toUserOutput()));
            if (const Result<> removed = target.removeFile(); !removed)
                return removed;
        }
        if (const Result<> dir = target.parentDir().ensureWritableDir(); !dir)
            return dir;
        // Through Qt Creator as well: an open editor, and a project listing the
        // file, both have to learn the new name.
        return Core::FileUtils::renameFile(filePath, target)
                   ? Result<>{}
                   : ResultError(Tr::tr("Cannot rename \"%1\" to \"%2\".")
                                     .arg(filePath.toUserOutput(), target.toUserOutput()));
    }

    return ResultError(Tr::tr("Unknown file operation \"%1\".").arg(kind));
}

// One keychain entry per extension and key. The aspects are kept because a
// SecretAspect writes through itself, and a fresh one would not know what it
// had just been told.
static QString secretService(const QString &extension)
{
    return "QtCreator.Alien." + extension;
}

Core::SecretAspect *ExtensionHost::secretFor(const QString &extension, const QString &key)
{
    const QString id = secretService(extension) + '/' + key;
    if (Core::SecretAspect *known = m_secrets.value(id))
        return known;
    auto secret = new Core::SecretAspect(nullptr);
    secret->setSettingsKey(id.toUtf8());
    secret->setService(secretService(extension));
    secret->setKey(key);
    secret->setParent(this);
    m_secrets.insert(id, secret);
    return secret;
}

void ExtensionHost::installHandlers()
{
    m_editor.installHandlers();
    m_window.installHandlers();
    m_run.installHandlers();

    m_connection->setRequestHandler(
        "env/clipboardWrite",
        [](const QJsonValue &params, const HostConnection::Responder &respond) {
            Utils::setClipboardAndSelection(params.toObject().value("text").toString());
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "env/clipboardRead",
        [](const QJsonValue &, const HostConnection::Responder &respond) {
            respond(QGuiApplication::clipboard()->text(), {});
        });

    // Loading a document is not showing it: an extension reads a file this way
    // far more often than it opens an editor for it.
    m_connection->setRequestHandler(
        "workspace/openTextDocument",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            // Cleaned, and answered with: the path an extension builds is often
            // relative to its own directory ("__dirname/../x"), and a document
            // filed under that spelling is not the one Qt Creator later talks
            // about - edits to it would go nowhere.
            const FilePath filePath
                = fromHostPath(params.toObject().value("path").toString()).cleanPath();
            // An open editor holds what the user sees, which may not be on disk.
            if (IDocument *document = DocumentModel::documentForFilePath(filePath)) {
                if (auto textDocument = qobject_cast<TextDocument *>(document)) {
                    respond(QJsonObject{{"text", textDocument->plainText()},
                                        {"languageId", languageIdFor(filePath)},
                                        {"path", toHostPath(filePath)}},
                            {});
                    return;
                }
            }
            const Result<QByteArray> contents = filePath.fileContents();
            if (!contents) {
                respond({}, contents.error());
                return;
            }
            respond(QJsonObject{{"text", QString::fromUtf8(*contents)},
                                {"languageId", languageIdFor(filePath)},
                                {"path", toHostPath(filePath)}},
                    {});
        });

    m_connection->setRequestHandler(
        "workspace/applyEdit",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            for (const QJsonValue &value : params.toObject().value("changes").toArray()) {
                const QJsonObject change = value.toObject();
                const FilePath filePath = fromHostPath(change.value("path").toString());
                const QJsonArray edits = change.value("edits").toArray();

                // Creating, renaming and deleting come in the order the
                // extension built them, so a file created here is there for the
                // edit that follows.
                const QString kind = change.value("kind").toString("edit");
                if (kind != "edit") {
                    const QString newPath = change.value("newPath").toString();
                    const FilePath target = newPath.isEmpty() ? FilePath() : fromHostPath(newPath);
                    if (const Result<> done = applyFileOperation(kind, filePath, target, change);
                        !done) {
                        respond({}, done.error());
                        return;
                    }
                    continue;
                }

                if (const Result<> done = applyTextEdits(filePath, edits); !done) {
                    respond({}, done.error());
                    return;
                }
            }
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "document/save",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const FilePath filePath = fromHostPath(params.toObject().value("path").toString());
            IDocument *document = DocumentModel::documentForFilePath(filePath);
            if (!document || !document->isModified()) {
                respond(true, {}); // nothing of ours is unsaved
                return;
            }
            respond(DocumentManager::saveDocument(document), {});
        });

    m_connection->setRequestHandler(
        "workspace/saveAll",
        [this](const QJsonValue &, const HostConnection::Responder &respond) {
            // It may ask about read-only files, in an event loop that must not
            // run inside the one reading the host's output.
            QMetaObject::invokeMethod(this, [respond] {
                respond(DocumentManager::saveAllModifiedDocumentsSilently(), {});
            }, Qt::QueuedConnection);
        });

    // Commands an extension calls on the editor rather than on itself.
    // Asking for the trash is asking for the file back later.
    m_connection->setRequestHandler(
        "workspace/moveToTrash",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const FilePath file = fromHostPath(params.toObject().value("path").toString());
            if (!file.isLocal() || !QFile::moveToTrash(file.toFSPathString())) {
                respond({}, Tr::tr("Cannot move \"%1\" to the trash.")
                                .arg(file.toUserOutput()));
                return;
            }
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "command/renameSymbol",
        [](const QJsonValue &, const HostConnection::Responder &respond) {
            TextEditorWidget *widget = TextEditorWidget::currentTextEditorWidget();
            if (!widget) {
                respond({}, Tr::tr("There is no editor to rename in."));
                return;
            }
            widget->renameSymbolUnderCursor();
            respond(true, {});
        });

    // An extension's own page is where Qt Creator keeps its settings.
    m_connection->setRequestHandler(
        "command/openExtension",
        [](const QJsonValue &, const HostConnection::Responder &respond) {
            ICore::showSettings(Constants::SETTINGS_ID);
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "command/copyFilePath",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QString path = params.toObject().value("path").toString();
            const FilePath file = path.isEmpty()
                                      ? (EditorManager::currentDocument()
                                             ? EditorManager::currentDocument()->filePath()
                                             : FilePath())
                                      : fromHostPath(path);
            if (file.isEmpty()) {
                respond({}, Tr::tr("There is no file to copy the path of."));
                return;
            }
            Utils::setClipboardAndSelection(file.toUserOutput());
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "command/quickOpen",
        [](const QJsonValue &params, const HostConnection::Responder &respond) {
            Core::LocatorManager::show(params.toObject().value("text").toString());
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "command/showIssues",
        [](const QJsonValue &, const HostConnection::Responder &respond) {
            for (IOutputPane *pane : IOutputPane::allOutputPanes()) {
                if (pane->id() == "ProjectExplorer.TaskWindow")
                    pane->popup(IOutputPane::ModeSwitch);
            }
            respond(true, {});
        });

    // The editor Qt Creator has for this, rather than one of the extension's
    // own making.
    m_connection->setRequestHandler(
        "command/showMarkdownPreview",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QString path = params.toObject().value("path").toString();
            const FilePath file = path.isEmpty()
                                      ? (EditorManager::currentDocument()
                                             ? EditorManager::currentDocument()->filePath()
                                             : FilePath())
                                      : fromHostPath(path);
            if (file.isEmpty() || !EditorManager::openEditor(file, "Editors.MarkdownViewer")) {
                respond({}, Tr::tr("Cannot show a Markdown preview of \"%1\".")
                                .arg(file.toUserOutput()));
                return;
            }
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "command/openSettings",
        [](const QJsonValue &params, const HostConnection::Responder &respond) {
            // With a setting named, the page opens on whoever declares it.
            const QString query = params.toObject().value("query").toString();
            ICore::showSettings(Constants::SETTINGS_ID,
                                query.isEmpty() ? Id() : Id::fromString(query));
            settings().enabledExtensions.openPreselected(); // for a page built before
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "extension/activate",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QString id = params.toObject().value("id").toString();
            if (!startExtension) {
                respond({}, Tr::tr("\"%1\" cannot be started.").arg(id));
                return;
            }
            startExtension(id, [respond, id](bool started) {
                if (started)
                    respond(true, {});
                else
                    respond({}, Tr::tr("\"%1\" did not start.").arg(id));
            });
        });

    m_connection->setRequestHandler(
        "command/wakeOwner",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QString command = params.toObject().value("command").toString();
            if (!wakeCommandOwner) {
                respond(false, {});
                return;
            }
            wakeCommandOwner(command, [respond](bool woken) { respond(woken, {}); });
        });

    m_connection->setRequestHandler(
        "command/formatDocument",
        [](const QJsonValue &, const HostConnection::Responder &respond) {
            TextDocument *document = TextDocument::currentTextDocument();
            if (!document) {
                respond({}, Tr::tr("No document is open."));
                return;
            }
            document->autoFormat(QTextCursor(document->document()));
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "command/revealInFileManager",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const FilePath filePath = fromHostPath(params.toObject().value("path").toString());
            if (!filePath.exists()) {
                respond({}, Tr::tr("\"%1\" does not exist.").arg(filePath.toUserOutput()));
                return;
            }
            Core::FileUtils::showInGraphicalShell(filePath);
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "command/openFolder",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const FilePath filePath = fromHostPath(params.toObject().value("path").toString());
            const ProjectExplorer::OpenProjectResult opened
                = ProjectExplorer::ProjectExplorerPlugin::openProject(filePath);
            if (!opened) {
                respond({}, opened.errorMessage());
                return;
            }
            respond(true, {});
        });

    m_connection->setRequestHandler(
        "workspace/changeFolders",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            QStringList errors;
            for (const QJsonValue &folder : object.value("added").toArray()) {
                const FilePath dir = fromHostPath(folder.toObject().value("path").toString());
                const ProjectExplorer::OpenProjectResult opened
                    = ProjectExplorer::ProjectExplorerPlugin::openProject(dir);
                if (!opened && opened.alreadyOpen().isEmpty())
                    errors.append(opened.errorMessage());
            }
            const QList<ProjectExplorer::Project *> open
                = ProjectExplorer::ProjectManager::projects();
            QList<ProjectExplorer::Project *> closing;
            for (const QJsonValue &path : object.value("removed").toArray()) {
                const FilePath dir = fromHostPath(path.toString());
                // Projects side by side in one directory are a folder each.
                for (ProjectExplorer::Project *project : open) {
                    if (project->projectDirectory() == dir && !closing.contains(project)) {
                        closing.append(project);
                        break;
                    }
                }
            }
            if (!closing.isEmpty())
                ProjectExplorer::ProjectManager::removeProjects(closing);
            if (!errors.isEmpty()) {
                respond({}, errors.join('\n'));
                return;
            }
            respond(true, {});
        });

    // Where an extension's secrets live: the keychain Qt Creator uses for its
    // own, keyed per extension so one cannot read another's.
    m_connection->setRequestHandler(
        "secrets/read",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            SecretAspect *secret = secretFor(object.value("extension").toString(),
                                             object.value("key").toString());
            secret->requestValue([respond](const Result<QString> &value) {
                // Nothing stored is not a failure: an extension asks before it
                // has ever stored anything.
                respond(value && !value->isEmpty() ? QJsonValue(*value) : QJsonValue::Null, {});
            });
        });

    m_connection->setRequestHandler(
        "secrets/write",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString extension = object.value("extension").toString();
            const QString key = object.value("key").toString();
            const QJsonValue value = object.value("value");
            if (value.isNull()) {
                Core::deleteSecret(secretService(extension), key);
                if (Core::SecretAspect *old = m_secrets.take(secretService(extension) + '/' + key))
                    old->deleteLater();
                respond(true, {});
                return;
            }
            // setValue only remembers it; writeSettings is what puts it in the
            // keychain, and without that it would be gone at the next start -
            // which is the whole point of storing it.
            Core::SecretAspect *secret = secretFor(extension, key);
            secret->setValue(value.toString());
            secret->writeSettings();
            respond(true, {});
        });

    // What an extension asked to be told about. Qt Creator does the watching:
    // it knows the file system the documents live on, including a remote one.
    m_connection->setNotificationHandler("watch/add", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        addFileWatch(object.value("id").toString(),
                     object.value("pattern").toString(),
                     object.value("base").toString());
    });

    m_connection->setNotificationHandler("watch/remove", [this](const QJsonValue &params) {
        const FileWatch removed = m_watches.take(params.toObject().value("id").toString());
        for (const FilePath &directory : removed.roots) {
            if (m_fileWatcher && !watchesDirectory(directory)) {
                m_fileWatcher->removeDirectory(directory);
                m_fileWatcher->removeFiles(
                    Utils::filtered(m_fileWatcher->files(), [&directory](const FilePath &file) {
                        return file.parentDir() == directory;
                    }));
                m_watchedContents.remove(directory);
            }
        }
    });

    // An extension writing a setting: kept in Qt Creator's own file, not in the
    // extensions directory, which may be the one VS Code installed into.
    m_connection->setRequestHandler(
        "configuration/write",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString key = object.value("key").toString();
            if (key.isEmpty()) {
                respond({}, Tr::tr("Cannot write a setting without a name."));
                return;
            }
            if (object.value("remove").toBool()) {
                if (const Result<> removed = removeConfiguration(key); !removed) {
                    respond({}, removed.error());
                    return;
                }
                respond(true, {});
                return;
            }
            if (const Result<> written
                = writeConfiguration({{key, object.value("value")}});
                !written) {
                respond({}, written.error());
                return;
            }
            respond(true, {});
        });

    // The same for one folder: VS Code writes this into the folder's own
    // settings, so a choice made for one project does not answer for the next.
    m_connection->setRequestHandler(
        "configuration/writeFolder",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString key = object.value("key").toString();
            if (key.isEmpty()) {
                respond({}, Tr::tr("Cannot write a setting without a name."));
                return;
            }
            const FilePath folder = fromHostPath(object.value("folder").toString());
            const Result<> written = object.value("remove").toBool()
                                         ? removeFolderConfiguration(folder, key)
                                         : writeFolderConfiguration(folder, key,
                                                                    object.value("value"));
            if (!written) {
                respond({}, written.error());
                return;
            }
            respond(true, {});
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
                roots << fromHostPath(base);
            } else {
                for (const QJsonValue &folder : m_workspaceFolders)
                    roots << fromHostPath(folder.toObject().value("path").toString());
            }

            const QRegularExpression includeRe = globExpression(include);
            const QRegularExpression excludeRe
                = exclude.isEmpty() ? QRegularExpression() : globExpression(exclude);

            // A whole tree, build directories and all, maybe on a device: not
            // in the GUI thread.
            const auto search = [roots, includeRe, excludeRe, exclude,
                                 maxResults](QPromise<FilePaths> &promise) {
                FilePaths found;
                for (const FilePath &root : roots) {
                    root.iterateDirectory(
                        [&](const FilePath &item) {
                            if (promise.isCanceled())
                                return IterationPolicy::Stop;
                            const QString relative = item.relativePathFromDir(root);
                            if (!includeRe.match(relative).hasMatch())
                                return IterationPolicy::Continue;
                            if (!exclude.isEmpty() && excludeRe.match(relative).hasMatch())
                                return IterationPolicy::Continue;
                            found.append(item);
                            return maxResults > 0 && found.size() >= maxResults
                                       ? IterationPolicy::Stop : IterationPolicy::Continue;
                        },
                        FileFilter({}, DirFilterFlag::Files, DirIteratorFlag::Subdirectories));
                    if (promise.isCanceled() || (maxResults > 0 && found.size() >= maxResults))
                        break;
                }
                promise.addResult(found);
            };
            Utils::futureSynchronizer()->addFuture(Utils::onResultReady(
                Utils::asyncRun(search), this, [this, respond](const FilePaths &found) {
                    QJsonArray paths;
                    for (const FilePath &path : found)
                        paths.append(toHostPath(path));
                    respond(paths, {});
                }));
        });

    m_connection->setNotificationHandler("commands/register", [this](const QJsonValue &params) {
        const QString command = params.toObject().value("command").toString();
        if (!command.isEmpty() && !m_commands.contains(command)) {
            m_commands.append(command);
            emit commandsChanged();
        }
    });

    m_connection->setNotificationHandler("commands/palette", [this](const QJsonValue &params) {
        QStringList commands;
        for (const QJsonValue &command : params.toObject().value("commands").toArray())
            commands.append(command.toString());
        if (commands == m_paletteCommands)
            return;
        m_paletteCommands = commands;
        emit commandsChanged();
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
            const CommandLine commandLine(fromHostPath(command.value("path").toString()),
                                          args);

            LanguageFilter filter;
            for (const QJsonValue &pattern : object.value("filePatterns").toArray())
                filter.filePattern << pattern.toString();

            const FilePath cwd = fromHostPath(object.value("cwd").toString());
            const QJsonObject initOptions = object.value("initializationOptions").toObject();

            stopClient(m_lspClients.take(id));

            m_lspClients.insert(
                id, new AlienClient(name.isEmpty() ? id : name, commandLine, filter, cwd, initOptions));
            emit languageClientStarted(id);
            respond(QJsonValue(QJsonValue::Null), {});
        });

    m_connection->setNotificationHandler("languageclient/stop", [this](const QJsonValue &params) {
        const QString id = params.toObject().value("id").toString();
        stopClient(m_lspClients.take(id));
    });

    m_connection->setNotificationHandler("log", [](const QJsonValue &params) {
        qCDebug(logHost).noquote() << params.toObject().value("message").toString();
    });

    m_connection->setNotificationHandler("host/fatal", [this](const QJsonValue &params) {
        const QString message = params.toObject().value("message").toString();
        MessageManager::writeFlashing(
            Tr::tr("The extension host hit an error it could not recover from. Extensions "
                   "may not work until it is restarted.\n%1").arg(message));
        emit hostFailed(message);
    });

    m_connection->setNotificationHandler("host/ready", [](const QJsonValue &params) {
        qCDebug(logHost) << "Host ready, node" << params.toObject().value("node").toString();
    });
}

void ExtensionHost::whenReady(const std::function<void()> &action,
                              const std::function<void()> &failed)
{
    if (isRunning()) {
        action();
    } else if (m_connection) {
        m_deferred.append({action, failed});
    } else if (failed) {
        // No host is on its way, and none would ever take the queue.
        failed();
    }
}

QJsonObject ExtensionHost::manifestAdapterFor(const QString &type) const
{
    for (const VscodeManifest &manifest : m_knownExtensions) {
        for (const VscodeDebugger &debugger : manifest.debuggers) {
            if (debugger.type != type || debugger.program.isEmpty())
                continue;
            const FilePath program = manifest.rootDir.resolvePath(debugger.program);
            if (debugger.runtime.isEmpty()) {
                return QJsonObject{{"kind", "executable"},
                                   {"command", program.toFSPathString()}};
            }
            // The adapter is a script: it is run by what the manifest says runs
            // it, and node means the one the host itself was started with.
            const FilePath runtime
                = debugger.runtime == "node"
                      ? settings().nodeJsPath()
                      : FilePath::fromUserInput(debugger.runtime).searchInPath();
            return QJsonObject{{"kind", "executable"},
                               {"command", runtime.toFSPathString()},
                               {"args", QJsonArray{program.toFSPathString()}}};
        }
    }
    return {};
}

void ExtensionHost::setKnownExtensions(const QList<VscodeManifest> &manifests)
{
    m_knownExtensions = manifests;
    // A token type only this extension knows is drawn as what it is built on,
    // which is the part of its name the editor already has a style for.
    for (const VscodeManifest &manifest : manifests) {
        for (const QString &type : manifest.semanticTokenTypes) {
            for (const QString &known : {QString("type"), QString("function"),
                                         QString("variable"), QString("keyword"),
                                         QString("string"), QString("number"),
                                         QString("comment"), QString("parameter"),
                                         QString("property")}) {
                if (type.contains(known, Qt::CaseInsensitive)) {
                    m_editor.m_tokenTypeAliases.insert(type, known);
                    break;
                }
            }
        }
    }
    // A host that is still to come is told when it starts.
    if (isRunning())
        sendKnownExtensions();
}

void ExtensionHost::sendKnownExtensions()
{
    QJsonArray extensions;
    for (const VscodeManifest &manifest : std::as_const(m_knownExtensions)) {
        extensions.append(QJsonObject{{"id", manifest.qualifiedId()},
                                      {"path", toHostPath(manifest.rootDir)},
                                      {"packageJSON", manifest.rawPackageJson}});
    }
    m_connection->sendNotification("extensions/known", QJsonObject{{"extensions", extensions}});
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
    m_declaredLanguages.removeIf([&id](const auto &declared) { return declared.first == id; });
    QJsonArray languages;
    for (const VscodeLanguage &language : manifest.languages) {
        QJsonArray extensions;
        for (const QString &extension : language.extensions)
            extensions.append(extension);
        m_declaredLanguages.append({id, language});
        languages.append(QJsonObject{{"id", language.id}, {"extensions", extensions}});
    }

    const QJsonObject params{
        {"id", id},
        {"path", toHostPath(manifest.rootDir)},
        {"main", toHostPath(manifest.mainPath())},
        // Where the extension may keep things between runs. Only for a local
        // host: a host on a device cannot reach Qt Creator's own directories,
        // and falls back to a directory of its own.
        {"storagePath",
         m_nodePath.isLocal()
             ? ICore::userResourcePath("alien/" + manifest.qualifiedId()).toFSPathString()
             : QString()},
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
                    return;
                }
                // Whatever it was going to register, it has registered by now.
                emit activated(id);
            });
        m_editor.ensureDocumentSync();
        m_editor.ensureEditorFeatures();
    }, [this, id] { emit activationFailed(id, Tr::tr("The extension host stopped.")); });
}

void ExtensionHost::deactivate(const QString &id)
{
    m_declaredLanguages.removeIf([&id](const auto &declared) { return declared.first == id; });

    // Stop the language servers this extension started, even if it did not
    // dispose them itself. Their key is "<extensionId>:<clientId>".
    const QString prefix = id + ':';
    for (const QString &key : m_lspClients.keys()) {
        if (key.startsWith(prefix))
            stopClient(m_lspClients.take(key));
    }

    // Behind an activation that still waits for the host to start.
    whenReady([this, id] {
        m_connection->sendRequest("deactivate", QJsonObject{{"id", id}},
                                  [](const QJsonValue &, const QString &) {});
    });
}

#ifdef WITH_TESTS
Result<> ExtensionHost::activateBundledTestExtension(const QString &directory,
                                                     const QStringList &alsoNeeded)
{
    if (const Result<> started = ensureStarted(); !started)
        return started;

    const FilePath packageJson = m_runtimeDir / directory / "package.json";
    for (const QString &file : QStringList{directory + "/package.json",
                                           directory + "/extension.js"} + alsoNeeded) {
        if (const Result<> r = extractResource(":/alien/host/" + file, m_runtimeDir / file); !r)
            return r;
    }

    const Result<VscodeManifest> manifest = VscodeManifest::fromPackageJson(packageJson);
    if (!manifest)
        return make_unexpected(manifest.error());

    activate(*manifest);
    return {};
}
#endif // WITH_TESTS

void ExtensionHost::setConfiguration(const QJsonObject &configuration)
{
    if (configuration == m_configuration)
        return;
    m_configuration = configuration;
    if (m_connection && m_connection->isRunning())
        m_connection->sendNotification("configuration/update", QJsonObject{{"config", configuration}});
}

// What a folder says about a setting itself, which VS Code reads from the
// project's .vscode/settings.json and hands to an extension that asks with the
// folder as the scope. qt-core keeps the Qt installation a project is built
// against there, so without this a project cannot name its own Qt.
static QJsonObject folderSettings(const FilePath &folder)
{
    QJsonObject settings;
    const FilePath file = folder / ".vscode" / "settings.json";
    if (const Result<QByteArray> contents = file.fileContents()) {
        QJsonParseError error;
        // Comments and trailing commas are allowed in there, and people write them.
        const QJsonDocument document
            = QJsonDocument::fromJson(Utils::cleanJson(*contents), &error);
        if (error.error == QJsonParseError::NoError)
            settings = document.object();
        else
            qCWarning(logHost) << "settings of" << folder << ":" << error.errorString();
    }
    // What an extension wrote for this folder is the later write, so it wins.
    const Result<QJsonObject> read = folderConfiguration(folder);
    if (!read)
        qCWarning(logHost) << read.error();
    const QJsonObject written = read.value_or(QJsonObject());
    for (auto it = written.begin(); it != written.end(); ++it)
        settings.insert(it.key(), it.value());
    return settings;
}

QJsonArray ExtensionHost::workspaceFoldersWithSettings() const
{
    QJsonArray folders;
    for (const QJsonValue &value : m_workspaceFolders) {
        QJsonObject folder = value.toObject();
        const QJsonObject settings = folderSettings(fromHostPath(folder.value("path").toString()));
        if (!settings.isEmpty())
            folder["settings"] = settings;
        folders.append(folder);
    }
    return folders;
}

void ExtensionHost::setWorkspaceFolders(const QJsonArray &folders)
{
    if (folders == m_workspaceFolders)
        return;
    m_workspaceFolders = folders;
    if (m_connection && m_connection->isRunning())
        m_connection->sendNotification("workspace/updateFolders",
                                       QJsonObject{{"folders", workspaceFoldersWithSettings()}});
}

AlienClient *ExtensionHost::languageClient(const QString &id) const
{
    return m_lspClients.value(id);
}

// The globs a language contribution lists are matched the way a documentSelector
// is: "**" crosses directories, "*" does not, and a pattern without a separator
// is about the name alone ("Dockerfile.*") rather than the path.
static bool globMatches(const QString &pattern, const FilePath &filePath)
{
    // The same reading of a glob as everywhere else: brace groups included, and
    // "**/" spanning any number of directories or none.
    const QString subject = pattern.contains('/') ? filePath.path() : filePath.fileName();
    // Asked on every keystroke. Only the GUI thread asks, so one cache does.
    static QHash<QString, QRegularExpression> expressions;
    auto it = expressions.find(pattern);
    if (it == expressions.end()) {
        it = expressions.insert(pattern, globExpression(pattern));
        it->optimize();
    }
    return it->match(subject).hasMatch();
}

bool languageMatchesFile(const VscodeLanguage &language, const FilePath &filePath)
{
    // An extension may have dots of its own, as ".d.ts" does.
    const QString name = filePath.fileName();
    for (const QString &extension : language.extensions) {
        QString declared = extension;
        if (!declared.startsWith('.'))
            declared.prepend('.');
        if (name.endsWith(declared, Qt::CaseInsensitive))
            return true;
    }
    for (const QString &fileName : language.filenames) {
        if (fileName.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return Utils::anyOf(language.filenamePatterns, [&filePath](const QString &pattern) {
        return globMatches(pattern, filePath);
    });
}

// A language an extension does not contribute itself - C++, Python, JSON - is
// one the editor is expected to know already. Without that, every real source
// file is "plaintext" to every extension: nothing waiting for "onLanguage:cpp"
// starts, and what does run is asked about the wrong language.
QString builtinLanguageId(const FilePath &filePath)
{
    static const QHash<QString, QString> ids = {
        {"text/x-c++src", "cpp"}, {"text/x-c++hdr", "cpp"},
        {"text/x-csrc", "c"}, {"text/x-chdr", "c"}, {"text/x-c", "c"},
        {"text/vnd.nvidia.cuda.csrc", "cuda-cpp"},
        {"text/x-objcsrc", "objective-c"}, {"text/x-objc++src", "objective-cpp"},
        {"text/x-python", "python"}, {"text/x-python3", "python"},
        {"text/x-java", "java"}, {"text/x-java-source", "java"},
        {"text/x-csharp", "csharp"}, {"text/x-go", "go"}, {"text/rust", "rust"},
        {"text/x-swift", "swift"}, {"text/x-lua", "lua"}, {"text/x-ruby", "ruby"},
        {"text/x-perl", "perl"}, {"text/x-php", "php"},
        {"application/javascript", "javascript"}, {"text/javascript", "javascript"},
        {"application/x-javascript-module", "javascript"},
        {"application/typescript", "typescript"}, {"text/typescript", "typescript"},
        {"application/json", "json"}, {"application/schema+json", "json"},
        {"application/xml", "xml"}, {"text/xml", "xml"},
        {"text/x-yaml", "yaml"}, {"application/x-yaml", "yaml"},
        {"text/markdown", "markdown"}, {"text/html", "html"}, {"text/css", "css"},
        {"text/x-shellscript", "shellscript"}, {"application/x-shellscript", "shellscript"},
        {"text/x-cmake", "cmake"}, {"text/x-qml", "qml"}, {"application/x-qml", "qml"},
        {"text/x-qdoc", "qdoc"},
        {"text/plain", "plaintext"},
    };

    // A Dockerfile has neither a suffix nor a mime type here, and it is one of
    // the commonest things an extension waits for, so the name answers first.
    const QString name = filePath.fileName().toLower();
    if (name == "dockerfile" || name == "containerfile"
        || name.startsWith("dockerfile.") || name.endsWith(".dockerfile")) {
        return "dockerfile";
    }

    QString found;
    // The nearest match wins, but a file Qt Creator knows only as a kind of C++
    // header should still answer "cpp", so its parents are asked in turn.
    Utils::visitMimeParents(Utils::mimeTypeForFile(filePath, Utils::MimeMatchMode::MatchExtension),
                            [&found](const Utils::MimeType &type) {
                                found = ids.value(type.name());
                                return found.isEmpty();
                            });
    return found;
}

QString ExtensionHost::languageIdFor(const FilePath &filePath) const
{
    const QString override = m_languageOverrides.value(filePath);
    if (!override.isEmpty())
        return override;
    for (const auto &[extensionId, language] : m_declaredLanguages) {
        if (languageMatchesFile(language, filePath))
            return language.id;
    }
    const QString known = builtinLanguageId(filePath);
    return known.isEmpty() ? QString("plaintext") : known;
}

// One watch an extension asked for. The pattern says which files it means; the
// directories under the base are what is actually watched, because that is what
// a file system reports on.
void ExtensionHost::addFileWatch(const QString &id, const QString &pattern, const QString &base)
{
    FileWatch watch;
    watch.id = id;
    watch.matcher = globExpression(pattern);
    watch.absolute = pattern.startsWith('/');
    // A pattern reaching below the base, as "**/" does in nearly every one an
    // extension writes, needs the directories there watched as well.
    const bool recursive = pattern.contains('/');

    FilePaths roots;
    if (!base.isEmpty()) {
        roots.append(fromHostPath(base));
    } else {
        for (const QJsonValue &folder : std::as_const(m_workspaceFolders))
            roots.append(fromHostPath(folder.toObject().value("path").toString()));
    }
    if (roots.isEmpty())
        return;
    watch.bases = roots;

    const auto watchDirectory = [this, &watch](const FilePath &directory) {
        watch.roots.append(directory);
        if (!fileWatcher()->watchesDirectory(directory))
            fileWatcher()->addDirectory(directory, Utils::FileSystemWatcher::WatchAllChanges);
        // What is in there already is not news: without this the first event
        // presents the whole directory as just created.
        if (!m_watchedContents.contains(directory))
            m_watchedContents.insert(directory, directoryStamps(directory));
    };

    for (const FilePath &root : roots) {
        if (!root.isDir())
            continue;
        watchDirectory(root);
        if (!recursive)
            continue;
        // One level of directories as well, which covers where extensions
        // actually look without walking a whole checkout.
        const FilePaths children = root.dirEntries(Utils::DirFilterFlags(Utils::DirFilterFlag::Dirs
                                                  | Utils::DirFilterFlag::NoDotAndDotDot));
        for (const FilePath &child : children)
            watchDirectory(child);
    }
    m_watches.insert(id, watch);
    for (const FilePath &directory : std::as_const(watch.roots))
        watchFilesIn(directory);
}

// The files themselves as well: one rewritten in place changes nothing in its
// directory that inotify reports.
void ExtensionHost::watchFilesIn(const FilePath &directory)
{
    FilePaths files;
    for (const FilePath &file : m_watchedContents.value(directory).keys()) {
        const bool wanted = Utils::anyOf(m_watches, [&](const FileWatch &watch) {
            return watch.roots.contains(directory) && watchMatches(watch, file);
        });
        if (wanted && !fileWatcher()->watchesFile(file))
            files.append(file);
    }
    fileWatcher()->addFiles(files, Utils::FileSystemWatcher::WatchModifiedDate);
}

bool ExtensionHost::watchesDirectory(const FilePath &directory) const
{
    for (const FileWatch &watch : m_watches) {
        if (watch.roots.contains(directory))
            return true;
    }
    return false;
}

Utils::FileSystemWatcher *ExtensionHost::fileWatcher()
{
    if (!m_fileWatcher) {
        m_fileWatcher = new Utils::FileSystemWatcher(this);
        connect(m_fileWatcher, &Utils::FileSystemWatcher::directoryChanged,
                this, &ExtensionHost::handleWatchedDirectory);
        // Read once the writes have stopped, as a directory is.
        auto settle = new QTimer(m_fileWatcher);
        settle->setSingleShot(true);
        settle->setInterval(100);
        connect(m_fileWatcher, &Utils::FileSystemWatcher::fileChanged, settle,
                [this, settle](const FilePath &file) {
                    m_rewrittenDirectories.insert(file.parentDir());
                    settle->start();
                });
        connect(settle, &QTimer::timeout, this, [this] {
            for (const FilePath &directory : std::exchange(m_rewrittenDirectories, {})) {
                if (m_watchedContents.contains(directory))
                    handleWatchedDirectory(directory);
            }
        });
    }
    return m_fileWatcher;
}

ExtensionHost::DirectoryStamps ExtensionHost::directoryStamps(const FilePath &directory)
{
    const FilePaths files = directory.dirEntries(Utils::DirFilterFlags(Utils::DirFilterFlag::Files
                                                | Utils::DirFilterFlag::NoDotAndDotDot));
    DirectoryStamps stamps;
    for (const FilePath &file : files)
        stamps.insert(file, FileStamp{file.lastModified(), file.fileSize()});
    return stamps;
}

// The same reading of the pattern as for finding files: relative to the base it
// was given for.
bool ExtensionHost::watchMatches(const FileWatch &watch, const FilePath &file) const
{
    if (watch.absolute)
        return watch.matcher.match(toHostPath(file)).hasMatch();
    return Utils::anyOf(watch.bases, [&watch, &file](const FilePath &base) {
        return file.isChildOf(base)
               && watch.matcher.match(file.relativePathFromDir(base)).hasMatch();
    });
}

// A directory changed: which of its files did, and which watch wanted to know.
void ExtensionHost::handleWatchedDirectory(const FilePath &directory)
{
    if (!m_connection)
        return;
    const DirectoryStamps now = directoryStamps(directory);
    const DirectoryStamps before = m_watchedContents.value(directory);
    m_watchedContents.insert(directory, now);
    watchFilesIn(directory);

    const auto report = [this, &directory](const FilePath &file, const QString &kind) {
        for (const FileWatch &watch : std::as_const(m_watches)) {
            if (!watch.roots.contains(directory) || !watchMatches(watch, file))
                continue;
            m_connection->sendNotification("watch/event",
                                           QJsonObject{{"id", watch.id},
                                                       {"path", toHostPath(file)},
                                                       {"kind", kind}});
        }
    };

    // A file the directory event was not about is one an extension must not
    // hear of: it reads what it is told changed, and reading a file that is
    // being written at that moment is how qt-cpp came to parse half a
    // cmake-tools-kits.json.
    for (auto it = now.cbegin(), end = now.cend(); it != end; ++it) {
        const auto was = before.constFind(it.key());
        if (was == before.cend())
            report(it.key(), "created");
        else if (!(was.value() == it.value()))
            report(it.key(), "changed");
    }
    for (auto it = before.cbegin(), end = before.cend(); it != end; ++it) {
        if (!now.contains(it.key()))
            report(it.key(), "deleted");
    }
}

void ExtensionHost::executeCommand(const QString &command, const QJsonArray &arguments,
                                   const QString &when)
{
    if (!m_connection)
        return;
    whenReady([this, command, arguments, when] {
        m_connection->sendRequest(
            "executeCommand",
            QJsonObject{{"command", command}, {"args", arguments}, {"when", when}},
            [command](const QJsonValue &, const QString &error) {
                if (!error.isEmpty()) {
                    MessageManager::writeFlashing(
                        Tr::tr("Command \"%1\" failed: %2").arg(command, error));
                }
            });
    });
}

} // namespace Alien::Internal
