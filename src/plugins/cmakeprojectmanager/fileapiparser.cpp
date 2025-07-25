// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "fileapiparser.h"

#include "cmakeprocess.h"
#include "cmakeprojectconstants.h"
#include "cmakeprojectmanagertr.h"

#include <coreplugin/messagemanager.h>
#include <projectexplorer/rawprojectpart.h>

#include <utils/algorithm.h>
#include <utils/filepath.h>
#include <utils/qtcassert.h>

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPromise>

using namespace ProjectExplorer;
using namespace Utils;

namespace CMakeProjectManager::Internal {

using namespace FileApiDetails;

const char CMAKE_RELATIVE_REPLY_PATH[] = ".cmake/api/v1/reply";
const char CMAKE_RELATIVE_QUERY_PATH[] = ".cmake/api/v1/query";

static Q_LOGGING_CATEGORY(cmakeFileApi, "qtc.cmake.fileApi", QtWarningMsg);

const QStringList CMAKE_QUERY_FILENAMES = {"cache-v2", "codemodel-v2", "cmakeFiles-v1"};

// --------------------------------------------------------------------
// Helper:
// --------------------------------------------------------------------

FilePath FileApiParser::cmakeReplyDirectory(const FilePath &buildDirectory)
{
    return buildDirectory.pathAppended(CMAKE_RELATIVE_REPLY_PATH);
}

static void reportFileApiSetupFailure()
{
    Core::MessageManager::writeFlashing(
        addCMakePrefix(Tr::tr("Failed to set up CMake file API support. %1 cannot "
                              "extract project information.")
                           .arg(QGuiApplication::applicationDisplayName())));
}

static std::pair<int, int> cmakeVersion(const QJsonObject &obj)
{
    const QJsonObject version = obj.value("version").toObject();
    const int major = version.value("major").toInt(-1);
    const int minor = version.value("minor").toInt(-1);
    return {major, minor};
}

static bool checkJsonObject(const QJsonObject &obj, const QString &kind, int major, int minor = -1)
{
    auto version = cmakeVersion(obj);
    if (major == -1)
        version.first = major;
    if (minor == -1)
        version.second = minor;
    return obj.value("kind").toString() == kind && version == std::make_pair(major, minor);
}

static std::pair<QString, QString> nameValue(const QJsonObject &obj)
{
    return {obj.value("name").toString(), obj.value("value").toString()};
}

static QJsonDocument readJsonFile(const FilePath &filePath)
{
    qCDebug(cmakeFileApi) << "readJsonFile:" << filePath;
    QTC_ASSERT(!filePath.isEmpty(), return {});

    const Result<QByteArray> contents = filePath.fileContents();
    if (!contents)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(*contents);

    return doc;
}

std::vector<int> indexList(const QJsonValue &v)
{
    const QJsonArray &indexList = v.toArray();
    std::vector<int> result;
    result.reserve(static_cast<size_t>(indexList.count()));

    for (const auto &v : indexList) {
        result.push_back(v.toInt(-1));
    }
    return result;
}

// Reply file:

static ReplyFileContents readReplyFile(const FilePath &filePath, QString &errorMessage)
{
    const QJsonDocument document = readJsonFile(filePath);
    static const QString msg = Tr::tr("Invalid reply file created by CMake.");

    ReplyFileContents result;
    if (document.isNull() || document.isEmpty() || !document.isObject()) {
        errorMessage = msg;
        return result;
    }

    const QJsonObject rootObject = document.object();

    {
        const QJsonObject cmakeObject = rootObject.value("cmake").toObject();
        {
            const QJsonObject paths = cmakeObject.value("paths").toObject();
            {
                result.cmakeExecutable = paths.value("cmake").toString();
                result.ctestExecutable = paths.value("ctest").toString();
                result.cmakeRoot = paths.value("root").toString();
            }
            const QJsonObject generator = cmakeObject.value("generator").toObject();
            {
                result.generator = generator.value("name").toString();
                result.isMultiConfig = generator.value("multiConfig").toBool();
            }
            const QJsonObject version = cmakeObject.value("version").toObject();
            {
                int major = version.value("major").toInt();
                int minor = version.value("minor").toInt();
                int patch = version.value("patch").toInt();
                result.cmakeVersion = QVersionNumber(major, minor, patch);
            }
        }
    }

    bool hadInvalidObject = false;
    {
        const QJsonArray objects = rootObject.value("objects").toArray();
        for (const auto &v : objects) {
            const QJsonObject object = v.toObject();
            {
                ReplyObject r;
                r.kind = object.value("kind").toString();
                r.file = object.value("jsonFile").toString();
                r.version = cmakeVersion(object);

                if (r.kind.isEmpty() || r.file.isEmpty() || r.version.first == -1
                    || r.version.second == -1)
                    hadInvalidObject = true;
                else
                    result.replies.append(r);
            }
        }
    }

    if (result.generator.isEmpty() || result.cmakeExecutable.isEmpty() || result.cmakeRoot.isEmpty()
        || result.replies.isEmpty() || hadInvalidObject)
        errorMessage = msg;

    return result;
}

// Cache file:

static CMakeConfig readCacheFile(const FilePath &cacheFile, QString &errorMessage)
{
    CMakeConfig result;

    const QJsonDocument doc = readJsonFile(cacheFile);
    const QJsonObject root = doc.object();

    if (!checkJsonObject(root, "cache", 2)) {
        errorMessage = Tr::tr("Invalid cache file generated by CMake.");
        return {};
    }

    const QJsonArray entries = root.value("entries").toArray();
    for (const auto &v : entries) {
        CMakeConfigItem item;

        const QJsonObject entry = v.toObject();
        auto nv = nameValue(entry);
        item.key = nv.first.toUtf8();
        item.value = nv.second.toUtf8();

        item.type = CMakeConfigItem::typeStringToType(entry.value("type").toString().toUtf8());

        {
            const QJsonArray properties = entry.value("properties").toArray();
            for (const auto &v : properties) {
                const QJsonObject prop = v.toObject();
                auto nv = nameValue(prop);
                if (nv.first == "ADVANCED") {
                    const auto boolValue = CMakeConfigItem::toBool(nv.second);
                    item.isAdvanced = boolValue.has_value() && *boolValue;
                } else if (nv.first == "HELPSTRING") {
                    item.documentation = nv.second.toUtf8();
                } else if (nv.first == "STRINGS") {
                    item.values = nv.second.split(';');
                }
            }
        }
        result.append(item);
    }
    return result;
}

// CMake Files:

static std::vector<CMakeFileInfo> readCMakeFilesFile(const FilePath &cmakeFilesFile, QString &errorMessage)
{
    std::vector<CMakeFileInfo> result;

    const QJsonDocument doc = readJsonFile(cmakeFilesFile);
    const QJsonObject root = doc.object();

    if (!checkJsonObject(root, "cmakeFiles", 1)) {
        errorMessage = Tr::tr( "Invalid cmakeFiles file generated by CMake.");
        return {};
    }

    const QJsonArray inputs = root.value("inputs").toArray();
    for (const auto &v : inputs) {
        CMakeFileInfo info;
        const QJsonObject input = v.toObject();
        info.path = cmakeFilesFile.withNewPath(input.value("path").toString());

        info.isCMake = input.value("isCMake").toBool();
        const QString filename = info.path.fileName();
        info.isCMakeListsDotTxt = (filename.compare(Constants::CMAKE_LISTS_TXT,
                                                    HostOsInfo::fileNameCaseSensitivity())
                                   == 0);

        info.isGenerated = input.value("isGenerated").toBool();
        info.isExternal = input.value("isExternal").toBool();

        result.emplace_back(std::move(info));
    }
    return result;
}

// Codemodel file:

std::vector<DirectoryInfo> extractDirectories(const QJsonArray &directories, QString &errorMessage)
{
    if (directories.isEmpty()) {
        errorMessage = Tr::tr(
            "Invalid codemodel file generated by CMake: No directories.");
        return {};
    }

    std::vector<DirectoryInfo> result;
    for (const auto &v : directories) {
        const QJsonObject obj = v.toObject();
        if (obj.isEmpty()) {
            errorMessage = Tr::tr(
                "Invalid codemodel file generated by CMake: Empty directory object.");
            continue;
        }
        DirectoryInfo dir;
        dir.sourcePath = obj.value("source").toString();
        dir.buildPath = obj.value("build").toString();
        dir.parent = obj.value("parentIndex").toInt(-1);
        dir.project = obj.value("projectIndex").toInt(-1);
        dir.children = indexList(obj.value("childIndexes"));
        dir.targets = indexList(obj.value("targetIndexes"));
        dir.hasInstallRule = obj.value("hasInstallRule").toBool();

        result.emplace_back(std::move(dir));
    }
    return result;
}

static std::vector<ProjectInfo> extractProjects(const QJsonArray &projects, QString &errorMessage)
{
    if (projects.isEmpty()) {
        errorMessage = Tr::tr(
            "Invalid codemodel file generated by CMake: No projects.");
        return {};
    }

    std::vector<ProjectInfo> result;
    for (const auto &v : projects) {
        const QJsonObject obj = v.toObject();
        if (obj.isEmpty()) {
            qCDebug(cmakeFileApi) << "Empty project skipped!";
            errorMessage = Tr::tr(
                "Invalid codemodel file generated by CMake: Empty project object.");
            continue;
        }
        ProjectInfo project;
        project.name = obj.value("name").toString();
        project.parent = obj.value("parentIndex").toInt(-1);
        project.children = indexList(obj.value("childIndexes"));
        project.directories = indexList(obj.value("directoryIndexes"));
        project.targets = indexList(obj.value("targetIndexes"));

        if (project.directories.empty()) {
            qCDebug(cmakeFileApi) << "Invalid project skipped!";
            errorMessage = Tr::tr(
                "Invalid codemodel file generated by CMake: Broken project data.");
            continue;
        }

        qCDebug(cmakeFileApi) << "Project read:" << project.name << project.directories;
        result.emplace_back(std::move(project));
    }
    return result;
}

static std::vector<TargetInfo> extractTargets(const QJsonArray &targets, QString &errorMessage)
{
    std::vector<TargetInfo> result;
    for (const auto &v : targets) {
        const QJsonObject obj = v.toObject();
        if (obj.isEmpty()) {
            errorMessage = Tr::tr(
                "Invalid codemodel file generated by CMake: Empty target object.");
            continue;
        }
        TargetInfo target;
        target.name = obj.value("name").toString();
        target.id = obj.value("id").toString();
        target.directory = obj.value("directoryIndex").toInt(-1);
        target.project = obj.value("projectIndex").toInt(-1);
        target.jsonFile = obj.value("jsonFile").toString();

        if (target.name.isEmpty() || target.id.isEmpty() || target.jsonFile.isEmpty()
            || target.directory == -1 || target.project == -1) {
            errorMessage = Tr::tr(
                "Invalid codemodel file generated by CMake: Broken target data.");
            continue;
        }

        result.emplace_back(std::move(target));
    }
    return result;
}

static bool validateIndexes(const ConfigurationInfo &config)
{
    const int directoryCount = static_cast<int>(config.directories.size());
    const int projectCount = static_cast<int>(config.projects.size());
    const int targetCount = static_cast<int>(config.targets.size());

    int topLevelCount = 0;
    for (const DirectoryInfo &d : config.directories) {
        if (d.parent == -1)
            ++topLevelCount;

        if (d.parent < -1 || d.parent >= directoryCount) {
            qCWarning(cmakeFileApi)
                << "Directory" << d.sourcePath << ": parent index" << d.parent << "is broken.";
            return false;
        }
        if (d.project < 0 || d.project >= projectCount) {
            qCWarning(cmakeFileApi)
                << "Directory" << d.sourcePath << ": project index" << d.project << "is broken.";
            return false;
        }
        if (contains(d.children, [directoryCount](int c) { return c < 0 || c >= directoryCount; })) {
            qCWarning(cmakeFileApi)
                << "Directory" << d.sourcePath << ": A child index" << d.children << "is broken.";
            return false;
        }
        if (contains(d.targets, [targetCount](int t) { return t < 0 || t >= targetCount; })) {
            qCWarning(cmakeFileApi)
                << "Directory" << d.sourcePath << ": A target index" << d.targets << "is broken.";
            return false;
        }
    }
    if (topLevelCount != 1) {
        qCWarning(cmakeFileApi) << "Directories: Invalid number of top level directories, "
                                << topLevelCount << " (expected: 1).";
        return false;
    }

    topLevelCount = 0;
    for (const ProjectInfo &p : config.projects) {
        if (p.parent == -1)
            ++topLevelCount;

        if (p.parent < -1 || p.parent >= projectCount) {
            qCWarning(cmakeFileApi)
                << "Project" << p.name << ": parent index" << p.parent << "is broken.";
            return false;
        }
        if (contains(p.children, [projectCount](int p) { return p < 0 || p >= projectCount; })) {
            qCWarning(cmakeFileApi)
                << "Project" << p.name << ": A child index" << p.children << "is broken.";
            return false;
        }
        if (contains(p.targets, [targetCount](int t) { return t < 0 || t >= targetCount; })) {
            qCWarning(cmakeFileApi)
                << "Project" << p.name << ": A target index" << p.targets << "is broken.";
            return false;
        }
        if (contains(p.directories,
                     [directoryCount](int d) { return d < 0 || d >= directoryCount; })) {
            qCWarning(cmakeFileApi)
                << "Project" << p.name << ": A directory index" << p.directories << "is broken.";
            return false;
        }
    }
    if (topLevelCount != 1) {
        qCWarning(cmakeFileApi) << "Projects: Invalid number of top level projects, "
                                << topLevelCount << " (expected: 1).";
        return false;
    }

    for (const TargetInfo &t : config.targets) {
        if (t.directory < 0 || t.directory >= directoryCount) {
            qCWarning(cmakeFileApi)
                << "Target" << t.name << ": directory index" << t.directory << "is broken.";
            return false;
        }
        if (t.project < 0 || t.project >= projectCount) {
            qCWarning(cmakeFileApi)
                << "Target" << t.name << ": project index" << t.project << "is broken.";
            return false;
        }
    }
    return true;
}

static std::vector<ConfigurationInfo> extractConfigurations(const QJsonArray &configs,
                                                        QString &errorMessage)
{
    if (configs.isEmpty()) {
        errorMessage = Tr::tr(
            "Invalid codemodel file generated by CMake: No configurations.");
        return {};
    }

    std::vector<FileApiDetails::ConfigurationInfo> result;
    for (const auto &v : configs) {
        const QJsonObject obj = v.toObject();
        if (obj.isEmpty()) {
            errorMessage = Tr::tr(
                "Invalid codemodel file generated by CMake: Empty configuration object.");
            continue;
        }
        ConfigurationInfo config;
        config.name = obj.value("name").toString();

        config.directories = extractDirectories(obj.value("directories").toArray(), errorMessage);
        config.projects = extractProjects(obj.value("projects").toArray(), errorMessage);
        config.targets = extractTargets(obj.value("targets").toArray(), errorMessage);

        if (!validateIndexes(config)) {
            errorMessage = Tr::tr("Invalid codemodel file generated by CMake: Broken "
                                  "indexes in directories, projects, or targets.");
            return {};
        }

        result.emplace_back(std::move(config));
    }
    return result;
}

static std::vector<ConfigurationInfo> readCodemodelFile(const FilePath &codemodelFile,
                                                    QString &errorMessage)
{
    const QJsonDocument doc = readJsonFile(codemodelFile);
    const QJsonObject root = doc.object();

    if (!checkJsonObject(root, "codemodel", 2)) {
        errorMessage = Tr::tr("Invalid codemodel file generated by CMake.");
        return {};
    }

    return extractConfigurations(root.value("configurations").toArray(), errorMessage);
}

// TargetDetails:

static std::vector<FileApiDetails::FragmentInfo> extractFragments(const QJsonObject &obj)
{
    const QJsonArray fragments = obj.value("commandFragments").toArray();
    return transform<std::vector>(fragments, [](const QJsonValue &v) {
        const QJsonObject o = v.toObject();
        return FileApiDetails::FragmentInfo{o.value("fragment").toString(),
                                            o.value("role").toString()};
    });
}

static void addIncludeInfo(const FilePath &replyDir,
                           std::vector<IncludeInfo> *includes,
                           const QJsonObject &compileGroups,
                           const QString &section)
{
    const std::vector<IncludeInfo> add
        = transform<std::vector>(compileGroups.value(section).toArray(), [replyDir](const QJsonValue &v) {
              const QJsonObject i = v.toObject();
              const FilePath path = replyDir.withNewPath(i.value("path").toString());
              const bool isSystem = i.value("isSystem").toBool();
              const HeaderPath hp(path, isSystem ? HeaderPathType::System : HeaderPathType::User);

              return IncludeInfo{RawProjectPart::frameworkDetectionHeuristic(hp),
                                 i.value("backtrace").toInt(-1)};
          });
    std::copy(add.cbegin(), add.cend(), std::back_inserter(*includes));
}

static TargetDetails extractTargetDetails
    (const FilePath &replyDir, const QJsonObject &root, QString &errorMessage)
{
    TargetDetails t;
    t.name = root.value("name").toString();
    t.id = root.value("id").toString();
    t.type = root.value("type").toString();

    if (t.name.isEmpty() || t.id.isEmpty() || t.type.isEmpty()) {
        errorMessage = Tr::tr("Invalid target file: Information is missing.");
        return {};
    }

    t.backtrace = root.value("backtrace").toInt(-1);
    {
        const QJsonObject folder = root.value("folder").toObject();
        t.folderTargetProperty = folder.value("name").toString();
    }
    {
        const QJsonObject paths = root.value("paths").toObject();
        t.sourceDir = replyDir.withNewPath(paths.value("source").toString());
        t.buildDir = replyDir.withNewPath(paths.value("build").toString());
    }
    t.nameOnDisk = root.value("nameOnDisk").toString();
    {
        const QJsonArray artifacts = root.value("artifacts").toArray();
        t.artifacts = transform<QList>(artifacts, [](const QJsonValue &v) {
            return FilePath::fromString(v.toObject().value("path").toString());
        });
    }
    t.isGeneratorProvided = root.value("isGeneratorProvided").toBool();
    {
        const QJsonObject install = root.value("install").toObject();
        t.installPrefix = install.value("prefix").toObject().value("path").toString();
        {
            const QJsonArray destinations = install.value("destinations").toArray();
            t.installDestination = transform<std::vector>(destinations, [](const QJsonValue &v) {
                const QJsonObject o = v.toObject();
                return InstallDestination{o.value("path").toString(),
                                          o.value("backtrace").toInt(-1)};
            });
        }
    }
    {
        const QJsonObject link = root.value("link").toObject();
        if (link.isEmpty()) {
            t.link = {};
        } else {
            LinkInfo info;
            info.language = link.value("language").toString();
            info.isLto = link.value("lto").toBool();
            info.sysroot = link.value("sysroot").toObject().value("path").toString();
            info.fragments = extractFragments(link);
            t.link = info;
        }
    }
    {
        const QJsonObject archive = root.value("archive").toObject();
        if (archive.isEmpty()) {
            t.archive = {};
        } else {
            ArchiveInfo info;
            info.isLto = archive.value("lto").toBool();
            info.fragments = extractFragments(archive);
            t.archive = info;
        }
    }
    {
        const QJsonArray dependencies = root.value("dependencies").toArray();
        t.dependencies = transform<std::vector>(dependencies, [](const QJsonValue &v) {
            const QJsonObject o = v.toObject();
            return DependencyInfo{o.value("id").toString(), o.value("backtrace").toInt(-1)};
        });
    }
    {
        const QJsonArray sources = root.value("sources").toArray();
        t.sources = transform<std::vector>(sources, [replyDir](const QJsonValue &v) {
            const QJsonObject o = v.toObject();
            return SourceInfo{replyDir.withNewPath(o.value("path").toString()),
                              o.value("compileGroupIndex").toInt(-1),
                              o.value("sourceGroupIndex").toInt(-1),
                              o.value("backtrace").toInt(-1),
                              o.value("isGenerated").toBool()};
        });
    }
    {
        const QJsonArray sourceGroups = root.value("sourceGroups").toArray();
        t.sourceGroups = transform<std::vector>(sourceGroups, [](const QJsonValue &v) {
            const QJsonObject o = v.toObject();
            return o.value("name").toString();
        });
    }
    {
        const QJsonArray compileGroups = root.value("compileGroups").toArray();
        t.compileGroups = transform<std::vector>(compileGroups, [replyDir](const QJsonValue &v) {
            const QJsonObject o = v.toObject();
            std::vector<IncludeInfo> includes;
            addIncludeInfo(replyDir, &includes, o, "includes");
            // new in CMake 3.27+:
            addIncludeInfo(replyDir, &includes, o, "frameworks");
            return CompileInfo{
                transform<std::vector>(o.value("sourceIndexes").toArray(),
                                       [](const QJsonValue &v) { return v.toInt(-1); }),
                o.value("language").toString(),
                transform<QList>(o.value("compileCommandFragments").toArray(),
                                 [](const QJsonValue &v) {
                                     const QJsonObject o = v.toObject();
                                     return o.value("fragment").toString();
                                 }),
                includes,
                transform<std::vector>(o.value("defines").toArray(),
                                       [](const QJsonValue &v) {
                                           const QJsonObject d = v.toObject();
                                           return DefineInfo{
                                               Macro::fromKeyValue(d.value("define").toString()),
                                               d.value("backtrace").toInt(-1),
                                           };
                                       }),
                o.value("sysroot").toString(),
            };
        });
    }
    {
        const QJsonObject backtraceGraph = root.value("backtraceGraph").toObject();
        t.backtraceGraph.files = transform<std::vector>(backtraceGraph.value("files").toArray(),
                                                        [](const QJsonValue &v) {
                                                            return v.toString();
                                                        });
        t.backtraceGraph.commands
            = transform<std::vector>(backtraceGraph.value("commands").toArray(),
                                     [](const QJsonValue &v) { return v.toString(); });
        t.backtraceGraph.nodes = transform<std::vector>(backtraceGraph.value("nodes").toArray(),
                                                        [](const QJsonValue &v) {
                                                            const QJsonObject o = v.toObject();
                                                            return BacktraceNode{
                                                                o.value("file").toInt(-1),
                                                                o.value("line").toInt(-1),
                                                                o.value("command").toInt(-1),
                                                                o.value("parent").toInt(-1),
                                                            };
                                                        });
    }
    {
        const QJsonArray launchers = root.value("launchers").toArray();
        if (launchers.size() > 0) {
            t.launcherInfos = transform<QList>(launchers, [](const QJsonValue &v) {
                const QJsonObject o = v.toObject();
                QList<QString> arguments;
                for (const QJsonValue &arg : o.value("arguments").toArray())
                    arguments.append(arg.toString());
                FilePath command = FilePath::fromString(o.value("command").toString());
                return LauncherInfo { o.value("type").toString(), command, arguments };
            });
        }
    }

    return t;
}

static int validateBacktraceGraph(const TargetDetails &t)
{
    const int backtraceFilesCount = static_cast<int>(t.backtraceGraph.files.size());
    const int backtraceCommandsCount = static_cast<int>(t.backtraceGraph.commands.size());
    const int backtraceNodeCount = static_cast<int>(t.backtraceGraph.nodes.size());

    int topLevelNodeCount = 0;
    for (const BacktraceNode &n : t.backtraceGraph.nodes) {
        if (n.parent == -1) {
            ++topLevelNodeCount;
        }
        if (n.file < 0 || n.file >= backtraceFilesCount) {
            qCWarning(cmakeFileApi) << "BacktraceNode: file index" << n.file << "is broken.";
            return -1;
        }
        if (n.command < -1 || n.command >= backtraceCommandsCount) {
            qCWarning(cmakeFileApi) << "BacktraceNode: command index" << n.command << "is broken.";
            return -1;
        }
        if (n.parent < -1 || n.parent >= backtraceNodeCount) {
            qCWarning(cmakeFileApi) << "BacktraceNode: parent index" << n.parent << "is broken.";
            return -1;
        }
    }

    if (topLevelNodeCount == 0 && backtraceNodeCount > 0) { // This is a forest, not a tree
        qCWarning(cmakeFileApi) << "BacktraceNode: Invalid number of top level nodes"
                                << topLevelNodeCount;
        return -1;
    }

    return backtraceNodeCount;
}

static bool validateTargetDetails(const TargetDetails &t)
{
    // The part filled in by the codemodel file has already been covered!

    // Internal consistency of backtraceGraph:
    const int backtraceCount = validateBacktraceGraph(t);
    if (backtraceCount < 0)
        return false;

    const int sourcesCount = static_cast<int>(t.sources.size());
    const int sourceGroupsCount = static_cast<int>(t.sourceGroups.size());
    const int compileGroupsCount = static_cast<int>(t.compileGroups.size());

    if (t.backtrace < -1 || t.backtrace >= backtraceCount) {
        qCWarning(cmakeFileApi) << "TargetDetails" << t.name << ": backtrace index" << t.backtrace
                                << "is broken.";
        return false;
    }
    for (const InstallDestination &id : t.installDestination) {
        if (id.backtrace < -1 || id.backtrace >= backtraceCount) {
            qCWarning(cmakeFileApi) << "TargetDetails" << t.name << ": backtrace index"
                                    << t.backtrace << "of install destination is broken.";
            return false;
        }
    }

    for (const DependencyInfo &dep : t.dependencies) {
        if (dep.backtrace < -1 || dep.backtrace >= backtraceCount) {
            qCWarning(cmakeFileApi) << "TargetDetails" << t.name << ": backtrace index"
                                    << t.backtrace << "of dependency is broken.";
            return false;
        }
    }

    for (const SourceInfo &s : t.sources) {
        if (s.compileGroup < -1 || s.compileGroup >= compileGroupsCount) {
            qCWarning(cmakeFileApi) << "TargetDetails" << t.name << ": compile group index"
                                    << s.compileGroup << "of source info is broken.";
            return false;
        }
        if (s.sourceGroup < -1 || s.sourceGroup >= sourceGroupsCount) {
            qCWarning(cmakeFileApi) << "TargetDetails" << t.name << ": source group index"
                                    << s.sourceGroup << "of source info is broken.";
            return false;
        }
        if (s.backtrace < -1 || s.backtrace >= backtraceCount) {
            qCWarning(cmakeFileApi) << "TargetDetails" << t.name << ": backtrace index"
                                    << s.backtrace << "of source info is broken.";
            return false;
        }
    }

    for (const CompileInfo &cg : t.compileGroups) {
        for (int s : cg.sources) {
            if (s < 0 || s >= sourcesCount) {
                qCWarning(cmakeFileApi) << "TargetDetails" << t.name << ": sources index" << s
                                        << "of compile group is broken.";
                return false;
            }
        }
        for (const IncludeInfo &i : cg.includes) {
            if (i.backtrace < -1 || i.backtrace >= backtraceCount) {
                qCWarning(cmakeFileApi) << "TargetDetails" << t.name << ": includes/backtrace index"
                                        << i.backtrace << "of compile group is broken.";
                return false;
            }
        }
        for (const DefineInfo &d : cg.defines) {
            if (d.backtrace < -1 || d.backtrace >= backtraceCount) {
                qCWarning(cmakeFileApi) << "TargetDetails" << t.name << ": defines/backtrace index"
                                        << d.backtrace << "of compile group is broken.";
                return false;
            }
        }
    }

    return true;
}

static TargetDetails readTargetFile(const FilePath &replyDir, const FilePath &targetFile, QString &errorMessage)
{
    const QJsonDocument doc = readJsonFile(targetFile);
    const QJsonObject root = doc.object();

    TargetDetails result = extractTargetDetails(replyDir, root, errorMessage);
    if (errorMessage.isEmpty() && !validateTargetDetails(result)) {
        errorMessage = Tr::tr(
            "Invalid target file generated by CMake: Broken indexes in target details.");
    }
    return result;
}

// --------------------------------------------------------------------
// ReplyFileContents:
// --------------------------------------------------------------------

FilePath FileApiDetails::ReplyFileContents::jsonFile(const QString &kind, const FilePath &replyDir) const
{
    const auto ro = findOrDefault(replies, equal(&ReplyObject::kind, kind));
    if (ro.file.isEmpty())
        return {};
    else
        return (replyDir / ro.file).absoluteFilePath();
}

// --------------------------------------------------------------------
// FileApi:
// --------------------------------------------------------------------

bool FileApiParser::setupCMakeFileApi(const FilePath &buildDirectory)
{
    // So that we have a directory to watch.
    buildDirectory.pathAppended(CMAKE_RELATIVE_REPLY_PATH).ensureWritableDir();

    FilePath queryDir = buildDirectory.pathAppended(CMAKE_RELATIVE_QUERY_PATH);
    queryDir.ensureWritableDir();

    if (!queryDir.exists()) {
        reportFileApiSetupFailure();
        return false;
    }
    QTC_ASSERT(queryDir.exists(), return false);

    bool failedBefore = false;
    for (const FilePath &filePath : cmakeQueryFilePaths(buildDirectory)) {
        const bool success = filePath.ensureExistingFile();
        if (!success && !failedBefore) {
            failedBefore = true;
            reportFileApiSetupFailure();
        }
    }

    return true;
}

static QStringList uniqueTargetFiles(const ConfigurationInfo &config)
{
    QSet<QString> knownIds;
    QStringList files;
    for (const TargetInfo &t : config.targets) {
        const int knownCount = knownIds.count();
        knownIds.insert(t.id);
        if (knownIds.count() > knownCount) {
            files.append(t.jsonFile);
        }
    }
    return files;
}

FileApiData FileApiParser::parseData(QPromise<std::shared_ptr<FileApiQtcData>> &promise,
                                     const FilePath &replyFilePath,
                                     const Utils::FilePath &buildDir,
                                     const QString &cmakeBuildType,
                                     QString &errorMessage)
{
    QTC_CHECK(errorMessage.isEmpty());
    const FilePath replyDir = replyFilePath.parentDir();

    FileApiData result;

    const auto cancelCheck = [&promise, &errorMessage] {
        if (promise.isCanceled()) {
            errorMessage = Tr::tr("CMake parsing was canceled.");
            return true;
        }
        return false;
    };

    result.replyFile = readReplyFile(replyFilePath, errorMessage);
    if (cancelCheck())
        return {};
    const FilePath cachePathFromReply = result.replyFile.jsonFile("cache", replyDir);
    if (cachePathFromReply.isEmpty())
        result.cache = CMakeConfig::fromFile(buildDir / Constants::CMAKE_CACHE_TXT, &errorMessage);
    else
        result.cache = readCacheFile(cachePathFromReply, errorMessage);
    if (cancelCheck())
        return {};
    result.cmakeFiles = readCMakeFilesFile(result.replyFile.jsonFile("cmakeFiles", replyDir),
                                           errorMessage);
    if (cancelCheck())
        return {};
    auto codeModels = readCodemodelFile(result.replyFile.jsonFile("codemodel", replyDir),
                                         errorMessage);

    if (codeModels.size() == 0) {
        //: General Messages refers to the output view
        errorMessage = Tr::tr(
                           "CMake project configuration failed. No CMake configuration for "
                           "build type \"%1\" found. Check General Messages for more information.")
                           .arg(cmakeBuildType);
        return result;
    }

    auto it = std::find_if(codeModels.begin(), codeModels.end(),
                           [cmakeBuildType](const ConfigurationInfo& cfg) {
                               return QString::compare(cfg.name, cmakeBuildType, Qt::CaseInsensitive) == 0;
                           });
    if (it == codeModels.end()) {
        QStringList buildTypes;
        for (const ConfigurationInfo &cfg: codeModels)
            buildTypes << cfg.name;

        if (result.replyFile.isMultiConfig) {
            errorMessage = Tr::tr("No \"%1\" CMake configuration found. Available configurations: \"%2\".\n"
                              "Make sure that CMAKE_CONFIGURATION_TYPES variable contains the \"Build type\" field.")
                           .arg(cmakeBuildType)
                           .arg(buildTypes.join(", "));
        } else {
            errorMessage = Tr::tr("No \"%1\" CMake configuration found. Available configuration: \"%2\".\n"
                              "Make sure that CMAKE_BUILD_TYPE variable matches the \"Build type\" field.")
                           .arg(cmakeBuildType)
                           .arg(buildTypes.join(", "));
        }
        return result;
    }
    result.codemodel = std::move(*it);
    if (cancelCheck())
        return {};

    const QStringList targetFiles = uniqueTargetFiles(result.codemodel);

    for (const QString &targetFile : targetFiles) {
        if (cancelCheck())
            return {};
        QString targetErrorMessage;
        TargetDetails td = readTargetFile(replyDir, replyDir / targetFile, targetErrorMessage);
        if (targetErrorMessage.isEmpty()) {
            result.targetDetails.emplace_back(std::move(td));
        } else {
            qWarning() << "Failed to retrieve target data from cmake fileapi:"
                       << targetErrorMessage;
            errorMessage = targetErrorMessage;
        }
    }

    return result;
}

FilePath FileApiParser::scanForCMakeReplyFile(const FilePath &buildDirectory)
{
    const FilePath replyDir = cmakeReplyDirectory(buildDirectory);
    if (!replyDir.exists())
        return {};

    const FilePaths entries = replyDir.dirEntries({{"index-*.json"}, QDir::Files}, QDir::Name);
    return entries.isEmpty() ? FilePath() : entries.first();
}

FilePaths FileApiParser::cmakeQueryFilePaths(const FilePath &buildDirectory)
{
    const FilePath queryDir = buildDirectory / CMAKE_RELATIVE_QUERY_PATH;
    return transform(CMAKE_QUERY_FILENAMES, [&queryDir](const QString &name) {
        return queryDir.resolvePath(name);
    });
}

} // CMakeProjectManager::Internal
