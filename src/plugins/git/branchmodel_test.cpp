// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "branchmodel_test.h"

#include "branchmodel.h"
#include "branchview.h"
#include "gitclient.h"
#include "gitsettings.h"

#include <utils/qtcprocess.h>

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QFocusEvent>
#include <QPersistentModelIndex>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

#include <memory>

using namespace Utils;

namespace Git::Internal {

static bool runGit(const FilePath &repository, const QStringList &arguments)
{
    const QStringList options{"-c", "core.hooksPath=" + (repository / "no-hooks").toFSPathString(),
                              "-c", "commit.gpgsign=false", "-c", "user.name=Test",
                              "-c", "user.email=test@example.com"};
    return gitClient().vcsSynchronousExec(repository, options + arguments).result()
           == ProcessResult::FinishedWithSuccess;
}

static bool createRepository(const FilePath &repository)
{
    return repository.ensureWritableDir() && runGit(repository, {"init"})
           && runGit(repository, {"symbolic-ref", "HEAD", "refs/heads/main"})
           && (repository / "file.txt").writeFileContents("content\n")
           && runGit(repository, {"add", "file.txt"})
           && runGit(repository, {"commit", "-m", "initial"});
}

static QModelIndex branchIndex(const BranchModel &model, const QString &name,
                              const QModelIndex &parent = {})
{
    for (int row = 0; row < model.rowCount(parent); ++row) {
        const QModelIndex index = model.index(row, 0, parent);
        if (model.isLeaf(index) && model.fullName(index) == name)
            return index;
        const QModelIndex child = branchIndex(model, name, index);
        if (child.isValid())
            return child;
    }
    return {};
}

class BranchModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void testIndexValidation();
    void testRefresh_data();
    void testRefresh();
    void testSameRepositoryRefresh();
    void testRepositorySwitch();
    void testSupersededRefresh();
    void testClearDuringRefresh();
    void testViewRepositorySwitch();

private:
    std::unique_ptr<QTemporaryDir> m_directory;
    FilePath m_repository;
};

void BranchModelTest::init()
{
    m_directory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_directory->isValid());
    m_repository = FilePath::fromString(m_directory->path()) / "first";
    QVERIFY(createRepository(m_repository));
    QVERIFY(runGit(m_repository, {"branch", "feature/nested"}));
}

void BranchModelTest::testIndexValidation()
{
    BranchModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    model.refresh(m_repository);
    QTRY_COMPARE(resets.count(), 2);
    QVERIFY(!model.index(-1, 0).isValid());
    QVERIFY(!model.index(0, -1).isValid());
    QVERIFY(!model.index(0, model.columnCount()).isValid());
    const QModelIndex dateColumn = model.index(0, 1);
    QVERIFY(dateColumn.isValid());
    QCOMPARE(model.rowCount(dateColumn), 0);
    QVERIFY(!model.index(0, 0, dateColumn).isValid());

    BranchModel other;
    const QModelIndex foreignIndex = other.index(0, 0);
    QVERIFY(foreignIndex.isValid());
    QCOMPARE(model.rowCount(foreignIndex), 0);
    QVERIFY(!model.index(0, 0, foreignIndex).isValid());
    QVERIFY(!model.parent(foreignIndex).isValid());
    QVERIFY(!model.data(foreignIndex).isValid());
    QCOMPARE(model.flags(foreignIndex), Qt::NoItemFlags);
}

void BranchModelTest::testRefresh_data()
{
    QTest::addColumn<QString>("headState");
    QTest::newRow("attached") << QString("attached");
    QTest::newRow("detached") << QString("detached");
    QTest::newRow("rebase-merge") << QString("rebase-merge");
    QTest::newRow("rebase-apply") << QString("rebase-apply");
    QTest::newRow("failed-command") << QString("invalid-repository");
}

void BranchModelTest::testRefresh()
{
    QFETCH(QString, headState);
    const bool invalidRepository = headState == "invalid-repository";
    if (!invalidRepository && headState != "attached") {
        QVERIFY(runGit(m_repository, {"checkout", "--detach", "HEAD"}));
        QVERIFY(runGit(m_repository, {"tag", "same-hash"}));
        QVERIFY(runGit(m_repository, {"update-ref", "refs/remotes/origin/same-hash", "HEAD"}));
        if (headState.startsWith("rebase-")) {
            const FilePath state = m_repository / ".git" / headState;
            QVERIFY(state.ensureWritableDir());
            QVERIFY((state / "head-name").writeFileContents("refs/heads/feature/nested\n"));
        }
    }
    const bool showTags = settings().showTags();
    const auto restoreTags = qScopeGuard([showTags] { settings().showTags.setValue(showTags); });
    settings().showTags.setValue(true);

    const FilePath repository = invalidRepository
        ? FilePath::fromString(m_directory->path()) : m_repository;
    BranchModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    model.refresh(repository);
    QCOMPARE(resets.count(), 1);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QTRY_COMPARE(resets.count(), 2);
    QCOMPARE(model.workingDirectory(), repository);
    if (invalidRepository) {
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.currentBranch().isValid());
        QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    } else {
        QVERIFY(branchIndex(model, "feature/nested").isValid());
        const QModelIndex current = model.currentBranch();
        QVERIFY(current.isValid());
        const QString expected = headState == "attached" ? QString("main")
            : headState == "detached" ? QString("HEAD") : QString("feature/nested");
        QCOMPARE(model.fullName(current), expected);
        QCOMPARE(model.isHead(current), headState == "detached");
    }
}

