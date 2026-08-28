// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionhost.h"

#include "alienclient.h"
#include "aliencompletion.h"
#include "alienconstants.h"
#include "aliensettings.h"
#include "alienhover.h"
#include "alientr.h"
#include "alientreeview.h"
#include "codicons.h"
#include "hostconnection.h"
#include "webviewrenderer.h"

#include <debugger/debuggerruncontrol.h>

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/fileutils.h>
#include <coreplugin/secretaspect.h>
#include <coreplugin/find/searchresultwindow.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/ioutputpane.h>
#include <coreplugin/locator/locatormanager.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <languageclient/languageclientmanager.h>
#include <languageclient/snippet.h>
#include <languageclient/languageclientsettings.h>

#include <projectexplorer/kit.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/taskhub.h>

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/completionassistprovider.h>
#include <texteditor/codeassist/functionhintproposal.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/ifunctionhintproposalmodel.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/codeassist/iassistprovider.h>
#include <texteditor/quickfix.h>
#include <texteditor/fontsettings.h>
#include <texteditor/autocompleter.h>
#include <texteditor/tabsettings.h>
#include <texteditor/textmark.h>
#include <texteditor/textdocument.h>
#include <texteditor/semantichighlighter.h>
#include <texteditor/tabsettings.h>
#include <texteditor/textsuggestion.h>
#include <texteditor/textdocumentlayout.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditor.h>
#include <texteditor/texteditorconstants.h>
#include <texteditor/textmark.h>

#include <utils/appinfo.h>
#include <utils/changeset.h>
#include <utils/filesystemwatcher.h>
#include <utils/commandline.h>
#include <utils/fileutils.h>
#include <utils/link.h>
#include <utils/mimeutils.h>
#include <utils/qtcprocess.h>
#include <utils/terminalhooks.h>
#include <utils/theme/theme.h>

#include <QTextBlock>

#include <QCoreApplication>
#include <QFile>
#include <utils/stringutils.h>
#include <QClipboard>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QJsonArray>
#include <QInputDialog>
#include <QDesktopServices>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QLoggingCategory>
#include <QPainter>
#include <QStyle>
#include <QRegularExpression>

using namespace Core;
using namespace LanguageClient;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

static Q_LOGGING_CATEGORY(logHost, "qtc.alien.host", QtWarningMsg)

namespace Alien::Internal {

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
static int offsetOf(const QString &text, const QJsonValue &position)
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
// move the next one: sort by position and work backwards.
static QList<QJsonObject> editsBackToFront(const QString &text, const QJsonArray &edits)
{
    QList<QJsonObject> sorted;
    for (const QJsonValue &value : edits)
        sorted.append(value.toObject());
    Utils::sort(sorted, [&text](const QJsonObject &a, const QJsonObject &b) {
        return offsetOf(text, a.value("range").toObject().value("start"))
               > offsetOf(text, b.value("range").toObject().value("start"));
    });
    return sorted;
}

// Runs a command line the way Qt Creator runs anything: in Application Output,
// with a stop button and an exit code. Used for what an extension calls a task
// and for what it calls a terminal - it cannot type into either, so both come
// down to "run this and let the user watch".
// The exit code is filled in when the process finishes, for whoever is waiting
// to be told how it went.
static Result<ProjectExplorer::RunControl *> runVisibly(
    const QString &name, const CommandLine &command, const FilePath &workingDirectory,
    const Utils::EnvironmentItems &environmentChanges = {},
    const std::shared_ptr<int> &exitCode = {})
{
    Kit *kit = KitManager::defaultKit();
    if (!kit)
        return make_unexpected(Tr::tr("Cannot run \"%1\": no kit to run it with.").arg(name));

    auto runControl = new RunControl(ProjectExplorer::Constants::NORMAL_RUN_MODE);
    runControl->setKit(kit);
    runControl->setDisplayName(name);
    runControl->setCommandLine(command);
    if (!workingDirectory.isEmpty())
        runControl->setWorkingDirectory(workingDirectory);
    // What an extension puts in the environment is what makes its command
    // work: zephyr-ide runs "west" from the workspace's Python environment,
    // and only the PATH it passes leads there.
    if (!environmentChanges.isEmpty()) {
        Environment environment = kit->buildEnvironment();
        environment.modify(environmentChanges);
        runControl->setEnvironment(environment);
    }
    // The exit code is only in hand here, on the process itself: RunControl
    // reports that it stopped, not what the process made of it.
    const auto observe = [exitCode](Utils::Process &process) {
        if (!exitCode)
            return QtTaskTree::SetupResult::Continue;
        QObject::connect(&process, &Utils::Process::done, &process, [exitCode, &process] {
            *exitCode = process.exitCode();
        });
        return QtTaskTree::SetupResult::Continue;
    };
    runControl->setRunRecipe(runControl->processRecipe(runControl->processTask(observe)));
    runControl->start();
    return runControl;
}

static Utils::EnvironmentItems environmentChanges(const QJsonObject &environment)
{
    Utils::EnvironmentItems items;
    for (auto it = environment.begin(); it != environment.end(); ++it)
        items.append({it.key(), it.value().toString()});
    return items;
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
    for (const Tasks &tasks : std::as_const(m_diagnosticTasks)) {
        for (const Task &task : tasks)
            TaskHub::removeTask(task);
    }

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

    // What was put on a document has to come off it: the document outlives the
    // host, and an extension's marks, colours and folds are not the editor's
    // own. Folding matters most - left switched to externally provided, a
    // document can never fold again once nobody is providing it.
    for (const QList<TextMark *> &marks : std::as_const(m_codeLenses))
        qDeleteAll(marks);
    for (const QList<TextMark *> &marks : std::as_const(m_colors))
        qDeleteAll(marks);
    for (const FilePath &path : std::as_const(m_foldingDocuments)) {
        if (TextDocument *document = TextDocument::textDocumentForFilePath(path)) {
            QTextDocument *contents = document->document();
            for (QTextBlock block = contents->begin(); block != contents->end();
                 block = block.next()) {
                TextEditor::TextBlockUserData::setFoldingIndent(block, 0);
            }
            document->setFoldingIndentExternallyProvided(false);
        }
    }
    for (const FilePath &path : std::as_const(m_semanticDocuments)) {
        if (TextDocument *document = TextDocument::textDocumentForFilePath(path)) {
            if (document->syntaxHighlighter()) {
                TextEditor::SemanticHighlighter::setExtraAdditionalFormats(
                    document->syntaxHighlighter(), {}, {});
            }
        }
    }
    for (const QPointer<TextEditorWidget> &widget : std::as_const(m_inlineWidgets)) {
        if (widget)
            widget->clearSuggestion();
    }

    if (!m_runtimeDir.isEmpty())
        m_runtimeDir.removeRecursively();
}

void ExtensionHost::shutdown()
{
    if (!m_connection)
        return;
    // Take it out of reach first: stopping is not the host dying under us, and
    // nothing that reacts to that should run now.
    HostConnection *connection = std::exchange(m_connection, nullptr);
    connection->disconnect(this);
    connection->stop();
    delete connection;
    m_deferred.clear();
}

bool ExtensionHost::isRunning() const
{
    return m_connection && m_connection->isRunning();
}

QList<QPointer<AlienClient>> ExtensionHost::languageClients() const
{
    QList<QPointer<AlienClient>> clients;
    for (const QPointer<AlienClient> &client : m_lspClients) {
        if (client)
            clients.append(client);
    }
    return clients;
}

FilePath ExtensionHost::writtenConfigurationFile()
{
    return ICore::userResourcePath("alien/settings.json");
}

Result<> ExtensionHost::writeConfigurationFile(const QJsonObject &values)
{
    const FilePath file = writtenConfigurationFile();
    QJsonObject written;
    if (const Result<QByteArray> contents = file.fileContents())
        written = QJsonDocument::fromJson(*contents).object();
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
    QJsonObject written;
    if (const Result<QByteArray> contents = file.fileContents())
        written = QJsonDocument::fromJson(*contents).object();
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
    const Result<FilePath> tmp = m_nodePath.tmpDir();
    if (!tmp)
        return make_unexpected(tmp.error());
    m_runtimeDir = *tmp / ("qtc-alien-" + QString::number(QCoreApplication::applicationPid()));
    if (const Result<> dir = m_runtimeDir.ensureWritableDir(); !dir)
        return dir;

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
                                       QJsonObject{{"folders", m_workspaceFolders}});
        const QList<std::function<void()>> deferred = std::exchange(m_deferred, {});
        for (const std::function<void()> &action : deferred)
            action();
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
        m_deferred.clear();
        m_documentVersions.clear();
        m_commands.clear();
        qDeleteAll(m_treeFactories);
        m_treeFactories.clear();
        emit commandsChanged();
        emit stopped();
    });

    m_connection->start();
    return {};
}

