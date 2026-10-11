// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <git/branchmodel_test.h>

#include <git/branchmodel.h>
#include <git/branchview.h>
#include <git/gitclient.h>

#include <utils/qtcprocess.h>

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTest>
#include <QTreeView>

using namespace Utils;

namespace Git::Internal {

class BranchModelStressTest final : public QObject
{
    Q_OBJECT

private slots:
    void testRefreshStress();
};

void BranchModelStressTest::testRefreshStress()
{
    const FilePath repository = FilePath::fromUserInput(
        qEnvironmentVariable("QTC_GIT_BRANCH_STRESS_REPOSITORY"));
    const QString prefix = qEnvironmentVariable("QTC_GIT_BRANCH_STRESS_REF_PREFIX");
    bool iterationsOk = false;
    const int iterations = qEnvironmentVariableIntValue("QTC_GIT_BRANCH_STRESS_ITERATIONS",
                                                       &iterationsOk);
    QVERIFY2(iterationsOk && iterations > 0, "Specify a positive stress iteration count.");
    QVERIFY2(prefix.startsWith("refs/heads/qtc-branch-stress-"),
             "Run this test through branchmodel-refresh-stress.sh.");
    const auto runGit = [&repository](const QStringList &arguments) {
        return gitClient().vcsSynchronousExec(repository, arguments).result()
               == ProcessResult::FinishedWithSuccess;
    };
    QVERIFY(runGit({"rev-parse", "--verify", "HEAD"}));
    constexpr int slotCount = 8;
    const auto cleanup = qScopeGuard([&] {
        for (int slot = 0; slot < slotCount; ++slot)
            runGit({"update-ref", "-d", prefix + '/' + QString::number(slot)});
    });

    BranchView view;
    view.setAttribute(Qt::WA_DontShowOnScreen,
                      !qEnvironmentVariableIsSet("QTC_GIT_BRANCH_STRESS_VISIBLE"));
    view.show();
    QTreeView *tree = view.findChild<QTreeView *>();
    QVERIFY(tree);
    auto proxy = qobject_cast<QSortFilterProxyModel *>(tree->model());
    QVERIFY(proxy);
    auto model = static_cast<BranchModel *>(proxy->sourceModel());
    QVERIFY(model);
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester proxyTester(proxy, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    view.refresh(repository, true);
    QTRY_COMPARE(resets.count(), 2);
    const QStringList originalBranches = model->localBranchNames();

    const auto exerciseView = [tree, proxy] {
        tree->expandAll();
        const QModelIndex current = proxy->index(0, 0, proxy->index(0, 0));
        tree->setCurrentIndex(current);
        QFocusEvent focusIn(QEvent::FocusIn, Qt::OtherFocusReason);
        QApplication::sendEvent(tree, &focusIn);
        const QPoint position = tree->visualRect(current).center();
        QMouseEvent mouseMove(QEvent::MouseMove, position,
                              tree->viewport()->mapToGlobal(position),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &mouseMove);
    };
    for (int iteration = 0; iteration < iterations; ++iteration) {
        {
            BranchModel pending;
            pending.refresh(repository);
        }
        const QString ref = prefix + '/' + QString::number(iteration % slotCount);
        exerciseView();
        QVERIFY(runGit({"update-ref", ref, "HEAD"}));
        view.refresh(repository, true);
        exerciseView();
        QVERIFY(runGit({"update-ref", "-d", ref}));
        const int previousResets = resets.count();
        view.refresh(repository, true);
        exerciseView();
        QTRY_COMPARE(resets.count(), previousResets + 1);
        QCOMPARE(model->localBranchNames(), originalBranches);
    }
}

QObject *createBranchModelStressTest()
{
    return new BranchModelStressTest;
}

} // Git::Internal

#include "branchmodelstress.moc"
