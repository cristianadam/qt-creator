// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "zephyrproject.h"

#include "zephyrconstants.h"
#include "zephyrtr.h"

#include <coreplugin/icontext.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildinfo.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/buildsystem.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/projectupdater.h>
#include <projectexplorer/rawprojectpart.h>
#include <projectexplorer/target.h>
#include <projectexplorer/toolchainmanager.h>

#include <utils/algorithm.h>
#include <utils/commandline.h>
#include <utils/qtcassert.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace ProjectExplorer;
using namespace Utils;

namespace PEConstants = ProjectExplorer::Constants;

namespace Zephyr::Internal {

const QLatin1StringView WEST_MANIFEST_MIMETYPE{"text/x-west-manifest"};

// Code model

static QStringList compileArguments(const QJsonObject &entry, OsType osType)
{
    const QJsonArray arguments = entry.value("arguments").toArray();
    if (arguments.isEmpty())
        return ProcessArgs::splitArgs(entry.value("command").toString(), osType);
    QStringList result;
    for (const QJsonValue &argument : arguments)
        result.append(argument.toString());
    return result;
}

static QString cmakeTargetFromOutput(const QString &output)
{
    static const QRegularExpression re(R"(CMakeFiles/([^/]+)\.dir/)");
    return re.match(output).captured(1);
}

// "arm-zephyr-eabi-gcc" -> "arm-zephyr-eabi", "/usr/bin/gcc" -> ""
static QString targetTripleFromCompiler(const FilePath &compiler)
{
    static const QRegularExpression re(
        R"(^(.+-.+)-(?:gcc|g\+\+|cc|c\+\+|clang|clang\+\+)(?:-[0-9.]+)?(?:\.exe)?$)");
    return re.match(compiler.fileName()).captured(1);
}

static Toolchain *toolchainForCompiler(const FilePath &compiler, Id language)
{
    if (compiler.isEmpty())
        return nullptr;
    const auto matches = [&](const Toolchain *tc) {
        return tc->isValid() && tc->language() == language && tc->matchesCompilerCommand(compiler);
    };
    if (Toolchain *tc = ToolchainManager::toolchain(matches))
        return tc;

    // The kit's compiler rejects the target flags of a cross build, so the code
    // model needs the one the build used, registered for this session only.
    for (ToolchainFactory *factory : ToolchainFactory::allToolchainFactories()) {
        const Toolchains detected = factory->detectForImport({compiler, language});
        if (detected.isEmpty())
            continue;
        for (Toolchain *tc : detected)
            tc->setDetectionSource(DetectionSource::Temporary);
        qDeleteAll(ToolchainManager::registerToolchains(detected));
        return ToolchainManager::toolchain(matches);
    }
    return nullptr;
}

class CompileGroup
{
public:
    QString target;
    bool isCxx = false;
    HeaderPaths headerPaths;
    Macros macros;
    FilePaths includedFiles;
    QStringList flags;
    FilePaths files;
};

class CompileDatabase
{
public:
    QList<CompileGroup> groups;
    FilePath cCompiler;
    FilePath cxxCompiler;
};

static bool isCxxSource(const FilePath &file)
{
    static const QStringList cxxSuffixes = {"cpp", "cxx", "cc", "c++", "C"};
    return cxxSuffixes.contains(file.suffix());
}

// Entries compiled with the same flags end up in one group, which keeps a
// Zephyr build with a thousand sources down to a handful of project parts.
static CompileDatabase parseCompileDatabase(const QByteArray &contents, const FilePath &buildDir)
{
    CompileDatabase db;
    QMap<QString, CompileGroup> groups;

    const QJsonArray entries = QJsonDocument::fromJson(contents).array();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const FilePath dir = buildDir.withNewMappedPath(
            FilePath::fromUserInput(entry.value("directory").toString()));
        const auto toPath = [&dir](const QString &path) {
            return dir.resolvePath(FilePath::fromUserInput(path).path());
        };

        const FilePath file = toPath(entry.value("file").toString());
        const QStringList args = compileArguments(entry, buildDir.osType());
        if (args.isEmpty() || file.isEmpty())
            continue;

        CompileGroup group;
        group.target = cmakeTargetFromOutput(entry.value("output").toString());
        group.isCxx = isCxxSource(file);

        FilePath &compiler = group.isCxx ? db.cxxCompiler : db.cCompiler;
        if (compiler.isEmpty())
            compiler = toPath(args.first());

        QStringList key = {group.target, group.isCxx ? QString("c++") : QString("c"),
                           args.first()};
        for (int i = 1; i < args.size(); ++i) {
            const QString &arg = args.at(i);
            const auto optionValue = [&](const QString &option) -> std::optional<QString> {
                if (arg == option)
                    return i + 1 < args.size() ? args.at(++i) : QString();
                if (arg.startsWith(option))
                    return arg.mid(option.size());
                return {};
            };

            if (arg == "-c" || arg == "-MD" || arg == "-MMD" || arg == "-MP")
                continue;
            if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ") {
                ++i;
                continue;
            }
            if (!arg.startsWith('-') && toPath(arg) == file)
                continue;
            const int first = i;
            if (const auto path = optionValue("-isystem")) {
                group.headerPaths.append(HeaderPath::makeSystem(toPath(*path)));
            } else if (const auto path = optionValue("-idirafter")) {
                group.headerPaths.append(HeaderPath::makeSystem(toPath(*path)));
            } else if (const auto path = optionValue("-iquote")) {
                group.headerPaths.append(HeaderPath::makeUser(toPath(*path)));
            } else if (const auto path = optionValue("-imacros")) {
                group.includedFiles.append(toPath(*path));
            } else if (const auto path = optionValue("-include")) {
                group.includedFiles.append(toPath(*path));
            } else if (const auto path = optionValue("-I")) {
                group.headerPaths.append(HeaderPath::makeUser(toPath(*path)));
            } else if (const auto macro = optionValue("-D")) {
                group.macros.append(Macro::fromKeyValue(*macro));
            } else if (const auto macro = optionValue("-U")) {
                group.macros.append(Macro(macro->toUtf8(), MacroType::Undefine));
            } else {
                group.flags.append(arg);
            }
            key.append(args.mid(first, i - first + 1));
        }

