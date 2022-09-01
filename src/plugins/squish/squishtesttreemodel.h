// Copyright (C) 2022 The Qt Company Ltd
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0
#pragma once

#include <utils/treemodel.h>

#include <QSortFilterProxyModel>

namespace {
enum ItemRole { LinkRole = Qt::UserRole + 2, TypeRole, DisplayNameRole };
}

namespace Squish {
namespace Internal {

class SquishFileHandler;

class SquishTestTreeItem : public Utils::TreeItem
{
public:
    enum Type {
        Root,
        SquishSuite,
        SquishTestCase,
        SquishSharedRoot,
        SquishSharedFolder,
        SquishSharedFile,
        SquishSharedDataFolder,
        SquishSharedData
    };

    SquishTestTreeItem(const QString &displayName, Type type);
    ~SquishTestTreeItem() override {}

    Qt::ItemFlags flags(int column) const override;
    QString displayName() const { return m_displayName; }
    void setFilePath(const QString &filePath);
    QString filePath() const { return m_filePath; }
    void setParentName(const QString &parentName);
    QString parentName() const { return m_parentName; }
    Type type() const { return m_type; }
    void setCheckState(Qt::CheckState state);
    Qt::CheckState checkState() const { return m_checked; }

    bool modifyContent(const SquishTestTreeItem &other);

private:
    void revalidateCheckState();

    QString m_displayName; // holds suite or test case name
    QString m_filePath;    // holds suite.conf path for suites, test.* for test cases
    Type m_type;
    QString m_parentName;     // holds suite name for test cases, folder path for shared files
    Qt::CheckState m_checked; // suites and test cases can have a check state
    Qt::ItemFlags m_flags = Qt::NoItemFlags;
};

class SquishTestTreeModel : public Utils::TreeModel<SquishTestTreeItem>
{
public:
    SquishTestTreeModel(QObject *parent = nullptr);
    ~SquishTestTreeModel() override;

    static SquishTestTreeModel *instance();
    static const int COLUMN_COUNT = 3;

    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &idx, const QVariant &data, int role) override;
    int columnCount(const QModelIndex &idx) const override;
    void addTreeItem(SquishTestTreeItem *item);
    void removeTreeItem(int row, const QModelIndex &parent);
    void modifyTreeItem(int row, const QModelIndex &parent, const SquishTestTreeItem &modified);
    void removeAllSharedFolders();
    QStringList getSelectedSquishTestCases(const QString &suiteConfPath) const;

private:
    SquishTestTreeItem *findSuite(const QString &displayName) const;
    void onSuiteTreeItemRemoved(const QString &suiteName);
    void onSuiteTreeItemModified(SquishTestTreeItem *item, const QString &display);
    Utils::TreeItem *m_squishSharedFolders;
    Utils::TreeItem *m_squishSuitesRoot;
    SquishFileHandler *m_squishFileHandler;
};

class SquishTestTreeSortModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    SquishTestTreeSortModel(SquishTestTreeModel *sourceModel, QObject *parent = nullptr);
    Utils::TreeItem *itemFromIndex(const QModelIndex &idx) const;

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

} // namespace Internal
} // namespace Squish
