// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include <app/app_version.h>

#include <utils/launcherinterface.h>
#include <utils/qtcprocess.h>
#include <utils/singleton.h>
#include <utils/temporarydirectory.h>

#include <QtTest>

#include <iostream>
#include <fstream>

using namespace Utils;

class tst_TaskTree : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void processing();
    void processTree_data();
    void processTree();

    void cleanupTestCase();

private:

    FilePath m_testAppPath;
    QEventLoop m_eventLoop;
};

void tst_TaskTree::initTestCase()
{
    Utils::TemporaryDirectory::setMasterTemporaryDirectory(QDir::tempPath() + "/"
                             + Core::Constants::IDE_CASED_ID + "-XXXXXX");
    const QString libExecPath(qApp->applicationDirPath() + '/'
                              + QLatin1String(TEST_RELATIVE_LIBEXEC_PATH));
    LauncherInterface::setPathToLauncher(libExecPath);
    m_testAppPath = FilePath::fromString(QLatin1String(TESTAPP_PATH)
                                       + QLatin1String("/testapp")).withExecutableSuffix();
}

void tst_TaskTree::cleanupTestCase()
{
    Utils::Singleton::deleteAll();
}

void tst_TaskTree::processing()
{
    using namespace Tasking;

    const Task process {
        parallel,
        Process([](QtcProcess &) {}, [](const QtcProcess &) {}),
        Process([](QtcProcess &) {}, [](const QtcProcess &) {}),
        Process([](QtcProcess &) {}, [](const QtcProcess &) {})
    };

    const Task tree1 {
        process
    };

    const Task tree2 {
        parallel,
        Task {
            parallel,
            Process([](QtcProcess &) {}, [](const QtcProcess &) {}),
            Task {
                parallel,
                Process([](QtcProcess &) {}, [](const QtcProcess &) {}),
                Task {
                    parallel,
                    Process([](QtcProcess &) {}, [](const QtcProcess &) {})
                }
            },
            Task {
                parallel,
                Process([](QtcProcess &) {}, [](const QtcProcess &) {}),
                OnSubTreeDone([] {}),
            }
        },
        process,
        OnSubTreeDone([] {}),
        OnSubTreeError([] {})
    };
}

enum class Handler {
    Setup,
    Done,
    Error,
    SubTreeSetup,
    SubTreeDone,
    SubTreeError
};

using Log = QList<QPair<int, Handler>>;
static const char s_processIdProperty[] = "__processId";
static Log s_log;