// One of a workspace edit's file operations. What the options ask for is what
// an extension relies on when it offers the same refactoring twice.
static Result<> applyFileOperation(const QString &kind, const FilePath &filePath,
                                   const QJsonObject &change)
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
        // Through Qt Creator, so version control and an open editor both learn
        // that the file is gone.
        Core::FileUtils::removeFiles({filePath}, true);
        return filePath.exists()
                   ? ResultError(Tr::tr("Cannot remove \"%1\".").arg(filePath.toUserOutput()))
                   : Result<>{};
    }

    if (kind == "rename") {
        const FilePath target = FilePath::fromUserInput(change.value("newPath").toString());
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

// The shell the user has, for a terminal that did not name one.
static FilePath defaultShell()
{
    if (HostOsInfo::isWindowsHost())
        return FilePath::fromString("cmd.exe");
    const QString shell = Utils::Environment::systemEnvironment().value("SHELL");
    return shell.isEmpty() ? FilePath::fromString("/bin/sh") : FilePath::fromUserInput(shell);
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

// A debug session for what the extension asked for. Its configuration is the
// one the Debug Adapter Protocol uses, so the field names are that vocabulary:
// "program" is the inferior, "processId" an already-running one, and a server
// address means the debugger attaches instead of starting anything.
// A debug adapter the extension brings: Qt Creator speaks the protocol to it
// rather than driving a debugger of its own. What the adapter reports is what
// the views show, so there are no Qt dumpers on this path.
static Result<RunControl *> debugThroughAdapter(
    const QJsonObject &configuration, const QJsonObject &adapter,
    const std::shared_ptr<Debugger::Internal::DapSessionChannel> &channel)
{
    Kit *kit = KitManager::defaultKit();
    if (!kit)
        return make_unexpected(Tr::tr("Cannot debug: there is no kit to debug with."));

    const QString name = configuration.value("name").toString().isEmpty()
                             ? configuration.value("type").toString()
                             : configuration.value("name").toString();

    Debugger::Internal::DapStartData startData;
    startData.adapterId = configuration.value("type").toString();
    startData.configuration = configuration;
    startData.attach = configuration.value("request").toString() == "attach";
    startData.channel = channel;

    using Kind = Debugger::Internal::DapAdapterDescriptor::Kind;
    const QString kind = adapter.value("kind").toString();
    if (kind == "executable") {
        const FilePath program
            = FilePath::fromUserInput(adapter.value("command").toString());
        if (program.isEmpty()) {
            return make_unexpected(Tr::tr("Cannot debug \"%1\": its adapter names no "
                                          "program to run.").arg(name));
        }
        CommandLine command{program};
        for (const QJsonValue &argument : adapter.value("args").toArray())
            command.addArg(argument.toString());
        startData.adapter.kind = Kind::Executable;
        startData.adapter.command = command;
        startData.adapter.runData.workingDirectory
            = FilePath::fromUserInput(adapter.value("cwd").toString());
        Environment environment = kit->buildEnvironment();
        const QJsonObject extra = adapter.value("env").toObject();
        for (auto it = extra.begin(); it != extra.end(); ++it)
            environment.set(it.key(), it.value().toString());
        startData.adapter.runData.environment = environment;
    } else if (kind == "server") {
        startData.adapter.kind = Kind::Server;
        startData.adapter.host = adapter.value("host").toString();
        startData.adapter.port = quint16(adapter.value("port").toInt());
    } else if (kind == "pipe") {
        startData.adapter.kind = Kind::Pipe;
        startData.adapter.pipePath = adapter.value("path").toString();
    } else {
        // An adapter the extension implements itself would have to be spoken to
        // over this connection, which is not something the engine can do.
        return make_unexpected(Tr::tr("Cannot debug \"%1\": its adapter runs inside the "
                                      "extension.").arg(name));
    }

    auto runControl = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
    runControl->setKit(kit);
    runControl->setDisplayName(name);
    Debugger::DebuggerRunParameters parameters
        = Debugger::DebuggerRunParameters::fromRunControl(runControl);
    parameters.setCppEngineType(Debugger::DapAdapterEngineType);
    parameters.setDapAdapter(startData);
    runControl->setRunRecipe(Debugger::debuggerRecipe(runControl, parameters));
    runControl->start();
    return runControl;
}

static Result<RunControl *> debugVisibly(const QJsonObject &configuration)
{
    Kit *kit = KitManager::defaultKit();
    if (!kit)
        return make_unexpected(Tr::tr("Cannot debug: there is no kit to debug with."));

    const QString name = configuration.value("name").toString().isEmpty()
                             ? configuration.value("type").toString()
                             : configuration.value("name").toString();
    const QString request = configuration.value("request").toString("launch");
    const QString server = configuration.value("miDebuggerServerAddress").toString().isEmpty()
                               ? configuration.value("gdbServerAddress").toString()
                               : configuration.value("miDebuggerServerAddress").toString();
    const qint64 processId = qint64(configuration.value("processId").toDouble());

    FilePath program = FilePath::fromUserInput(configuration.value("program").toString());
    if (program.isEmpty())
        program = FilePath::fromUserInput(configuration.value("executable").toString());
    if (program.isEmpty() && server.isEmpty() && processId <= 0) {
        return make_unexpected(
            Tr::tr("Cannot debug \"%1\": its configuration names no program.").arg(name));
    }

    auto runControl = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
    runControl->setKit(kit);
    runControl->setDisplayName(name);

    Debugger::DebuggerRunParameters parameters
        = Debugger::DebuggerRunParameters::fromRunControl(runControl);
    parameters.setDisplayName(name);

    Utils::ProcessRunData inferior;
    inferior.command.setExecutable(program);
    for (const QJsonValue &argument : configuration.value("args").toArray())
        inferior.command.addArg(argument.toString());
    inferior.workingDirectory
        = FilePath::fromUserInput(configuration.value("cwd").toString());
    inferior.environment = kit->buildEnvironment();
    // Either a map or the array of {name, value} the protocol also allows.
    const QJsonObject environmentMap = configuration.value("environment").toObject();
    for (auto it = environmentMap.begin(); it != environmentMap.end(); ++it)
        inferior.environment.set(it.key(), it.value().toString());
    for (const QJsonValue &entry : configuration.value("environment").toArray()) {
        const QJsonObject variable = entry.toObject();
        inferior.environment.set(variable.value("name").toString(),
                                 variable.value("value").toString());
    }
    parameters.setInferior(inferior);

    if (!server.isEmpty()) {
        parameters.setStartMode(Debugger::AttachToRemoteServer);
        parameters.setRemoteChannel(server);
        parameters.setSymbolFile(program);
    } else if (processId > 0) {
        parameters.setStartMode(Debugger::AttachToLocalProcess);
        parameters.setAttachPid(Utils::ProcessHandle(processId));
        parameters.setSymbolFile(program);
    } else {
        parameters.setStartMode(Debugger::StartInternal);
    }
    if (configuration.value("stopAtEntry").toBool()
        || configuration.value("stopOnEntry").toBool()) {
        Debugger::DebuggerRunParameters::setBreakOnMainNextTime();
    }
    if (request == "attach" && server.isEmpty() && processId <= 0) {
        delete runControl;
        return make_unexpected(
            Tr::tr("Cannot attach for \"%1\": its configuration names neither a process nor a "
                   "server to attach to.").arg(name));
    }

    runControl->setRunRecipe(Debugger::debuggerRecipe(runControl, parameters));
    runControl->start();
    return runControl;
}

void ExtensionHost::installHandlers()
{
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

    m_connection->setRequestHandler(
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
            emit messageQuestionRequested(id, level, message, items,
                                          object.value("detail").toString(),
                                          object.value("modal").toBool());
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

    // An edit lands in the open editor when there is one, so that the user sees
    // it and can undo it in one step; otherwise it is written to the file.
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
                    if (const Result<> done = applyFileOperation(kind, filePath, change); !done) {
                        respond({}, done.error());
                        return;
                    }
                    continue;
                }

                if (auto document = TextDocument::textDocumentForFilePath(filePath)) {
                    QTextDocument *contents = document->document();
                    const QString text = contents->toPlainText();
                    QTextCursor cursor(contents);
                    cursor.beginEditBlock();
                    for (const QJsonObject &edit : editsBackToFront(text, edits)) {
                        const QJsonObject range = edit.value("range").toObject();
                        cursor.setPosition(offsetOf(text, range.value("start")));
                        cursor.setPosition(offsetOf(text, range.value("end")),
                                           QTextCursor::KeepAnchor);
                        cursor.insertText(edit.value("newText").toString());
                    }
                    cursor.endEditBlock();
                    continue;
                }

                const Result<QByteArray> contents = filePath.fileContents();
                if (!contents) {
                    respond({}, contents.error());
                    return;
                }
                QString text = QString::fromUtf8(*contents);
                for (const QJsonObject &edit : editsBackToFront(text, edits)) {
                    const QJsonObject range = edit.value("range").toObject();
                    const int start = offsetOf(text, range.value("start"));
                    const int end = offsetOf(text, range.value("end"));
                    text.replace(start, end - start, edit.value("newText").toString());
                }
                if (const Result<qint64> written = filePath.writeFileContents(text.toUtf8());
                    !written) {
                    respond({}, written.error());
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
        [](const QJsonValue &, const HostConnection::Responder &respond) {
            DocumentManager::saveAllModifiedDocumentsSilently();
            respond(true, {});
        });

    // A document an extension provides itself has no file behind it, so it goes
    // into an editor holding the text, keyed by its URI so that showing the
    // same one twice reuses that editor.
    m_connection->setRequestHandler(
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

    m_connection->setRequestHandler(
        "window/showTextDocument",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const FilePath filePath = fromHostPath(object.value("path").toString());
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
            if (!EditorManager::openEditorAt(link, {}, flags)) {
                respond({}, Tr::tr("Cannot open \"%1\".").arg(filePath.toUserOutput()));
                return;
            }
            if (TextEditorWidget *widget = TextEditorWidget::currentTextEditorWidget()) {
                if (revealType == 1 || revealType == 2)
                    widget->centerCursor();
                else if (revealType == 3)
                    widget->gotoLine(link.target.line, link.target.column, true, true);
            }
            respond(true, {});
        });

    // An extension's own markup on the text: a finding, a review comment. The
    // format is remembered when the type is made, and applied to whatever
    // ranges the extension later hands over for a file.
    m_connection->setNotificationHandler("decoration/register", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        m_decorations.insert(object.value("key").toString(),
                             decorationFormat(object.value("options").toObject()));
    });

    m_connection->setNotificationHandler("decoration/unregister", [this](const QJsonValue &params) {
        const QString key = params.toObject().value("key").toString();
        m_decorations.remove(key);
        // Whatever it marked up goes with it.
        for (TextEditorWidget *widget : m_decoratedWidgets) {
            if (widget)
                widget->setExtraSelections(Id::fromString(key), {});
        }
    });

    m_connection->setNotificationHandler("decoration/set", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString key = object.value("key").toString();
        const auto format = m_decorations.constFind(key);
        if (format == m_decorations.constEnd())
            return;
        const FilePath filePath = fromHostPath(object.value("path").toString());
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
            if (const QString owner = extensionOwning(query); !owner.isEmpty())
                ICore::showSettings(Constants::SETTINGS_ID, Id::fromString(query));
            else
                ICore::showSettings(Constants::SETTINGS_ID);
            respond(true, {});
        });

    // Starting a session on what an extension asked for, and stopping one.
    m_connection->setRequestHandler(
        "debug/start",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject configuration
                = params.toObject().value("configuration").toObject();
            const int id = m_nextDebugSessionId++;
            // What the extension registered, else what its manifest names, and
            // only failing both the debugger of the kit.
            QJsonObject adapter = params.toObject().value("adapter").toObject();
            if (adapter.isEmpty())
                adapter = manifestAdapterFor(configuration.value("type").toString());
            const auto channel = std::make_shared<Debugger::Internal::DapSessionChannel>();
            // An extension asks its adapter things the moment it hears the
            // session started, so it is only told once the adapter has
            // accepted the launch. A debugger of Qt Creator's own has no such
            // channel and is announced as soon as it runs.
            const auto answered = std::make_shared<bool>(false);
            const auto answer = [respond, answered](int session) {
                if (*answered)
                    return;
                *answered = true;
                respond(session, {});
            };
            channel->reportRunning = [answer, id](bool running) { answer(running ? id : 0); };
            const Result<RunControl *> running
                = adapter.isEmpty()
                      ? debugVisibly(configuration)
                      : debugThroughAdapter(configuration, adapter, channel);
            if (!running) {
                MessageManager::writeFlashing(running.error());
                answer(0);
                return;
            }
            m_debugSessions.insert(id, *running);
            m_debugChannels.insert(id, channel);
            connect(*running, &RunControl::stopped, this, [this, id, answer] {
                m_debugSessions.remove(id);
                m_debugChannels.remove(id);
                // A session that ended before it was ever announced never
                // started, and saying so beats leaving the extension waiting.
                answer(0);
                if (m_connection)
                    m_connection->sendNotification("debug/terminated", QJsonObject{{"id", id}});
            });
            if (adapter.isEmpty())
                answer(id);
        });

    // A request the adapter defines itself. Only a session running through an
    // adapter has one to ask; Qt Creator's own debuggers have no such channel,
    // and saying so beats an answer the extension would read as an adapter's.
    m_connection->setRequestHandler(
        "debug/customRequest",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const int id = object.value("id").toString().toInt();
            const QString command = object.value("command").toString();
            const std::shared_ptr<Debugger::Internal::DapSessionChannel> requests
                = m_debugChannels.value(id);
            if (!requests || !requests->send) {
                respond({}, Tr::tr("The debug session \"%1\" has no adapter to ask.")
                                .arg(object.value("id").toString()));
                return;
            }
            requests->send(command, object.value("arguments").toObject(),
                           [respond](const Result<QJsonObject> &body) {
                               if (body)
                                   respond(*body, {});
                               else
                                   respond({}, body.error());
                           });
        });

    m_connection->setRequestHandler(
        "debug/stop",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const int id = params.toObject().value("id").toString().toInt();
            if (RunControl *control = m_debugSessions.value(id))
                control->initiateStop();
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
                m_secrets.remove(secretService(extension) + '/' + key);
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
        m_watches.remove(params.toObject().value("id").toString());
    });

    m_connection->setRequestHandler(
        "editor/insertSnippet",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const FilePath filePath = fromHostPath(object.value("path").toString());
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

    m_connection->setRequestHandler(
        "window/showOpenDialog",
        [this, fileDialogFilter](const QJsonValue &params,
                                 const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString caption = object.value("title").toString().isEmpty()
                                        ? Tr::tr("Open File")
                                        : object.value("title").toString();
            const FilePath dir = fromHostPath(object.value("defaultPath").toString());
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
                paths.append(toHostPath(path));
            respond(paths, {});
        });

    m_connection->setRequestHandler(
        "window/showSaveDialog",
        [this, fileDialogFilter](const QJsonValue &params,
                                 const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QString caption = object.value("title").toString().isEmpty()
                                        ? Tr::tr("Save File")
                                        : object.value("title").toString();
            const FilePath file = Utils::FileUtils::getSaveFilePath(
                caption,
                fromHostPath(object.value("defaultPath").toString()),
                fileDialogFilter(object.value("filters").toObject()));
            if (file.isEmpty()) {
                respond(QJsonValue(QJsonValue::Null), {});
                return;
            }
            respond(toHostPath(file), {});
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

    // An extension's terminal is Qt Creator's terminal, running what the
    // extension sends. Text cannot be typed into a running one - the terminal
    // owns its input - so the first line an extension sends is what the
    // terminal runs, which is how extensions use this: create, send the
    // command, show. A second line has nowhere to go and says so.
    m_connection->setRequestHandler(
        "terminal/create",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            Terminal terminal;
            terminal.name = object.value("name").toString();
            // Somewhere it can actually run: what was asked for, else the
            // workspace, else the user's home - never nothing.
            if (const QString cwd = object.value("cwd").toString(); !cwd.isEmpty())
                terminal.workingDirectory = fromHostPath(cwd);
            else if (!m_workspaceFolders.isEmpty())
                terminal.workingDirectory
                    = fromHostPath(m_workspaceFolders.first().toObject().value("path").toString());
            else
                terminal.workingDirectory = Utils::FileUtils::homePath();
            terminal.environmentChanges = environmentChanges(object.value("env").toObject());
            terminal.pty = object.value("pty").toBool();
            // The shell it asked for, which is not always the system's.
            if (const QString shell = object.value("shellPath").toString(); !shell.isEmpty())
                terminal.shell = fromHostPath(shell);
            for (const QJsonValue &argument : object.value("shellArgs").toArray())
                terminal.shellArguments << argument.toString();
            const int id = m_nextTerminalId++;
            m_terminals.insert(id, terminal);
            // Nothing to run: what is in this terminal is what the extension
            // writes into it, so it is there as soon as it is asked for.
            if (terminal.pty) {
                emit extensionTerminalOpened(id, terminal.name);
                respond(id, {});
                return;
            }
            // A terminal the user asked for is a shell, and starts now - the one
            // the extension named, or the one the user has. A terminal an
            // extension made to run commands in cannot begin before there are
            // any.
            if (!terminal.shell.isEmpty()) {
                startTerminal(id, CommandLine{terminal.shell, terminal.shellArguments});
            } else if (object.value("shellWanted").toBool()) {
                startTerminal(id, CommandLine{defaultShell(), terminal.shellArguments});
            }
            respond(id, {});
        });

    m_connection->setNotificationHandler("terminal/send", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const int id = object.value("id").toInt();
        const QString text = object.value("text").toString().trimmed();
        if (text.isEmpty() || !m_terminals.contains(id))
            return;

        const Terminal &terminal = m_terminals.value(id);
        const CommandLine command
            = !terminal.shell.isEmpty()
                  ? CommandLine{terminal.shell, QStringList(terminal.shellArguments) << text}
              : HostOsInfo::isWindowsHost()
                  ? CommandLine{FilePath::fromString("cmd.exe"), {"/c", text}}
                  : CommandLine{FilePath::fromString("/bin/sh"), {"-c", text}};
        startTerminal(id, command);
    });

    // A list the extension fills in after showing it: it asks for a choice
    // before it knows the choices.
    m_connection->setNotificationHandler("quickPick/hide", [this](const QJsonValue &params) {
        emit quickPickHidden(params.toObject().value("pickId").toString());
    });

    m_connection->setNotificationHandler("quickPick/update", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit quickPickUpdated(object.value("pickId").toString(), quickPickLabels(object),
                              object.value("placeholder").toString());
    });

    m_connection->setNotificationHandler(
        "terminalProfiles/changed", [this](const QJsonValue &params) {
            m_terminalProfiles = params.toObject().value("profiles").toArray();
            emit terminalProfilesChanged();
        });

    m_connection->setNotificationHandler("terminal/show", [this](const QJsonValue &params) {
        const int id = params.toObject().value("id").toString().toInt();
        if (m_terminals.value(id).pty) {
            emit extensionTerminalShowRequested(id);
            return;
        }
        for (IOutputPane *pane : IOutputPane::allOutputPanes()) {
            if (pane->id() == "ProjectExplorer.ApplicationOutput")
                pane->popup(IOutputPane::ModeSwitch);
        }
    });

    m_connection->setNotificationHandler("terminal/dispose", [this](const QJsonValue &params) {
        const int id = params.toObject().value("id").toInt();
        if (m_terminals.take(id).pty)
            emit extensionTerminalClosed(id);
    });

    m_connection->setNotificationHandler("terminal/write", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit extensionTerminalOutput(object.value("id").toString().toInt(),
                                     object.value("text").toString());
    });

    m_connection->setNotificationHandler("terminal/setName", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit extensionTerminalRenamed(object.value("id").toString().toInt(),
                                      object.value("name").toString());
    });

    // An extension's task is a command line it wants run: Qt Creator runs it as
    // it runs anything else, so it lands in Application Output with a stop
    // button and an exit code, instead of happening invisibly or not at all.
    m_connection->setRequestHandler(
        "tasks/execute",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QJsonObject execution = object.value("execution").toObject();
            const QString name = object.value("name").toString();

            CommandLine command;
            if (execution.value("kind").toString() == "shell") {
                const QString line = execution.value("commandLine").toString();
                command = HostOsInfo::isWindowsHost()
                              ? CommandLine{FilePath::fromString("cmd.exe"), {"/c", line}}
                              : CommandLine{FilePath::fromString("/bin/sh"), {"-c", line}};
            } else {
                command = CommandLine{fromHostPath(execution.value("command").toString())};
                for (const QJsonValue &argument : execution.value("args").toArray())
                    command.addArg(argument.toString());
            }
            if (command.executable().isEmpty()) {
                respond({}, Tr::tr("The task \"%1\" has nothing to run.").arg(name));
                return;
            }

            const auto exitCode = std::make_shared<int>(0);
            const Result<RunControl *> running = runVisibly(
                name.isEmpty() ? Tr::tr("Extension task") : name, command,
                execution.value("cwd").toString().isEmpty()
                    ? FilePath()
                    : fromHostPath(execution.value("cwd").toString()),
                environmentChanges(execution.value("env").toObject()),
                exitCode);
            if (!running) {
                respond({}, running.error());
                return;
            }
            const int id = m_nextTaskId++;
            m_runningTasks.insert(id, *running);
            connect(*running, &RunControl::stopped, this, [this, id, exitCode] {
                m_runningTasks.remove(id);
                // Whoever started it is waiting to hear that it is over, and
                // what it made of the job.
                if (m_connection) {
                    m_connection->sendNotification(
                        "tasks/ended", QJsonObject{{"id", id}, {"exitCode", *exitCode}});
                }
            });
            emit taskStarted(name, command);
            respond(id, {});
        });

    m_connection->setNotificationHandler("tasks/terminate", [this](const QJsonValue &params) {
        if (RunControl *runControl = m_runningTasks.take(params.toObject().value("id").toInt()))
            runControl->initiateStop();
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

            QJsonArray found;
            for (const FilePath &root : roots) {
                root.iterateDirectory(
                    [&](const FilePath &item) {
                        const QString relative = item.relativePathFromDir(root);
                        if (!includeRe.match(relative).hasMatch())
                            return IterationPolicy::Continue;
                        if (!exclude.isEmpty() && excludeRe.match(relative).hasMatch())
                            return IterationPolicy::Continue;
                        found.append(toHostPath(item));
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
            const QStringList items = quickPickLabels(object);
            const int id = m_nextPromptId++;
            m_pendingPrompts.insert(id, respond);
            if (object.value("canPickMany").toBool())
                m_multiPicks.insert(id);
            emit quickPickRequested(id, items, object.value("placeholder").toString(),
                                    object.value("canPickMany").toBool(),
                                    object.value("pickId").toString());
        });

    m_connection->setRequestHandler(
        "window/showInputBox",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const int id = m_nextPromptId++;
            m_pendingPrompts.insert(id, respond);
            emit inputBoxRequested(id, object.value("prompt").toString(),
                                   object.value("value").toString(),
                                   object.value("placeholder").toString(),
                                   object.value("password").toBool());
        });

    m_connection->setNotificationHandler("statusbar/setMessage", [this](const QJsonValue &params) {
        emit statusBarMessageChanged(params.toObject().value("text").toString());
    });

    m_connection->setNotificationHandler("statusbar/update", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit statusBarItemChanged(object.value("id").toString(), object);
    });

    m_connection->setNotificationHandler("statusbar/remove", [this](const QJsonValue &params) {
        emit statusBarItemRemoved(params.toObject().value("id").toString());
    });

    // An extension reports its long work through window.withProgress; Qt
    // Creator shows that in the progress bar it already has.
    // What an extension asked for that we do not implement yet. Said once per
    // member, so it reads as a list of what to build rather than noise.
    m_connection->setNotificationHandler("api/unimplemented", [](const QJsonValue &params) {
        MessageManager::writeSilently(
            Tr::tr("An extension used \"%1\", which is not implemented yet.")
                .arg(params.toObject().value("member").toString()));
    });

    m_connection->setNotificationHandler("progress/start", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString id = object.value("id").toString();
        if (m_progress.contains(id))
            return;
        // A window progress is a line in the status bar and nothing more; only
        // a notification is a task with a bar and a stop button.
        if (object.value("location").toInt() == 10) {
            emit statusBarMessageChanged(stripCodicons(object.value("title").toString()));
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
                if (m_connection)
                    m_connection->sendRequest("progress/cancel", QJsonObject{{"id", id}}, {});
            });
            watcher->setFuture(interface->future());
        }
    });

    m_connection->setNotificationHandler("progress/report", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        QFutureInterface<void> *interface = m_progress.value(object.value("id").toString());
        if (!interface)
            return;
        const int value = interface->progressValue()
                          + qRound(object.value("increment").toDouble());
        interface->setProgressValueAndText(qBound(0, value, 100),
                                           stripCodicons(object.value("message").toString()));
    });

    m_connection->setNotificationHandler("progress/end", [this](const QJsonValue &params) {
        if (m_statusProgress.remove(params.toObject().value("id").toString())) {
            emit statusBarMessageChanged({});
            return;
        }
        const QString id = params.toObject().value("id").toString();
        delete m_progressWatchers.take(id);
        if (QFutureInterface<void> *interface = m_progress.take(id)) {
            interface->reportFinished();
            delete interface;
        }
    });

    m_connection->setNotificationHandler("treeview/register", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString viewId = object.value("viewId").toString();
        requestTreeMenu(viewId, {}, "view/title", [this, viewId](const QJsonArray &items) {
            m_treeTitleMenus.insert(viewId, items);
        });
        // "Gerrit AI: Dashboard" rather than "gerritAI.dashboard": the sidebar
        // lists views from every source, so the container says whose it is.
        const QString name = object.value("name").toString();
        const QString container = object.value("container").toString();
        QString displayName = viewId;
        if (!name.isEmpty())
            displayName = container.isEmpty() ? name : QString("%1: %2").arg(container, name);
        if (!m_treeFactories.contains(viewId))
            m_treeFactories.insert(viewId, new AlienTreeViewFactory(this, viewId, displayName));
        emit treeViewRegistered(viewId);
    });

    m_connection->setNotificationHandler("views/welcome", [this](const QJsonValue &params) {
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
                emit viewWelcomeChanged(viewId);
        }
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
        const QJsonObject options = object.value("options").toObject();
        if (m_webviewRenderer) {
            m_webviewRenderer->createPanel(id, viewType, title);
            m_webviewRenderer->setPanelOptions(id, options);
        }
        m_webviewPanels.append(QJsonObject{{"id", id},
                                           {"viewType", viewType},
                                           {"title", title},
                                           {"scripts", options.value("enableScripts").toBool()}});
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
        for (qsizetype i = 0; i < m_webviewPanels.size(); ++i) {
            if (m_webviewPanels.at(i).toObject().value("id").toString() == id) {
                m_webviewPanels.removeAt(i);
                break;
            }
        }
        emit webviewDisposed(id);
    });

    m_connection->setNotificationHandler("output/append", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const QString channel = object.value("channel").toString();
        const QString value = object.value("value").toString();
        emit channelOutput(channel, value, object.value("line").toBool());
        qCDebug(logHost).noquote() << QString("<%1> %2").arg(channel, value);
    });

    m_connection->setNotificationHandler("output/show", [this](const QJsonValue &params) {
        emit channelShowRequested(params.toObject().value("preserveFocus").toBool());
    });

    m_connection->setNotificationHandler("output/clear", [this](const QJsonValue &) {
        emit channelClearRequested();
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

    m_connection->setNotificationHandler("menus/editorTitle", [this](const QJsonValue &params) {
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

    m_connection->setNotificationHandler("menus/editorContext", [this](const QJsonValue &params) {
        m_editorContextItems.clear();
        for (const QJsonValue &item : params.toObject().value("items").toArray()) {
            const QJsonObject object = item.toObject();
            m_editorContextItems.append({object.value("command").toString(),
                                         object.value("title").toString()});
        }
        updateEditorContextActions();
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
    m_connection->setNotificationHandler("hover/registerProvider",
                                         registerLanguageFeature(m_hoverLanguageIds));
    m_connection->setNotificationHandler("definition/registerProvider",
                                         registerLanguageFeature(m_definitionLanguageIds));
    m_connection->setNotificationHandler("typeDefinition/registerProvider",
                                         registerLanguageFeature(m_typeDefinitionLanguageIds));
    m_connection->setNotificationHandler("codeLens/registerProvider",
                                         registerLanguageFeature(m_codeLensLanguageIds));
    m_connection->setNotificationHandler("color/registerProvider",
                                         registerLanguageFeature(m_colorLanguageIds));
    m_connection->setNotificationHandler("folding/registerProvider",
                                         registerLanguageFeature(m_foldingLanguageIds));
    m_connection->setNotificationHandler("semanticTokens/registerProvider",
                                         registerLanguageFeature(m_semanticLanguageIds));
    m_connection->setNotificationHandler("inlineCompletion/registerProvider",
                                         registerLanguageFeature(m_inlineLanguageIds));
    const auto registerOnType = [this, registerLanguageFeature](const QJsonValue &params) {
        m_onTypeTriggers.clear();
        for (const QJsonValue &character : params.toObject().value("triggerCharacters").toArray())
            m_onTypeTriggers.insert(character.toString());
        registerLanguageFeature(m_onTypeLanguageIds)(params);
    };
    m_connection->setNotificationHandler("onTypeFormatting/registerProvider", registerOnType);

    m_connection->setNotificationHandler("document/setLanguage", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const FilePath filePath = fromHostPath(object.value("uri").toString());
        m_languageOverrides.insert(filePath, object.value("languageId").toString());
        for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
            attachEditorFeatures(editor);
    });

    m_connection->setRequestHandler(
        "languages/known",
        [this](const QJsonValue &, const HostConnection::Responder &respond) {
            QSet<QString> ids;
            for (const VscodeLanguage &language : m_declaredLanguages)
                ids.insert(language.id);
            for (const QString &id : m_languageOverrides)
                ids.insert(id);
            for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
                ids.insert(languageIdFor(editor->document()->filePath()));
            ids.insert("plaintext");
            QJsonArray array;
            for (const QString &id : ids)
                array.append(id);
            respond(QJsonObject{{"languageIds", array}}, {});
        });


    // A document the extension makes up rather than reads from disk, said again.
    m_connection->setNotificationHandler(
        "document/replaceContents", [this](const QJsonValue &params) {
            const QJsonObject object = params.toObject();
            const FilePath path = fromHostPath(object.value("uri").toString());
            TextDocument *document = TextDocument::textDocumentForFilePath(path);
            if (!document)
                return;
            QTextCursor cursor(document->document());
            cursor.select(QTextCursor::Document);
            cursor.insertText(object.value("text").toString());
        });

    m_connection->setNotificationHandler("codeLens/changed", [this](const QJsonValue &) {
        for (const FilePath &path : m_codeLenses.keys())
            refreshCodeLenses(path);
    });
    m_connection->setNotificationHandler("formatting/registerProvider",
                                         registerLanguageFeature(m_formattingLanguageIds));
    m_connection->setNotificationHandler("rangeFormatting/registerProvider",
                                         registerLanguageFeature(m_rangeFormattingLanguageIds));
    m_connection->setNotificationHandler("codeAction/registerProvider",
                                         registerLanguageFeature(m_codeActionLanguageIds));
    m_connection->setNotificationHandler("rename/registerProvider",
                                         registerLanguageFeature(m_renameLanguageIds));
    m_connection->setNotificationHandler("references/registerProvider",
                                         registerLanguageFeature(m_referenceLanguageIds));
    m_connection->setNotificationHandler("symbols/registerProvider",
                                         registerLanguageFeature(m_symbolLanguageIds));
    m_connection->setNotificationHandler("workspaceSymbols/registerProvider",
                                         [this](const QJsonValue &) {
                                             m_hasWorkspaceSymbols = true;
                                         });
    m_connection->setNotificationHandler("highlights/registerProvider",
                                         registerLanguageFeature(m_highlightLanguageIds));
    m_connection->setNotificationHandler("links/registerProvider",
                                         registerLanguageFeature(m_linkLanguageIds));
    m_connection->setNotificationHandler(
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

void ExtensionHost::whenReady(const std::function<void()> &action)
{
    if (isRunning())
        action();
    else
        m_deferred.append(action);
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
                    m_tokenTypeAliases.insert(type, known);
                    break;
                }
            }
        }
    }
    QJsonArray extensions;
    for (const VscodeManifest &manifest : manifests) {
        extensions.append(QJsonObject{{"id", manifest.qualifiedId()},
                                      {"path", toHostPath(manifest.rootDir)},
                                      {"packageJSON", manifest.rawPackageJson}});
    }
    whenReady([this, extensions] {
        m_connection->sendNotification("extensions/known", QJsonObject{{"extensions", extensions}});
    });
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
        for (const QString &extension : language.extensions)
            extensions.append(extension);
        m_declaredLanguages.append(language);
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

// What an extension offers to put in a launch configuration for its type.
void ExtensionHost::requestDebugConfigurations(
    const QString &type, const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    emit debugTypeRequested(type);
    whenReady([this, type, callback] {
        m_connection->sendRequest(
            "debug/configurations", QJsonObject{{"type", type}},
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("configurations").toArray()
                                         : QJsonArray());
            });
    });
}

void ExtensionHost::requestDebugAdapter(
    const QString &type, const QJsonObject &configuration,
    const std::function<void(const QJsonObject &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    // The extension that knows this type may not be running yet: asking for its
    // adapter is what wakes it, the way asking for a command does.
    emit debugTypeRequested(type);
    whenReady([this, type, configuration, callback] {
        m_connection->sendRequest(
            "debug/resolve",
            QJsonObject{{"type", type}, {"configuration", configuration}},
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject() : QJsonObject());
            });
    });
}

