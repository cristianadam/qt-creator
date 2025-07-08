// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "loggingcategorymodel.h"

#include "coreicons.h"
#include "coreplugintr.h"
#include "icore.h"

#include <utils/async.h>
#include <utils/basetreeview.h>
#include <utils/fancylineedit.h>
#include <utils/fileutils.h>
#include <utils/layoutbuilder.h>
#include <utils/listmodel.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Utils;

namespace Core::Internal {

QHash<QString, QColor> s_categoryColor;

QColor colorForCategory(const QString &category)
{
    auto entry = s_categoryColor.find(category);
    if (entry == s_categoryColor.end())
        return Utils::creatorTheme()->palette().text().color();
    return entry.value();
}

void setCategoryColor(const QString &category, const QColor &color)
{
    const QColor baseColor = Utils::creatorTheme()->palette().text().color();
    if (color != baseColor)
        s_categoryColor.insert(category, color);
    else
        s_categoryColor.remove(category);
}

LoggingCategoryModel::LoggingCategoryModel(LoggingCategoryRegistry *registry, QObject *parent)
    : QAbstractListModel(parent), m_registry(registry)
{
    auto newCategory = [this](QLoggingCategory *category) {
        QString name = QString::fromUtf8(category->categoryName());
        auto itExists = std::find_if(m_categories.begin(),
                                     m_categories.end(),
                                     [name](const auto &cat) { return name == cat.name(); });

        if (itExists != m_categories.end()) {
            itExists->setLogCategory(category);
        } else {
            LoggingCategoryEntry entry(category);
            append(entry);
        }
    };

    for (QLoggingCategory *cat : m_registry->categories())
        newCategory(cat);

    connect(m_registry,
            &LoggingCategoryRegistry::newLogCategory,
            this,
            newCategory);

    m_registry->start();
}

LoggingCategoryModel::~LoggingCategoryModel() {}

void LoggingCategoryModel::append(const LoggingCategoryEntry &entry)
{
    beginInsertRows(QModelIndex(), m_categories.size(), m_categories.size() + 1);
    m_categories.push_back(entry);
    endInsertRows();
}

int LoggingCategoryModel::columnCount(const QModelIndex &) const { return 7; }

int LoggingCategoryModel::rowCount(const QModelIndex &) const { return m_categories.size(); }

QVariant LoggingCategoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    if (index.column() == Column::Name && role == Qt::DisplayRole) {
        return m_categories.at(index.row()).name();
    } else if (role == Qt::DecorationRole && index.column() == Column::Color) {
        const QColor color = m_categories.at(index.row()).color();
        if (color.isValid())
            return color;

        static const QColor defaultColor = Utils::creatorTheme()->palette().text().color();
        return defaultColor;
    } else if (index.column() >= Column::Debug && index.column() <= Column::Info) {
        if (role == Qt::CheckStateRole) {
            const LoggingCategoryEntry &entry = m_categories.at(index.row());
            const bool isEnabled = entry.isEnabled(
                static_cast<QtMsgType>(index.column() - Column::Debug));
            return isEnabled ? Qt::Checked : Qt::Unchecked;
        } else if (role == OriginalStateRole) {
            const LoggingCategoryEntry &entry = m_categories.at(index.row());
            return entry.isEnabledOriginally(static_cast<QtMsgType>(index.column() - Column::Debug))
                       ? Qt::Checked
                       : Qt::Unchecked;
        }
    }
    return {};
}

bool LoggingCategoryModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    if (role == Qt::CheckStateRole && index.column() >= Column::Debug
        && index.column() <= Column::Info) {
        QtMsgType msgType = static_cast<QtMsgType>(index.column() - Column::Debug);
        auto &entry = m_categories[index.row()];
        bool isEnabled = entry.isEnabled(msgType);

        const Qt::CheckState current = isEnabled ? Qt::Checked : Qt::Unchecked;

        if (current != value.toInt()) {
            entry.setEnabled(msgType, value.toInt() == Qt::Checked);
            m_registry->updateCategory(entry);
            return true;
        }
    } else if (role == Qt::DecorationRole && index.column() == Column::Color) {
        auto &category = m_categories[index.row()];
        QColor currentColor = category.color();
        QColor color = value.value<QColor>();
        if (color.isValid() && color != currentColor) {
            category.setColor(color);
            setCategoryColor(category.name(), color);
            emit dataChanged(index, index, {Qt::DisplayRole});
            return true;
        }
    }

    return false;
}

Qt::ItemFlags LoggingCategoryModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    if (index.column() == LoggingCategoryModel::Column::Fatal)
        return Qt::NoItemFlags;

    if (index.column() == Column::Name || index.column() == Column::Color)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    if (m_useOriginal)
        return Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}

QVariant LoggingCategoryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section >= 0 && section < 8) {
        switch (section) {
        case Column::Name:
            return Tr::tr("Category");
        case Column::Color:
            return Tr::tr("Color");
        case Column::Debug:
            return Tr::tr("Debug");
        case Column::Warning:
            return Tr::tr("Warning");
        case Column::Critical:
            return Tr::tr("Critical");
        case Column::Fatal:
            return Tr::tr("Fatal");
        case Column::Info:
            return Tr::tr("Info");
        }
    }
    return {};
}