void tst_TaskTree::processTree_data()
{
    using namespace Tasking;
    using namespace std::placeholders;

    QTest::addColumn<Task>("root");
    QTest::addColumn<QList<QPair<int, Handler>>>("expectedLog");
    QTest::addColumn<bool>("runningAfterStart");
    QTest::addColumn<bool>("success");

    const auto setupProcessHelper = [this](QtcProcess &process, const QStringList &args, int processId) {
        process.setCommand(CommandLine(m_testAppPath, args));
        process.setProperty(s_processIdProperty, processId);
        s_log.append({processId, Handler::Setup});
    };
    const auto setupProcess = [setupProcessHelper](QtcProcess &process, int processId) {
        setupProcessHelper(process, {"-return", "0"}, processId);
    };
    const auto setupErrorProcess = [setupProcessHelper](QtcProcess &process, int processId) {
        setupProcessHelper(process, {"-return", "1"}, processId);
    };
    const auto setupCrashProcess = [setupProcessHelper](QtcProcess &process, int processId) {
        setupProcessHelper(process, {"-crash"}, processId);
    };
    const auto readResultAnonymous = [](const QtcProcess &) {
        s_log.append({-1, Handler::Done});
    };
    const auto readResult = [](const QtcProcess &process) {
        const int processId = process.property(s_processIdProperty).toInt();
        s_log.append({processId, Handler::Done});
    };
    const auto readError = [](const QtcProcess &process) {
        const int processId = process.property(s_processIdProperty).toInt();
        s_log.append({processId, Handler::Error});
    };
    const auto subTreeSetup = [](int processId) {
        s_log.append({processId, Handler::SubTreeSetup});
    };
    const auto subTreeDone = [](int processId) {
        s_log.append({processId, Handler::SubTreeDone});
    };
    const auto topLevelTreeDone = [this] {
        s_log.append({-1, Handler::SubTreeDone});
        m_eventLoop.quit();
    };
    const auto topLevelTreeError = [this] {
        s_log.append({-1, Handler::SubTreeError});
        m_eventLoop.quit();
    };

    const Task emptyRoot {
        OnSubTreeDone(topLevelTreeDone),
    };
    const Log emptyLog{{-1, Handler::SubTreeDone}};

    const Task nestedRoot {
        Task {
            Task {
                Task {
                    Task {
                        Task {
                            Process(std::bind(setupProcess, _1, 5), readResult),
                            OnSubTreeSetup(std::bind(subTreeSetup, 5)),
                            OnSubTreeDone(std::bind(subTreeDone, 5))
                        },
                        OnSubTreeSetup(std::bind(subTreeSetup, 4)),
                        OnSubTreeDone(std::bind(subTreeDone, 4))
                    },
                    OnSubTreeSetup(std::bind(subTreeSetup, 3)),
                    OnSubTreeDone(std::bind(subTreeDone, 3))
                },
                OnSubTreeSetup(std::bind(subTreeSetup, 2)),
                OnSubTreeDone(std::bind(subTreeDone, 2))
            },
            OnSubTreeSetup(std::bind(subTreeSetup, 1)),
            OnSubTreeDone(std::bind(subTreeDone, 1))
        },
        OnSubTreeDone(topLevelTreeDone)
    };
    const Log nestedLog{{1, Handler::SubTreeSetup},
                        {2, Handler::SubTreeSetup},
                        {3, Handler::SubTreeSetup},
                        {4, Handler::SubTreeSetup},
                        {5, Handler::SubTreeSetup},
                        {5, Handler::Setup},
                        {5, Handler::Done},
                        {5, Handler::SubTreeDone},
                        {4, Handler::SubTreeDone},
                        {3, Handler::SubTreeDone},
                        {2, Handler::SubTreeDone},
                        {1, Handler::SubTreeDone},
                        {-1, Handler::SubTreeDone}};

    const Task parallelRoot {
        parallel,
        Process(std::bind(setupProcess, _1, 1), readResultAnonymous),
        Process(std::bind(setupProcess, _1, 2), readResultAnonymous),
        Process(std::bind(setupProcess, _1, 3), readResultAnonymous),
        Process(std::bind(setupProcess, _1, 4), readResultAnonymous),
        Process(std::bind(setupProcess, _1, 5), readResultAnonymous),
        OnSubTreeDone(topLevelTreeDone)
    };
    const Log parallelLog{{1, Handler::Setup}, // Setup order is determined in parallel mode
                          {2, Handler::Setup},
                          {3, Handler::Setup},
                          {4, Handler::Setup},
                          {5, Handler::Setup},
                          {-1, Handler::Done}, // Done order isn't determined in parallel mode
                          {-1, Handler::Done},
                          {-1, Handler::Done},
                          {-1, Handler::Done},
                          {-1, Handler::Done},
                          {-1, Handler::SubTreeDone}}; // Done handlers may come in different order

    const Task sequentialRoot {
        Process(std::bind(setupProcess, _1, 1), readResult),
        Process(std::bind(setupProcess, _1, 2), readResult),
        Process(std::bind(setupProcess, _1, 3), readResult),
        Process(std::bind(setupProcess, _1, 4), readResult),
        Process(std::bind(setupProcess, _1, 5), readResult),
        OnSubTreeDone(topLevelTreeDone)
    };
    const Task sequentialEncapsulatedRoot {
        Task {
            Process(std::bind(setupProcess, _1, 1), readResult)
        },
        Task {
            Process(std::bind(setupProcess, _1, 2), readResult)
        },
        Task {
            Process(std::bind(setupProcess, _1, 3), readResult)
        },
        Task {
            Process(std::bind(setupProcess, _1, 4), readResult)
        },
        Task {
            Process(std::bind(setupProcess, _1, 5), readResult)
        },
        OnSubTreeDone(topLevelTreeDone)
    };
    const Log sequentialLog{{1, Handler::Setup},
                            {1, Handler::Done},
                            {2, Handler::Setup},
                            {2, Handler::Done},
                            {3, Handler::Setup},
                            {3, Handler::Done},
                            {4, Handler::Setup},
                            {4, Handler::Done},
                            {5, Handler::Setup},
                            {5, Handler::Done},
                            {-1, Handler::SubTreeDone}};

    const Task sequentialNestedRoot {
        Task {
            Process(std::bind(setupProcess, _1, 1), readResult),
            Task {
                Process(std::bind(setupProcess, _1, 2), readResult),
                Task {
                    Process(std::bind(setupProcess, _1, 3), readResult),
                    Task {
                        Process(std::bind(setupProcess, _1, 4), readResult),
                        Task {
                            Process(std::bind(setupProcess, _1, 5), readResult),
                            OnSubTreeDone(std::bind(subTreeDone, 5))
                        },
                        OnSubTreeDone(std::bind(subTreeDone, 4))
                    },
                    OnSubTreeDone(std::bind(subTreeDone, 3))
                },
                OnSubTreeDone(std::bind(subTreeDone, 2))
            },
            OnSubTreeDone(std::bind(subTreeDone, 1))
        },
        OnSubTreeDone(topLevelTreeDone)
    };
    const Log sequentialNestedLog{{1, Handler::Setup},
                                  {1, Handler::Done},
                                  {2, Handler::Setup},
                                  {2, Handler::Done},
                                  {3, Handler::Setup},
                                  {3, Handler::Done},
                                  {4, Handler::Setup},
                                  {4, Handler::Done},
                                  {5, Handler::Setup},
                                  {5, Handler::Done},
                                  {5, Handler::SubTreeDone},
                                  {4, Handler::SubTreeDone},
                                  {3, Handler::SubTreeDone},
                                  {2, Handler::SubTreeDone},
                                  {1, Handler::SubTreeDone},
                                  {-1, Handler::SubTreeDone}};

    const Task sequentialErrorRoot {
        Process(std::bind(setupProcess, _1, 1), readResult),
        Process(std::bind(setupProcess, _1, 2), readResult),
        Process(std::bind(setupCrashProcess, _1, 3), readResult, readError),
        Process(std::bind(setupProcess, _1, 4), readResult),
        Process(std::bind(setupProcess, _1, 5), readResult),
        OnSubTreeDone(topLevelTreeDone),
        OnSubTreeError(topLevelTreeError)
    };
    const Log sequentialErrorLog{{1, Handler::Setup},
                                 {1, Handler::Done},
                                 {2, Handler::Setup},
                                 {2, Handler::Done},
                                 {3, Handler::Setup},
                                 {3, Handler::Error},
                                 {-1, Handler::SubTreeError}};

    const QList<TaskBase> simpleSequence {
        Process(std::bind(setupProcess, _1, 1), readResult),
        Process(std::bind(setupCrashProcess, _1, 2), readResult, readError),
        Process(std::bind(setupProcess, _1, 3), readResult),
        OnSubTreeDone(topLevelTreeDone),
        OnSubTreeError(topLevelTreeError)
    };

    const auto constructSimpleSequence = [=](const WorkflowPolicy &policy) {
        return Task {
            policy,
            Process(std::bind(setupProcess, _1, 1), readResult),
            Process(std::bind(setupCrashProcess, _1, 2), readResult, readError),
            Process(std::bind(setupProcess, _1, 3), readResult),
            OnSubTreeDone(topLevelTreeDone),
            OnSubTreeError(topLevelTreeError)
        };
    };

    const Task stopOnErrorRoot = constructSimpleSequence(stopOnError);
    const Log stopOnErrorLog{{1, Handler::Setup},
                             {1, Handler::Done},
                             {2, Handler::Setup},
                             {2, Handler::Error},
                             {-1, Handler::SubTreeError}};

    const Task continueOnErrorRoot = constructSimpleSequence(continueOnError);
    const Log continueOnErrorLog{{1, Handler::Setup},
                                 {1, Handler::Done},
                                 {2, Handler::Setup},
                                 {2, Handler::Error},
                                 {3, Handler::Setup},
                                 {3, Handler::Done},
                                 {-1, Handler::SubTreeError}};

    const Task stopOnDoneRoot = constructSimpleSequence(stopOnDone);
    const Log stopOnDoneLog{{1, Handler::Setup},
                            {1, Handler::Done},
                            {-1, Handler::SubTreeDone}};

    const Task continueOnDoneRoot = constructSimpleSequence(continueOnDone);
    const Log continueOnDoneLog{{1, Handler::Setup},
                                {1, Handler::Done},
                                {2, Handler::Setup},
                                {2, Handler::Error},
                                {3, Handler::Setup},
                                {3, Handler::Done},
                                {-1, Handler::SubTreeDone}};

    const Task optionalRoot {
        optional,
        Process(std::bind(setupCrashProcess, _1, 1), readResult, readError),
        Process(std::bind(setupCrashProcess, _1, 2), readResult, readError),
        OnSubTreeDone(topLevelTreeDone),
        OnSubTreeError(topLevelTreeError)
    };
    const Log optionalLog{{1, Handler::Setup},
                          {1, Handler::Error},
                          {2, Handler::Setup},
                          {2, Handler::Error},
                          {-1, Handler::SubTreeDone}};

    QTest::newRow("Empty") << emptyRoot << emptyLog << false << true;
    QTest::newRow("Nested") << nestedRoot << nestedLog << true << true;
    QTest::newRow("Parallel") << parallelRoot << parallelLog << true << true;
    QTest::newRow("Sequential") << sequentialRoot << sequentialLog << true << true;
    QTest::newRow("SequentialEncapsulated") << sequentialEncapsulatedRoot << sequentialLog << true << true;
    QTest::newRow("SequentialNested") << sequentialNestedRoot << sequentialNestedLog << true << true;
    QTest::newRow("SequentialError") << sequentialErrorRoot << sequentialErrorLog << true << false;
    QTest::newRow("StopOnError") << stopOnErrorRoot << stopOnErrorLog << true << false;
    QTest::newRow("ContinueOnError") << continueOnErrorRoot << continueOnErrorLog << true << false;
    QTest::newRow("StopOnDone") << stopOnDoneRoot << stopOnDoneLog << true << true;
    QTest::newRow("ContinueOnDone") << continueOnDoneRoot << continueOnDoneLog << true << true;
    QTest::newRow("Optional") << optionalRoot << optionalLog << true << true;
}

