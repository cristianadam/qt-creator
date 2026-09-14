// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "autotestunittests.h"

#include "testcodeparser.h"
#include "testtreemodel.h"

#include "qtest/qttestframework.h"
#include "qtest/qttestparser.h"

#include <cppeditor/cpptoolstestcase.h>
#include <cppeditor/projectinfo.h>

#include <projectexplorer/kitmanager.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/toolchainkitaspect.h>

#include <qtsupport/qtkitaspect.h>

#include <utils/algorithm.h>
#include <utils/environment.h>

#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace Autotest::Internal {

class AutotestUnitTests : public QObject
{
    Q_OBJECT

public:
    AutotestUnitTests()
        : m_model(TestTreeModel::instance())
    {}

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testCodeParser();
    void testCodeParser_data();
    void testCodeParserSwitchStartup();
    void testCodeParserSwitchStartup_data();
    void testCodeParserGTest();
    void testCodeParserGTest_data();
    void testCodeParserBoostTest();
    void testCodeParserBoostTest_data();

private:
    TestTreeModel *m_model = nullptr;
    CppEditor::Tests::TemporaryCopiedDir *m_tmpDir = nullptr;
    bool m_isQt4 = false;
    bool m_checkBoost = false;
    ProjectExplorer::Kit *m_kit = nullptr;
};


void AutotestUnitTests::initTestCase()
{
    const QList<Kit *> allKits = KitManager::kits();
    if (allKits.count() == 0)
        QSKIP("This test requires at least one kit to be present");

    m_kit = findOr(allKits, nullptr, [](Kit *k) {
            return k->isValid() && QtSupport::QtKitAspect::qtVersion(k) != nullptr;
    });
    if (!m_kit)
        QSKIP("The test requires at least one valid kit with a valid Qt");

    if (auto qtVersion = QtSupport::QtKitAspect::qtVersion(m_kit))
        m_isQt4 = qtVersion->qtVersionString().startsWith('4');
    else
        QSKIP("Could not figure out which Qt version is used for default kit.");
    const Toolchain * const toolchain = ToolchainKitAspect::cxxToolchain(m_kit);
    if (!toolchain)
        QSKIP("This test requires that there is a kit with a toolchain.");

    m_tmpDir = new CppEditor::Tests::TemporaryCopiedDir(":/unit_test");

    if (!qtcEnvironmentVariableIsEmpty("BOOST_INCLUDE_DIR")) {
        m_checkBoost = true;
    } else {
        if (HostOsInfo::isLinuxHost()
                && (QFileInfo::exists("/usr/include/boost/version.hpp")
                    || QFileInfo::exists("/usr/local/include/boost/version.hpp"))) {
            qDebug() << "Found boost at system level - will run boost parser test.";
            m_checkBoost = true;
        }
    }

    // Enable quick check for derived tests
    theQtTestFramework().quickCheckForDerivedTests.setValue(true);
}

void AutotestUnitTests::cleanupTestCase()
{
    delete m_tmpDir;
}

void AutotestUnitTests::testCodeParser()
{
    QFETCH(FilePath, projectFilePath);
    QFETCH(int, expectedAutoTestsCount);
    QFETCH(int, expectedNamedQuickTestsCount);
    QFETCH(int, expectedUnnamedQuickTestsCount);
    QFETCH(int, expectedDataTagsCount);

    CppEditor::Tests::ProjectOpenerAndCloser projectManager;
    QVERIFY(projectManager.open(projectFilePath, m_kit));

    QSignalSpy parserSpy(m_model->parser(), &TestCodeParser::parsingFinished);
    QSignalSpy modelUpdateSpy(m_model, &TestTreeModel::sweepingDone);
    QVERIFY(parserSpy.wait(20000));
    QVERIFY(modelUpdateSpy.wait());

    if (m_isQt4)
        expectedNamedQuickTestsCount = expectedUnnamedQuickTestsCount = 0;

    QCOMPARE(m_model->autoTestsCount(), expectedAutoTestsCount);
    QCOMPARE(m_model->namedQuickTestsCount(), expectedNamedQuickTestsCount);
    QCOMPARE(m_model->unnamedQuickTestsCount(), expectedUnnamedQuickTestsCount);
    QCOMPARE(m_model->dataTagsCount(), expectedDataTagsCount);
}