        const QString groupKey = key.join('\n');
        auto it = groups.find(groupKey);
        if (it == groups.end())
            it = groups.insert(groupKey, group);
        it->files.append(file);
    }

    db.groups = groups.values();
    return db;
}

static FilePath compileDatabaseFile(const FilePath &buildDir, const QString &appName)
{
    // A sysbuild puts the application into a subdirectory named after it.
    const FilePath sysbuildAppDb = buildDir / appName / "compile_commands.json";
    if (sysbuildAppDb.isReadableFile())
        return sysbuildAppDb;
    return buildDir / "compile_commands.json";
}

// ZephyrBuildSystem

class ZephyrBuildSystem final : public BuildSystem
{
public:
    explicit ZephyrBuildSystem(BuildConfiguration *bc)
        : BuildSystem(bc)
        , m_cppCodeModelUpdater(ProjectUpdaterFactory::createCppProjectUpdater())
    {
        const auto reparseIfActive = [this] {
            if (target()->activeBuildConfiguration() == buildConfiguration())
                requestDelayedParse();
        };
        connect(project(), &Project::projectFileIsDirty, this, reparseIfActive);
        connect(bc, &BuildConfiguration::buildDirectoryChanged, this, reparseIfActive);
        connect(BuildManager::instance(), &BuildManager::buildQueueFinished,
                this, reparseIfActive);
        requestDelayedParse();
    }

    static QString name() { return "zephyr"; }

