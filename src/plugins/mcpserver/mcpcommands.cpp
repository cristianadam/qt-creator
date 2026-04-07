// Copyright (C) 2025 David M. Cotter
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mcpcommands.h"
#include "issuesmanager.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/find/findplugin.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/session.h>
#include <coreplugin/vcsmanager.h>

#include <debugger/breakhandler.h>
#include <debugger/breakpoint.h>
#include <debugger/debuggerengine.h>
#include <debugger/debuggerruncontrol.h>
#include <debugger/enginemanager.h>
#include <debugger/stackhandler.h>
#include <debugger/threadshandler.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/editorconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/target.h>

#include <texteditor/textdocument.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/filesearch.h>
#include <utils/id.h>
#include <utils/mimeutils.h>
#include <utils/savefile.h>

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QThread>
#include <QTimer>

using namespace Utils;
using namespace ProjectExplorer;

Q_LOGGING_CATEGORY(mcpCommands, "qtc.mcpserver.commands", QtWarningMsg)

namespace Mcp::Internal {

McpCommands::McpCommands(QObject *parent)
    : QObject(parent)
{}

Mcp::Schema::Tool::OutputSchema McpCommands::searchResultsSchema()
{
    static Mcp::Schema::Tool::OutputSchema cachedSchema = [] {
        QFile schemaFile(":/mcpserver/schemas/search-results-schema.json");
        if (!schemaFile.open(QIODevice::ReadOnly)) {
            qCWarning(mcpCommands) << "Failed to open schemas/search-results-schema.json from resources:"
                                   << schemaFile.errorString();
            return Mcp::Schema::Tool::OutputSchema{};
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(schemaFile.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(mcpCommands) << "Failed to parse search-results-schema.json:"
                                   << parseError.errorString();
            return Mcp::Schema::Tool::OutputSchema{};
        }

        QJsonObject obj = doc.object();
        Mcp::Schema::Tool::OutputSchema schema;
        //if (obj.contains("properties"))
        //    schema._properties = obj["properties"].toObject();
        if (obj.contains("required") && obj["required"].isArray()) {
            QStringList req;
            for (const QJsonValue &v : obj["required"].toArray())
                req.append(v.toString());
            schema._required = req;
        }
        return schema;
    }();

    return cachedSchema;
}

QString McpCommands::stopDebug()
{
    QStringList results;
    results.append("=== STOP DEBUGGING ===");

    // Use ActionManager to trigger the "Stop Debugging" action
    Core::ActionManager *actionManager = Core::ActionManager::instance();
    if (!actionManager) {
        results.append("ERROR: ActionManager not available");
        return results.join("\n");
    }

    // Try different possible action IDs for stopping debugging
    QStringList stopActionIds
        = {"Debugger.StopDebugger",
           "Debugger.Stop",
           "ProjectExplorer.StopDebugging",
           "ProjectExplorer.Stop",
           "Debugger.StopDebugging"};

    bool actionTriggered = false;
    for (const QString &actionId : stopActionIds) {
        results.append("Trying stop debug action: " + actionId);

        Core::Command *command = actionManager->command(Id::fromString(actionId));
        if (command && command->action()) {
            results.append("Found stop debug action, triggering...");
            command->action()->trigger();
            results.append("Stop debug action triggered successfully");
            actionTriggered = true;
            break;
        } else {
            results.append("Stop debug action not found: " + actionId);
        }
    }

    if (!actionTriggered) {
        results.append("WARNING: No stop debug action found among tried IDs");
        results.append(
            "You may need to stop debugging manually from Qt Creator's debugger interface");
    }

    results.append("");
    results.append("=== STOP DEBUG RESULT ===");
    results.append("Stop debug command completed.");

    return results.join("\n");
}

QString McpCommands::getVersion()
{
    return QCoreApplication::applicationVersion();
}

QString McpCommands::getBuildStatus()
{
    const auto buildProgress = BuildManager::currentProgress();
    if (buildProgress)
        return QString("Building: %1% - %2").arg(buildProgress->first).arg(buildProgress->second);
    else
        return "Not building";
}

bool McpCommands::openFile(const QString &path)
{
    if (path.isEmpty()) {
        qCDebug(mcpCommands) << "Empty file path provided";
        return false;
    }

    FilePath filePath = FilePath::fromUserInput(path);

    if (!filePath.exists()) {
        qCDebug(mcpCommands) << "File does not exist:" << path;
        return false;
    }

    qCDebug(mcpCommands) << "Opening file:" << path;

    Core::EditorManager::openEditor(filePath);

    return true;
}

QString McpCommands::getFilePlainText(const QString &path)
{
    if (path.isEmpty()) {
        qCDebug(mcpCommands) << "Empty file path provided";
        return QString();
    }

    FilePath filePath = FilePath::fromUserInput(path);

    if (!filePath.exists()) {
        qCDebug(mcpCommands) << "File does not exist:" << path;
        return QString();
    }

    if (auto doc = TextEditor::TextDocument::textDocumentForFilePath(filePath))
        return doc->plainText();

    MimeType mime = mimeTypeForFile(filePath);
    if (!mime.inherits("text/plain")) {
        qCDebug(mcpCommands) << "File is not a plain text document:" << path
                             << "MIME type:" << mime.name();
        return QString();
    }

    Result<QByteArray> contents = filePath.fileContents();
    if (contents.has_value())
        return Core::EditorManager::defaultTextEncoding().decode(*contents);

    qCDebug(mcpCommands) << "Failed to read file contents:" << path << "Error:" << contents.error();
    return QString();
}

bool McpCommands::setFilePlainText(const QString &path, const QString &contents)
{
    if (path.isEmpty()) {
        qCDebug(mcpCommands) << "Empty file path provided";
        return false;
    }

    FilePath filePath = FilePath::fromUserInput(path);

    if (!filePath.exists()) {
        qCDebug(mcpCommands) << "File does not exist:" << path;
        return false;
    }

    auto doc = Core::DocumentModel::documentForFilePath(filePath);

    if (!doc) {
        qCDebug(mcpCommands) << "No document found for file:" << path;
        return false;
    }

    if (auto textDoc = qobject_cast<TextEditor::TextDocument *>(doc)) {
        textDoc->document()->setPlainText(contents);
        return true;
    }

    MimeType mime = mimeTypeForFile(filePath);
    if (!mime.inherits("text/plain")) {
        qCDebug(mcpCommands) << "File is not a plain text document:" << path
                             << "MIME type:" << mime.name();
        return false;
    }

    qCDebug(mcpCommands) << "Setting plain text for file:" << path;

    Result<qint64> result = filePath.writeFileContents(
        Core::EditorManager::defaultTextEncoding().encode(contents));

    if (!result)
        qCDebug(mcpCommands) << "Failed to write file contents:" << path << "Error:" << result.error();
    return result.has_value();
}

bool McpCommands::saveFile(const QString &path)
{
    if (path.isEmpty()) {
        qCDebug(mcpCommands) << "Empty file path provided";
        return false;
    }

    FilePath filePath = FilePath::fromUserInput(path);

    auto doc = Core::DocumentModel::documentForFilePath(filePath);

    if (!doc) {
        qCDebug(mcpCommands) << "No document found for file:" << path;
        return false;
    }

    if (!doc->isModified()) {
        qCDebug(mcpCommands) << "Document is not modified, no need to save:" << path;
        return true;
    }

    qCDebug(mcpCommands) << "Saving file:" << path;

    Result<> res = doc->save();
    if (!res)
        qCDebug(mcpCommands) << "Failed to save document:" << path << "Error:" << res.error();

    return res.has_value();
}

bool McpCommands::closeFile(const QString &path)
{
    if (path.isEmpty()) {
        qCDebug(mcpCommands) << "Empty file path provided";
        return false;
    }

    FilePath filePath = FilePath::fromUserInput(path);

    auto doc = Core::DocumentModel::documentForFilePath(filePath);

    if (!doc) {
        qCDebug(mcpCommands) << "No document found for file:" << path;
        return false;
    }

    qCDebug(mcpCommands) << "Closing file:" << path;

    bool closed = Core::EditorManager::closeDocuments({doc}, false);
    if (!closed)
        qCDebug(mcpCommands) << "Failed to close document:" << path;

    return closed;
}

QStringList McpCommands::findFiles(
    const QList<ProjectExplorer::Project *> &projects, const QRegularExpression &re)
{
    QStringList result;
    for (auto project : projects) {
        const FilePaths matches = project->files([&re](const Node *n) {
            return !n->filePath().isEmpty() && re.match(n->filePath().fileName()).hasMatch();
        });
        result.append(Utils::transform(matches, &FilePath::toUserOutput));
    }
    return result;
}

static QList<Project *> projectsForName(const QString &name)
{
    return Utils::filtered(ProjectManager::projects(), Utils::equal(&Project::displayName, name));
}

static FindFlags findFlags(bool regex, bool caseSensitive)
{
    FindFlags flags;
    if (regex)
        flags |= FindRegularExpression;
    if (caseSensitive)
        flags |= FindCaseSensitively;
    return flags;
}

static void findInFiles(
    FileContainer fileContainer,
    bool regex,
    bool caseSensitive,
    const QString &pattern,
    QObject *guard,
    const McpCommands::ResponseCallback &callback)
{
    const QFuture<SearchResultItems> future = Utils::findInFiles(
        pattern,
        fileContainer,
        findFlags(regex, caseSensitive),
        TextEditor::TextDocument::openedTextDocumentContents());
    Utils::onFinished(future, guard, [callback](const QFuture<SearchResultItems> &future) {
        QJsonArray resultsArray;
        for (Utils::SearchResultItems results : future.results()) {
            for (const SearchResultItem &item : results) {
                QJsonObject resultObj;
                const Text::Range range = item.mainRange();
                const QString lineText = item.lineText();
                const int startCol = range.begin.column;
                const int endCol = range.end.column;
                const QString matchedText = lineText.mid(startCol, endCol - startCol);

                resultObj["file"] = item.path().value(0, QString());
                resultObj["line"] = range.begin.line;
                resultObj["column"] = startCol + 1; // Convert 0-based to 1-based
                resultObj["text"] = matchedText;
                resultsArray.append(resultObj);
            }
        }

        QJsonObject response;
        response["results"] = resultsArray;
        callback(response);
    });
}

void McpCommands::searchInFile(
    const QString &path,
    const QString &pattern,
    bool regex,
    bool caseSensitive,
    const ResponseCallback &callback)
{
    const FilePath filePath = FilePath::fromUserInput(path);
    if (!filePath.exists()) {
        callback({});
        qCDebug(mcpCommands) << "File does not exist:" << path;
        return;
    }

    TextEncoding encoding;
    if (auto doc = TextEditor::TextDocument::textDocumentForFilePath(filePath)) {
        encoding = doc->encoding();
    } else {
        for (const Project *project : ProjectManager::projects()) {
            const EditorConfiguration *config = project->editorConfiguration();
            if (project->isKnownFile(filePath)) {
                encoding = config->useGlobalSettings() ? Core::EditorManager::defaultTextEncoding()
                                                       : config->textEncoding();
                break;
            }
        }
    }
    if (!encoding.isValid())
        encoding = Core::EditorManager::defaultTextEncoding();

    FileListContainer fileContainer({filePath}, {encoding});

    findInFiles(fileContainer, regex, caseSensitive, pattern, this, callback);
}

void McpCommands::searchInFiles(
    const QString &filePattern,
    const std::optional<QString> &projectName,
    const QString &pattern,
    bool regex,
    bool caseSensitive,
    const ResponseCallback &callback)
{
    const QList<Project *> projects = projectName ? projectsForName(*projectName)
                                                  : ProjectManager::projects();

    const FilterFilesFunction filterFiles
        = Utils::filterFilesFunction({filePattern.isEmpty() ? "*" : filePattern}, {});
    const QMap<FilePath, TextEncoding> openEditorEncodings
        = TextEditor::TextDocument::openedTextDocumentEncodings();
    QMap<FilePath, TextEncoding> encodings;
    for (const Project *project : projects) {
        const EditorConfiguration *config = project->editorConfiguration();
        TextEncoding projectEncoding = config->useGlobalSettings()
                                           ? Core::EditorManager::defaultTextEncoding()
                                           : config->textEncoding();
        const FilePaths filteredFiles = filterFiles(project->files(
            Core::Find::hasFindFlag(DontFindGeneratedFiles) ? Project::SourceFiles
                                                            : Project::AllFiles));
        for (const FilePath &fileName : filteredFiles) {
            TextEncoding encoding = openEditorEncodings.value(fileName);
            if (!encoding.isValid())
                encoding = projectEncoding;
            encodings.insert(fileName, encoding);
        }
    }
    FileListContainer fileContainer(encodings.keys(), encodings.values());

    findInFiles(fileContainer, regex, caseSensitive, pattern, this, callback);
}

void McpCommands::searchInDirectory(
    const QString directory,
    const QString &pattern,
    bool regex,
    bool caseSensitive,
    const ResponseCallback &callback)
{
    const FilePath dirPath = FilePath::fromUserInput(directory);
    if (!dirPath.exists() || !dirPath.isDir()) {
        callback({});
        qCDebug(mcpCommands) << "Directory does not exist or is not a directory:" << directory;
        return;
    }

    SubDirFileContainer fileContainer({dirPath}, {}, {}, {});

    findInFiles(fileContainer, regex, caseSensitive, pattern, this, callback);
}

static void replace(
    FileContainer fileContainer,
    bool regex,
    bool caseSensitive,
    const QString &pattern,
    const QString &replacement,
    QObject *guard,
    const McpCommands::ResponseCallback &callback)
{
    const QFuture<SearchResultItems> future = Utils::findInFiles(
        pattern,
        fileContainer,
        findFlags(regex, caseSensitive),
        TextEditor::TextDocument::openedTextDocumentContents());
    Utils::onFinished(future, guard, [callback, replacement](const QFuture<SearchResultItems> &future) {
        QJsonObject response;
        bool success = true;

        TextEditor::PlainRefactoringFileFactory changes;

        QHash<Utils::FilePath, TextEditor::RefactoringFilePtr> refactoringFiles;
        for (const SearchResultItems &results : future.results()) {
            for (const SearchResultItem &item : results) {
                Text::Range range = item.mainRange();
                if (range.begin >= range.end)
                    continue;
                const FilePath filePath = FilePath::fromUserInput(item.path().value(0));
                if (filePath.isEmpty())
                    continue;
                TextEditor::RefactoringFilePtr refactoringFile = refactoringFiles.value(filePath);
                if (!refactoringFile)
                    refactoringFile = refactoringFiles.insert(filePath, changes.file(filePath)).value();
                const int start = refactoringFile->position(range.begin);
                const int end = refactoringFile->position(range.end);
                ChangeSet changeSet = refactoringFile->changeSet();
                changeSet.replace(ChangeSet::Range(start, end), replacement);
                refactoringFile->setChangeSet(changeSet);
            }
        }

        for (auto refactoringFile : refactoringFiles) {
            if (!refactoringFile->apply()) {
                qCDebug(mcpCommands) << "Failed to apply changes for file:" << refactoringFile->filePath().toUserOutput();
                success = false;
            }
        }

        response["ok"] = success;
        callback(response);
    });
}

void McpCommands::replaceInFile(
    const QString &path,
    const QString &pattern,
    const QString &replacement,
    bool regex,
    bool caseSensitive,
    const ResponseCallback &callback)
{
     const FilePath filePath = FilePath::fromUserInput(path);
    if (!filePath.exists()) {
        callback({});
        qCDebug(mcpCommands) << "File does not exist:" << path;
        return;
    }

    TextEncoding encoding;
    if (auto doc = TextEditor::TextDocument::textDocumentForFilePath(filePath)) {
        encoding = doc->encoding();
    } else {
        for (const Project *project : ProjectManager::projects()) {
            const EditorConfiguration *config = project->editorConfiguration();
            if (project->isKnownFile(filePath)) {
                encoding = config->useGlobalSettings() ? Core::EditorManager::defaultTextEncoding()
                                                       : config->textEncoding();
                break;
            }
        }
    }
    if (!encoding.isValid())
        encoding = Core::EditorManager::defaultTextEncoding();

    FileListContainer fileContainer({filePath}, {encoding});

    replace(fileContainer, regex, caseSensitive, pattern, replacement, this, callback);
}

void McpCommands::replaceInFiles(
    const QString &filePattern,
    const std::optional<QString> &projectName,
    const QString &pattern,
    const QString &replacement,
    bool regex,
    bool caseSensitive,
    const ResponseCallback &callback)
{
    const QList<Project *> projects = projectName ? projectsForName(*projectName)
                                                  : ProjectManager::projects();

    const FilterFilesFunction filterFiles
        = Utils::filterFilesFunction({filePattern.isEmpty() ? "*" : filePattern}, {});
    const QMap<FilePath, TextEncoding> openEditorEncodings
        = TextEditor::TextDocument::openedTextDocumentEncodings();
    QMap<FilePath, TextEncoding> encodings;
    for (const Project *project : projects) {
        const EditorConfiguration *config = project->editorConfiguration();
        TextEncoding projectEncoding = config->useGlobalSettings()
                                           ? Core::EditorManager::defaultTextEncoding()
                                           : config->textEncoding();
        const FilePaths filteredFiles = filterFiles(project->files(
            Core::Find::hasFindFlag(DontFindGeneratedFiles) ? Project::SourceFiles
                                                            : Project::AllFiles));
        for (const FilePath &fileName : filteredFiles) {
            TextEncoding encoding = openEditorEncodings.value(fileName);
            if (!encoding.isValid())
                encoding = projectEncoding;
            encodings.insert(fileName, encoding);
        }
    }
    FileListContainer fileContainer(encodings.keys(), encodings.values());

    replace(fileContainer, regex, caseSensitive, pattern, replacement, this, callback);
}

void McpCommands::replaceInDirectory(
    const QString directory,
    const QString &pattern,
    const QString &replacement,
    bool regex,
    bool caseSensitive,
    const ResponseCallback &callback)
{
    const FilePath dirPath = FilePath::fromUserInput(directory);
    if (!dirPath.exists() || !dirPath.isDir()) {
        callback({});
        qCDebug(mcpCommands) << "Directory does not exist or is not a directory:" << directory;
        return;
    }

    SubDirFileContainer fileContainer({dirPath}, {}, {}, {});

    replace(fileContainer, regex, caseSensitive, pattern, replacement, this, callback);
}

QStringList McpCommands::listProjects()
{
    QStringList projects;

    QList<Project *> projectList = ProjectManager::projects();
    for (Project *project : projectList) {
        projects.append(project->displayName());
    }

    qCDebug(mcpCommands) << "Found projects:" << projects;

    return projects;
}

QStringList McpCommands::listBuildConfigs()
{
    QStringList configs;

    Project *project = ProjectManager::startupProject();
    if (!project) {
        qCDebug(mcpCommands) << "No current project";
        return configs;
    }

    Target *target = project->activeTarget();
    if (!target) {
        qCDebug(mcpCommands) << "No active target";
        return configs;
    }

    QList<BuildConfiguration *> buildConfigs = target->buildConfigurations();
    for (BuildConfiguration *config : buildConfigs) {
        configs.append(config->displayName());
    }

    qCDebug(mcpCommands) << "Found build configurations:" << configs;

    return configs;
}

bool McpCommands::switchToBuildConfig(const QString &name)
{
    if (name.isEmpty()) {
        qCDebug(mcpCommands) << "Empty build configuration name provided";
        return false;
    }

    Project *project = ProjectManager::startupProject();
    if (!project) {
        qCDebug(mcpCommands) << "No current project";
        return false;
    }

    Target *target = project->activeTarget();
    if (!target) {
        qCDebug(mcpCommands) << "No active target";
        return false;
    }

    QList<BuildConfiguration *> buildConfigs = target->buildConfigurations();
    for (BuildConfiguration *config : buildConfigs) {
        if (config->displayName() == name) {
            qCDebug(mcpCommands) << "Switching to build configuration:" << name;
            target->setActiveBuildConfiguration(config, SetActive::Cascade);
            return true;
        }
    }

    qCDebug(mcpCommands) << "Build configuration not found:" << name;
    return false;
}

bool McpCommands::quit()
{
    Core::ICore::exit();
    return true;
}

void McpCommands::executeCommand(
    const QString &command,
    const QString &arguments,
    const QString &workingDirectory,
    const ResponseCallback &callback)
{
    CommandLine cmd(FilePath::fromUserInput(command), arguments, CommandLine::Raw);
    auto process = new Process(this);
    connect(process, &Process::done, this, [process, callback]() {
        QJsonObject response;
        response["exitCode"] = process->exitCode();
        response["exitMessage"] = process->verboseExitMessage();
        response["stdout"] = process->readAllStandardOutput();
        response["stderr"] = process->readAllStandardError();
        callback(response);
        process->deleteLater();
    });
    process->setCommand(cmd);
    if (!workingDirectory.isEmpty())
        process->setWorkingDirectory(FilePath::fromUserInput(workingDirectory));
    process->start();
}

bool McpCommands::isDebuggingActive()
{
    // Check if debugging is currently active by looking at debugger actions
    Core::ActionManager *actionManager = Core::ActionManager::instance();
    if (!actionManager) {
        return false;
    }

    // Try different possible action IDs for checking if debugging is active
    QStringList stopActionIds
        = {"Debugger.Stop", "Debugger.StopDebugger", "ProjectExplorer.StopDebugging"};

    for (const QString &actionId : stopActionIds) {
        Core::Command *command = actionManager->command(Id::fromString(actionId));
        if (command && command->action() && command->action()->isEnabled()) {
            qCDebug(mcpCommands) << "Debug session is active (Stop action enabled):" << actionId;
            return true;
        }
    }

    // Also check "Abort Debugging" action
    QStringList abortActionIds
        = {"Debugger.Abort", "Debugger.AbortDebugger", "ProjectExplorer.AbortDebugging"};

    for (const QString &actionId : abortActionIds) {
        Core::Command *command = actionManager->command(Id::fromString(actionId));
        if (command && command->action() && command->action()->isEnabled()) {
            qCDebug(mcpCommands) << "Debug session is active (Abort action enabled):" << actionId;
            return true;
        }
    }

    qCDebug(mcpCommands) << "No active debug session detected";
    return false;
}

QString McpCommands::abortDebug()
{
    qCDebug(mcpCommands) << "Attempting to abort debug session...";

    // Use ActionManager to trigger the "Abort Debugging" action
    Core::ActionManager *actionManager = Core::ActionManager::instance();
    if (!actionManager) {
        return "ERROR: ActionManager not available";
    }

    // Try different possible action IDs for aborting debugging
    QStringList abortActionIds
        = {"Debugger.Abort",
           "Debugger.AbortDebugger",
           "ProjectExplorer.AbortDebugging",
           "Debugger.AbortDebug"};

    for (const QString &actionId : abortActionIds) {
        qCDebug(mcpCommands) << "Trying abort debug action:" << actionId;

        Core::Command *command = actionManager->command(Id::fromString(actionId));
        if (command && command->action() && command->action()->isEnabled()) {
            qCDebug(mcpCommands) << "Found abort debug action, triggering...";
            command->action()->trigger();
            return "Abort debug action triggered successfully: " + actionId;
        }
    }

    return "Abort debug action not found or not enabled";
}

bool McpCommands::killDebuggedProcesses()
{
    qCDebug(mcpCommands) << "Attempting to kill debugged processes...";

    // This is a simplified implementation
    // In a real scenario, you'd need to:
    // 1. Get the list of processes being debugged from the debugger
    // 2. Kill each process appropriately

    // For now, we'll try to find and kill any processes that might be debugged
    // This is platform-specific and would need proper implementation

    // TODO: Implement proper process killing for debugged applications
    // This could involve:
    // - Finding the debugged process PID
    // - Using platform-specific kill commands
    // - Handling different types of debugged processes (local, remote, etc.)

    return true; // Simplified for now - always return true
}

QString McpCommands::getCurrentProject()
{
    Project *project = ProjectManager::startupProject();
    if (project) {
        return project->displayName();
    }
    return QString();
}

QString McpCommands::getCurrentBuildConfig()
{
    Project *project = ProjectManager::startupProject();
    if (!project) {
        return QString();
    }

    Target *target = project->activeTarget();
    if (!target) {
        return QString();
    }

    BuildConfiguration *buildConfig = target->activeBuildConfiguration();
    if (buildConfig) {
        return buildConfig->displayName();
    }

    return QString();
}

QStringList McpCommands::listOpenFiles()
{
    QStringList files;

    QList<Core::IDocument *> documents = Core::DocumentModel::openedDocuments();
    for (Core::IDocument *doc : documents) {
        files.append(doc->filePath().toUserOutput());
    }

    qCDebug(mcpCommands) << "Open files:" << files;

    return files;
}

QStringList McpCommands::listVisibleFiles()
{
    QStringList files;

    const QList<Core::IEditor *> editors = Core::EditorManager::visibleEditors();
    for (Core::IEditor *editor : editors) {
        if (auto doc = editor->document()) {
            if (editor == Core::EditorManager::currentEditor())
                files.prepend(doc->filePath().toUserOutput());
            else
                files.append(doc->filePath().toUserOutput());
        }
    }

    qCDebug(mcpCommands) << "Visible files:" << files;

    return files;
}

QStringList McpCommands::listSessions()
{
    QStringList sessions = Core::SessionManager::sessions();
    qCDebug(mcpCommands) << "Available sessions:" << sessions;
    return sessions;
}

QString McpCommands::getCurrentSession()
{
    QString session = Core::SessionManager::activeSession();
    qCDebug(mcpCommands) << "Current session:" << session;
    return session;
}

bool McpCommands::loadSession(const QString &sessionName)
{
    return Core::SessionManager::loadSession(sessionName);
}

bool McpCommands::saveSession()
{
    qCDebug(mcpCommands) << "Saving current session";

    bool successB = Core::SessionManager::saveSession();
    if (successB) {
        qCDebug(mcpCommands) << "Successfully saved session";
    } else {
        qCDebug(mcpCommands) << "Failed to save session";
    }

    return successB;
}

QJsonObject McpCommands::listIssues()
{
    return m_issuesManager.getCurrentIssues();
}

QJsonObject McpCommands::listIssues(const QString &path)
{
    return m_issuesManager.getCurrentIssues(Utils::FilePath::fromUserInput(path));
}

Utils::Result<QStringList> McpCommands::projectDependencies(const QString &projectName)
{
    for (Project * candidate : ProjectManager::projects()) {
        if (candidate->displayName() == projectName) {
            QStringList projects;
            const QList<Project *> projectList = ProjectManager::dependencies(candidate);
            for (Project *project : projectList)
                projects.append(project->displayName());
            return projects;
        }
    }

    return Utils::ResultError("No project found with name: " + projectName);
}

QMap<QString, QSet<QString>> McpCommands::knownRepositoriesInProject(const QString &projectName)
{
    const FilePaths projectDirectories
        = Utils::transform(projectsForName(projectName), &Project::projectDirectory);
    if (projectDirectories.isEmpty())
        return {};
    QMap<QString, QSet<QString>> repos;
    const QList<Core::IVersionControl *> versionControls = Core::VcsManager::versionControls();
    for (const Core::IVersionControl *vcs : versionControls) {
        const FilePaths repositories = Utils::filteredUnique(Core::VcsManager::repositories(vcs));
        for (const FilePath &repo : repositories) {
            if (Utils::anyOf(projectDirectories, [repo](const FilePath &projectDir) {
                    return repo == projectDir || repo.isChildOf(projectDir);
                })) {
                repos[vcs->displayName()].insert(repo.toUserOutput());
            }
        }
    }

    return repos;
}

// handleSessionLoadRequest method removed - using direct session loading instead

static bool triggerCommand(const QString toolName, const Utils::Id commandId)
{
    auto error = [toolName](const QString &msg) {
        qDebug() << "Cannot run tool " << toolName << ": " << msg;
        return false;
    };
    Core::Command *command = Core::ActionManager::command(commandId);
    if (!command)
        return error("Cannot find command with id" + commandId.toString());
    QAction *action = command->action();
    if (!action)
        return error("Command with id" + commandId.toString() + "has no action assigned");
    if (!action->isEnabled())
        return error("Action for Command with id" + commandId.toString() + "is not enabled");
    action->trigger();
    return true;
}

static void initializeToolsForCommands(Mcp::Server &server)
{
    auto addToolForCommand = [&server](const QString &name, const Utils::Id commandId) {
        Core::Command *command = Core::ActionManager::command(commandId);
        if (!command)
            return;
        QAction *action = command->action();
        QTC_ASSERT(action, return);

        const QString title = action->text();
        const QString description = command->description();

        using namespace Mcp::Schema;

        server.addTool(
            Tool{}
                .name(name)
                .title(title)
                .description(description)
                .outputSchema(
                    Tool::OutputSchema{}
                        .addProperty("success", QJsonObject{{"type", "boolean"}})
                        .addRequired("success")),
            [commandId, name](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
                const bool ok = triggerCommand(name, commandId);
                qDebug() << "Tool" << name << "execution result:" << (ok ? "success" : "failure");
                return CallToolResult{}.isError(!ok).structuredContent(QJsonObject{{"success", ok}});
            });
    };

    addToolForCommand("clean_project", ProjectExplorer::Constants::CLEAN);
}

static Utils::Result<Debugger::Internal::ThreadsHandler *> getThreadsHandler()
{
    using namespace Debugger::Internal;
    const QPointer<DebuggerEngine> engine = EngineManager::currentEngine();
    if (!engine)
        return Utils::ResultError("No active debug session");
    if (engine->state() != Debugger::InferiorStopOk)
        return Utils::ResultError("Debugger is not paused (current state: "
                                  + DebuggerEngine::stateName(engine->state()) + ")");
    return engine->threadsHandler();
}

Utils::Result<QJsonArray> McpCommands::getThreads()
{
    using namespace Debugger::Internal;
    const Utils::Result<ThreadsHandler *> handler = getThreadsHandler();
    if (!handler)
        return Utils::ResultError(handler.error());

    const Thread current = (*handler)->currentThread();
    QJsonArray result;
    (*handler)->forItemsAtLevel<1>([&](ThreadItem *item) {
        const ThreadData &d = item->threadData;
        QJsonObject obj;
        obj["id"] = d.id;
        obj["current"] = (current && current->id() == d.id);
        if (!d.name.isEmpty())
            obj["name"] = d.name;
        if (!d.state.isEmpty())
            obj["state"] = d.state;
        if (!d.targetId.isEmpty())
            obj["targetId"] = d.targetId;
        if (!d.details.isEmpty())
            obj["details"] = d.details;
        if (!d.function.isEmpty())
            obj["function"] = d.function;
        if (!d.fileName.isEmpty())
            obj["file"] = d.fileName;
        if (d.lineNumber >= 0)
            obj["line"] = d.lineNumber;
        if (d.address != 0)
            obj["address"] = QString("0x%1").arg(d.address, 0, 16);
        result.append(obj);
    });
    return result;
}

Utils::Result<bool> McpCommands::selectThread(const QString &id)
{
    using namespace Debugger::Internal;
    const Utils::Result<ThreadsHandler *> handler = getThreadsHandler();
    if (!handler)
        return Utils::ResultError(handler.error());

    const Thread thread = (*handler)->threadForId(id);
    if (!thread)
        return Utils::ResultError("No thread with id: " + id);

    (*handler)->setCurrentThread(thread);
    EngineManager::currentEngine()->selectThread(thread);
    return true;
}

Utils::Result<QJsonArray> McpCommands::getCallStack()
{
    using namespace Debugger::Internal;
    const QPointer<DebuggerEngine> engine = EngineManager::currentEngine();
    if (!engine)
        return Utils::ResultError("No active debug session");

    if (engine->state() != Debugger::InferiorStopOk)
        return Utils::ResultError("Debugger is not paused (current state: "
                                  + DebuggerEngine::stateName(engine->state()) + ")");

    const StackHandler *handler = engine->stackHandler();
    if (!handler || !handler->isContentsValid())
        return Utils::ResultError("Call stack is not available");

    QJsonArray frames;
    const int count = handler->stackSize();
    const int currentIndex = handler->currentIndex();
    for (int i = 0; i < count; ++i) {
        const StackFrame frame = handler->frameAt(i);
        QJsonObject obj;
        obj["level"] = i;
        obj["current"] = (i == currentIndex);
        if (!frame.function.isEmpty())
            obj["function"] = frame.function;
        if (!frame.file.isEmpty())
            obj["file"] = frame.file.toUserOutput();
        if (frame.line >= 0)
            obj["line"] = frame.line;
        if (frame.address != 0)
            obj["address"] = QString("0x%1").arg(frame.address, 0, 16);
        if (!frame.module.isEmpty())
            obj["module"] = frame.module;
        frames.append(obj);
    }
    return frames;
}

static QString breakpointTypeToString(Debugger::Internal::BreakpointType type)
{
    using namespace Debugger::Internal;
    switch (type) {
    case BreakpointByFileAndLine:      return "fileAndLine";
    case BreakpointByFunction:         return "function";
    case BreakpointByAddress:          return "address";
    case BreakpointAtThrow:            return "throw";
    case BreakpointAtCatch:            return "catch";
    case BreakpointAtMain:             return "main";
    case BreakpointAtFork:             return "fork";
    case BreakpointAtExec:             return "exec";
    case BreakpointAtSysCall:          return "syscall";
    case WatchpointAtAddress:          return "watchAddress";
    case WatchpointAtExpression:       return "watchExpression";
    case BreakpointOnQmlSignalEmit:    return "qmlSignal";
    case BreakpointAtJavaScriptThrow:  return "jsThrow";
    default:                           return "unknown";
    }
}

bool McpCommands::deleteBreakpoint(int id)
{
    using namespace Debugger::Internal;
    const GlobalBreakpoints bps = BreakpointManager::globalBreakpoints();
    for (const GlobalBreakpoint &gbp : bps) {
        if (gbp && gbp->modelId() == id) {
            gbp->deleteBreakpoint();
            return true;
        }
    }
    return false;
}

QJsonArray McpCommands::getBreakpoints()
{
    using namespace Debugger::Internal;
    QJsonArray result;
    const GlobalBreakpoints bps = BreakpointManager::globalBreakpoints();
    for (const GlobalBreakpoint &gbp : bps) {
        if (!gbp)
            continue;
        const BreakpointParameters &p = gbp->requestedParameters();
        QJsonObject obj;
        obj["id"] = gbp->modelId();
        obj["type"] = breakpointTypeToString(p.type);
        obj["enabled"] = p.enabled;
        if (!p.fileName.isEmpty())
            obj["file"] = p.fileName.toUserOutput();
        if (p.textPosition.line > 0)
            obj["line"] = p.textPosition.line;
        if (!p.functionName.isEmpty())
            obj["function"] = p.functionName;
        if (p.address != 0)
            obj["address"] = QString("0x%1").arg(p.address, 0, 16);
        if (!p.condition.isEmpty())
            obj["condition"] = p.condition;
        if (p.ignoreCount != 0)
            obj["ignoreCount"] = p.ignoreCount;
        if (p.oneShot)
            obj["oneShot"] = true;
        if (!p.message.isEmpty())
            obj["message"] = p.message;
        result.append(obj);
    }
    return result;
}

QJsonObject McpCommands::addBreakpoint(
    const QString &type,
    const QString &file,
    int line,
    const QString &functionName,
    quint64 address,
    const QString &condition,
    int ignoreCount,
    bool enabled,
    bool oneShot)
{
    using namespace Debugger::Internal;

    BreakpointType bpType = BreakpointByFileAndLine;
    if (type == "function")
        bpType = BreakpointByFunction;
    else if (type == "address")
        bpType = BreakpointByAddress;
    else if (type == "throw")
        bpType = BreakpointAtThrow;
    else if (type == "catch")
        bpType = BreakpointAtCatch;
    else if (type == "main")
        bpType = BreakpointAtMain;
    else if (type == "watchAddress")
        bpType = WatchpointAtAddress;
    else if (type == "watchExpression")
        bpType = WatchpointAtExpression;

    BreakpointParameters params(bpType);
    params.enabled = enabled;
    params.oneShot = oneShot;
    if (!file.isEmpty())
        params.fileName = FilePath::fromUserInput(file);
    if (line > 0)
        params.textPosition.line = line;
    if (!functionName.isEmpty())
        params.functionName = functionName;
    if (address != 0)
        params.address = address;
    if (!condition.isEmpty())
        params.condition = condition;
    if (ignoreCount > 0)
        params.ignoreCount = ignoreCount;

    const GlobalBreakpoint gbp = BreakpointManager::createBreakpoint(params);
    if (!gbp)
        return QJsonObject{{"success", false}, {"error", "Failed to create breakpoint"}};

    return QJsonObject{{"success", true}, {"id", gbp->modelId()}};
}

void McpCommands::registerCommands(Mcp::Server &server)
{
    using namespace Mcp::Schema;

    initializeToolsForCommands(server);

    static McpCommands commands;

    using SimplifiedCallback = std::function<QJsonObject(const QJsonObject &)>;

    static const auto wrap = [](const SimplifiedCallback &cb) {
        return [cb](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
            return CallToolResult{}.structuredContent(cb(params.argumentsAsObject())).isError(false);
        };
    };

    using Callback = std::function<void(const QJsonObject &response)>;
    using SimplifiedAsyncCallback = std::function<void(const QJsonObject &, const Callback &)>;
    static const auto wrapAsync =
        [](SimplifiedAsyncCallback asyncFunc) -> Mcp::Server::ToolInterfaceCallback {
        return [asyncFunc](
                   const Schema::CallToolRequestParams &params,
                   const ToolInterface &toolInterface) -> Utils::Result<> {
            asyncFunc(params.argumentsAsObject(), [toolInterface](QJsonObject result) {
                toolInterface.finish(CallToolResult{}.isError(false).structuredContent(result));
            });
            return ResultOk;
        };
    };

    server.addTool(
        Schema::Tool()
            .name("build")
            .title("Build project")
            .description(
                "Builds the chosen project, or the currently active project if no name is provided")
            .execution(ToolExecution().taskSupport(ToolExecution::TaskSupport::optional))
            .inputSchema(
                Tool::InputSchema().addProperty(
                    "projectName",
                    QJsonObject{{"description", "Name of the project to build"}, {"type", "string"}}))
            .outputSchema(IssuesManager::issuesSchema())
            .annotations(
                Schema::ToolAnnotations()
                    .destructiveHint(false)
                    .idempotentHint(true)
                    .openWorldHint(false)
                    .readOnlyHint(false)),
        [](const Schema::CallToolRequestParams &params,
           const ToolInterface &toolInterface) -> Utils::Result<> {
            const QString projectName = params.arguments()->value("projectName").toString();

            QList<Project *> projects{ProjectManager::startupProject()};
            if (!projectName.isEmpty())
                projects = projectsForName(projectName);

            if (projects.isEmpty()) {
                qCDebug(mcpCommands) << "No project found, cannot build";
                return ResultError("No project named '" + projectName + "' found");
            }

            BuildManager::buildProjects(projects, ConfigSelection::Active);

            using namespace std::chrono_literals;

            toolInterface.startTask(
                1s,
                [](Schema::Task task) -> Schema::Task {
                    auto progress = BuildManager::currentProgress();
                    if (!progress) {
                        task.status(Schema::TaskStatus::completed);
                        task.statusMessage("Build finished");

                        letTaskDieIn(task, 1min);
                        return task;
                    }
                    task.statusMessage(
                        QString("%1 (%2%)").arg(progress->second).arg(progress->first));
                    return task;
                },
                []() -> Utils::Result<Schema::CallToolResult> {
                    auto issues = commands.listIssues();
                    return CallToolResult{}.structuredContent(issues).isError(false);
                },
                []() { BuildManager::cancel(); },
                Mcp::progressToken(params));

            return ResultOk;
        });

    server.addTool(
        Tool{}
            .name("get_build_status")
            .title("Get current build status")
            .description("Get current build progress and status")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("status", QJsonObject{{"type", "string"}})
                    .addRequired("status")),
        wrap([](const QJsonObject &) {
            return QJsonObject{{"status", commands.getBuildStatus()}};
        }));

    server.addTool(
        Tool{}
            .name("open_file")
            .title("Open a file in Qt Creator")
            .description("Open a file in Qt Creator")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "path",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the file to open"}})
                    .addRequired("path"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &p) {
            const QString path = p.value("path").toString();
            bool ok = commands.openFile(path);
            return QJsonObject{{"success", ok}};
        }));