void BranchModelTest::testSameRepositoryRefresh()
{
    BranchModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    model.refresh(m_repository);
    QTRY_COMPARE(resets.count(), 2);
    const QPersistentModelIndex previous = branchIndex(model, "feature/nested");
    QVERIFY(previous.isValid());
    QVERIFY(runGit(m_repository, {"branch", "new-branch"}));

    resets.clear();
    model.refresh(m_repository);
    QCOMPARE(resets.count(), 0);
    QVERIFY(previous.isValid());
    QVERIFY(!branchIndex(model, "new-branch").isValid());
    QTRY_COMPARE(resets.count(), 1);
    QVERIFY(!previous.isValid());
    QVERIFY(branchIndex(model, "new-branch").isValid());
}

void BranchModelTest::testRepositorySwitch()
{
    const FilePath second = FilePath::fromString(m_directory->path()) / "second";
    QVERIFY(createRepository(second));
    QVERIFY(runGit(second, {"branch", "second/only"}));
    BranchModel model;
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester proxyTester(&proxy, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    model.refresh(m_repository);
    QTRY_COMPARE(resets.count(), 2);
    const QPersistentModelIndex sourceIndex = branchIndex(model, "feature/nested");
    const QPersistentModelIndex proxyIndex = proxy.mapFromSource(sourceIndex);
    QVERIFY(sourceIndex.isValid());
    QVERIFY(proxyIndex.isValid());

    resets.clear();
    model.refresh(second);
    QCOMPARE(resets.count(), 1);
    QCOMPARE(model.workingDirectory(), second);
    QVERIFY(!sourceIndex.isValid());
    QVERIFY(!proxyIndex.isValid());
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QTRY_COMPARE(resets.count(), 2);
    QVERIFY(branchIndex(model, "second/only").isValid());
    QVERIFY(!branchIndex(model, "feature/nested").isValid());
}

void BranchModelTest::testSupersededRefresh()
{
    const FilePath second = FilePath::fromString(m_directory->path()) / "second";
    QVERIFY(createRepository(second));
    QVERIFY(runGit(second, {"branch", "second/only"}));
    BranchModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    model.refresh(m_repository);
    model.refresh(second);
    QCOMPARE(resets.count(), 2);
    QTRY_COMPARE(resets.count(), 3);
    QCOMPARE(model.workingDirectory(), second);
    QVERIFY(branchIndex(model, "second/only").isValid());
    QVERIFY(!branchIndex(model, "feature/nested").isValid());
}

void BranchModelTest::testClearDuringRefresh()
{
    BranchModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    model.refresh(m_repository);
    QTRY_COMPARE(resets.count(), 2);
    const QPersistentModelIndex previous = model.currentBranch();
    QVERIFY(previous.isValid());

    resets.clear();
    model.refresh(m_repository);
    model.refresh({});
    QCOMPARE(resets.count(), 1);
    QVERIFY(model.workingDirectory().isEmpty());
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QVERIFY(!previous.isValid());

    // A subsequent completed refresh provides a causal boundary for canceled work.
    const FilePath emptyRepository = FilePath::fromString(m_directory->path());
    model.refresh(emptyRepository);
    QCOMPARE(resets.count(), 2);
    QTRY_COMPARE(resets.count(), 3);
    QCOMPARE(model.workingDirectory(), emptyRepository);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
}

void BranchModelTest::testViewRepositorySwitch()
{
    const FilePath second = FilePath::fromString(m_directory->path()) / "second";
    QVERIFY(createRepository(second));
    QVERIFY(runGit(second, {"branch", "second/only"}));
    BranchView view;
    view.setAttribute(Qt::WA_DontShowOnScreen);
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
    view.refresh(m_repository, true);
    QTRY_COMPARE(resets.count(), 2);
    const QPersistentModelIndex current = proxy->mapFromSource(branchIndex(*model, "feature/nested"));
    QVERIFY(current.isValid());
    tree->setCurrentIndex(current);

    resets.clear();
    view.refresh(second, true);
    QCOMPARE(resets.count(), 1);
    QVERIFY(!current.isValid());
    QFocusEvent focusIn(QEvent::FocusIn, Qt::OtherFocusReason);
    QApplication::sendEvent(tree, &focusIn);
    QTRY_COMPARE(resets.count(), 2);
    QVERIFY(branchIndex(*model, "second/only").isValid());
    QVERIFY(!branchIndex(*model, "feature/nested").isValid());
    proxy->setFilterRole(Qt::EditRole);
    proxy->setFilterRegularExpression("second/only");
    QVERIFY(proxy->mapFromSource(branchIndex(*model, "second/only")).isValid());
}

QObject *createBranchModelTest()
{
    return new BranchModelTest;
}

} // Git::Internal

#include "branchmodel_test.moc"