void AutotestUnitTests::testCodeParser_data()
{
    QTest::addColumn<FilePath>("projectFilePath");
    QTest::addColumn<int>("expectedAutoTestsCount");
    QTest::addColumn<int>("expectedNamedQuickTestsCount");
    QTest::addColumn<int>("expectedUnnamedQuickTestsCount");
    QTest::addColumn<int>("expectedDataTagsCount");

    const FilePath base = m_tmpDir->filePath();
    QTest::newRow("plainAutoTest")
            << base / "plain/plain.pro"
            << 1 << 0 << 0 << 0;
    QTest::newRow("mixedAutoTestAndQuickTests")
            << base / "mixed_atp/mixed_atp.pro"
            << 4 << 10 << 5 << 10;
    // the test is declared in a library the application target links against
    QTest::newRow("libraryAutoTest")
            << base / "lib_atp/lib_atp.pro"
            << 1 << 0 << 0 << 0;
    QTest::newRow("plainAutoTestQbs")
            << base / "plain/plain.qbs"
            << 1 << 0 << 0 << 0;
    QTest::newRow("mixedAutoTestAndQuickTestsQbs")
            << base / "mixed_atp/mixed_atp.qbs"
            << 4 << 10 << 5 << 10;
    QTest::newRow("libraryAutoTestQbs")
            << base / "lib_atp/lib_atp.qbs"
            << 1 << 0 << 0 << 0;
}

void AutotestUnitTests::testCodeParserSwitchStartup()
{
    QFETCH(FilePaths, projectFilePaths);
    QFETCH(QList<int>, expectedAutoTestsCount);
    QFETCH(QList<int>, expectedNamedQuickTestsCount);
    QFETCH(QList<int>, expectedUnnamedQuickTestsCount);
    QFETCH(QList<int>, expectedDataTagsCount);

    CppEditor::Tests::ProjectOpenerAndCloser projectManager;
    for (int i = 0; i < projectFilePaths.size(); ++i) {
        qDebug() << "Opening project" << projectFilePaths.at(i);
        QVERIFY(projectManager.open(projectFilePaths.at(i), m_kit));

        QSignalSpy parserSpy(m_model->parser(), &TestCodeParser::parsingFinished);
        QSignalSpy modelUpdateSpy(m_model, &TestTreeModel::sweepingDone);
        QVERIFY(parserSpy.wait(20000));
        QVERIFY(modelUpdateSpy.wait());

        QCOMPARE(m_model->autoTestsCount(), expectedAutoTestsCount.at(i));
        QCOMPARE(m_model->namedQuickTestsCount(),
                 m_isQt4 ? 0 : expectedNamedQuickTestsCount.at(i));
        QCOMPARE(m_model->unnamedQuickTestsCount(),
                 m_isQt4 ? 0 : expectedUnnamedQuickTestsCount.at(i));
        QCOMPARE(m_model->dataTagsCount(),
                 expectedDataTagsCount.at(i));
    }
}