void ExtensionHost::requestTreeMenu(const QString &viewId, const QString &id, const QString &kind,
                                    const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    whenReady([this, viewId, id, kind, callback] {
        m_connection->sendRequest(
            "treeview/menu", QJsonObject{{"viewId", viewId}, {"id", id}, {"kind", kind}},
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("items").toArray()
                                         : QJsonArray());
            });
    });
}

// The command gets the item's element, which only the host has, so it is
// addressed by node id rather than shipping the element back and forth.
// Setting up the terminal the extension described and showing it.
// Runs it where Qt Creator runs anything else, and says what became of it.
void ExtensionHost::sendTerminalInput(int id, const QString &text)
{
    if (m_connection)
        m_connection->sendRequest("terminal/input",
                                  QJsonObject{{"id", QString::number(id)}, {"text", text}},
                                  {});
}

void ExtensionHost::setTerminalDimensions(int id, int columns, int rows)
{
    if (m_connection) {
        m_connection->sendRequest("terminal/dimensions",
                                  QJsonObject{{"id", QString::number(id)},
                                              {"columns", columns},
                                              {"rows", rows}},
                                  {});
    }
}

void ExtensionHost::startTerminal(int id, const Utils::CommandLine &command)
{
    const Terminal terminal = m_terminals.value(id);
    const Result<RunControl *> running
        = runVisibly(terminal.name, command, terminal.workingDirectory,
                     terminal.environmentChanges);
    if (!running) {
        MessageManager::writeFlashing(running.error());
        return;
    }
    connect(*running, &RunControl::stopped, this, [this, id, control = *running] {
        if (!m_connection)
            return;
        m_connection->sendNotification(
            "terminal/ended",
            QJsonObject{{"id", id}, {"exitCode", control->lastExitCode().value_or(0)}});
    });
    emit terminalOpened(terminal.name, command);
}

