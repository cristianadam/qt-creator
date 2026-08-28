// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alientreeview.h"

#include "codicons.h"
#include "extensionhost.h"

#include <utils/stylehelper.h>
#include <utils/utilsicons.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
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
        // An item's command runs when it is picked, with a single click, so
        // the dashboards these views are used for stay one click deep.
        connect(this, &QTreeView::clicked, this, [this](const QModelIndex &index) {
            const QString command = index.data(CommandRole).toString();
            if (command.isEmpty())
                return;
            m_host->executeCommand(
                command,
                QJsonDocument::fromJson(index.data(CommandArgumentsRole).toByteArray()).array());
        });
        // The interesting commands of a tree-view extension are contributed to
        // the item context menu, and are deliberately not in the command
        // palette, so without this they cannot be reached at all.
        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
            const QModelIndex index = indexAt(pos);
            if (!index.isValid())
                return;
            const QString id = index.data(NodeIdRole).toString();
            const QPoint global = viewport()->mapToGlobal(pos);
            m_host->requestTreeMenu(m_viewId, id, "view/item/context", [this, id, global](const QJsonArray &items) {
                if (items.isEmpty())
                    return;
                // Shown, not exec()ed: the menu has to outlive this call, since
                // an action may be triggered from outside (a scripted run posts
                // the trigger), and a menu on the stack would take its actions
                // down with it first.
                auto menu = new QMenu(this);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                fill(menu, items, id);
                menu->popup(global);
            });
        });
        connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
            QStringList ids;
            for (const QModelIndex &index : selectionModel()->selectedRows())
                ids << index.data(NodeIdRole).toString();
            m_host->reportTreeSelection(m_viewId, ids);
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

    // An entry is a command or, one level down, a menu of its own.
    void fill(QMenu *menu, const QJsonArray &items, const QString &id)
    {
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            const QString submenu = item.value("submenu").toString();
            if (!submenu.isEmpty()) {
                fill(menu->addMenu(stripCodicons(submenu)), item.value("items").toArray(), id);
                continue;
            }
            const QString command = item.value("command").toString();
            menu->addAction(item.value("title").toString(), this, [this, id, command] {
                m_host->executeTreeItemCommand(m_viewId, id, command);
            });
        }
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
                const QString icon = node.value("icon").toString();
                item->setIcon(icon.isEmpty() ? firstCodicon(node.value("label").toString())
                                             : iconFromSpec(icon));
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

QString welcomeAsRichText(const QString &contents)
{
    static const QRegularExpression link(R"(\[([^\]]*)\]\(command:([^)\s]+)\))");
    QString html;
    const QStringList lines = contents.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i).trimmed();
        QString rendered;
        qsizetype at = 0;
        QRegularExpressionMatchIterator it = link.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            rendered += line.mid(at, match.capturedStart() - at).toHtmlEscaped();
            rendered += QString("<a href=\"%1\">%2</a>")
                            .arg(match.captured(2).toHtmlEscaped(),
                                 match.captured(1).toHtmlEscaped());
            at = match.capturedEnd();
        }
        rendered += line.mid(at).toHtmlEscaped();
        html += rendered;
        if (i + 1 < lines.size())
            html += "<br/>";
    }
    return html;
}

// The tree, with what the extension says for an empty one in its place. A view
// that has nothing to show and nothing to say about it leaves the user with a
// blank panel and no idea what to do.
class AlienViewWidget final : public QStackedWidget
{
public:
    AlienViewWidget(ExtensionHost *host, const QString &viewId)
        : m_host(host)
        , m_viewId(viewId)
        , m_tree(new AlienTreeView(host, viewId))
    {
        m_welcome->setWordWrap(true);
        m_welcome->setTextFormat(Qt::RichText);
        m_welcome->setAlignment(Qt::AlignTop);
        m_welcome->setMargin(StyleHelper::SpacingTokens::PaddingHM);
        connect(m_welcome, &QLabel::linkActivated, this, [this](const QString &command) {
            m_host->executeCommand(command);
        });

        addWidget(m_tree);
        addWidget(m_welcome);

        const auto update = [this] { showRightPage(); };
        connect(m_tree->model(), &QAbstractItemModel::rowsInserted, this, update);
        connect(m_tree->model(), &QAbstractItemModel::rowsRemoved, this, update);
        connect(m_tree->model(), &QAbstractItemModel::modelReset, this, update);
        connect(host, &ExtensionHost::viewWelcomeChanged, this,
                [this, viewId](const QString &changed) {
                    if (changed == viewId)
                        showRightPage();
                });
        showRightPage();
    }

private:
    void showRightPage()
    {
        const QString contents = m_host->viewWelcome(m_viewId);
        const bool empty = m_tree->model()->rowCount() == 0;
        if (empty && !contents.isEmpty()) {
            m_welcome->setText(welcomeAsRichText(contents));
            setCurrentWidget(m_welcome);
        } else {
            setCurrentWidget(m_tree);
        }
    }

    ExtensionHost *m_host;
    QString m_viewId;
    AlienTreeView *m_tree;
    QLabel *m_welcome = new QLabel;
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

// "view/title" contributions are the buttons of the view itself: the
// "navigation" group sits in the dock toolbar, the "overflow" group behind a
// single menu button.
Core::NavigationView AlienTreeViewFactory::createWidget()
{
    Core::NavigationView view;
    view.widget = new AlienViewWidget(m_host, m_viewId);

    QMenu *overflow = nullptr;
    for (const QJsonValue &value : m_host->treeTitleMenu(m_viewId)) {
        const QJsonObject item = value.toObject();
        const QString command = item.value("command").toString();
        const QString title = stripCodicons(item.value("title").toString());
        ExtensionHost *host = m_host;
        const QIcon icon = firstCodicon(item.value("icon").toString());
        // An entry we have no icon for would need a wide text button, which is
        // not what a view's toolbar looks like, so it joins the overflow menu.
        if (icon.isNull() || item.value("group").toString().startsWith("overflow")) {
            if (!overflow) {
                auto button = new QToolButton;
                button->setObjectName("alienViewButton." + m_viewId + ".overflow");
                button->setIcon(Utils::Icons::TOOLBAR_EXTENSION.icon());
                button->setPopupMode(QToolButton::InstantPopup);
                overflow = new QMenu(button);
                button->setMenu(overflow);
                view.dockToolBarWidgets.append(button);
            }
            overflow->addAction(title, host, [host, command] { host->executeCommand(command); });
            continue;
        }
        auto button = new QToolButton;
        button->setObjectName("alienViewButton." + command);
        button->setIcon(icon);
        button->setToolTip(title);
        QObject::connect(button, &QToolButton::clicked, host, [host, command] {
            host->executeCommand(command);
        });
        view.dockToolBarWidgets.append(button);
    }
    return view;
}

} // namespace Alien::Internal