void AutotestUnitTests::testCodeParserSwitchStartup_data()
{
    QTest::addColumn<FilePaths>("projectFilePaths");
    QTest::addColumn<QList<int> >("expectedAutoTestsCount");
    QTest::addColumn<QList<int> >("expectedNamedQuickTestsCount");
    QTest::addColumn<QList<int> >("expectedUnnamedQuickTestsCount");
    QTest::addColumn<QList<int> >("expectedDataTagsCount");

    const FilePath base = m_tmpDir->filePath();
    FilePaths projects {
        base / "plain/plain.pro",
        base / "mixed_atp/mixed_atp.pro",
        base / "plain/plain.qbs",
        base / "mixed_atp/mixed_atp.qbs"
    };

    QList<int> expectedAutoTests = QList<int>()         << 1 << 4 << 1 << 4;
    QList<int> expectedNamedQuickTests = QList<int>()   << 0 << 10 << 0 << 10;
    QList<int> expectedUnnamedQuickTests = QList<int>() << 0 << 5 << 0 << 5;
    QList<int> expectedDataTagsCount = QList<int>()     << 0 << 10 << 0 << 10;

    QTest::newRow("loadMultipleProjects")
            << projects << expectedAutoTests << expectedNamedQuickTests
            << expectedUnnamedQuickTests << expectedDataTagsCount;
}

void AutotestUnitTests::testCodeParserGTest()
{
    if (qtcEnvironmentVariableIsEmpty("GOOGLETEST_DIR")) {
        const QString qcSource = QString(QTCREATORDIR);
        const FilePath gtestSrc = FilePath::fromUserInput(qcSource)
                                      .pathAppended("src/libs/3rdparty/googletest");
        if (gtestSrc.exists()) {
            qDebug() << "Trying to use googletest submodule in" << gtestSrc.toUserOutput() << ".";
            Environment::modifySystemEnvironment({EnvironmentItem{"GOOGLETEST_DIR",
                                                                  gtestSrc.toUserOutput()}});
        } else {
            QSKIP("This test needs googletest - set GOOGLETEST_DIR (point to googletest repository)");
        }
    }

    QFETCH(FilePath, projectFilePath);
    CppEditor::Tests::ProjectOpenerAndCloser projectManager;
    QVERIFY(projectManager.open(projectFilePath, m_kit));

    QSignalSpy parserSpy(m_model->parser(), &TestCodeParser::parsingFinished);
    QSignalSpy modelUpdateSpy(m_model, &TestTreeModel::sweepingDone);
    QVERIFY(parserSpy.wait(20000));
    QVERIFY(modelUpdateSpy.wait());

    QCOMPARE(m_model->gtestNamesCount(), 9);

    QMultiMap<QString, int> expectedNamesAndSets;
    expectedNamesAndSets.insert(QStringLiteral("FactorialTest"), 3);
    expectedNamesAndSets.insert(QStringLiteral("FactorialTest_Iterative"), 2);
    expectedNamesAndSets.insert(QStringLiteral("Sum"), 2);
    expectedNamesAndSets.insert(QStringLiteral("QueueTest"), 2);
    expectedNamesAndSets.insert(QStringLiteral("DummyTest"), 1); // used as parameterized test
    expectedNamesAndSets.insert(QStringLiteral("DummyTest"), 1); // used as 'normal' test
    expectedNamesAndSets.insert(QStringLiteral("NumberAsNameStart"), 1);
    expectedNamesAndSets.insert(QStringLiteral("NamespaceTest"), 1);
    expectedNamesAndSets.insert(QStringLiteral("InLibTest"), 2); // in a linked library

    QMultiMap<QString, int> foundNamesAndSets = m_model->gtestNamesAndSets();
    QCOMPARE(expectedNamesAndSets.size(), foundNamesAndSets.size());
    for (const QString &name : expectedNamesAndSets.keys())
        QCOMPARE(expectedNamesAndSets.values(name), foundNamesAndSets.values(name));

    // check also that no Qt related tests have been found
    QCOMPARE(m_model->autoTestsCount(), 0);
    QCOMPARE(m_model->namedQuickTestsCount(), 0);
    QCOMPARE(m_model->unnamedQuickTestsCount(), 0);
    QCOMPARE(m_model->dataTagsCount(), 0);
    QCOMPARE(m_model->boostTestNamesCount(), 0);
}