void LoggingCategoryModel::setUseOriginal(bool useOriginal)
{
    if (useOriginal != m_useOriginal) {
        beginResetModel();
        for (auto &entry : m_categories)
            entry.setUseOriginal(useOriginal);

        m_useOriginal = useOriginal;
        endResetModel();
    }
}

Result<SavedEntry> SavedEntry::fromJson(const QJsonObject &obj)
{
    if (!obj.contains("name"))
        return ResultError(Tr::tr("Entry is missing a logging category name."));

    SavedEntry result;
    result.name = obj.value("name").toString();

    if (!obj.contains("entry"))
        return ResultError(Tr::tr("Entry is missing data."));

    auto entry = obj.value("entry").toObject();
    if (entry.contains("color"))
        result.color = QColor(entry.value("color").toString());

    if (entry.contains("level")) {
        int lvl = entry.value("level").toInt(0);
        if (lvl < QtDebugMsg || lvl > QtInfoMsg)
            return ResultError(Tr::tr("Invalid level: %1").arg(lvl));
        result.level = static_cast<QtMsgType>(lvl);
    }

    if (entry.contains("levels")) {
        QVariantMap map = entry.value("levels").toVariant().toMap();
        std::array<bool, 5> levels{
            map.contains("Debug") && map["Debug"].toBool(),
            map.contains("Warning") && map["Warning"].toBool(),
            map.contains("Critical") && map["Critical"].toBool(),
            true,
            map.contains("Info") && map["Info"].toBool(),
        };
        result.levels = levels;
    }

    return result;
}

LoggingCategoryEntry::LoggingCategoryEntry(const QString &name)
    : m_name(name)
{}

LoggingCategoryEntry::LoggingCategoryEntry(QLoggingCategory *category)
    : m_name(QString::fromUtf8(category->categoryName()))
{
    setLogCategory(category);
}

LoggingCategoryEntry::LoggingCategoryEntry(const SavedEntry &savedEntry)
    : m_name(savedEntry.name)
{
    m_saved = savedEntry.levels;
    m_color = savedEntry.color;
    if (!m_saved) {
        m_saved = std::array<bool, 5>();
        for (int i = QtDebugMsg; i <= QtInfoMsg; ++i) {
            (*m_saved)[i] = savedEntry.level <= i;
        }
    }
}

QString LoggingCategoryEntry::name() const { return m_name; }

QColor LoggingCategoryEntry::color() const { return m_color; }

void LoggingCategoryEntry::setColor(const QColor &c) { m_color = c; }

void LoggingCategoryEntry::setUseOriginal(bool useOriginal)
{
    if (!m_useOriginal && m_category && m_originalSettings) {
        m_saved = std::array<bool, 5>{};

        for (int i = QtDebugMsg; i < QtInfoMsg; i++) {
            (*m_saved)[i] = m_category->isEnabled(static_cast<QtMsgType>(i));
            m_category->setEnabled(static_cast<QtMsgType>(i), (*m_originalSettings)[i]);
        }

    } else if (!useOriginal && m_useOriginal && m_saved && m_category) {
        for (int i = QtDebugMsg; i < QtInfoMsg; i++)
            m_category->setEnabled(static_cast<QtMsgType>(i), (*m_saved)[i]);
    }
    m_useOriginal = useOriginal;
}

bool LoggingCategoryEntry::isEnabled(QtMsgType msgType) const
{
    if (m_category)
        return m_category->isEnabled(msgType);
    if (m_saved)
        return (*m_saved)[msgType];
    return false;
}

bool LoggingCategoryEntry::isEnabledOriginally(QtMsgType msgType) const
{
    if (m_originalSettings)
        return (*m_originalSettings)[msgType];
    return isEnabled(msgType);
}

void LoggingCategoryEntry::setEnabled(QtMsgType msgType, bool isEnabled)
{
    QTC_ASSERT(!m_useOriginal, return);

    if (m_category)
        m_category->setEnabled(msgType, isEnabled);

    if (m_saved)
        (*m_saved)[msgType] = isEnabled;
}

void LoggingCategoryEntry::setSaved(const SavedEntry &entry)
{
    QTC_ASSERT(entry.name == name(), return);

    m_saved = entry.levels;
    m_color = entry.color;
    if (!m_saved) {
        m_saved = std::array<bool, 5>();
        for (int i = QtDebugMsg; i <= QtInfoMsg; ++i) {
            (*m_saved)[i] = entry.level <= i;
        }
    }
    if (m_category)
        setLogCategory(m_category);
}

void LoggingCategoryEntry::setLogCategory(QLoggingCategory *category)
{
    QTC_ASSERT(QString::fromUtf8(category->categoryName()) == m_name, return);

    m_category = category;
    if (!m_originalSettings) {
        m_originalSettings = {
            category->isDebugEnabled(),
            category->isWarningEnabled(),
            category->isCriticalEnabled(),
            true, // always enable fatal
            category->isInfoEnabled(),
        };
    }

    if (m_saved && !m_useOriginal) {
        m_category->setEnabled(QtDebugMsg, m_saved->at(0));
        m_category->setEnabled(QtWarningMsg, m_saved->at(1));
        m_category->setEnabled(QtCriticalMsg, m_saved->at(2));
        m_category->setEnabled(QtInfoMsg, m_saved->at(4));
    }
}

} // namespace Core::Internal