    void triggerParsing() final
    {
        ParseGuard guard = guardParsingRun();
        const FilePath root = projectDirectory();

        static const QStringList skipDirs = {".git", ".west", "build"};

        FilePaths files;
        root.iterateDirectory(
            [&](const FilePath &path) {
                const QString pathStr = path.path();
                for (const QString &skip : skipDirs) {
                    if (pathStr.contains('/' + skip + '/') || pathStr.endsWith('/' + skip))
                        return IterationPolicy::Continue;
                }
                files.append(path);
                return IterationPolicy::Continue;
            },
            {{"*.c", "*.h", "*.cpp", "*.hpp", "*.cmake", "*.conf", "*.yml", "*.yaml",
              "CMakeLists.txt"},
             DirFilterFlag::Files,
             DirIteratorFlag::Subdirectories});

        auto newRoot = std::make_unique<ProjectNode>(root);
        for (const FilePath &f : files)
            newRoot->addNestedNode(std::make_unique<FileNode>(f, FileNode::fileTypeForFileName(f)));
        setRootProjectNode(std::move(newRoot));

        updateCppCodeModel();

        guard.markAsSuccess();
        emitBuildSystemUpdated();
    }

private:
    void updateCppCodeModel()
    {
        if (!m_cppCodeModelUpdater)
            return;
        KitInfo kitInfo(kit());
        QTC_ASSERT(kitInfo.isValid(), return);

        const FilePath buildDir = buildConfiguration()->buildDirectory();
        const FilePath dbFile = compileDatabaseFile(buildDir, projectDirectory().fileName());
        const CompileDatabase db = parseCompileDatabase(dbFile.fileContents().value_or(""),
                                                        buildDir);

        // Prefer a toolchain for the compiler the build actually used. Otherwise
        // the code model at least has to know the target architecture.
        QString targetFlag;
        if (Toolchain *tc = toolchainForCompiler(db.cCompiler, PEConstants::C_LANGUAGE_ID)) {
            kitInfo.cToolchain = tc;
        } else if (const QString triple = targetTripleFromCompiler(db.cCompiler);
                   !triple.isEmpty() && kitInfo.cToolchain
                   && kitInfo.cToolchain->typeId() == PEConstants::CLANG_TOOLCHAIN_TYPEID) {
            targetFlag = "--target=" + triple;
        }
        if (Toolchain *tc = toolchainForCompiler(db.cxxCompiler, PEConstants::CXX_LANGUAGE_ID))
            kitInfo.cxxToolchain = tc;

        RawProjectParts rpps;
        for (const CompileGroup &group : db.groups) {
            QStringList flags = group.flags;
            if (!targetFlag.isEmpty()
                && !Utils::anyOf(flags, [](const QString &f) {
                       return f == "-target" || f.startsWith("--target=");
                   })) {
                flags.prepend(targetFlag);
            }

            RawProjectPart rpp;
            rpp.setDisplayName(group.target.isEmpty() ? project()->displayName() : group.target);
            rpp.setProjectFileLocation(projectFilePath());
            rpp.setBuildSystemTarget(group.target);
            rpp.setHeaderPaths(group.headerPaths);
            rpp.setMacros(group.macros);
            rpp.setIncludedFiles(group.includedFiles);
            rpp.setFiles(group.files);
            if (group.isCxx)
                rpp.setFlagsForCxx({kitInfo.cxxToolchain, flags, buildDir});
            else
                rpp.setFlagsForC({kitInfo.cToolchain, flags, buildDir});
            rpps.append(rpp);
        }

        m_cppCodeModelUpdater->update({project(), kitInfo, activeParseEnvironment(), rpps});
    }

    std::unique_ptr<ProjectUpdater> m_cppCodeModelUpdater;
};

// ZephyrBuildConfiguration

class ZephyrBuildConfiguration final : public BuildConfiguration
{
public:
    ZephyrBuildConfiguration(Target *target, Id id)
        : BuildConfiguration(target, id)
    {
        setBuildDirectoryHistoryCompleter("Zephyr.BuildDir.History");
        setConfigWidgetDisplayName(Tr::tr("Zephyr"));
        appendInitialBuildStep(Constants::WEST_BUILD_STEP_ID);
    }
};

class ZephyrBuildConfigurationFactory final : public BuildConfigurationFactory
{
public:
    ZephyrBuildConfigurationFactory()
    {
        registerBuildConfiguration<ZephyrBuildConfiguration>("Zephyr.ZephyrBuildConfiguration");
        setSupportedProjectType(Constants::ZEPHYR_PROJECT_ID);
        setSupportedProjectMimeTypeName(WEST_MANIFEST_MIMETYPE);
        setBuildGenerator([](const Kit *, const FilePath &projectPath, bool forSetup) {
            BuildInfo info;
            info.typeName = Tr::tr("Build");
            info.buildDirectory = forSetup ? projectPath.parentDir() / "build" : projectPath;
            info.buildSystemName = "zephyr";
            if (forSetup)
                info.displayName = Tr::tr("Default");
            return QList<BuildInfo>{info};
        });
    }
};

// ZephyrProject

class ZephyrProject final : public Project
{
public:
    explicit ZephyrProject(const FilePath &file)
        : Project(WEST_MANIFEST_MIMETYPE, file)
    {
        setType(Constants::ZEPHYR_PROJECT_ID);
        setProjectLanguages(Core::Context(ProjectExplorer::Constants::C_LANGUAGE_ID));
        setDisplayName(projectDirectory().fileName());
        setBuildSystemCreator<ZephyrBuildSystem>();
    }
};

void setupZephyrProject()
{
    static ZephyrBuildConfigurationFactory theBuildConfigurationFactory;
    ProjectManager::registerProjectType<ZephyrProject>(WEST_MANIFEST_MIMETYPE);
}

#ifdef WITH_TESTS

