// Copyright (C) 2016 Denis Mingulov
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "classviewparsertreeitem.h"

#include "classviewconstants.h"

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/projectmanager.h>

#include <QDebug>
#include <QHash>
#include <QStandardItem>

namespace ClassView::Internal {

///////////////////////////////// ParserTreeItemPrivate //////////////////////////////////

/*!
    \class ParserTreeItemPrivate
    \brief The ParserTreeItemPrivate class defines private class data for
    the ParserTreeItem class.
   \sa ParserTreeItem
 */
class ParserTreeItemPrivate
{
public:
    void mergeWith(const ParserTreeItem::ConstPtr &target);
    ParserTreeItem::ConstPtr cloneTree() const;

    QHash<SymbolInformation, ParserTreeItem::ConstPtr> m_symbolInformations;
    QSet<SymbolLocation> m_symbolLocations;
    const Utils::FilePath m_projectFilePath;
};

void ParserTreeItemPrivate::mergeWith(const ParserTreeItem::ConstPtr &target)
{
    if (!target)
        return;

    m_symbolLocations.unite(target->d->m_symbolLocations);

    // merge children
    for (auto it = target->d->m_symbolInformations.cbegin();
              it != target->d->m_symbolInformations.cend(); ++it) {
        const SymbolInformation &inf = it.key();
        const ParserTreeItem::ConstPtr &targetChild = it.value();

        ParserTreeItem::ConstPtr child = m_symbolInformations.value(inf);
        if (child) {
            child->d->mergeWith(targetChild);
        } else {
            const ParserTreeItem::ConstPtr clone = targetChild ? targetChild->d->cloneTree()
                                                               : ParserTreeItem::ConstPtr();
            m_symbolInformations.insert(inf, clone);
        }
    }
}

/*!
    Creates a deep clone of this tree.
*/
ParserTreeItem::ConstPtr ParserTreeItemPrivate::cloneTree() const
{
    ParserTreeItem::ConstPtr newItem(new ParserTreeItem(m_projectFilePath));
    newItem->d->m_symbolLocations = m_symbolLocations;

    for (auto it = m_symbolInformations.cbegin(); it != m_symbolInformations.cend(); ++it) {
        ParserTreeItem::ConstPtr child = it.value();
        if (!child)
            continue;
        newItem->d->m_symbolInformations.insert(it.key(), child->d->cloneTree());
    }

    return newItem;
}

///////////////////////////////// ParserTreeItem //////////////////////////////////

/*!
    \class ParserTreeItem
    \brief The ParserTreeItem class is an item for the internal Class View tree.

    Not virtual - to speed up its work.
*/

ParserTreeItem::ParserTreeItem()
    : d(new ParserTreeItemPrivate())
{
}

ParserTreeItem::ParserTreeItem(const Utils::FilePath &projectFilePath)
    : d(new ParserTreeItemPrivate({{}, {}, projectFilePath}))
{
}

ParserTreeItem::ParserTreeItem(const QHash<SymbolInformation, ConstPtr> &children)
    : d(new ParserTreeItemPrivate({children, {}, {}}))
{
}

ParserTreeItem::~ParserTreeItem()
{
    delete d;
}

Utils::FilePath ParserTreeItem::projectFilePath() const
{
    return d->m_projectFilePath;
}

/*!
    Gets information about symbol positions.
    \sa SymbolLocation, addSymbolLocation, removeSymbolLocation
*/

QSet<SymbolLocation> ParserTreeItem::symbolLocations() const
{
    return d->m_symbolLocations;
}

/*!
    Returns the child item specified by \a inf symbol information.
*/

ParserTreeItem::ConstPtr ParserTreeItem::child(const SymbolInformation &inf) const
{
    return d->m_symbolInformations.value(inf);
}

/*!
    Returns the amount of children of the tree item.
*/

int ParserTreeItem::childCount() const
{
    return d->m_symbolInformations.count();
}

ParserTreeItem::ConstPtr ParserTreeItem::fromDeclarations(
    const QList<CppEditor::WrittenDeclaration> &declarations)
{
    ConstPtr root(new ParserTreeItem());

    // The row each declaration got, so that what is written inside it is
    // added under that row. A scope comes before its members, so a parent's
    // row is always there by the time its members are read.
    QList<ConstPtr> rowOf(declarations.size());

    // The namespaces among them, since one that turns out to hold nothing a
    // reader is shown is taken out again once everything is in.
    QList<int> namespaces;

    const auto rowFor = [&](const CppEditor::WrittenDeclaration &declaration) {
        return declaration.parent < 0 ? root : rowOf.at(declaration.parent);
    };
    const auto informationOf = [](const CppEditor::WrittenDeclaration &declaration) {
        return SymbolInformation(declaration.name, declaration.type, declaration.iconType);
    };

    for (int i = 0; i < declarations.size(); ++i) {
        const CppEditor::WrittenDeclaration &declaration = declarations.at(i);
        const ConstPtr parent = rowFor(declaration);
        if (!parent)
            continue;

        const SymbolInformation information = informationOf(declaration);

        // Two declarations of one thing are one row: what tells one row from
        // another is what is written in it, and both places are kept.
        ConstPtr row = parent->d->m_symbolInformations.value(information);
        if (!row) {
            row = ConstPtr(new ParserTreeItem());
            parent->d->m_symbolInformations.insert(information, row);
            if (declaration.isNamespace)
                namespaces.append(i);
        }

        row->d->m_symbolLocations.insert(SymbolLocation(declaration.filePath, declaration.line,
                                                        declaration.column));
        rowOf[i] = row;
    }

    // Innermost first, since taking one out can leave the one around it
    // empty too.
    for (auto it = namespaces.crbegin(); it != namespaces.crend(); ++it) {
        const CppEditor::WrittenDeclaration &declaration = declarations.at(*it);
        const ConstPtr parent = rowFor(declaration);
        const SymbolInformation information = informationOf(declaration);
        if (const ConstPtr row = parent->child(information); row && row->childCount() == 0)
            parent->d->m_symbolInformations.remove(information);
    }

    return root;
}

ParserTreeItem::ConstPtr ParserTreeItem::mergeTrees(const Utils::FilePath &projectFilePath,
                                               const QList<ConstPtr> &docTrees)
{
    ConstPtr item(new ParserTreeItem(projectFilePath));
    for (const ConstPtr &docTree : docTrees)
        item->d->mergeWith(docTree);

    return item;
}

/*!
    Converts internal location container to QVariant compatible.
    \a locations specifies a set of symbol locations.
    Returns a list of variant locations that can be added to the data of an
    item.
*/

static QList<QVariant> locationsToRole(const QSet<SymbolLocation> &locations)
{
    QList<QVariant> locationsVar;
    for (const SymbolLocation &loc : locations)
        locationsVar.append(QVariant::fromValue(loc));

    return locationsVar;
}

/*!
    Checks \a item in a QStandardItemModel for lazy data population.
    Make sure this method is called only from the GUI thread.
*/
bool ParserTreeItem::canFetchMore(QStandardItem *item) const
{
    if (!item)
        return false;
    return item->rowCount() < d->m_symbolInformations.count();
}

/*!
    Appends this item to the QStandardIten item \a item.
    Make sure this method is called only from the GUI thread.
*/
void ParserTreeItem::fetchMore(QStandardItem *item) const
{
    using ProjectExplorer::ProjectManager;
    if (!item)
        return;

    // convert to map - to sort it
    QMap<SymbolInformation, ConstPtr> map;
    for (auto it = d->m_symbolInformations.cbegin(); it != d->m_symbolInformations.cend(); ++it)
        map.insert(it.key(), it.value());

    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const SymbolInformation &inf = it.key();
        ConstPtr ptr = it.value();

        auto add = new QStandardItem;
        add->setData(inf.name(), Constants::SymbolNameRole);
        add->setData(inf.type(), Constants::SymbolTypeRole);
        add->setData(inf.iconType(), Constants::IconTypeRole);

        if (ptr) {
            // icon
            const Utils::FilePath &filePath = ptr->projectFilePath();
            if (!filePath.isEmpty()) {
                ProjectExplorer::Project *project = ProjectManager::projectForFile(filePath);
                if (project)
                    add->setIcon(project->containerNode()->icon());
                add->setToolTip(filePath.toUserOutput());
            }

            // draggable
            if (!ptr->symbolLocations().isEmpty())
                add->setFlags(add->flags() | Qt::ItemIsDragEnabled);

            // locations
            add->setData(locationsToRole(ptr->symbolLocations()), Constants::SymbolLocationsRole);
        }
        item->appendRow(add);
    }
}

/*!
    Debug dump.
*/

void ParserTreeItem::debugDump(int indent) const
{
    for (auto it = d->m_symbolInformations.cbegin(); it != d->m_symbolInformations.cend(); ++it) {
        const SymbolInformation &inf = it.key();
        const ConstPtr &child = it.value();
        qDebug() << QString(2 * indent, QLatin1Char(' ')) << inf.iconType() << inf.name()
                 << inf.type() << bool(child);
        if (child)
            child->debugDump(indent + 1);
    }
}

} // namespace ClassView::Internal

