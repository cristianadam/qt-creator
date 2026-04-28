// Copyright (C) 2025 David M. Cotter
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#pragma once

#include "issuesmanager.h"
#include <mcp/server/mcpserver.h>

#include <utils/result.h>

#include <QMap>
#include <QObject>
#include <QStringList>

namespace ProjectExplorer {
class Project;
}

namespace Mcp::Internal {

class IssuesManager;

class McpCommands : public QObject
{
    Q_OBJECT

public:
    explicit McpCommands(QObject *parent = nullptr);

    static void registerCommands();

    using ResponseCallback = std::function<void(const QJsonObject &response)>;

    // Core MCP commands
    QString stopDebug();

    QStringList listProjects();
    QStringList listBuildConfigs();
    bool switchToBuildConfig(const QString &name);
    bool quit();
    QString getVersion();
    QString getBuildStatus();

    // document management commands
    bool openFile(const QString &path);
    QString getFilePlainText(const QString &path);
    bool setFilePlainText(const QString &path, const QString &contents);
    bool saveFile(const QString &path);
    bool closeFile(const QString &path);
    QStringList findFiles(
        const QList<ProjectExplorer::Project *> &projects, const QRegularExpression &re);
    bool reformatFile(const QString &path);
    void searchInFile(
        const QString &path,
        const QString &pattern,
        bool regex,
        bool caseSensitive,
        const ResponseCallback &callback);
    void searchInFiles(
        const QString &filePattern,
        const std::optional<QString> &projectName,
        const QString &pattern,
        bool regex,
        bool caseSensitive,
        const ResponseCallback &callback);
    void searchInDirectory(
        const QString directory,
        const QString &pattern,
        bool regex,
        bool caseSensitive,
        const ResponseCallback &callback);

    static Mcp::Schema::Tool::OutputSchema searchResultsSchema();

    void replaceInFile(
        const QString &path,
        const QString &pattern,
        const QString &replacement,
        bool regex,
        bool caseSensitive,
        const ResponseCallback &callback);
    void replaceInFiles(
        const QString &filePattern,
        const std::optional<QString> &projectName,
        const QString &pattern,
        const QString &replacement,
        bool regex,
        bool caseSensitive,
        const ResponseCallback &callback);
    void replaceInDirectory(
        const QString directory,
        const QString &pattern,
        const QString &replacement,
        bool regex,
        bool caseSensitive,
        const ResponseCallback &callback);

    // Additional useful commands
    QString getCurrentProject();
    QString getCurrentBuildConfig();
    QStringList listOpenFiles();
    QStringList listVisibleFiles();
    Utils::Result<QStringList> projectDependencies(const QString &projectName);
    bool createNewFile(const QString &path, const QString &text);
    QJsonArray getRunConfigurations();
    QMap<QString, QSet<QString> > knownRepositoriesInProject(const QString &projectName);

    // Session management commands
    QStringList listSessions();
    QString getCurrentSession();
    bool loadSession(const QString &sessionName);
    bool saveSession();

    // Issue management commands
    QJsonObject listIssues();
    QJsonObject listIssues(const QString &path);

    // Debugging management helpers
    Utils::Result<QString> debuggerStepOver();
    Utils::Result<QString> debuggerStepIn();
    Utils::Result<QString> debuggerStepOut();
    Utils::Result<QString> debuggerContinue();
    Utils::Result<QString> debuggerInterrupt();
    Utils::Result<QJsonObject> debuggerGetStatus();
    void evaluateExpression(const QString &expression, std::function<void(Utils::Result<QJsonObject>)> callback);
    Utils::Result<QJsonArray> getCallStack();
    Utils::Result<QJsonArray> getThreads();
    Utils::Result<bool> selectThread(const QString &id);
    void getVariables(bool includeWatchers, std::function<void(Utils::Result<QJsonArray>)> callback);
    void getVariable(const QString &iname, std::function<void(Utils::Result<QJsonObject>)> callback);
    Utils::Result<bool> setVariable(const QString &iname, const QString &value);
    Utils::Result<QString> addWatchExpression(const QString &expression, const QString &name);
    Utils::Result<bool> removeWatchExpression(const QString &iname);
    QJsonArray getBreakpoints();
    bool deleteBreakpoint(int id);
    QJsonObject addBreakpoint(
        const QString &type,
        const QString &file,
        int line,
        const QString &functionName,
        quint64 address,
        const QString &condition,
        int ignoreCount,
        bool enabled,
        bool oneShot);

    void executeCommand(
        const QString &command,
        const QString &arguments,
        const QString &workingDirectory,
        const ResponseCallback &callback);

private:
    // Issues management
    IssuesManager m_issuesManager;
};

} // namespace Mcp::Internal