void AutotestUnitTests::testCodeParserGTest_data()
{
    QTest::addColumn<FilePath>("projectFilePath");
    QTest::newRow("simpleGoogletest")
        << m_tmpDir->filePath() / "simple_gt/simple_gt.pro";
    QTest::newRow("simpleGoogletestQbs")
        << m_tmpDir->filePath() / "simple_gt/simple_gt.qbs";
}

void AutotestUnitTests::testCodeParserBoostTest()
{
    if (!m_checkBoost)
        QSKIP("This test needs boost - set BOOST_INCLUDE_DIR (or have it installed)");

    QFETCH(FilePath, projectFilePath);
    QFETCH(QString, extension);
    CppEditor::Tests::ProjectOpenerAndCloser projectManager;
    const CppEditor::ProjectInfo::ConstPtr projectInfo
            = projectManager.open(projectFilePath, m_kit);
    QVERIFY(projectInfo);

    QSignalSpy parserSpy(m_model->parser(), &TestCodeParser::parsingFinished);
    QSignalSpy modelUpdateSpy(m_model, &TestTreeModel::sweepingDone);
    QVERIFY(parserSpy.wait(20000));
    QVERIFY(modelUpdateSpy.wait());

    QCOMPARE(m_model->boostTestNamesCount(), 5);

    const FilePath basePath = projectInfo->projectRoot();
    QVERIFY(!basePath.isEmpty());

    QMap<QString, int> expectedSuitesAndTests;

    auto pathConstructor = [basePath, extension](const QString &name, const QString &subPath) {
        return QString(name + '|' + basePath.pathAppended(subPath + extension).toUrlishString());
    };
    expectedSuitesAndTests.insert(pathConstructor("Master Test Suite", "tests/deco/deco"), 2); // decorators w/o suite
    expectedSuitesAndTests.insert(pathConstructor("Master Test Suite", "tests/fix/fix"), 2); // fixtures
    expectedSuitesAndTests.insert(pathConstructor("Master Test Suite", "tests/params/params"), 3); // functions
    expectedSuitesAndTests.insert(pathConstructor("Suite1", "tests/deco/deco"), 4);
    expectedSuitesAndTests.insert(pathConstructor("SuiteOuter", "tests/deco/deco"), 5); // 2 sub suites + 3 tests

    QMap<QString, int> foundNamesAndSets = m_model->boostTestSuitesAndTests();
    QCOMPARE(expectedSuitesAndTests.size(), foundNamesAndSets.size());
    for (auto it = expectedSuitesAndTests.cbegin(); it != expectedSuitesAndTests.cend(); ++it)
        QCOMPARE(*it, foundNamesAndSets.value(it.key()));

    // check also that no Qt related tests have been found
    QCOMPARE(m_model->autoTestsCount(), 0);
    QCOMPARE(m_model->namedQuickTestsCount(), 0);
    QCOMPARE(m_model->unnamedQuickTestsCount(), 0);
    QCOMPARE(m_model->dataTagsCount(), 0);
    QCOMPARE(m_model->gtestNamesCount(), 0);
}

void AutotestUnitTests::testCodeParserBoostTest_data()
{
    QTest::addColumn<FilePath>("projectFilePath");
    QTest::addColumn<QString>("extension");
    QTest::newRow("simpleBoostTest")
        << m_tmpDir->filePath() / "simple_boost/simple_boost.pro" << QString(".pro");
    QTest::newRow("simpleBoostTestQbs")
        << m_tmpDir->filePath() / "simple_boost/simple_boost.qbs" << QString(".qbs");
}

// Apart from the suite above, which wants a kit, a Qt and a toolchain before
// it will run anything: what a text says is none of their business, and a
// QSKIP in that suite's initTestCase() would take these rows with it.
class QtTestParserTest final : public QObject
{
    Q_OBJECT

private slots:
    void testMainsWrittenIn();
    void testMainsWrittenIn_data();
};