void tst_TaskTree::processTree()
{
    s_log = {};

    using namespace Tasking;

    QFETCH(Task, root);
    QFETCH(Log, expectedLog);
    QFETCH(bool, runningAfterStart);
    QFETCH(bool, success);

    TaskTree processTree(root);
    int doneCount = 0;
    int errorCount = 0;
    connect(&processTree, &TaskTree::done, this, [&doneCount] { ++doneCount; });
    connect(&processTree, &TaskTree::errorOccurred, this, [&errorCount] { ++errorCount; });
    processTree.start();
    QCOMPARE(processTree.isRunning(), runningAfterStart);

    QTimer timer;
    connect(&timer, &QTimer::timeout, &m_eventLoop, &QEventLoop::quit);
    timer.setInterval(1000);
    timer.setSingleShot(true);
    timer.start();
    m_eventLoop.exec();

    QVERIFY(!processTree.isRunning());
    QCOMPARE(s_log, expectedLog);

    const int expectedDoneCount = success ? 1 : 0;
    const int expectedErrorCount = success ? 0 : 1;
    QCOMPARE(doneCount, expectedDoneCount);
    QCOMPARE(errorCount, expectedErrorCount);
}

QTEST_GUILESS_MAIN(tst_TaskTree)

#include "tst_tasktree.moc"
