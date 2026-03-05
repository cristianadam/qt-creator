// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include <QAbstractItemModel>
#include <QApplication>
#include <QHBoxLayout>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTreeView>

class FlatModel : public QAbstractItemModel
{
public:
    FlatModel() = default;

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override
    {
        if (parent.isValid() || row < 0 || row >= m_strings.size() || column != 0)
            return {};
        return createIndex(row, column);
    }

    QModelIndex parent(const QModelIndex &) const override { return {}; }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_strings.size();
    }

    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 1;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        return m_strings.at(index.row());
    }

private:
    const QStringList m_strings {
        "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    };

};

class FilterModel : public QSortFilterProxyModel
{
public:
    using Filter = std::function<bool(int)>;

    explicit FilterModel(const Filter &filter) : m_filter(filter) {}

    Filter filter() const { return m_filter; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &) const final
    {
        return m_filter(sourceRow);
    }

private:
    Filter m_filter;
};

class NestedModel : public QAbstractItemModel
{
public:
    NestedModel(QAbstractItemModel *base)
        : m_base(base)
    {
        auto filter0 = [this](int sourceRow) {
            for (int i = 1; i < m_filters.size(); ++i) {
                if (m_filters.at(i)->filter()(sourceRow))
                    return false;
            }
            return true;
        };
        addFilter("Unfiltered", filter0);
    }

    ~NestedModel() { qDeleteAll(m_filters); }

    void addFilter(const QString &title, const FilterModel::Filter &filter = {})
    {
        auto model = new FilterModel(filter);
        model->setObjectName(title);
        model->setSourceModel(m_base);
        m_filters.append(model);
    }

    // Maps a child index in the nested model to the corresponding source index.
    // Returns an invalid index for group header items.
    QModelIndex mapToSource(const QModelIndex &index) const
    {
        if (!index.isValid() || index.internalId() == quintptr(-1))
            return {};
        FilterModel *child = m_filters.at(int(index.internalId()));
        return child->mapToSource(child->index(index.row(), index.column()));
    }

    // Maps a source index to the first matching child index in the nested model.
    // Returns an invalid index if the source row appears in no filter group.
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const
    {
        if (!sourceIndex.isValid())
            return {};
        for (int i = 0; i < m_filters.size(); ++i) {
            const QModelIndex proxyIndex = m_filters.at(i)->mapFromSource(sourceIndex);
            if (proxyIndex.isValid())
                return createIndex(proxyIndex.row(), sourceIndex.column(), quintptr(i));
        }
        return {};
    }

private:
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override
    {
        if (column < 0 || column >= columnCount())
            return {};
        if (!parent.isValid()) {
            if (row < 0 || row >= m_filters.size())
                return {};
            return createIndex(row, column, quintptr(-1));
        }
        if (parent.internalId() != quintptr(-1))
            return {};
        FilterModel *child = m_filters.at(parent.row());
        if (row < 0 || row >= child->rowCount())
            return {};
        return createIndex(row, column, quintptr(parent.row()));
    }

    QModelIndex parent(const QModelIndex &index) const override
    {
        if (!index.isValid() || index.internalId() == quintptr(-1))
            return {};
        return createIndex(int(index.internalId()), 0, quintptr(-1));
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        if (!parent.isValid())
            return m_filters.count();
        if (parent.internalId() != quintptr(-1))
            return 0;
        return m_filters.at(parent.row())->rowCount();
    }

    int columnCount(const QModelIndex &parent = {}) const override
    {
        if (parent.isValid() && parent.internalId() != quintptr(-1))
            return 0;
        return m_base ? m_base->columnCount() : 0;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || role != Qt::DisplayRole)
            return {};
        if (index.internalId() == quintptr(-1))
            return m_filters.at(index.row())->objectName();
        FilterModel *child = m_filters.at(int(index.internalId()));
        return child->data(child->index(index.row(), index.column()));
    }

private:
    QAbstractItemModel * const m_base;
    QList<FilterModel *> m_filters;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    FlatModel flat;

    NestedModel nested{&flat};
    nested.addFilter("Small", [](int row) { return row < 5; });
    nested.addFilter("Eight", [](int row) { return row == 8; });

    QWidget window;
    QHBoxLayout layout(&window);

    QTreeView flatView;
    flatView.setModel(&flat);
    flatView.setWindowTitle("Flat");
    flatView.expandAll();

    QTreeView nestedView;
    nestedView.setModel(&nested);
    nestedView.setWindowTitle("Nested");
    nestedView.expandAll();

    layout.addWidget(&flatView);
    layout.addWidget(&nestedView);

    window.resize(800, 300);
    window.show();

    return app.exec();
}
