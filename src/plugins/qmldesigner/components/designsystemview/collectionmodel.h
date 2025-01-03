// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <designsystem/dsconstants.h>
#include <QAbstractItemModel>

#include <optional>

namespace QmlDesigner {
class DSThemeManager;
using PropInfo = std::pair<GroupType, PropertyName>;

class CollectionModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum class Roles { GroupRole = Qt::UserRole + 1, BindingRole, ActiveThemeRole };

    CollectionModel(DSThemeManager *collection);

    // QAbstractItemModel Interface
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;
    // Add Themes
    bool insertColumns(int column, int count, const QModelIndex &parent = QModelIndex()) override;
    // Remove Themes
    bool removeColumns(int column, int count, const QModelIndex &parent = QModelIndex()) override;
    // Remove Properties
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    void updateCache();
    Q_INVOKABLE void addProperty(GroupType group,
                                 const QString &name,
                                 const QVariant &value,
                                 bool isBinding);
    // Edit property value
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    // Edit property name / Theme name
    Q_INVOKABLE bool setHeaderData(int section,
                                   Qt::Orientation orientation,
                                   const QVariant &value,
                                   int role = Qt::EditRole) override;

private:
    ThemeId findThemeId(int column) const;
    std::optional<PropInfo> findPropertyName(int row) const;

private:
    DSThemeManager *m_collection = nullptr;

    // cache
    std::vector<ThemeId> m_themeIdList;
    std::vector<PropInfo> m_propertyInfoList;
};
} // namespace QmlDesigner
