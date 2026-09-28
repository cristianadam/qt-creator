// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "zephyrdebug.h"

#include "zephyrconstants.h"
#include "zephyrsettings.h"

#include <debugger/debuggerengine.h>
#include <debugger/debuggerruncontrol.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>

#include <utils/qtcprocess.h>

#include <QtTaskTree/QBarrier>

#include <QRegularExpression>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Debugger;
using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace Zephyr::Internal {

struct RunnerConfig
{
    FilePath elfFile;
    FilePath gdb;
    int gdbPort = 1234;
};

// The list of "  <runner>:" below "args:", one "    - <arg>" per line.
static QStringList runnerArguments(const QString &yaml, const QString &runner)
{
    QStringList result;
    bool inArgs = false;
    bool inRunner = false;
    for (const QString &line : yaml.split('\n')) {
        if (!line.startsWith(' ')) {
            inArgs = line.startsWith("args:");
            inRunner = false;
        } else if (inArgs && !line.startsWith("   ")) {
            inRunner = line.trimmed() == runner + ':';
        } else if (inRunner) {
            const QString item = line.trimmed();
            if (item.startsWith("- "))
                result.append(item.mid(2).trimmed());
        }
    }
    return result;
}

static std::optional<int> gdbPortFromArguments(const QStringList &args)
{
    for (int i = 0; i < args.size(); ++i) {
        QString value;
        if (args.at(i).startsWith("--gdb-port="))
            value = args.at(i).mid(11);
        else if (args.at(i) == "--gdb-port" && i + 1 < args.size())
            value = args.at(i + 1);
        bool ok = false;
        const int port = value.toInt(&ok);
        if (ok && port > 0)
            return port;
    }
    return {};
}

static RunnerConfig parseRunnerConfig(const QString &yaml, const FilePath &buildDir)
{
    RunnerConfig result;
    result.elfFile = buildDir / "zephyr/zephyr.elf";

    static const QRegularExpression debugRunnerRe(R"(^debug-runner:\s*(\S+))",
                                           QRegularExpression::MultilineOption);
    const QString runner = debugRunnerRe.match(yaml).captured(1);

    static const QRegularExpression elfRe(R"(^\s+elf_file:\s*(\S+))",
                                          QRegularExpression::MultilineOption);

    const QString elfName = elfRe.match(yaml).captured(1);
    if (!elfName.isEmpty())
        result.elfFile = buildDir / "zephyr" / elfName;

    static const QRegularExpression gdbRe(R"(^\s+gdb:\s*(\S.*?)\s*$)",
                                          QRegularExpression::MultilineOption);
    const QString gdb = gdbRe.match(yaml).captured(1);
    if (!gdb.isEmpty())
        result.gdb = buildDir.withNewMappedPath(FilePath::fromUserInput(gdb));

    if (const std::optional<int> port = gdbPortFromArguments(runnerArguments(yaml, runner)))
        result.gdbPort = *port;
    else if (runner == "qemu")
        result.gdbPort = 1234;
    else if (runner == "jlink" || runner == "nrfjprog" || runner == "nrfutil")
        result.gdbPort = 2331;
    else
        result.gdbPort = 3333; // openocd, pyocd, linkserver, ...

    return result;
}

static RunnerConfig readRunnerConfig(const FilePath &buildDir)
{
    const FilePath yamlFile = buildDir / "zephyr/runners.yaml";
    const QString yaml = QString::fromUtf8(yamlFile.fileContents().value_or(QByteArray{}));
    return parseRunnerConfig(yaml, buildDir);
}

static void killQemuIfRunning(const FilePath &buildDir)
{
    const FilePath pidFile = buildDir / "qemu.pid";
    if (!pidFile.isReadableFile())
        return;
    bool ok = false;
    const qint64 pid = pidFile.fileContents().value_or(QByteArray()).trimmed().toLongLong(&ok);
    if (ok && pid > 0) {
        Process kill;
        if (buildDir.osType() == OsTypeWindows) {
            kill.setCommand({buildDir.withNewPath("C:/Windows/System32/taskkill.exe"),
                             {"/PID", QString::number(pid), "/F"}});
        } else {
            kill.setCommand({buildDir.withNewPath("/bin/kill"), {QString::number(pid)}});
        }
        kill.runBlocking();
    }
    pidFile.removeFile();
}

class ZephyrDebugWorkerFactory final : public RunWorkerFactory
{
public:
    ZephyrDebugWorkerFactory()
    {
        setRecipeProducer([](RunControl *runControl) -> Group {
            const FilePath buildDir = runControl->buildDirectory();
            const RunnerConfig rc = readRunnerConfig(buildDir);

            DebuggerRunParameters rp = DebuggerRunParameters::fromRunControl(runControl);
            rp.setStartMode(AttachToRemoteServer);
            rp.setCloseMode(KillAtClose);
            rp.setRemoteChannel("localhost:" + QString::number(rc.gdbPort));
            rp.setSymbolFile(rc.elfFile);
            if (!rc.gdb.isEmpty()) {
                // The SDK's gdb knows the target architecture, a desktop kit's does not.
                ProcessRunData debugger = rp.debugger();
                debugger.command = CommandLine{rc.gdb};
                rp.setDebugger(debugger);
                rp.setCppEngineType(GdbEngineType);
            }
            rp.setUseContinueInsteadOfRun(true);
            rp.setSkipDebugServer(true);

            const auto modifier = [buildDir](Process &process) {
                CommandLine cmd{settings().westFilePath()};
                cmd.addArg("build");
                cmd.addArg("-d");
                cmd.addArg(buildDir.nativePath());
                cmd.addArg("-t");
                cmd.addArg("debugserver");
                process.setCommand(cmd);
                process.setWorkingDirectory(settings().workspaceDir());
            };

            return Group {
                onGroupSetup([buildDir] {
                    killQemuIfRunning(buildDir);
                    return SetupResult::Continue;
                }),
                When(runControl->processTaskWithModifier(modifier), &Process::started) >> Do {
                    debuggerRecipe(runControl, rp)
                }
            };
        });
        addSupportedRunMode(ProjectExplorer::Constants::DEBUG_RUN_MODE);
        addSupportedRunConfig(Constants::ZEPHYR_RUN_CONFIG_ID);
    }
};

void setupZephyrDebug()
{
    static ZephyrDebugWorkerFactory theDebugWorkerFactory;
}

#ifdef WITH_TESTS

class ZephyrRunnerConfigTest final : public QObject
{
    Q_OBJECT

private slots:
    void testRunnerConfig_data();
    void testRunnerConfig();
};

void ZephyrRunnerConfigTest::testRunnerConfig_data()
{
    QTest::addColumn<QString>("yaml");
    QTest::addColumn<QString>("gdb");
    QTest::addColumn<int>("gdbPort");

    QTest::newRow("qemu, no gdb") << R"(runners:
- qemu
debug-runner: qemu
config:
  elf_file: zephyr.elf
  openocd: /usr/bin/openocd
args:
  qemu:
    []
)" << "" << 1234;

    QTest::newRow("jlink default port") << R"(debug-runner: jlink
config:
  elf_file: zephyr.elf
  # Host tools:
  gdb: /sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb
args:
  jlink:
    - --dt-flash=y
    - --device=nRF52840_xxAA
)" << "/sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb" << 2331;

    QTest::newRow("port of the debug runner") << R"(debug-runner: openocd
config:
  gdb: /sdk/gdb
args:
  jlink:
    - --gdb-port=2000
  openocd:
    - --cmd-load
    - --gdb-port=4444
  pyocd:
    - --gdb-port=5555
)" << "/sdk/gdb" << 4444;
}

void ZephyrRunnerConfigTest::testRunnerConfig()
{
    QFETCH(QString, yaml);
    QFETCH(QString, gdb);
    QFETCH(int, gdbPort);

    const RunnerConfig rc = parseRunnerConfig(yaml, FilePath::fromString("/build"));
    QCOMPARE(rc.gdb, FilePath::fromUserInput(gdb));
    QCOMPARE(rc.gdbPort, gdbPort);
    QCOMPARE(rc.elfFile, FilePath::fromString("/build/zephyr/zephyr.elf"));
}

QObject *createZephyrRunnerConfigTest()
{
    return new ZephyrRunnerConfigTest;
}

#endif // WITH_TESTS

} // namespace Zephyr::Internal

#ifdef WITH_TESTS
#include "zephyrdebug.moc"
#endif