    server.addTool(
        Tool{}
            .name("file_plain_text")
            .title("file plain text")
            .description("Returns the content of the file as plain text")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "path",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the file"}})
                    .addRequired("path"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("text", QJsonObject{{"type", "string"}})
                    .addRequired("text")),
        wrap([](const QJsonObject &p) {
            const QString path = p.value("path").toString();
            const QString text = commands.getFilePlainText(path);
            return QJsonObject{{"text", text}};
        }));

    server.addTool(
        Tool{}
            .name("set_file_plain_text")
            .title("set file plain text")
            .description(
                "overrided the content of the file with the provided plain text. If the "
                "file is currently open it is not saved!")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "path",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the file"}})
                    .addProperty(
                        "plainText",
                        QJsonObject{
                            {"type", "string"}, {"description", "text to write into the file"}})
                    .addRequired("path"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &p) {
            const QString path = p.value("path").toString();
            const QString text = p.value("plainText").toString();
            bool ok = commands.setFilePlainText(path, text);
            return QJsonObject{{"success", ok}};
        }));

    server.addTool(
        Tool{}
            .name("save_file")
            .title("Save a file in Qt Creator")
            .description("Save a file in Qt Creator")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "path",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the file to save"}})
                    .addRequired("path"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &p) {
            const QString path = p.value("path").toString();
            bool ok = commands.saveFile(path);
            return QJsonObject{{"success", ok}};
        }));

    server.addTool(
        Tool{}
            .name("close_file")
            .title("Close a file in Qt Creator")
            .description("Close a file in Qt Creator")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "path",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the file to close"}})
                    .addRequired("path"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &p) {
            const QString path = p.value("path").toString();
            bool ok = commands.closeFile(path);
            return QJsonObject{{"success", ok}};
        }));

    server.addTool(
        Tool{}
            .name("find_files_in_projects")
            .title("Find files in project")
            .description("Find all files matching the pattern in a given project")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "projectName",
                        QJsonObject{
                            {"type", "string"},
                            {"description",
                             "Name of the project to limit the search to (optional)"}})
                    .addProperty(
                        "pattern",
                        QJsonObject{
                            {"type", "string"},
                            {"description",
                             "Pattern for finding the file, either a glob pattern or a regex"}})
                    .addProperty(
                        "regex",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the pattern is a regex (default is false)"}})
                    .addRequired("pattern"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty(
                        "files",
                        QJsonObject{
                            {"type", "array"},
                            {"description", "List of file paths matching the pattern"},
                            {"items", QJsonObject{{"type", "string"}}}})
                    .addRequired("files")),
        [](const Schema::CallToolRequestParams &params) -> Utils::Result<Schema::CallToolResult> {
            const QJsonObject &p = params.argumentsAsObject();
            const QString projectName = p.value("projectName").toString();
            const QString pattern = p.value("pattern").toString();
            const bool isRegex = p.value("regex").toBool();

            QRegularExpression re(
                isRegex ? pattern : QRegularExpression::wildcardToRegularExpression(pattern));
            if (!re.isValid()) {
                qCDebug(mcpCommands) << "Invalid regex pattern:" << pattern;
                return CallToolResult{}.isError(true).structuredContent(
                    QJsonObject{{"error", "Invalid regex pattern"}});
            }

            const QList<Project *> projects = projectName.isEmpty() ? ProjectManager::projects()
                                                                    : projectsForName(projectName);

            const QStringList files = commands.findFiles(projects, re);
            return CallToolResult{}
                .structuredContent(QJsonObject{{"files", QJsonArray::fromStringList(files)}})
                .isError(false);
        });

    server.addTool(
        Tool{}
            .name("search_in_file")
            .title("Search for pattern in a single file")
            .description(
                "Search for a text pattern in a single file and return all matches with "
                "line, column, and matched text")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "path",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the file to search"}})
                    .addProperty(
                        "pattern",
                        QJsonObject{{"type", "string"}, {"description", "Text pattern to search for"}})
                    .addProperty(
                        "regex",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the pattern is a regular expression"}})
                    .addProperty(
                        "caseSensitive",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the search should be case sensitive"}})
                    .addRequired("path")
                    .addRequired("pattern"))
            .outputSchema(McpCommands::searchResultsSchema()),
        wrapAsync([](const QJsonObject &p, const Callback &callback) {
            const QString path = p.value("path").toString();
            const QString pattern = p.value("pattern").toString();
            const bool isRegex = p.value("regex").toBool(false);
            const bool caseSensitive = p.value("caseSensitive").toBool(false);
            commands.searchInFile(path, pattern, isRegex, caseSensitive, callback);
        }));

    server.addTool(
        Tool{}
            .name("search_in_files")
            .title("Search for pattern in project files")
            .description(
                "Search for a text pattern in files matching a file pattern within a "
                "project (or all projects) and return all matches")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "filePattern",
                        QJsonObject{
                            {"type", "string"},
                            {"description",
                             "File pattern to filter which files to search (e.g., '*.cpp', "
                             "'*.h')"}})
                    .addProperty(
                        "projectName",
                        QJsonObject{
                            {"type", "string"},
                            {"description",
                             "Optional: name of the project to search in (searches all projects if "
                             "not specified)"}})
                    .addProperty(
                        "pattern",
                        QJsonObject{{"type", "string"}, {"description", "Text pattern to search for"}})
                    .addProperty(
                        "regex",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the pattern is a regular expression"}})
                    .addProperty(
                        "caseSensitive",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the search should be case sensitive"}})
                    .addRequired("filePattern")
                    .addRequired("pattern"))
            .outputSchema(McpCommands::searchResultsSchema()),
        wrapAsync([](const QJsonObject &p, const Callback &callback) {
            const QString filePattern = p.value("filePattern").toString();
            const QString pattern = p.value("pattern").toString();
            const bool isRegex = p.value("regex").toBool(false);
            const bool caseSensitive = p.value("caseSensitive").toBool(false);
            const std::optional<QString> projectName = p.contains("projectName")
                                                           ? std::optional<QString>(
                                                                 p.value("projectName").toString())
                                                           : std::nullopt;
            commands
                .searchInFiles(filePattern, projectName, pattern, isRegex, caseSensitive, callback);
        }));

    server.addTool(
        Tool{}
            .name("search_in_directory")
            .title("Search for pattern in a directory")
            .description(
                "Search for a text pattern recursively in all files within a directory "
                "and return all matches")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "directory",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the directory to search in"}})
                    .addProperty(
                        "pattern",
                        QJsonObject{{"type", "string"}, {"description", "Text pattern to search for"}})
                    .addProperty(
                        "regex",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the pattern is a regular expression"}})
                    .addProperty(
                        "caseSensitive",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the search should be case sensitive"}})
                    .addRequired("directory")
                    .addRequired("pattern"))
            .outputSchema(McpCommands::searchResultsSchema()),
        wrapAsync([](const QJsonObject &p, const Callback &callback) {
            const QString directory = p.value("directory").toString();
            const QString pattern = p.value("pattern").toString();
            const bool isRegex = p.value("regex").toBool(false);
            const bool caseSensitive = p.value("caseSensitive").toBool(false);
            commands.searchInDirectory(directory, pattern, isRegex, caseSensitive, callback);
        }));

    server.addTool(
        Tool{}
            .name("replace_in_file")
            .title("Replace pattern in a single file")
            .description(
                "Replace all matches of a text pattern in a single file with replacement text")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "path",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the file to modify"}})
                    .addProperty(
                        "pattern",
                        QJsonObject{{"type", "string"}, {"description", "Text pattern to search for"}})
                    .addProperty(
                        "replacement",
                        QJsonObject{{"type", "string"}, {"description", "Replacement text"}})
                    .addProperty(
                        "regex",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the pattern is a regular expression"}})
                    .addProperty(
                        "caseSensitive",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the search should be case sensitive"}})
                    .addRequired("path")
                    .addRequired("pattern")
                    .addRequired("replacement"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("ok", QJsonObject{{"type", "boolean"}})
                    .addRequired("ok")),
        wrapAsync([](const QJsonObject &p, const Callback &callback) {
            const QString path = p.value("path").toString();
            const QString pattern = p.value("pattern").toString();
            const QString replacement = p.value("replacement").toString();
            const bool isRegex = p.value("regex").toBool(false);
            const bool caseSensitive = p.value("caseSensitive").toBool(false);
            commands.replaceInFile(path, pattern, replacement, isRegex, caseSensitive, callback);
        }));

    server.addTool(
        Tool{}
            .name("replace_in_files")
            .title("Replace pattern in project files")
            .description(
                "Replace all matches of a text pattern in files matching a file pattern "
                "within a project (or all projects) with replacement text")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "filePattern",
                        QJsonObject{
                            {"type", "string"},
                            {"description",
                             "File pattern to filter which files to modify (e.g., '*.cpp', "
                             "'*.h')"}})
                    .addProperty(
                        "projectName",
                        QJsonObject{
                            {"type", "string"},
                            {"description",
                             "Optional: name of the project to search in (searches all projects if "
                             "not specified)"}})
                    .addProperty(
                        "pattern",
                        QJsonObject{{"type", "string"}, {"description", "Text pattern to search for"}})
                    .addProperty(
                        "replacement",
                        QJsonObject{{"type", "string"}, {"description", "Replacement text"}})
                    .addProperty(
                        "regex",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the pattern is a regular expression"}})
                    .addProperty(
                        "caseSensitive",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the search should be case sensitive"}})
                    .addRequired("filePattern")
                    .addRequired("pattern")
                    .addRequired("replacement"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("ok", QJsonObject{{"type", "boolean"}})
                    .addRequired("ok")),
        wrapAsync([](const QJsonObject &p, const Callback &callback) {
            const QString filePattern = p.value("filePattern").toString();
            const QString pattern = p.value("pattern").toString();
            const QString replacement = p.value("replacement").toString();
            const bool isRegex = p.value("regex").toBool(false);
            const bool caseSensitive = p.value("caseSensitive").toBool(false);
            const std::optional<QString> projectName = p.contains("projectName")
                                                           ? std::optional<QString>(
                                                                 p.value("projectName").toString())
                                                           : std::nullopt;
            commands.replaceInFiles(
                filePattern, projectName, pattern, replacement, isRegex, caseSensitive, callback);
        }));

    server.addTool(
        Tool{}
            .name("replace_in_directory")
            .title("Replace pattern in a directory")
            .description(
                "Replace all matches of a text pattern recursively in all files within "
                "a directory with replacement text")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "directory",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the directory to search in"}})
                    .addProperty(
                        "pattern",
                        QJsonObject{{"type", "string"}, {"description", "Text pattern to search for"}})
                    .addProperty(
                        "replacement",
                        QJsonObject{{"type", "string"}, {"description", "Replacement text"}})
                    .addProperty(
                        "regex",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the pattern is a regular expression"}})
                    .addProperty(
                        "caseSensitive",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the search should be case sensitive"}})
                    .addRequired("directory")
                    .addRequired("pattern")
                    .addRequired("replacement"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("ok", QJsonObject{{"type", "boolean"}})
                    .addRequired("ok")),
        wrapAsync([](const QJsonObject &p, const Callback &callback) {
            const QString directory = p.value("directory").toString();
            const QString pattern = p.value("pattern").toString();
            const QString replacement = p.value("replacement").toString();
            const bool isRegex = p.value("regex").toBool(false);
            const bool caseSensitive = p.value("caseSensitive").toBool(false);
            commands.replaceInDirectory(
                directory, pattern, replacement, isRegex, caseSensitive, callback);
        }));

    server.addTool(
        Tool{}
            .name("list_projects")
            .title("List all available projects")
            .description("List all available projects")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty(
                        "projects",
                        QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}})
                    .addRequired("projects")),
        wrap([](const QJsonObject &) {
            const QStringList projects = commands.listProjects();
            QJsonArray arr;
            for (const QString &pr : projects)
                arr.append(pr);
            return QJsonObject{{"projects", arr}};
        }));

    server.addTool(
        Tool{}
            .name("list_build_configs")
            .title("List available build configurations")
            .description("List available build configurations")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty(
                        "buildConfigs",
                        QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}})
                    .addRequired("buildConfigs")),
        wrap([](const QJsonObject &) {
            const QStringList configs = commands.listBuildConfigs();
            QJsonArray arr;
            for (const QString &c : configs)
                arr.append(c);
            return QJsonObject{{"buildConfigs", arr}};
        }));

    server.addTool(
        Tool{}
            .name("switch_build_config")
            .title("Switch to a specific build configuration")
            .description("Switch to a specific build configuration")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "name",
                        QJsonObject{
                            {"type", "string"},
                            {"description", "Name of the build configuration to switch to"}})
                    .addRequired("name"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &p) {
            const QString name = p.value("name").toString();
            bool ok = commands.switchToBuildConfig(name);
            return QJsonObject{{"success", ok}};
        }));

    server.addTool(
        Tool{}
            .name("list_open_files")
            .title("List currently open files")
            .description("List currently open files")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty(
                        "openFiles",
                        QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}})
                    .addRequired("openFiles")),
        wrap([](const QJsonObject &) {
            const QStringList files = commands.listOpenFiles();
            QJsonArray arr;
            for (const QString &f : files)
                arr.append(f);
            return QJsonObject{{"openFiles", arr}};
        }));

    server.addTool(
        Tool{}
            .name("list_visible_files")
            .title("List currently visible files")
            .description("List all files that are currently visible to the user in an editor.")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty(
                        "visibleFiles",
                        QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}})
                    .addRequired("visibleFiles")),
        wrap([](const QJsonObject &) {
            const QStringList files = commands.listVisibleFiles();
            QJsonArray arr;
            for (const QString &f : files)
                arr.append(f);
            return QJsonObject{{"visibleFiles", arr}};
        }));

    server.addTool(
        Tool{}
            .name("list_sessions")
            .title("List available sessions")
            .description("List available sessions")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty(
                        "sessions",
                        QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}})
                    .addRequired("sessions")),
        wrap([](const QJsonObject &) {
            const QStringList sessions = commands.listSessions();
            QJsonArray arr;
            for (const QString &s : sessions)
                arr.append(s);
            return QJsonObject{{"sessions", arr}};
        }));

    server.addTool(
        Tool{}
            .name("load_session")
            .title("Load a specific session")
            .description("Load a specific session")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "sessionName",
                        QJsonObject{
                            {"type", "string"}, {"description", "Name of the session to load"}})
                    .addRequired("sessionName"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &p) {
            const QString name = p.value("sessionName").toString();
            bool ok = commands.loadSession(name);
            return QJsonObject{{"success", ok}};
        }));

    server.addTool(
        Tool{}
            .name("list_issues")
            .title("List current issues (warnings and errors)")
            .description("List current issues (warnings and errors)")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(IssuesManager::issuesSchema()),
        wrap([](const QJsonObject &) { return commands.listIssues(); }));

    server.addTool(
        Tool{}
            .name("list_file_issues")
            .title("List current issues for file (warnings and errors)")
            .description("List current issues for file (warnings and errors)")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "path",
                        QJsonObject{
                            {"type", "string"},
                            {"format", "uri"},
                            {"description", "Absolute path of the file to open"}})
                    .addRequired("path"))
            .outputSchema(IssuesManager::issuesSchema()),
        wrap([](const QJsonObject &p) {
            const QString path = p.value("path").toString();
            return commands.listIssues(path);
        }));

    server.addTool(
        Tool{}
            .name("quit")
            .title("Quit Qt Creator")
            .description("Quit Qt Creator")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &) {
            bool ok = commands.quit();
            return QJsonObject{{"success", ok}};
        }));

    server.addTool(
        Tool{}
            .name("get_current_project")
            .title("Get the currently active project")
            .description("Get the currently active project")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("project", QJsonObject{{"type", "string"}})
                    .addRequired("project")),
        wrap([](const QJsonObject &) {
            QString proj = commands.getCurrentProject();
            return QJsonObject{{"project", proj}};
        }));

    server.addTool(
        Tool{}
            .name("get_current_build_config")
            .title("Get the currently active build configuration")
            .description("Get the currently active build configuration")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("buildConfig", QJsonObject{{"type", "string"}})
                    .addRequired("buildConfig")),
        wrap([](const QJsonObject &) {
            QString cfg = commands.getCurrentBuildConfig();
            return QJsonObject{{"buildConfig", cfg}};
        }));

    server.addTool(
        Tool{}
            .name("get_current_session")
            .title("Get the currently active session")
            .description("Get the currently active session")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("session", QJsonObject{{"type", "string"}})
                    .addRequired("session")),
        wrap([](const QJsonObject &) {
            QString sess = commands.getCurrentSession();
            return QJsonObject{{"session", sess}};
        }));

    server.addTool(
        Tool{}
            .name("save_session")
            .title("Save the current session")
            .description("Save the current session")
            .annotations(ToolAnnotations{}.readOnlyHint(false))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &) {
            bool ok = commands.saveSession();
            return QJsonObject{{"success", ok}};
        }));

    server.addTool(
        Tool()
            .name("known_repositories_in_projects")
            .title("Get known version control repositories in all projects")
            .description(
                "List all known version control repositories (e.g., Git, Subversion) that are "
                "within the directories of all open projects")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .inputSchema(
                Tool::InputSchema()
                    .addProperty(
                        "name",
                        QJsonObject{
                            {"type", "string"},
                            {"description", "Name of the project to query repositories for"}})
                    .addRequired("name"))
            .outputSchema(
                Tool::OutputSchema()
                    .addProperty(
                        "repositories",
                        QJsonObject{
                            {"type", "object"},
                            {"description",
                             "Map of version control system names to lists of repository paths"}})
                    .addRequired("repositories")),
        wrap([](const QJsonObject &p) {
            const QString projectName = p.value("name").toString();
            const QMap<QString, QSet<QString>> repos = commands.knownRepositoriesInProject(
                projectName);

            // Convert QMap<QString, QStringList> to QJsonObject
            QJsonObject reposJson;
            for (auto it = repos.constBegin(); it != repos.constEnd(); ++it) {
                reposJson[it.key()] = QJsonArray::fromStringList(Utils::toList(it.value()));
            }

            return QJsonObject{{"repositories", reposJson}};
        }));

    server.addTool(
        Tool()
            .name("project_dependencies")
            .title("List project dependencies for all projects")
            .description("List project dependencies for all projects")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "name",
                        QJsonObject{
                            {"type", "string"},
                            {"description", "Name of the project to query dependencies for"}})
                    .addRequired("name"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty(
                        "dependencies",
                        QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}})
                    .addRequired("dependencies")),
        wrap([](const QJsonObject &p) {
            const Utils::Result<QStringList> projects = commands.projectDependencies(
                p["name"].toString());
            QJsonArray arr;
            for (const QString &pr : projects.value_or(QStringList{})) // TODO: proper error handling
                arr.append(pr);
            return QJsonObject{{"dependencies", arr}};
        }));

    server.addTool(
        Tool{}
            .name("get_breakpoints")
            .title("Get current breakpoints")
            .description("Returns all breakpoints currently set in Qt Creator's debugger")
            .annotations(ToolAnnotations{}.readOnlyHint(true).destructiveHint(false))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty(
                        "breakpoints",
                        QJsonObject{
                            {"type", "array"},
                            {"items", QJsonObject{{"type", "object"}}},
                            {"description", "List of breakpoints"}})
                    .addRequired("breakpoints")),
        wrap([](const QJsonObject &) {
            return QJsonObject{{"breakpoints", commands.getBreakpoints()}};
        }));

    server.addTool(
        Tool{}
            .name("get_threads")
            .title("Get current threads")
            .description(
                "Returns all threads of the current debug session. "
                "Returns an error if no debug session is active or the debugger is not paused.")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema([] {
                const QJsonObject threadProperties{
                    {"id",       QJsonObject{{"type", "string"},  {"description", "Thread ID"}}},
                    {"current",  QJsonObject{{"type", "boolean"}, {"description", "True for the currently selected thread"}}},
                    {"name",     QJsonObject{{"type", "string"},  {"description", "Thread name"}}},
                    {"state",    QJsonObject{{"type", "string"},  {"description", "Thread state, e.g. \"stopped\""}}},
                    {"targetId", QJsonObject{{"type", "string"},  {"description", "Target-level thread identifier"}}},
                    {"details",  QJsonObject{{"type", "string"},  {"description", "Additional details from the debugger"}}},
                    {"function", QJsonObject{{"type", "string"},  {"description", "Current function name"}}},
                    {"file",     QJsonObject{{"type", "string"},  {"description", "Current source file"}}},
                    {"line",     QJsonObject{{"type", "integer"}, {"description", "Current line number"}}},
                    {"address",  QJsonObject{{"type", "string"},  {"description", "Current instruction address"}}},
                };
                const QJsonObject threadItem{
                    {"type", "object"},
                    {"required", QJsonArray{"id", "current"}},
                    {"properties", threadProperties},
                };
                return Tool::OutputSchema{}
                    .addProperty(
                        "threads",
                        QJsonObject{
                            {"type", "array"},
                            {"description", "List of threads"},
                            {"items", threadItem}})
                    .addRequired("threads");
            }()),
        [](const Schema::CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            const Utils::Result<QJsonArray> threads = commands.getThreads();
            if (!threads)
                return CallToolResult{}.isError(true).addContent(Schema::TextContent{}.text(threads.error()));
            return CallToolResult{}.isError(false).structuredContent(QJsonObject{{"threads", *threads}});
        });

    server.addTool(
        Tool{}
            .name("select_thread")
            .title("Select a thread")
            .description(
                "Switches the current thread in the active debug session. "
                "Returns an error if no debug session is active or the debugger is not paused.")
            .annotations(ToolAnnotations{}.readOnlyHint(false).idempotentHint(true))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "id",
                        QJsonObject{
                            {"type", "string"},
                            {"description", "Thread ID to select (as returned by get_threads)"}})
                    .addRequired("id"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        [](const Schema::CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
            const Utils::Result<bool> ok = commands.selectThread(params.argumentsAsObject().value("id").toString());
            if (!ok)
                return CallToolResult{}.isError(true).addContent(Schema::TextContent{}.text(ok.error()));
            return CallToolResult{}.isError(false).structuredContent(QJsonObject{{"success", true}});
        });

    server.addTool(
        Tool{}
            .name("get_call_stack")
            .title("Get current call stack")
            .description(
                "Returns the call stack (stack frames) of the current debug session. "
                "Returns an error if no debug session is active or the debugger is not paused.")
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema([] {
                const QJsonObject frameProperties{
                    {"level",    QJsonObject{{"type", "integer"}, {"description", "Frame index, 0 = innermost"}}},
                    {"current",  QJsonObject{{"type", "boolean"}, {"description", "True for the currently active frame"}}},
                    {"function", QJsonObject{{"type", "string"},  {"description", "Function or method name"}}},
                    {"file",     QJsonObject{{"type", "string"},  {"description", "Absolute path to the source file"}}},
                    {"line",     QJsonObject{{"type", "integer"}, {"description", "Line number in the source file"}}},
                    {"address",  QJsonObject{{"type", "string"},  {"description", "Instruction address, e.g. \"0x1234abcd\""}}},
                    {"module",   QJsonObject{{"type", "string"},  {"description", "Module or shared library name"}}},
                };
                const QJsonObject frameItem{
                    {"type", "object"},
                    {"required", QJsonArray{"level", "current"}},
                    {"properties", frameProperties},
                };
                return Tool::OutputSchema{}
                    .addProperty(
                        "frames",
                        QJsonObject{
                            {"type", "array"},
                            {"description", "Stack frames, innermost first"},
                            {"items", frameItem}})
                    .addRequired("frames");
            }()),
        [](const Schema::CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            const Utils::Result<QJsonArray> frames = commands.getCallStack();
            if (!frames)
                return CallToolResult{}.isError(true).addContent(Schema::TextContent{}.text(frames.error()));
            return CallToolResult{}.isError(false).structuredContent(QJsonObject{{"frames", *frames}});
        });

    server.addTool(
        Tool{}
            .name("delete_breakpoint")
            .title("Delete a breakpoint")
            .description(
                "Deletes a breakpoint by its ID (as returned by get_breakpoints or add_breakpoint)")
            .annotations(ToolAnnotations().destructiveHint(true).idempotentHint(true))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "id",
                        QJsonObject{
                            {"type", "integer"}, {"description", "ID of the breakpoint to delete"}})
                    .addRequired("id"))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &p) {
            const bool ok = commands.deleteBreakpoint(p.value("id").toInt());
            return QJsonObject{{"success", ok}};
        }));

    server.addTool(
        Tool{}
            .name("add_breakpoint")
            .title("Add a breakpoint")
            .description("Adds a new breakpoint in Qt Creator's debugger.")
            .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false))
            .inputSchema(
                Tool::InputSchema{}
                    .addProperty(
                        "type",
                        QJsonObject{
                            {"type", "string"},
                            {"enum",
                             QJsonArray{
                                 "fileAndLine",
                                 "function",
                                 "address",
                                 "throw",
                                 "catch",
                                 "main",
                                 "watchAddress",
                                 "watchExpression"}},
                            {"description", "Breakpoint type. Defaults to fileAndLine."},
                            {"default", "fileAndLine"}})
                    .addProperty(
                        "file",
                        QJsonObject{
                            {"type", "string"},
                            {"description",
                             "Absolute path to the source file (for fileAndLine type)"}})
                    .addProperty(
                        "line",
                        QJsonObject{
                            {"type", "integer"},
                            {"description",
                             "Line number in the source file (for fileAndLine type)"}})
                    .addProperty(
                        "function",
                        QJsonObject{
                            {"type", "string"},
                            {"description", "Function name (for function type)"}})
                    .addProperty(
                        "address",
                        QJsonObject{
                            {"type", "integer"},
                            {"description", "Memory address (for address or watchAddress type)"}})
                    .addProperty(
                        "condition",
                        QJsonObject{
                            {"type", "string"}, {"description", "Optional condition expression"}})
                    .addProperty(
                        "ignoreCount",
                        QJsonObject{
                            {"type", "integer"},
                            {"description", "Number of hits to ignore before breaking"}})
                    .addProperty(
                        "enabled",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description", "Whether the breakpoint is enabled. Defaults to true."},
                            {"default", true}})
                    .addProperty(
                        "oneShot",
                        QJsonObject{
                            {"type", "boolean"},
                            {"description",
                             "If true, the breakpoint is removed after the first hit"},
                            {"default", false}}))
            .outputSchema(
                Tool::OutputSchema{}
                    .addProperty("success", QJsonObject{{"type", "boolean"}})
                    .addProperty(
                        "id",
                        QJsonObject{
                            {"type", "integer"}, {"description", "ID of the created breakpoint"}})
                    .addProperty("error", QJsonObject{{"type", "string"}})
                    .addRequired("success")),
        wrap([](const QJsonObject &p) {
            return commands.addBreakpoint(
                p.value("type").toString("fileAndLine"),
                p.value("file").toString(),
                p.value("line").toInt(0),
                p.value("function").toString(),
                static_cast<quint64>(p.value("address").toInteger(0)),
                p.value("condition").toString(),
                p.value("ignoreCount").toInt(0),
                p.value("enabled").toBool(true),
                p.value("oneShot").toBool(false));
        }));

    server.addTool(
        Tool()
            .name("execute_command")
            .title("executes the command")
            .description(
                "executes the command and returns the exit code as well as standart output and "
                "error")
            .inputSchema(
                Tool::InputSchema()
                    .addRequired("command")
                    .addProperty(
                        "command",
                        QJsonObject{{"type", "string"}, {"description", "Command to execute"}})
                    .addProperty(
                        "arguments",
                        QJsonObject{
                            {"type", "string"}, {"description", "Arguments passed to the command"}})
                    .addProperty(
                        "workingDir",
                        QJsonObject{
                            {"type", "string"},
                            {"description", "Directory in which the command is executed"}}))
            .outputSchema(
                Tool::OutputSchema()
                    .addRequired("exitCode")
                    .addProperty(
                        "exitCode",
                        QJsonObject{{"type", "integer"}, {"description", "Exit code of the command"}})
                    .addProperty(
                        "exitMessage",
                        QJsonObject{
                            {"type", "string"},
                            {"description",
                             "Verbose exit message of the command, useful for error reporting"}})
                    .addProperty(
                        "stdout",
                        QJsonObject{
                            {"type", "string"}, {"description", "Standard output of the command"}})
                    .addProperty(
                        "stderr",
                        QJsonObject{
                            {"type", "string"}, {"description", "Standard error of the command"}})),
        wrapAsync([](const QJsonObject &p, const Callback &callback) {
            commands.executeCommand(
                p["command"].toString(),
                p["arguments"].toString(),
                p["workingDir"].toString(),
                callback);
        }));

    // Shared output schema for run/debug tools.
    const auto runToolOutputSchema = [] {
        const Tool::OutputSchema issSchema = IssuesManager::issuesSchema();
        QJsonObject issuesField{
            {"type", "object"},
            {"description",
             "Build issues — present when the build failed; same format as list_issues"}};
        if (issSchema._properties) {
            QJsonObject props;
            for (auto it = issSchema._properties->cbegin();
                 it != issSchema._properties->cend();
                 ++it)
                props[it.key()] = it.value();
            issuesField["properties"] = props;
        }
        if (issSchema._required)
            issuesField["required"] = QJsonArray::fromStringList(*issSchema._required);

        return Tool::OutputSchema{}
            .addProperty(
                "output",
                QJsonObject{
                    {"type", "string"},
                    {"description", "Collected output from the run (present on success)"}})
            .addProperty("issues", issuesField);
    }();

    // Shared callback factory for run/debug tools.
    const auto makeRunCallback =
        [](Utils::Id runMode, const QString &finishedMessage)
        -> Server::ToolInterfaceCallback {
        return [runMode, finishedMessage](
                   const Schema::CallToolRequestParams &params,
                   const ToolInterface &toolInterface) -> Utils::Result<> {
            const Utils::Result<> canRun
                = ProjectExplorer::ProjectExplorerPlugin::canRunStartupProject(runMode);
            if (!canRun) {
                toolInterface.finish(CallToolResult{}.isError(true).addContent(
                    Schema::TextContent{}.text(canRun.error())));
                return ResultOk;
            }

            struct State
            {
                QStringList output;
                bool finished = false;
                QJsonObject failureIssues; // non-empty when build failed
                QPointer<ProjectExplorer::RunControl> rc;
            };
            auto state = std::make_shared<State>();

            using namespace std::chrono_literals;

            const Utils::Result<ToolInterface::TaskProgressNotify> task = toolInterface.startTask(
                500ms,
                [state](Schema::Task t) {
                    if (state->finished)
                        letTaskDieIn(t, 1min);
                    const bool failed = !state->failureIssues.isEmpty();
                    return t
                        .status(
                            !state->finished  ? Schema::TaskStatus::working
                            : failed          ? Schema::TaskStatus::failed
                                              : Schema::TaskStatus::completed)
                        .statusMessage(
                            state->output.isEmpty() ? std::nullopt
                                                    : std::optional{state->output.last()});
                },
                [state]() -> Utils::Result<CallToolResult> {
                    if (!state->failureIssues.isEmpty())
                        return CallToolResult{}.isError(true).structuredContent(
                            QJsonObject{{"issues", state->failureIssues}});
                    return CallToolResult{}.isError(false).structuredContent(
                        QJsonObject{{"output", state->output.join('\n')}});
                },
                [state]() {
                    if (state->rc)
                        state->rc->initiateStop();
                },
                progressToken(params));

            if (!task) {
                toolInterface.finish(CallToolResult{}.isError(true).addContent(
                    Schema::TextContent{}.text(task.error())));
                return ResultOk;
            }

            const ToolInterface::TaskProgressNotify notify = *task;

            // rcStartedConn is shared between the runControlStarted and
            // buildQueueFinished handlers so each can disconnect the other.
            auto rcStartedConn = std::make_shared<QMetaObject::Connection>();

            // Grab the RunControl we're about to start. Use a plain (non-single-shot)
            // connection so a RunControl with a different mode starting first doesn't
            // consume it prematurely; disconnect manually once we find ours.
            *rcStartedConn = QObject::connect(
                ProjectExplorer::ProjectExplorerPlugin::instance(),
                &ProjectExplorer::ProjectExplorerPlugin::runControlStarted,
                ProjectExplorer::ProjectExplorerPlugin::instance(),
                [state, notify, rcStartedConn, runMode, finishedMessage](
                    ProjectExplorer::RunControl *rc) {
                    if (rc->runMode() != runMode)
                        return;
                    QObject::disconnect(*rcStartedConn);
                    state->rc = rc;
                    QObject::connect(
                        rc,
                        &ProjectExplorer::RunControl::appendMessage,
                        rc,
                        [state, notify](const QString &msg, Utils::OutputFormat) {
                            const QString trimmed = msg.trimmed();
                            if (trimmed.isEmpty())
                                return;
                            state->output.append(trimmed);
                            if (notify)
                                notify(Schema::TaskStatus::working, trimmed, std::nullopt);
                        });
                    QObject::connect(
                        rc,
                        &ProjectExplorer::RunControl::stopped,
                        rc,
                        [state, notify, finishedMessage]() {
                            state->finished = true;
                            if (notify)
                                notify(Schema::TaskStatus::completed, finishedMessage, std::nullopt);
                        },
                        Qt::SingleShotConnection);
                });

            // If the build fails before the RunControl starts, abort the task.
            QObject::connect(
                ProjectExplorer::BuildManager::instance(),
                &ProjectExplorer::BuildManager::buildQueueFinished,
                ProjectExplorer::BuildManager::instance(),
                [state, notify, rcStartedConn](bool success) {
                    if (success || state->rc)
                        return;
                    QObject::disconnect(*rcStartedConn);
                    state->finished = true;

                    state->failureIssues = commands.listIssues();
                    const int errorCount = state->failureIssues.value("summary")
                                               .toObject()
                                               .value("errorCount")
                                               .toInt();
                    const QString statusMsg
                        = errorCount > 0
                              ? QString("Build failed with %1 error(s)").arg(errorCount)
                              : QString("Build failed");
                    if (notify)
                        notify(Schema::TaskStatus::failed, statusMsg, std::nullopt);
                },
                Qt::SingleShotConnection);

            ProjectExplorer::ProjectExplorerPlugin::runStartupProject(runMode, false);
            return ResultOk;
        };
    };

    server.addTool(
        Tool{}
            .name("run_project")
            .title("Run project")
            .description(
                "Runs the current startup project and waits for it to finish. "
                "Progress messages from the application are streamed during execution. "
                "On success, returns the full output. "
                "On build failure, returns isError=true with structured content in the same "
                "format as list_issues (issues array + summary). "
                "Returns an error if there is no startup project, no active build configuration, "
                "or the project cannot currently be run.")
            .execution(ToolExecution().taskSupport(ToolExecution::TaskSupport::optional))
            .outputSchema(runToolOutputSchema),
        makeRunCallback(ProjectExplorer::Constants::NORMAL_RUN_MODE, "Run finished"));

    server.addTool(
        Tool{}
            .name("debug")
            .title("Start debugging")
            .description(
                "Starts a debug session for the current startup project and waits for it to "
                "finish. "
                "Progress messages from the debugged application are streamed during execution. "
                "On success, returns the full output. "
                "On build failure, returns isError=true with structured content in the same "
                "format as list_issues (issues array + summary). "
                "Returns an error if there is no startup project, no active build configuration, "
                "or the project cannot currently be run in debug mode.")
            .execution(ToolExecution().taskSupport(ToolExecution::TaskSupport::optional))
            .outputSchema(runToolOutputSchema),
        makeRunCallback(ProjectExplorer::Constants::DEBUG_RUN_MODE, "Debug session finished"));
}
} // namespace Mcp::Internal