class ZephyrCompileDatabaseTest final : public QObject
{
    Q_OBJECT

private slots:
    void testGrouping();
    void testTargetTriple_data();
    void testTargetTriple();
};

void ZephyrCompileDatabaseTest::testGrouping()
{
    const QByteArray json = R"([
      {"directory": "/b", "file": "/src/a.c", "output": "zephyr/CMakeFiles/zephyr.dir/a.c.obj",
       "command": "/sdk/arm-zephyr-eabi-gcc -DKERNEL -I/inc -isystem /sys -imacros /b/autoconf.h -mcpu=cortex-m3 -o zephyr/CMakeFiles/zephyr.dir/a.c.obj -c /src/a.c"},
      {"directory": "/b", "file": "/src/b.c", "output": "zephyr/CMakeFiles/zephyr.dir/b.c.obj",
       "command": "/sdk/arm-zephyr-eabi-gcc -DKERNEL -I/inc -isystem /sys -imacros /b/autoconf.h -mcpu=cortex-m3 -o zephyr/CMakeFiles/zephyr.dir/b.c.obj -c /src/b.c"},
      {"directory": "/b", "file": "/app/main.cpp", "output": "CMakeFiles/app.dir/main.cpp.obj",
       "arguments": ["/sdk/arm-zephyr-eabi-g++", "-I", "rel", "-UNDEBUG", "-c", "/app/main.cpp"]}
    ])";

    const CompileDatabase db = parseCompileDatabase(json, FilePath::fromString("/b"));
    QCOMPARE(db.cCompiler, FilePath::fromString("/sdk/arm-zephyr-eabi-gcc"));
    QCOMPARE(db.cxxCompiler, FilePath::fromString("/sdk/arm-zephyr-eabi-g++"));
    QCOMPARE(db.groups.size(), 2);

    const CompileGroup &app = db.groups.at(0).target == "app" ? db.groups.at(0) : db.groups.at(1);
    const CompileGroup &zephyr = db.groups.at(0).target == "app" ? db.groups.at(1) : db.groups.at(0);

    QCOMPARE(zephyr.target, "zephyr");
    QVERIFY(!zephyr.isCxx);
    QCOMPARE(zephyr.files, FilePaths({FilePath::fromString("/src/a.c"),
                                      FilePath::fromString("/src/b.c")}));
    QCOMPARE(zephyr.headerPaths, HeaderPaths({HeaderPath::makeUser(FilePath::fromString("/inc")),
                                              HeaderPath::makeSystem(FilePath::fromString("/sys"))}));
    QCOMPARE(zephyr.macros, Macros({Macro("KERNEL", "1")}));
    QCOMPARE(zephyr.includedFiles, FilePaths({FilePath::fromString("/b/autoconf.h")}));
    QCOMPARE(zephyr.flags, QStringList({"-mcpu=cortex-m3"}));

    QVERIFY(app.isCxx);
    QCOMPARE(app.headerPaths, HeaderPaths({HeaderPath::makeUser(FilePath::fromString("/b/rel"))}));
    QCOMPARE(app.macros, Macros({Macro("NDEBUG", MacroType::Undefine)}));
    QVERIFY(app.flags.isEmpty());
}

void ZephyrCompileDatabaseTest::testTargetTriple_data()
{
    QTest::addColumn<QString>("compiler");
    QTest::addColumn<QString>("triple");

    QTest::newRow("sdk") << "/sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" << "arm-zephyr-eabi";
    QTest::newRow("c++") << "x86_64-zephyr-elf-g++" << "x86_64-zephyr-elf";
    QTest::newRow("versioned") << "/usr/bin/arm-none-eabi-gcc-14.2" << "arm-none-eabi";
    QTest::newRow("windows") << "C:/sdk/riscv64-zephyr-elf-gcc.exe" << "riscv64-zephyr-elf";
    QTest::newRow("host") << "/usr/bin/gcc" << "";
    QTest::newRow("host versioned") << "/usr/bin/gcc-14" << "";
}

void ZephyrCompileDatabaseTest::testTargetTriple()
{
    QFETCH(QString, compiler);
    QFETCH(QString, triple);

    QCOMPARE(targetTripleFromCompiler(FilePath::fromUserInput(compiler)), triple);
}

QObject *createZephyrCompileDatabaseTest()
{
    return new ZephyrCompileDatabaseTest;
}

#endif // WITH_TESTS

} // namespace Zephyr::Internal

#ifdef WITH_TESTS
#include "zephyrproject.moc"
#endif
