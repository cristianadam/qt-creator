// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alientreeview.h"

#include "codicons.h"
#include "extensionhost.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStandardItemModel>
#include <QTreeView>

using namespace Utils;

namespace Alien::Internal {

enum TreeRoles {
    NodeIdRole = Qt::UserRole + 1,
    FetchedRole,
    CommandRole,
    CommandArgumentsRole,
};

// A lazily populated tree view for one in-host tree data provider.
class AlienTreeView final : public QTreeView
{
public:
    AlienTreeView(ExtensionHost *host, const QString &viewId)
        : m_host(host)
        , m_viewId(viewId)
    {
        setObjectName("alienTreeView." + viewId);
        setHeaderHidden(true);
        setModel(m_model);

        connect(this, &QTreeView::expanded, this, [this](const QModelIndex &index) {
            QStandardItem *item = m_model->itemFromIndex(index);
            if (item && !item->data(FetchedRole).toBool())
                fetchChildren(item);
        });
        // VS Code runs an item's command when it is picked, with a single
        // click, so the dashboards these views are used for stay one click deep.
        connect(this, &QTreeView::clicked, this, [this](const QModelIndex &index) {
            const QString command = index.data(CommandRole).toString();
            if (command.isEmpty())
                return;
            m_host->executeCommand(
                command,
                QJsonDocument::fromJson(index.data(CommandArgumentsRole).toByteArray()).array());
        });
        connect(m_host, &ExtensionHost::treeViewRefreshed, this, [this](const QString &viewId) {
            if (viewId == m_viewId)
                reload();
        });
        reload();
    }

private:
    // The ids the host hands out are per-request, so what was open is
    // remembered by the labels leading to a row.
    static QString pathOf(const QStandardItem *item)
    {
        QStringList labels;
        for (const QStandardItem *i = item; i && i->parent(); i = i->parent())
            labels.prepend(i->text());
        return labels.join(QChar(0x1f));
    }

    void reload()
    {
        m_expanded.clear();
        for (int row = 0; row < m_model->rowCount(); ++row)
            collectExpanded(m_model->item(row));
        ++m_generation;
        m_model->clear();
        fetchChildren(m_model->invisibleRootItem());
    }

    void collectExpanded(const QStandardItem *item)
    {
        if (!item || !isExpanded(item->index()))
            return;
        m_expanded.insert(pathOf(item));
        for (int row = 0; row < item->rowCount(); ++row)
            collectExpanded(item->child(row));
    }

    void fetchChildren(QStandardItem *parent)
    {
        parent->setData(true, FetchedRole);
        parent->removeRows(0, parent->rowCount()); // drop the lazy placeholder
        const QString id = parent->data(NodeIdRole).toString();
        const int generation = m_generation;

        m_host->requestTreeChildren(m_viewId, id, [this, parent, generation](const QJsonArray &nodes) {
            if (generation != m_generation)
                return; // reloaded meanwhile
            for (const QJsonValue &value : nodes) {
                const QJsonObject node = value.toObject();
                auto item = new QStandardItem(stripCodicons(node.value("label").toString()));
                item->setEditable(false);
                item->setIcon(firstCodicon(node.value("label").toString()));
                item->setToolTip(stripCodicons(node.value("tooltip").toString()));
                item->setData(node.value("id").toString(), NodeIdRole);
                item->setData(node.value("command").toString(), CommandRole);
                item->setData(QJsonDocument(node.value("commandArguments").toArray()).toJson(
                                  QJsonDocument::Compact),
                              CommandArgumentsRole);

                if (node.value("collapsibleState").toInt() != 0) {
                    item->setData(false, FetchedRole);
                    item->appendRow(new QStandardItem); // placeholder to show the expander
                }
                parent->appendRow(item);
                if (m_expanded.contains(pathOf(item))) {
                    fetchChildren(item);
                    setExpanded(item->index(), true);
                }
            }
        });
    }

    ExtensionHost *m_host;
    QString m_viewId;
    QStandardItemModel *m_model = new QStandardItemModel(this);
    QSet<QString> m_expanded; // label paths that were open before a reload
    int m_generation = 0;
};

AlienTreeViewFactory::AlienTreeViewFactory(ExtensionHost *host, const QString &viewId,
                                           const QString &displayName)
    : m_host(host)
    , m_viewId(viewId)
{
    setId(Utils::Id::fromString(QString("Alien.%1").arg(viewId)));
    setDisplayName(displayName);
    setPriority(500);
}

Core::NavigationView AlienTreeViewFactory::createWidget()
{
    Core::NavigationView view;
    view.widget = new AlienTreeView(m_host, m_viewId);
    return view;
}

} // namespace Alien::Internal