void QtTestParserTest::testMainsWrittenIn_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QStringList>("expected");

    // The underscore is the point of this row: the pattern used to ask for
    // alphanumerics only, so the name a Qt test is conventionally written
    // under went unmatched and the file was left out of the tree.
    QTest::newRow("the macro and what it is handed")
        << "QTEST_MAIN(tst_Simple)\n"
        << QStringList{"tst_Simple"};

    QTest::newRow("a name with no underscore in it")
        << "QTEST_MAIN(MyTest)\n"
        << QStringList{"MyTest"};

    QTest::newRow("the appless and guiless ones too")
        << "QTEST_APPLESS_MAIN(tst_One)\n"
           "QTEST_GUILESS_MAIN(tst_Two)\n"
        << QStringList{"tst_One", "tst_Two"};

    QTest::newRow("space around what it is handed")
        << "QTEST_MAIN (  tst_Spaced  )\n"
        << QStringList{"tst_Spaced"};

    // A macro nobody runs, and the reason the comments have to be read at all.
    QTest::newRow("one commented out with a line comment")
        << "// QTEST_MAIN(tst_Disabled)\n"
           "QTEST_MAIN(tst_Real)\n"
        << QStringList{"tst_Real"};

    QTest::newRow("one commented out with a block comment")
        << "/* QTEST_MAIN(tst_Disabled) */\n"
           "QTEST_MAIN(tst_Real)\n"
        << QStringList{"tst_Real"};

    QTest::newRow("one inside a block comment of several lines")
        << "/*\n"
           " * QTEST_MAIN(tst_Disabled)\n"
           " */\n"
        << QStringList{};

    // What reading the file's own text changed: this used to be searched for
    // in the preprocessed source, where a branch that is not built is gone,
    // so the class went unnamed and the file was left out of the tree.
    QTest::newRow("one in a branch that is not built")
        << "#if 0\n"
           "QTEST_MAIN(tst_NotBuilt)\n"
           "#endif\n"
        << QStringList{"tst_NotBuilt"};

    QTest::newRow("one in an ifdef nobody defined")
        << "#ifdef NEVER_DEFINED\n"
           "QTEST_MAIN(tst_Conditional)\n"
           "#endif\n"
        << QStringList{"tst_Conditional"};

    QTest::newRow("no macro at all")
        << "int main() { return 0; }\n"
        << QStringList{};

    // A macro standing in for one of these names no class: "C" is its
    // parameter. The preprocessed source had no #define lines left to match,
    // so reading the text as written is what makes this reachable at all.
    QTest::newRow("a macro written to stand in for one")
        << "#define APP_TEST_MAIN(C) QTEST_MAIN(C)\n"
           "APP_TEST_MAIN(tst_Real)\n"
        << QStringList{};

    QTest::newRow("a define with space after the hash")
        << "#  define APP_TEST_MAIN(C) QTEST_MAIN(C)\n"
        << QStringList{};

    // The name has to be one word, which is what the pattern asks for.
    QTest::newRow("a qualified name is not matched")
        << "QTEST_MAIN(ns::tst_Scoped)\n"
        << QStringList{};
}

void QtTestParserTest::testMainsWrittenIn()
{
    QFETCH(QString, source);
    QFETCH(QStringList, expected);

    const TestCases cases = mainsWrittenIn(source);
    QCOMPARE(Utils::transform(cases, &TestCase::name), expected);

    // Where a file names more than one, none of them is the only test its
    // executable runs, which is what the qExec() reading says too.
    const bool several = cases.size() > 1;
    for (const TestCase &testCase : cases)
        QCOMPARE(testCase.multipleTestCases, several);
}

QObject *createAutotestUnitTests()
{
    return new AutotestUnitTests;
}

QObject *createQtTestParserTest()
{
    return new QtTestParserTest;
}

} // namespace Autotest::Internal

#include "autotestunittests.moc"