void ExtensionHost::runInWebview(
    const QString &id, const QString &script,
    const std::function<void(const QJsonValue &, const QString &)> &done)
{
    if (!m_webviewRenderer) {
        done({}, Tr::tr("This build has no webview backend."));
        return;
    }
    m_webviewRenderer->runScript(id, script, done);
}

void ExtensionHost::openTerminalProfile(const QString &id)
{
    if (!m_connection)
        return;
    m_connection->sendRequest("terminalProfile/open", QJsonObject{{"id", id}},
                              [id](const QJsonValue &result, const QString &error) {
                                  if (!error.isEmpty() || result.toObject().value("opened").toBool())
                                      return;
                                  MessageManager::writeFlashing(
                                      Tr::tr("The extension did not offer the terminal \"%1\".")
                                          .arg(id));
                              });
}

void ExtensionHost::reportTreeSelection(const QString &viewId, const QStringList &ids)
{
    if (!m_connection)
        return;
    QJsonArray array;
    for (const QString &id : ids)
        array.append(id);
    m_connection->sendNotification("treeview/selectionChanged",
                                   QJsonObject{{"viewId", viewId}, {"ids", array}});
}

void ExtensionHost::executeTreeItemCommand(const QString &viewId, const QString &id,
                                           const QString &command)
{
    if (!m_connection)
        return;
    whenReady([this, viewId, id, command] {
        m_connection->sendRequest(
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
    if (configuration == m_configuration)
        return;
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

// The globs a language contribution lists are matched the way a documentSelector
// is: "**" crosses directories, "*" does not, and a pattern without a separator
// is about the name alone ("Dockerfile.*") rather than the path.
static bool globMatches(const QString &pattern, const FilePath &filePath)
{
    // The same reading of a glob as everywhere else: brace groups included, and
    // "**/" spanning any number of directories or none.
    const QString subject = pattern.contains('/') ? filePath.path() : filePath.fileName();
    return globExpression(pattern).match(subject).hasMatch();
}

bool languageMatchesFile(const VscodeLanguage &language, const FilePath &filePath)
{
    const QString suffix = filePath.suffix().toLower();
    for (const QString &extension : language.extensions) {
        const QString declared = extension.startsWith('.') ? extension.mid(1) : extension;
        if (declared.toLower() == suffix)
            return true;
    }
    const QString name = filePath.fileName();
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
    for (const VscodeLanguage &language : m_declaredLanguages) {
        if (languageMatchesFile(language, filePath))
            return language.id;
    }
    const QString known = builtinLanguageId(filePath);
    return known.isEmpty() ? QString("plaintext") : known;
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
                    if (isOnHostDevice(path))
                        files.append(toHostPath(path));
                }
                if (!files.isEmpty())
                    m_connection->sendNotification(method, QJsonObject{{"files", files}});
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
                if (isOnHostDevice(to)) {
                    m_connection->sendNotification(
                        "files/didRename",
                        QJsonObject{{"from", toHostPath(from)}, {"to", toHostPath(to)}});
                }
            });

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

    // A document on another device is not reachable for the host, and its path
    // would collide with a same-named one on the host's own device.
    if (!isOnHostDevice(filePath))
        return;

    m_documentVersions.insert(filePath, 1);
    m_connection->sendNotification("document/didOpen", QJsonObject{
        {"uri", toHostPath(filePath)},
        {"languageId", languageIdFor(filePath)},
        {"version", 1},
        // Whether it has unsaved changes, and how its lines end: an extension
        // writing back has to know both.
        {"isDirty", textDocument->isModified()},
        {"eol", textDocument->format().lineTerminationMode
                        == Utils::TextFileFormat::CRLFLineTerminator ? 2 : 1},
        {"text", textDocument->plainText()},
    });

    maybeAttachCompletion(textDocument);

    // Saving is what a great many extensions wait for - to lint, to build, to
    // run what the file describes - and it was never passed on. Queued like the
    // change below, so that an extension acting on a save sees the text that
    // was saved rather than the one before it.
    connect(textDocument, &IDocument::aboutToSave, this,
            [this, textDocument](const FilePath &path, IDocument::SaveOption) {
                if (isOnHostDevice(path)) {
                    m_connection->sendNotification("document/willSave",
                                                   QJsonObject{{"uri", toHostPath(path)}});
                }
            }, Qt::QueuedConnection);
    connect(textDocument, &IDocument::saved, this,
            [this](const FilePath &path, IDocument::SaveOption) {
                if (isOnHostDevice(path)) {
                    m_connection->sendNotification("document/didSave",
                                                   QJsonObject{{"uri", toHostPath(path)}});
                }
            }, Qt::QueuedConnection);

    // Queued: the signal arrives while the change is being applied, and the
    // text read at that moment can still be the half-done one - an editor
    // filled in one go reports itself empty. After the event loop it is what
    // the user sees.
    connect(textDocument, &TextDocument::contentsChangedWithPosition, this,
            [this, textDocument](int position, int charsRemoved, int charsAdded) {
                const FilePath path = textDocument->filePath();
                const int version = m_documentVersions.value(path) + 1;
                m_documentVersions.insert(path, version);
                // Where the change was, as well as the text after it. An
                // extension is told what the user did - a keystroke is a
                // keystroke, not a replacement of the whole file - while the
                // text stays the authority on what the document now says, so
                // the two cannot drift apart.
                m_connection->sendNotification("document/didChange", QJsonObject{
                    {"uri", toHostPath(path)},
                    {"version", version},
                    {"text", textDocument->plainText()},
                    {"position", position},
                    {"charsRemoved", charsRemoved},
                    {"charsAdded", charsAdded},
                });
            }, Qt::QueuedConnection);
}

void ExtensionHost::onDocumentClosed(IDocument *document)
{
    auto textDocument = qobject_cast<TextDocument *>(document);
    if (!textDocument)
        return;
    const FilePath filePath = textDocument->filePath();
    m_documentVersions.remove(filePath);
    m_connection->sendNotification("document/didClose",
                                   QJsonObject{{"uri", toHostPath(filePath)}});
}

void ExtensionHost::syncActiveEditor()
{
    IDocument *document = EditorManager::currentDocument();
    auto textDocument = qobject_cast<TextDocument *>(document);
    const QJsonValue uri = textDocument ? QJsonValue(toHostPath(textDocument->filePath()))
                                        : QJsonValue(QJsonValue::Null);
    // What the editor is set to, which is what an extension formats to.
    const TextEditor::TabSettingsData tabs = textDocument ? textDocument->tabSettings()
                                                          : TextEditor::TabSettingsData();
    m_connection->sendNotification(
        "editor/didChangeActive",
        QJsonObject{{"uri", uri},
                    {"tabSize", tabs.m_indentSize},
                    {"insertSpaces",
                     tabs.m_tabPolicy == TextEditor::TabSettingsData::SpacesOnlyTabPolicy}});

    // What is on screen, which an extension walks to decorate or inspect every
    // editor the user can see. An empty list means it decorates nothing.
    QJsonArray visible;
    for (IEditor *editor : EditorManager::visibleEditors()) {
        auto visibleDocument = qobject_cast<TextDocument *>(editor->document());
        if (visibleDocument && !visibleDocument->filePath().isEmpty()
            && isOnHostDevice(visibleDocument->filePath())) {
            visible.append(toHostPath(visibleDocument->filePath()));
        }
    }
    m_connection->sendNotification("editor/didChangeVisible", QJsonObject{{"uris", visible}});

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
        {"uri", toHostPath(document->filePath())},
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
    for (const Task &task : m_diagnosticTasks.take(key))
        TaskHub::removeTask(task);

    const FilePath filePath = fromHostPath(uri);
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
                           .arg(fromHostPath(related.value("uri").toString()).fileName())
                           .arg(at.value("line").toInt() + 1)
                           .arg(related.value("message").toString());
        }

        auto mark = new TextMark(filePath, line, category);
        mark->setToolTip(tooltip);
        mark->setLineAnnotation(message);

        Task::TaskType type = Task::Unknown;
        switch (diagnostic.value("severity").toInt()) {
        case 0:
            mark->setColor(Theme::CodeModel_Error_TextMarkColor);
            type = Task::Error;
            break;
        case 1:
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
        TaskHub::addTask(task);
        tasks.append(task);
    }

    showDiagnosticTags(filePath);

    if (!marks.isEmpty())
        m_diagnosticMarks.insert(key, marks);
    if (!tasks.isEmpty())
        m_diagnosticTasks.insert(key, tasks);

    emit diagnosticsPublished(uri, int(diagnostics.size()));
}

