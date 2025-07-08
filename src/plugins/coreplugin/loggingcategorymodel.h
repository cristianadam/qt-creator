// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"
#include "loggingcategoryregistry.h"

#include <utils/result.h>

#include <QAbstractListModel>
#include <QLoggingCategory>

namespace Core::Internal {

QColor colorForCategory(const QString &category);
void setCategoryColor(const QString &category, const QColor &color);

struct SavedEntry
{
    QColor color;
    QString name;
    QtMsgType level{QtFatalMsg};
    std::optional<std::array<bool, 5>> levels;

    static Utils::Result<SavedEntry> fromJson(const QJsonObject &obj);
};

class CORE_EXPORT LoggingCategoryEntry
{
public:
    LoggingCategoryEntry(const QString &name);
    LoggingCategoryEntry(QLoggingCategory *category);

    LoggingCategoryEntry(const SavedEntry &savedEntry);

    QString name() const;
    QColor color() const;
    void setColor(const QColor &c);

    void setUseOriginal(bool useOriginal);

    bool isEnabled(QtMsgType msgType) const;

    bool isEnabledOriginally(QtMsgType msgType) const;

    void setEnabled(QtMsgType msgType, bool isEnabled);

    void setSaved(const SavedEntry &entry);

    void setLogCategory(QLoggingCategory *category);

    bool isDebugEnabled() const { return isEnabled(QtDebugMsg); }
    bool isWarningEnabled() const { return isEnabled(QtWarningMsg); }
    bool isCriticalEnabled() const { return isEnabled(QtCriticalMsg); }
    bool isInfoEnabled() const { return isEnabled(QtInfoMsg); }

private:
    QString m_name;
    QLoggingCategory *m_category{nullptr};
    std::optional<std::array<bool, 5>> m_originalSettings;
    std::optional<std::array<bool, 5>> m_saved;
    QColor m_color;
    bool m_useOriginal{false};
};

class CORE_EXPORT LoggingCategoryModel : public QAbstractListModel
{
    Q_OBJECT
public:
    LoggingCategoryModel(LoggingCategoryRegistry* registry, QObject *parent);;

    ~LoggingCategoryModel() override;
    enum Column { Color, Name, Debug, Warning, Critical, Fatal, Info };

    enum Role { OriginalStateRole = Qt::UserRole + 1 };

    void append(const LoggingCategoryEntry &entry);
    int columnCount(const QModelIndex &) const final;
    int rowCount(const QModelIndex & = QModelIndex()) const final;
    QVariant data(const QModelIndex &index, int role) const final;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) final;
    Qt::ItemFlags flags(const QModelIndex &index) const final;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const final;

    void saveEnabledCategoryPreset() const;
    void loadAndUpdateFromPreset();

    void setUseOriginal(bool useOriginal);

    const QList<LoggingCategoryEntry> &categories() const { return m_categories; }

private:
    LoggingCategoryRegistry *m_registry;
    QList<LoggingCategoryEntry> m_categories;
    bool m_useOriginal{false};
};

} // namespace Core::Internal
