// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/qtcprocess.h>

#include <QTest>
#include <QUuid>

using namespace Utils;

class tst_GitBranchModel final : public QObject
{
    Q_OBJECT

private slots:
    void regressionTests();
};

void tst_GitBranchModel::regressionTests()
{
    Environment environment = Environment::systemEnvironment();
    environment.set("QT_QPA_PLATFORM", "offscreen");
    environment.unset("QTC_KEEP_OPEN_AFTER_TEST");
    environment.unset("QTC_GIT_BRANCH_STRESS_REPOSITORY");
    const QString clientId = "qtc-gitbranchmodel-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    Process process;
    process.setEnvironment(environment);
    process.setCommand({FilePath::fromString(QString::fromUtf8(QT_CREATOR_BINARY_PATH)),
                        {"-clientid", clientId, "-tcs", "-test", "Git,BranchModelTest"}});
    process.runBlocking(std::chrono::seconds(45));
    const QString output = process.allOutput();
    QVERIFY2(process.result() == ProcessResult::FinishedWithSuccess, qPrintable(output));
    // Qt Creator can exit successfully when no tests match or plugin loading fails.
    QVERIFY2(output.contains("BranchModelTest::testViewRepositorySwitch()"), qPrintable(output));
    QVERIFY2(output.contains("Finished testing of Git::Internal::BranchModelTest"), qPrintable(output));
}

QTEST_GUILESS_MAIN(tst_GitBranchModel)

#include "tst_gitbranchmodel.moc"