// Unnecessary code is worth showing as such rather than as one more warning in
// the margin, and so is code that still works but should not be used.
void ExtensionHost::showDiagnosticTags(const FilePath &filePath)
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
            for (const QJsonValue &tag : diagnostic.value("tags").toArray()) {
                if (tag.toInt() == 1) {
                    format.setForeground(Utils::creatorColor(Theme::TextColorDisabled));
                    tagged = true;
                } else if (tag.toInt() == 2) {
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

void ExtensionHost::resolveCompletion(const QString &id,
                                      const std::function<void(const QJsonObject &)> &callback)
{
    if (!m_connection || id.isEmpty())
        return;
    m_connection->sendRequest("completion/resolve", QJsonObject{{"id", id}},
                              [callback](const QJsonValue &result, const QString &error) {
                                  if (error.isEmpty() && result.isObject())
                                      callback(result.toObject());
                              });
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
        {"uri", toHostPath(uri)},
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
        {"uri", toHostPath(uri)},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    whenReady([this, params, callback] {
        m_connection->sendRequest(
            "hover/provide", params, [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("contents").toString() : QString());
            });
    });
}

void ExtensionHost::requestTypeDefinition(const FilePath &uri, int line, int character,
                                          const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    const QJsonObject params{
        {"uri", toHostPath(uri)},
        {"position", QJsonObject{{"line", line}, {"character", character}}},
    };
    whenReady([this, params, callback] {
        m_connection->sendRequest(
            "typeDefinition/provide", params,
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("locations").toArray()
                                         : QJsonArray());
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
        {"uri", toHostPath(uri)},
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
                              callback](const QJsonArray &edits) {
                if (!document) // closed while the extension was thinking
                    return;
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
            && m_host->formatsRangesFor(m_document->filePath())) {
            QTextCursor start = cursor;
            start.setPosition(cursor.selectionStart());
            QTextCursor end = cursor;
            end.setPosition(cursor.selectionEnd());
            m_host->requestRangeFormatting(m_document->filePath(), tabSize, spaces,
                                           {start.blockNumber(), start.positionInBlock()},
                                           {end.blockNumber(), end.positionInBlock()}, applied);
            return;
        }
        m_host->requestFormatting(m_document->filePath(), tabSize, spaces, applied);
    }

    void setMode(FormatMode mode) final { m_mode = mode; }

private:
    QPointer<ExtensionHost> m_host;
    QPointer<TextEditor::TextDocument> m_document;
    FormatMode m_mode = FormatMode::Range;
};

bool ExtensionHost::formatsRangesFor(const FilePath &filePath) const
{
    return m_rangeFormattingLanguageIds.contains(languageIdFor(filePath));
}

void ExtensionHost::requestRangeFormatting(const FilePath &filePath, int tabSize,
                                           bool insertSpaces, const QPair<int, int> &start,
                                           const QPair<int, int> &end,
                                           const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "rangeFormatting/provide",
        QJsonObject{{"uri", toHostPath(filePath)},
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

void ExtensionHost::requestFormatting(const FilePath &filePath, int tabSize, bool insertSpaces,
                                     const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "formatting/provide",
        QJsonObject{{"uri", toHostPath(filePath)},
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
            m_host->applyChanges(m_changes);
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
class AlienQuickFixProcessor final : public TextEditor::IAssistProcessor
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
        m_host->requestCodeActions(
            interface()->filePath(),
            {start.blockNumber(), start.positionInBlock()},
            {end.blockNumber(), end.positionInBlock()},
            [this](const QJsonArray &actions) {
                TextEditor::QuickFixOperations operations;
                for (const QJsonValue &value : actions) {
                    const QJsonObject action = value.toObject();
                    operations << new AlienQuickFixOperation(action, m_host);
                }
                setAsyncProposalAvailable(operations.isEmpty()
                                              ? nullptr
                                              : TextEditor::GenericProposal::createProposal(
                                                    interface(), operations));
            });
        return nullptr;
    }

private:
    QPointer<ExtensionHost> m_host;
};

class AlienQuickFixProvider final : public TextEditor::IAssistProvider
{
public:
    explicit AlienQuickFixProvider(ExtensionHost *host) : m_host(host) {}

    TextEditor::IAssistProcessor *createProcessor(const TextEditor::AssistInterface *) const final
    {
        return new AlienQuickFixProcessor(m_host);
    }

private:
    ExtensionHost *m_host;
};

void ExtensionHost::requestCodeActions(const FilePath &filePath, const QPair<int, int> &start,
                                      const QPair<int, int> &end,
                                      const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "codeAction/provide",
        QJsonObject{{"uri", toHostPath(filePath)},
                    {"startLine", start.first},
                    {"startCharacter", start.second},
                    {"endLine", end.first},
                    {"endCharacter", end.second}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// The edits of one workspace edit, applied where the file is open and on disk
// where it is not - the same two cases as an edit an extension asks for itself.
void ExtensionHost::applyChanges(const QJsonArray &changes)
{
    for (const QJsonValue &value : changes) {
        const QJsonObject change = value.toObject();
        const FilePath filePath = fromHostPath(change.value("path").toString());
        const QJsonArray edits = change.value("edits").toArray();
        if (auto document = TextDocument::textDocumentForFilePath(filePath)) {
            const QString text = document->plainText();
            QTextCursor cursor(document->document());
            cursor.beginEditBlock();
            for (const QJsonObject &edit : editsBackToFront(text, edits)) {
                const QJsonObject range = edit.value("range").toObject();
                cursor.setPosition(offsetOf(text, range.value("start")));
                cursor.setPosition(offsetOf(text, range.value("end")), QTextCursor::KeepAnchor);
                cursor.insertText(edit.value("newText").toString());
            }
            cursor.endEditBlock();
        }
    }
}

TextEditor::IAssistProvider *ExtensionHost::quickFixProvider()
{
    if (!m_quickFixProvider)
        m_quickFixProvider = new AlienQuickFixProvider(this);
    return m_quickFixProvider;
}

void ExtensionHost::requestRenameEdits(const FilePath &filePath, int line, int character,
                                      const QString &newName,
                                      const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "rename/provide",
        QJsonObject{{"uri", toHostPath(filePath)},
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

void ExtensionHost::requestDocumentLinks(
    const FilePath &filePath, const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "links/provide",
        QJsonObject{{"uri", toHostPath(filePath)}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// A link an extension put on the text. Hovering asks for it so the editor can
// underline it; following it opens what it points at - a file in the editor, a
// page in the browser.
void ExtensionHost::followDocumentLink(TextEditorWidget *widget, const QTextCursor &cursor,
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
                    answer.targetFilePath = fromHostPath(link.value("path").toString());
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

void ExtensionHost::requestHighlights(const FilePath &filePath, int line, int character,
                                     const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "highlights/provide",
        QJsonObject{{"uri", toHostPath(filePath)},
                    {"line", line},
                    {"character", character}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// Marks what the extension says is the same thing as what is under the cursor,
// in the colour the editor uses for its own occurrences.
void ExtensionHost::highlightOccurrences(TextEditorWidget *widget)
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

bool ExtensionHost::providesSymbolsFor(const FilePath &filePath) const
{
    return m_symbolLanguageIds.contains(languageIdFor(filePath));
}

void ExtensionHost::requestDocumentSymbols(
    const FilePath &filePath, const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "symbols/provide",
        QJsonObject{{"uri", toHostPath(filePath)}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

void ExtensionHost::requestReferences(const FilePath &filePath, int line, int character,
                                     const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "references/provide",
        QJsonObject{{"uri", toHostPath(filePath)},
                    {"line", line},
                    {"character", character}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

// Where the extension says the word is used, in the pane Qt Creator shows every
// other search in.
void ExtensionHost::findUsages(const FilePath &filePath, int line, int character,
                               const QString &word,
                               const std::function<void(Core::SearchResult *)> &reportSearch)
{
    requestReferences(filePath, line, character, [word, reportSearch](
                                                    const QJsonArray &locations) {
        Core::SearchResult *search = Core::SearchResultWindow::instance()->startNewSearch(
            Tr::tr("VSIX Extension Usages:"), {}, word);
        Utils::SearchResultItems items;
        for (const QJsonValue &value : locations) {
            const QJsonObject location = value.toObject();
            const FilePath path = FilePath::fromUserInput(location.value("path").toString());
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
void ExtensionHost::askAndRename(const FilePath &filePath, int line, int character,
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

class AlienSignatureProcessor final : public TextEditor::IAssistProcessor
{
public:
    explicit AlienSignatureProcessor(ExtensionHost *host) : m_host(host) {}

    TextEditor::IAssistProposal *perform() final
    {
        if (!m_host)
            return nullptr;
        const QTextCursor cursor = interface()->cursor();
        m_host->requestSignatureHelp(
            interface()->filePath(), cursor.blockNumber(), cursor.positionInBlock(),
            [this, position = interface()->position()](const QJsonObject &help) {
                const QJsonArray signatures = help.value("signatures").toArray();
                if (signatures.isEmpty()) {
                    setAsyncProposalAvailable(nullptr);
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
                setAsyncProposalAvailable(new TextEditor::FunctionHintProposal(
                    position,
                    TextEditor::FunctionHintProposalModelPtr(new AlienFunctionHintModel(
                        labels, help.value("activeParameter").toInt()))));
            });
        return nullptr;
    }

private:
    QPointer<ExtensionHost> m_host;
};

class AlienSignatureProvider final : public TextEditor::CompletionAssistProvider
{
public:
    explicit AlienSignatureProvider(ExtensionHost *host) : m_host(host) {}

    TextEditor::IAssistProcessor *createProcessor(const TextEditor::AssistInterface *) const final
    {
        return new AlienSignatureProcessor(m_host);
    }

    // What opens the hint: the characters the extension said it triggers on,
    // which for a call is nearly always the bracket that starts one.
    int activationCharSequenceLength() const final { return 1; }
    bool isActivationCharSequence(const QString &sequence) const final
    {
        return m_host && m_host->signatureTriggers().contains(sequence);
    }

private:
    QPointer<ExtensionHost> m_host;
};

// One watch an extension asked for. The pattern says which files it means; the
// directories under the base are what is actually watched, because that is what
// a file system reports on.
void ExtensionHost::addFileWatch(const QString &id, const QString &pattern, const QString &base)
{
    FileWatch watch;
    watch.id = id;
    // "**/" means any depth below the base, which is how nearly every pattern
    // an extension writes begins.
    QString glob = pattern;
    watch.recursive = glob.startsWith("**/");
    if (watch.recursive)
        glob = glob.mid(3);
    watch.matcher = QRegularExpression(
        QRegularExpression::wildcardToRegularExpression(glob,
                                                        QRegularExpression::UnanchoredWildcardConversion));

    FilePaths roots;
    if (!base.isEmpty()) {
        roots.append(fromHostPath(base));
    } else {
        for (const QJsonValue &folder : std::as_const(m_workspaceFolders))
            roots.append(fromHostPath(folder.toObject().value("path").toString()));
    }
    if (roots.isEmpty())
        return;

    for (const FilePath &root : roots) {
        if (!root.isDir())
            continue;
        watch.roots.append(root);
        fileWatcher()->addDirectory(root, Utils::FileSystemWatcher::WatchAllChanges);
        if (!watch.recursive)
            continue;
        // One level of directories as well, which covers where extensions
        // actually look without walking a whole checkout.
        const FilePaths children = root.dirEntries(Utils::DirFilterFlags(Utils::DirFilterFlag::Dirs
                                                  | Utils::DirFilterFlag::NoDotAndDotDot));
        for (const FilePath &child : children) {
            watch.roots.append(child);
            fileWatcher()->addDirectory(child, Utils::FileSystemWatcher::WatchAllChanges);
        }
    }
    m_watches.insert(id, watch);
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
    }
    return m_fileWatcher;
}

// A directory changed: which of its files did, and which watch wanted to know.
void ExtensionHost::handleWatchedDirectory(const FilePath &directory)
{
    const FilePaths present = directory.dirEntries(Utils::DirFilterFlags(Utils::DirFilterFlag::Files
                                               | Utils::DirFilterFlag::NoDotAndDotDot));
    QSet<FilePath> now(present.begin(), present.end());
    const QSet<FilePath> before = m_watchedContents.value(directory);
    m_watchedContents.insert(directory, now);

    const auto report = [this, &directory](const FilePath &file, const QString &kind) {
        for (const FileWatch &watch : std::as_const(m_watches)) {
            if (!watch.roots.contains(directory))
                continue;
            if (!watch.matcher.match(file.fileName()).hasMatch())
                continue;
            m_connection->sendNotification("watch/event",
                                           QJsonObject{{"id", watch.id},
                                                       {"path", toHostPath(file)},
                                                       {"kind", kind}});
        }
    };

    for (const FilePath &file : std::as_const(now)) {
        if (!before.contains(file))
            report(file, "created");
        else
            report(file, "changed");
    }
    for (const FilePath &file : before) {
        if (!now.contains(file))
            report(file, "deleted");
    }
}

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
void ExtensionHost::requestOnTypeFormatting(TextDocument *document, int line, int character,
                                            const QString &typed)
{
    if (!m_connection || !document)
        return;
    const FilePath filePath = document->filePath();
    const TextEditor::TabSettingsData tabs = document->tabSettings();
    m_connection->sendRequest(
        "onTypeFormatting/provide",
        QJsonObject{{"uri", toHostPath(filePath)},
                    {"line", line},
                    {"character", character},
                    {"ch", typed},
                    {"tabSize", tabs.m_indentSize},
                    {"insertSpaces",
                     tabs.m_tabPolicy == TextEditor::TabSettingsData::SpacesOnlyTabPolicy}},
        [filePath](const QJsonValue &result, const QString &error) {
            const QJsonArray edits = result.toArray();
            if (!error.isEmpty() || edits.isEmpty())
                return;
            TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
            if (!document)
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

// What the extension would write next, shown ahead of the caret as Qt Creator
// shows any other suggestion - the same ghost text Copilot uses, so accepting
// it works the way the user already knows.
// A language nobody provides for any more: whatever an extension put on those
// documents comes off, rather than waiting for the next edit to notice.
void ExtensionHost::releaseLanguageFeatures(const QString &languageId)
{
    for (IEditor *editor : DocumentModel::editorsForOpenedDocuments()) {
        TextEditorWidget *widget = TextEditorWidget::fromEditor(editor);
        if (!widget)
            continue;
        TextDocument *document = widget->textDocument();
        const FilePath path = document->filePath();
        if (languageIdFor(path) != languageId)
            continue;

        if (!m_foldingLanguageIds.contains(languageId) && m_foldingDocuments.contains(path)) {
            QTextDocument *contents = document->document();
            for (QTextBlock block = contents->begin(); block != contents->end();
                 block = block.next()) {
                TextEditor::TextBlockUserData::setFoldingIndent(block, 0);
            }
            document->setFoldingIndentExternallyProvided(false);
        }
        if (!m_semanticLanguageIds.contains(languageId) && document->syntaxHighlighter()) {
            TextEditor::SemanticHighlighter::setExtraAdditionalFormats(
                document->syntaxHighlighter(), {}, {});
        }
        if (!m_colorLanguageIds.contains(languageId))
            qDeleteAll(m_colors.take(path));
        if (!m_inlineLanguageIds.contains(languageId))
            widget->clearSuggestion();
    }
}

void ExtensionHost::refreshInlineCompletion(TextEditorWidget *widget)
{
    if (!m_connection || !widget)
        return;
    const FilePath filePath = widget->textDocument()->filePath();
    if (!m_inlineLanguageIds.contains(languageIdFor(filePath)))
        return;
    const QTextCursor cursor = widget->textCursor();
    const int line = cursor.blockNumber();
    const int character = cursor.positionInBlock();
    m_connection->sendRequest(
        "inlineCompletion/provide",
        QJsonObject{{"uri", toHostPath(filePath)}, {"line", line}, {"character", character}},
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

void ExtensionHost::refreshSemanticTokens(const FilePath &filePath)
{
    if (!m_connection || !m_semanticLanguageIds.contains(languageIdFor(filePath)))
        return;
    m_connection->sendRequest(
        "semanticTokens/provide", QJsonObject{{"uri", toHostPath(filePath)}},
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
void ExtensionHost::refreshFolding(const FilePath &filePath)
{
    if (!m_connection || !m_foldingLanguageIds.contains(languageIdFor(filePath)))
        return;
    m_connection->sendRequest(
        "folding/provide", QJsonObject{{"uri", toHostPath(filePath)}},
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

void ExtensionHost::refreshColors(const FilePath &filePath)
{
    if (!m_connection || !m_colorLanguageIds.contains(languageIdFor(filePath)))
        return;
    m_connection->sendRequest(
        "color/provide", QJsonObject{{"uri", toHostPath(filePath)}},
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

void ExtensionHost::refreshCodeLenses(const FilePath &filePath)
{
    if (!m_connection || !m_codeLensLanguageIds.contains(languageIdFor(filePath)))
        return;
    m_connection->sendRequest(
        "codeLens/provide", QJsonObject{{"uri", toHostPath(filePath)}},
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
                                                   this));
            }
            if (marks.isEmpty())
                return;
            m_codeLenses.insert(filePath, marks);
        });
}

void ExtensionHost::requestTasks(const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "tasks/list", QJsonObject{},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject().value("tasks").toArray() : QJsonArray());
        });
}

void ExtensionHost::runTask(const QString &id)
{
    if (!m_connection)
        return;
    m_connection->sendRequest(
        "tasks/run", QJsonObject{{"id", id}},
        [id](const QJsonValue &result, const QString &error) {
            if (!error.isEmpty() || !result.toObject().value("started").toBool()) {
                MessageManager::writeFlashing(
                    Tr::tr("The task \"%1\" did not start: %2")
                        .arg(id, error.isEmpty() ? Tr::tr("the extension did not run it")
                                                 : error));
            }
        });
}

void ExtensionHost::requestWorkspaceSymbols(
    const QString &query, const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection || !m_hasWorkspaceSymbols) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "workspaceSymbols/provide", QJsonObject{{"query", query}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toArray() : QJsonArray());
        });
}

void ExtensionHost::requestSignatureHelp(
    const FilePath &filePath, int line, int character,
    const std::function<void(const QJsonObject &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "signature/provide",
        QJsonObject{{"uri", toHostPath(filePath)}, {"line", line}, {"character", character}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject() : QJsonObject());
        });
}

TextEditor::CompletionAssistProvider *ExtensionHost::signatureProvider()
{
    if (!m_signatureProvider)
        m_signatureProvider = new AlienSignatureProvider(this);
    return m_signatureProvider;
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
    // Asked for when the selection changes rather than when the menu opens:
    // the answer comes from the host, and a menu is built the moment it is
    // shown.
    connect(ProjectExplorer::ProjectTree::instance(),
            &ProjectExplorer::ProjectTree::currentNodeChanged,
            this, &ExtensionHost::updateProjectTreeActions);
    for (IEditor *editor : DocumentModel::editorsForOpenedDocuments())
        attachEditorFeatures(editor);
}

void ExtensionHost::updateEditorTitleActions(TextEditorWidget *widget)
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
                    [this, command] { executeCommand(command); });
            widget->insertExtraToolBarAction(TextEditorWidget::Right, action);
        }
        action->setText(title);
        action->setVisible(true);
    }
}

// The editor's context menu is built from a shared container whenever it is
// opened, so one action per command reaches every editor - which is also why
// what a "when" clause rules out is hidden rather than left to be seen.
// Whether the editor has the keyboard, and whether what is in it can be
// changed: both gate what an extension contributes to the editor.
void ExtensionHost::reportEditorFocus()
{
    if (!m_connection)
        return;
    TextEditorWidget *widget = TextEditorWidget::currentTextEditorWidget();
    const bool focused = widget && widget->hasFocus();
    IDocument *document = EditorManager::currentDocument();
    const bool readOnly = document && document->isFileReadOnly();
    if (focused == m_editorFocused && readOnly == m_editorReadOnly)
        return;
    m_editorFocused = focused;
    m_editorReadOnly = readOnly;
    m_connection->sendNotification("editor/didChangeFocus",
                                   QJsonObject{{"focused", focused}, {"readOnly", readOnly}});
}

void ExtensionHost::updateEditorContextActions()
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
                    [this, command] { executeCommand(command); });
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
void ExtensionHost::updateProjectTreeActions(ProjectExplorer::Node *node)
{
    ActionContainer *fileMenu
        = ActionManager::actionContainer(ProjectExplorer::Constants::M_FILECONTEXT);
    ActionContainer *folderMenu
        = ActionManager::actionContainer(ProjectExplorer::Constants::M_FOLDERCONTEXT);
    if (!fileMenu || !folderMenu)
        return;

    for (QAction *action : std::as_const(m_projectTreeActions))
        action->setVisible(false);
    if (!m_connection || !node)
        return;

    const FilePath path = node->filePath();
    const bool isFolder = node->asFolderNode() != nullptr;
    if (!isOnHostDevice(path))
        return;

    requestResourceMenu(
        "explorer/context", path, isFolder,
        [this, isFolder, path, fileMenu, folderMenu](const QJsonArray &items) {
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
                        executeCommand(command,
                                       QJsonArray{QJsonObject{{"$uri", m_projectTreeResource}}});
                    });
                    container->addAction(ActionManager::registerAction(
                        action, Id("Alien.ProjectTree").withSuffix(QString('.' + key)),
                        Core::Context(ProjectExplorer::Constants::C_PROJECT_TREE)));
                }
                action->setText(item.value("title").toString());
                action->setVisible(true);
            }
            m_projectTreeResource = toHostPath(path);
        });
}

void ExtensionHost::requestResourceMenu(const QString &location, const FilePath &path,
                                        bool isFolder,
                                        const std::function<void(const QJsonArray &)> &callback)
{
    if (!m_connection) {
        callback({});
        return;
    }
    m_connection->sendRequest(
        "menus/forResource",
        QJsonObject{{"location", location},
                    {"path", toHostPath(path)},
                    {"isFolder", isFolder},
                    {"languageId", languageIdFor(path)}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject().value("items").toArray() : QJsonArray());
        });
}

// The pairs the extension named, rather than the ones a C-like language has.
class AlienAutoCompleter final : public TextEditor::AutoCompleter
{
public:
    AlienAutoCompleter(const QList<QPair<QString, QString>> &closing,
                       const QList<QPair<QString, QString>> &surrounding)
        : m_closing(closing)
        , m_surrounding(surrounding)
    {}

    QString insertMatchingBrace(const QTextCursor &, const QString &text, QChar,
                                bool skipChars, int *skippedChars) const final
    {
        Q_UNUSED(skipChars)
        Q_UNUSED(skippedChars)
        for (const auto &[open, close] : m_closing) {
            if (text == open)
                return close;
        }
        return {};
    }

    QString insertMatchingQuote(const QTextCursor &, const QString &text, QChar,
                                bool skipChars, int *skippedChars) const final
    {
        Q_UNUSED(skipChars)
        Q_UNUSED(skippedChars)
        for (const auto &[open, close] : m_surrounding) {
            if (text == open && open == close)
                return close;
        }
        return {};
    }

private:
    QList<QPair<QString, QString>> m_closing;
    QList<QPair<QString, QString>> m_surrounding;
};

// How the extension says the language is written, given to the editor: what a
// comment looks like, which characters come in pairs, and what marks a region.
void ExtensionHost::applyLanguageConfiguration(TextEditorWidget *widget)
{
    TextDocument *document = widget->textDocument();
    const QString languageId = languageIdFor(document->filePath());
    const VscodeLanguage *language = nullptr;
    for (const VscodeLanguage &candidate : m_declaredLanguages) {
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
    if (!pairs.isEmpty() || !language->surroundingPairs.isEmpty())
        widget->setAutoCompleter(new AlienAutoCompleter(pairs, language->surroundingPairs));

    if (!language->foldingStartMarker.isEmpty() && !language->foldingEndMarker.isEmpty()) {
        const FilePath path = document->filePath();
        if (!m_markerFoldedDocuments.contains(path)) {
            m_markerFoldedDocuments.insert(path);
            connect(document, &TextDocument::contentsChanged, this,
                    [this, path] { refreshMarkerFolding(path); });
        }
        refreshMarkerFolding(document->filePath());
    }
}

// Regions the extension marks off by hand, which fold like any other block.
void ExtensionHost::refreshMarkerFolding(const FilePath &filePath)
{
    TextDocument *document = TextDocument::textDocumentForFilePath(filePath);
    if (!document)
        return;
    const QString languageId = languageIdFor(filePath);
    const VscodeLanguage *language = nullptr;
    for (const VscodeLanguage &candidate : m_declaredLanguages) {
        if (candidate.id == languageId) {
            language = &candidate;
            break;
        }
    }
    if (!language || language->foldingStartMarker.isEmpty())
        return;

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

void ExtensionHost::attachEditorFeatures(IEditor *editor)
{
    TextEditorWidget *widget = TextEditorWidget::fromEditor(editor);
    if (!widget)
        return;
    updateEditorTitleActions(widget);
    const QString languageId = languageIdFor(widget->textDocument()->filePath());

    if (m_hoverLanguageIds.contains(languageId) && !m_hoverWidgets.contains(widget)) {
        widget->addHoverHandler(hoverHandler());
        m_hoverWidgets.append(widget);
    }

    if (m_signatureLanguageIds.contains(languageId)) {
        widget->textDocument()->setFunctionHintAssistProvider(signatureProvider());
    }

    if (m_linkLanguageIds.contains(languageId)) {
        widget->setOptionalActions(widget->optionalActions()
                                   | TextEditor::OptionalActions::JumpToFileUnderCursor);
        connect(widget, &TextEditorWidget::requestLinkAt, this,
                [this, widget](const QTextCursor &cursor, const Utils::LinkHandler &callback,
                               bool resolveTarget) {
                    followDocumentLink(widget, cursor, callback, resolveTarget);
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

    if (m_referenceLanguageIds.contains(languageId)) {
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

    if (m_renameLanguageIds.contains(languageId)) {
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
        widget->textDocument()->setQuickFixAssistProvider(quickFixProvider());
    }

    if (m_formattingLanguageIds.contains(languageId)
        || m_rangeFormattingLanguageIds.contains(languageId)) {
        // Ownership goes to the document, and it replaces whatever formatter
        // was there - the extension claimed this language.
        TextEditor::TextDocument *document = widget->textDocument();
        document->setFormatter(new AlienFormatter(this, document));
        // Range mode where a selection can be formatted on its own, which is
        // what makes Format Selection do less than Format Document.
        document->setFormatterMode(m_rangeFormattingLanguageIds.contains(languageId)
                                       ? TextEditor::Formatter::FormatMode::Range
                                       : TextEditor::Formatter::FormatMode::FullDocument);
    }

    applyLanguageConfiguration(widget);

    if (m_onTypeLanguageIds.contains(languageId)) {
        const FilePath path = widget->textDocument()->filePath();
        if (!m_onTypeDocuments.contains(path)) {
            m_onTypeDocuments.insert(path);
            TextDocument *document = widget->textDocument();
            connect(document, &TextDocument::contentsChangedWithPosition, this,
                    [this, document](int position, int charsRemoved, int charsAdded) {
                        // One character, freshly typed, and one this extension
                        // asked to hear about.
                        if (charsRemoved != 0 || charsAdded != 1)
                            return;
                        const QString typed = document->plainText().mid(position, 1);
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

    if (m_semanticLanguageIds.contains(languageId)) {
        const FilePath path = widget->textDocument()->filePath();
        refreshSemanticTokens(path);
        if (!m_semanticDocuments.contains(path)) {
            m_semanticDocuments.insert(path);
            connect(widget->textDocument(), &TextEditor::TextDocument::contentsChanged, this,
                    [this, path] { refreshSemanticTokens(path); });
        }
    }

    if (m_foldingLanguageIds.contains(languageId)) {
        const FilePath path = widget->textDocument()->filePath();
        refreshFolding(path);
        if (!m_foldingDocuments.contains(path)) {
            m_foldingDocuments.insert(path);
            connect(widget->textDocument(), &TextEditor::TextDocument::contentsChanged, this,
                    [this, path] { refreshFolding(path); });
        }
    }

    if (m_colorLanguageIds.contains(languageId)) {
        const FilePath path = widget->textDocument()->filePath();
        refreshColors(path);
        if (!m_colorDocuments.contains(path)) {
            m_colorDocuments.insert(path);
            connect(widget->textDocument(), &TextEditor::TextDocument::contentsChanged, this,
                    [this, path] { refreshColors(path); });
        }
    }

    if (m_codeLensLanguageIds.contains(languageId)) {
        const FilePath path = widget->textDocument()->filePath();
        refreshCodeLenses(path);
        // What an extension says about a line depends on what the line says.
        // Connected once per document, since a file can be open twice.
        if (!m_codeLensDocuments.contains(path)) {
            m_codeLensDocuments.insert(path);
            connect(widget->textDocument(), &TextEditor::TextDocument::contentsChanged, this,
                    [this, path] { refreshCodeLenses(path); });
        }
    }

    if (m_typeDefinitionLanguageIds.contains(languageId)) {
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
                            Utils::Link link(fromHostPath(location.value("uri").toString()),
                                             at.value("line").toInt() + 1,
                                             at.value("character").toInt());
                            link.linkTextStart = start;
                            link.linkTextEnd = end;
                            callback(link);
                        });
                });
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
                        [this, callback, start = wordCursor.selectionStart(),
                         end = wordCursor.selectionEnd()](const QJsonArray &locations) {
                            if (locations.isEmpty()) {
                                callback(Utils::Link());
                                return;
                            }
                            const QJsonObject location = locations.first().toObject();
                            const QJsonObject startPos
                                = location.value("range").toObject().value("start").toObject();
                            Utils::Link link(
                                fromHostPath(location.value("uri").toString()),
                                startPos.value("line").toInt() + 1,
                                startPos.value("character").toInt());
                            link.linkTextStart = start;
                            link.linkTextEnd = end;
                            callback(link);
                        });
                });
    }
}

void ExtensionHost::resolveQuickPick(int id, const QList<int> &indexes)
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

void ExtensionHost::resolveMessageQuestion(int id, int index)
{
    if (const auto respond = m_pendingPrompts.take(id)) {
        respond(index >= 0 ? QJsonValue(index) : QJsonValue(QJsonValue::Null), {});
    }
}

void ExtensionHost::resolveInputBox(int id, const QString &value, bool accepted)
{
    if (const auto respond = m_pendingPrompts.take(id))
        respond(accepted ? QJsonValue(value) : QJsonValue(QJsonValue::Null), {});
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
