// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "clangtoolsunittests.h"

#include "clangtool.h"
#include "clangtoolsdiagnostic.h"
#include "clangtoolssettings.h"

#include <coreplugin/icore.h>

#include <cppeditor/clangdiagnosticconfig.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cpptoolsreuse.h>
#include <cppeditor/cpptoolstestcase.h>

#include <projectexplorer/kitinformation.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/toolchain.h>

#include <qtsupport/qtkitinformation.h>

#include <utils/environment.h>
#include <utils/executeondestruction.h>
#include <utils/fileutils.h>

#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace CppEditor;
using namespace ProjectExplorer;
using namespace Utils;

Q_DECLARE_METATYPE(CppEditor::ClangDiagnosticConfig)

namespace ClangTools::Internal {

void ClangToolsUnitTests::initTestCase()
{
    const QList<Kit *> allKits = KitManager::kits();
    if (allKits.count() == 0)
        QSKIP("This test requires at least one kit to be present");

    m_kit = findOr(allKits, nullptr, [](Kit *k) {
            return k->isValid() && QtSupport::QtKitAspect::qtVersion(k) != nullptr;
    });
    if (!m_kit)
        QSKIP("This test requires at least one valid kit with a valid Qt");

    const ToolChain *const toolchain = ToolChainKitAspect::cxxToolChain(m_kit);
    if (!toolchain)
        QSKIP("This test requires that there is a kit with a toolchain.");

    if (Core::ICore::clangExecutable(CLANG_BINDIR).isEmpty())
        QSKIP("No clang suitable for analyzing found");

    m_tmpDir = new Tests::TemporaryCopiedDir(":/clangtools/unit-tests");
    QVERIFY(m_tmpDir->isValid());
}

void ClangToolsUnitTests::cleanupTestCase()
{
    delete m_tmpDir;
}

static ClangDiagnosticConfig configFor(const QString &tidyChecks,
                                                 const QString &clazyChecks)
{
    ClangDiagnosticConfig config;
    config.setId("Test.MyTestConfig");
    config.setDisplayName("Test");
    config.setIsReadOnly(true);
    config.setClangOptions(QStringList{QStringLiteral("-Wno-everything")});
    config.setClangTidyMode(ClangDiagnosticConfig::TidyMode::UseCustomChecks);
    const QString theTidyChecks = tidyChecks.isEmpty() ? tidyChecks : "-*," + tidyChecks;
    config.setClangTidyChecks(theTidyChecks);
    config.setClazyChecks(clazyChecks);
    return config;
}

void ClangToolsUnitTests::testProject()
{
    QFETCH(QString, projectFilePath);
    QFETCH(int, expectedDiagCount);
    QFETCH(ClangDiagnosticConfig, diagnosticConfig);
    if (projectFilePath.contains("mingw")) {
        const auto toolchain = ToolChainKitAspect::cxxToolChain(m_kit);
        if (toolchain->typeId() != ProjectExplorer::Constants::MINGW_TOOLCHAIN_TYPEID)
            QSKIP("This test is mingw specific, does not run for other toolchains");
    }

    // Open project
    Tests::ProjectOpenerAndCloser projectManager;
    QVERIFY(projectManager.open(FilePath::fromString(projectFilePath), true, m_kit));

    static const int timeOut = [] {
        const int t = qtcEnvironmentVariableIntValue("QTC_CLANGTOOLS_TEST_TIMEOUT");
        return t > 0 ? t : 480000;
    }();

    // Run tool
    ClangTool *tool = ClangTool::instance();
    tool->startTool(ClangTool::FileSelectionType::AllFiles,
                    ClangToolsSettings::instance()->runSettings(),
                    diagnosticConfig);
    QSignalSpy waitForFinishedTool(tool, &ClangTool::finished);
    QVERIFY(waitForFinishedTool.wait(timeOut));

    // Check for errors
    const QString errorText = waitForFinishedTool.takeFirst().constFirst().toString();
    const bool finishedSuccessfully = errorText.isEmpty();
    if (!finishedSuccessfully)
        qWarning("Error: %s", qPrintable(errorText));
    QVERIFY(finishedSuccessfully);
    QCOMPARE(tool->diagnostics().count(), expectedDiagCount);
}

void ClangToolsUnitTests::testProject_data()
{
    QTest::addColumn<QString>("projectFilePath");
    QTest::addColumn<int>("expectedDiagCount");
    QTest::addColumn<ClangDiagnosticConfig>("diagnosticConfig");

    // Test simple C++ project.
    ClangDiagnosticConfig config = configFor("modernize-use-nullptr", QString());

    const auto addTestRow = [this](const QString &relativeFilePath,
            int expectedDiagCount,
            const ClangDiagnosticConfig &diagnosticConfig)
    {
        const FilePath absoluteFilePath = m_tmpDir->absolutePath(relativeFilePath);
        const QString fileName = absoluteFilePath.fileName();

        QTest::newRow(fileName.toUtf8().constData())
            << absoluteFilePath << expectedDiagCount << diagnosticConfig;
    };

    addTestRow("simple/simple.qbs", 1, config);
    addTestRow("simple/simple.pro", 1, config);

    // Test simple Qt project.
    config = configFor("readability-static-accessed-through-instance", QString());
    addTestRow("qt-widgets-app/qt-widgets-app.qbs", 1, config);
    addTestRow("qt-widgets-app/qt-widgets-app.pro", 1, config);

    // Test that libraries can be analyzed.
    config = configFor(QString(), QString());
    addTestRow("simple-library/simple-library.qbs", 0, config);
    addTestRow("simple-library/simple-library.pro", 0, config);

    // Test that standard headers can be parsed.
    addTestRow("stdc++11-includes/stdc++11-includes.qbs", 0, config);
    addTestRow("stdc++11-includes/stdc++11-includes.pro", 0, config);

    // Test that qt essential headers can be parsed.
    addTestRow("qt-essential-includes/qt-essential-includes.qbs", 0, config);
    addTestRow("qt-essential-includes/qt-essential-includes.pro", 0, config);

    // Test that mingw includes can be parsed.
    addTestRow("mingw-includes/mingw-includes.qbs", 0, config);
    addTestRow("mingw-includes/mingw-includes.pro", 0, config);

    // Test that tidy and clazy diagnostics are emitted for the same project.
    addTestRow("clangtidy_clazy/clangtidy_clazy.pro",
               1 /*tidy*/ + 1 /*clazy*/,
               configFor("misc-unconventional-assign-operator", "qgetenv"));
}

} // ClangTools::Internal
