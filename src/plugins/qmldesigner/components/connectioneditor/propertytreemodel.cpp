// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "propertytreemodel.h"
#include "connectionview.h"

#include <bindingproperty.h>
#include <exception.h>
#include <nodeabstractproperty.h>
#include <nodelistproperty.h>
#include <nodemetainfo.h>
#include <qmldesignerconstants.h>
#include <qmldesignerplugin.h>
#include <rewritertransaction.h>
#include <rewriterview.h>
#include <signalhandlerproperty.h>
#include <variantproperty.h>

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

#include <QMessageBox>
#include <QTimer>

namespace QmlDesigner {

const std::vector<PropertyName> blockListProperties = {"children",
                                                       "data",
                                                       "childrenRect",
                                                       "icon",
                                                       "left",
                                                       "top",
                                                       "bottom",
                                                       "right",
                                                       "locale",
                                                       "objectName",
                                                       "transitions",
                                                       "states",
                                                       "resources",
                                                       "data",
                                                       "transformOrigin",
                                                       "transformOriginPoint",
                                                       "verticalCenter",
                                                       "horizontalCenter",
                                                       "anchors.bottom",
                                                       "anchors.top",
                                                       "anchors.left",
                                                       "anchors.right",
                                                       "anchors.fill",
                                                       "anchors.horizontalCenter",
                                                       "anchors.verticalCenter",
                                                       "anchors.centerIn",
                                                       "transform",
                                                       "visibleChildren"};

const std::vector<PropertyName> blockListSlots = {"childAt",
                                                  "contains",
                                                  "destroy",
                                                  "dumpItemTree",
                                                  "ensurePolished",
                                                  "grabToImage",
                                                  "mapFromGlobal",
                                                  "mapFromItem",
                                                  "mapToGlobal",
                                                  "mapToItem",
                                                  "valueAt",
                                                  "toString",
                                                  "getText",
                                                  "inputMethodQuery",
                                                  "positionAt",
                                                  "positionToRectangle",
                                                  "isRightToLeft"

};

const std::vector<PropertyName> priorityListSignals = {"clicked",
                                                       "doubleClicked",
                                                       "pressed",
                                                       "released",
                                                       "toggled",
                                                       "valueModified",
                                                       "valueChanged",
                                                       "checkedChanged",
                                                       "moved",
                                                       "accepted",
                                                       "editingFinished",
                                                       "entered",
                                                       "exited",
                                                       "canceled",
                                                       "triggered",
                                                       "stateChanged",
                                                       "started",
                                                       "stopped",
                                                       "finished"
                                                       "enabledChanged",
                                                       "visibleChanged",
                                                       "opacityChanged",
                                                       "rotationChanged"};

const std::vector<PropertyName> priorityListProperties
    = {"opacity",  "visible",       "value",   "x",       "y",       "width", "height",
       "rotation", "color",         "scale",   "state",   "enabled", "z",     "text",
       "pressed",  "containsMouse", "checked", "hovered", "down",    "clip",  "parent"};

const std::vector<PropertyName> priorityListSlots = {"toggle",
                                                     "increase",
                                                     "decrease",
                                                     "clear",
                                                     "complete",
                                                     "pause",
                                                     "restart",
                                                     "resume",
                                                     "start",
                                                     "stop",
                                                     "forceActiveFocus"};

std::vector<PropertyName> properityLists()
{
    std::vector<PropertyName> result;

    result.insert(result.end(), priorityListSignals.begin(), priorityListSignals.end());
    result.insert(result.end(), priorityListProperties.begin(), priorityListProperties.end());
    result.insert(result.end(), priorityListSlots.begin(), priorityListSlots.end());

    return result;
}

PropertyTreeModel::PropertyTreeModel(ConnectionView *parent)
    : QAbstractItemModel(parent), m_connectionView(parent)
{}

void PropertyTreeModel::resetModel()
{
    beginResetModel();

    m_indexCache.clear();
    m_indexHash.clear();
    m_indexCount = 0;
    m_nodeList = allIdsSorted();

    endResetModel();
    testModel();
}

QVariant PropertyTreeModel::data(const QModelIndex &index, int role) const
{
    int internalId = index.internalId();

    if (role == InternalIdRole)
        return internalId;

    if (role == RowRole)
        return index.row();

    if (role == PropertyNameRole || role == PropertyPriorityRole || role == ExpressionRole) {
        if (!index.isValid())
            return {};

        if (internalId < 0)
            return {};

        QTC_ASSERT(internalId < m_indexCount, return {"assert"});

        QTC_ASSERT(internalId < m_indexHash.size(), return {"assert"});

        DataCacheItem item = m_indexHash.at(index.internalId());

        if (item.propertyName.isEmpty()) { //node
            if (role == PropertyNameRole)
                return item.modelNode.displayName();

            return true; //nodes are always shown
        }

        if (role == ExpressionRole)
            return QString(item.modelNode.id() + item.propertyName);

        if (role == PropertyNameRole)
            return item.propertyName;

        auto priority = properityLists();
        if (std::find(priority.begin(), priority.end(), item.propertyName) != priority.end())
            return true; //listed priority properties

        auto dynamic = getDynamicProperties(item.modelNode);
        if (std::find(dynamic.begin(), dynamic.end(), item.propertyName) != dynamic.end())
            return true; //dynamic properties have priority
        return false;
    }

    // can be removed later since we only use the two roles above in QML
    // just for testing

    if (!(role == Qt::DisplayRole || role == Qt::FontRole))
        return {};

    if (!index.isValid())
        return {};

    if (internalId < 0)
        return {};

    QTC_ASSERT(internalId < m_indexCount, return {"assert"});

    QTC_ASSERT(internalId < m_indexHash.size(), return {"assert"});

    DataCacheItem item = m_indexHash.at(index.internalId());

    if (item.propertyName.isEmpty()) {
        const QString name = item.modelNode.displayName();
        if (role == Qt::DisplayRole)
            return name;
        QFont f;
        f.setBold(true);
        return f;
    }

    if (role == Qt::DisplayRole)
        return item.propertyName;

    QFont f;
    auto priority = properityLists();
    if (std::find(priority.begin(), priority.end(), item.propertyName) != priority.end())
        f.setBold(true);
    auto dynamic = getDynamicProperties(item.modelNode);
    if (std::find(dynamic.begin(), dynamic.end(), item.propertyName) != dynamic.end())
        f.setBold(true);

    return f;
}

Qt::ItemFlags PropertyTreeModel::flags(const QModelIndex &index) const
{
    return Qt::ItemIsEnabled;
}

QModelIndex PropertyTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    int internalId = parent.internalId();
    if (!m_connectionView->isAttached())
        return {};

    if (!hasIndex(row, column, parent))
        return QModelIndex();

    const int rootId = -1;

    if (!parent.isValid())
        return createIndex(0, 0, rootId);

    if (internalId == rootId) { //root level model node
        const ModelNode modelNode = m_nodeList.at(row);
        return ensureModelIndex(modelNode, row);
    }

    //property

    QTC_ASSERT(internalId >= 0, return {});

    DataCacheItem item = m_indexHash.at(internalId);
    QTC_ASSERT(item.modelNode.isValid(), return {});

    if (!item.propertyName.isEmpty()) {
        // "." aka sub property
        auto properties = sortedDotPropertyNamesSignalsSlots(item.modelNode, item.propertyName);
        PropertyName propertyName = properties.at(row);
        return ensureModelIndex(item.modelNode, propertyName, row);
    }

    auto properties = sortedAndFilteredPropertyNamesSignalsSlots(item.modelNode);

    QTC_ASSERT(row < properties.size(), return {});
    PropertyName propertyName = properties.at(row);

    return ensureModelIndex(item.modelNode, propertyName, row);
}

QModelIndex PropertyTreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    int internalId = index.internalId();

    if (internalId == -1)
        return {};

    QTC_ASSERT(internalId < m_indexCount, return {});
    QTC_ASSERT(internalId < m_indexHash.size(), return {});

    const DataCacheItem item = m_indexHash.at(index.internalId());

    // no property means the parent is the root item
    if (item.propertyName.isEmpty())
        return createIndex(0, 0, -1);

    if (item.propertyName.contains(".")) {
        auto list = item.propertyName.split('.');
        DataCacheItem parent;
        parent.modelNode = item.modelNode;
        parent.propertyName = list.first();
        auto iter = m_indexCache.find(parent);
        if (iter != m_indexCache.end()) {
            const auto vector = sortedAndFilteredPropertyNamesSignalsSlots(item.modelNode);
            QList<PropertyName> list(vector.begin(), vector.end());
            int row = list.indexOf(parent.propertyName);
            return createIndex(row, 0, iter->internalIndex);
        }
    }

    // find the parent

    int row = m_nodeList.indexOf(item.modelNode);
    return ensureModelIndex(item.modelNode, row);
}

QPersistentModelIndex PropertyTreeModel::indexForInernalIdAndRow(int internalId, int row)
{
    return createIndex(row, 0, internalId);
}

int PropertyTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!m_connectionView->isAttached() || parent.column() > 0)
        return 0;

    if (!parent.isValid())
        return 1;

    int internalId = parent.internalId();

    if (internalId == -1)
        return m_nodeList.size();

    QTC_ASSERT(internalId < m_indexCount, return 0);
    QTC_ASSERT(internalId < m_indexHash.size(), return 0);

    DataCacheItem item = m_indexHash.at(internalId);
    if (!item.propertyName.isEmpty()) {
        if (item.modelNode.metaInfo().property(item.propertyName).isPointer()) {
            auto subProbs = sortedDotPropertyNamesSignalsSlots(item.modelNode, item.propertyName);

            return static_cast<int>(subProbs.size());
        }

        return 0;
    }

    return static_cast<int>(sortedAndFilteredPropertyNamesSignalsSlots(item.modelNode).size());
}
int PropertyTreeModel::columnCount(const QModelIndex &parent) const
{
    return 1;
}

void PropertyTreeModel::setPropertyType(PropertyTypes type)
{
    if (m_type == type)
        return;

    m_type = type;
    resetModel();
}

void PropertyTreeModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;

    m_filter = filter;
    resetModel();
}

QModelIndex PropertyTreeModel::ensureModelIndex(const ModelNode &node, int row) const
{
    DataCacheItem item;
    item.modelNode = node;

    auto iter = m_indexCache.find(item);
    if (iter != m_indexCache.end())
        return createIndex(row, 0, iter->internalIndex);

    item.internalIndex = m_indexCount;
    m_indexCount++;
    m_indexHash.push_back(item);
    m_indexCache.insert(item);

    return createIndex(row, 0, item.internalIndex);
}
QModelIndex PropertyTreeModel::ensureModelIndex(const ModelNode &node,
                                                const PropertyName &name,
                                                int row) const
{
    DataCacheItem item;
    item.modelNode = node;
    item.propertyName = name;

    auto iter = m_indexCache.find(item);
    if (iter != m_indexCache.end())
        return createIndex(row, 0, iter->internalIndex);

    item.internalIndex = m_indexCount;
    m_indexCount++;
    m_indexHash.push_back(item);
    m_indexCache.insert(item);

    return createIndex(row, 0, item.internalIndex);
}

void PropertyTreeModel::testModel()
{
    qDebug() << Q_FUNC_INFO;
    qDebug() << rowCount({});

    QModelIndex rootIndex = index(0, 0);

    qDebug() << rowCount(rootIndex);
    QModelIndex firstItem = index(0, 0, rootIndex);

    qDebug() << "fi" << data(firstItem, Qt::DisplayRole) << rowCount(firstItem);

    firstItem = index(1, 0, rootIndex);
    qDebug() << "fi" << data(firstItem, Qt::DisplayRole) << rowCount(firstItem);

    firstItem = index(2, 0, rootIndex);
    qDebug() << "fi" << data(firstItem, Qt::DisplayRole) << rowCount(firstItem);

    QModelIndex firstProperty = index(0, 0, firstItem);

    qDebug() << "fp" << data(firstProperty, Qt::DisplayRole) << rowCount(firstProperty);

    qDebug() << m_indexCount << m_indexHash.size() << m_indexCache.size();
}

const QList<ModelNode> PropertyTreeModel::allIdsSorted() const
{
    if (!m_connectionView->isAttached())
        return {};

    return Utils::sorted(m_connectionView->allModelNodesWithId(),
                         [](const ModelNode &lhs, const ModelNode &rhs) {
                             return lhs.displayName() < rhs.displayName();
                         });
}

const std::vector<PropertyName> PropertyTreeModel::sortedAndFilteredPropertyNamesSignalsSlots(
    const ModelNode &modelNode) const
{
    std::vector<PropertyName> returnValue;
    if (m_type == SignalType) {
        returnValue = sortedAndFilteredSignalNames(modelNode.metaInfo());
    } else if (m_type == SlotType) {
        returnValue = sortedAndFilteredSlotNames(modelNode.metaInfo());
    } else {
        auto list = sortedAndFilteredPropertyNames(modelNode.metaInfo());
        returnValue = getDynamicProperties(modelNode);
        std::move(list.begin(), list.end(), std::back_inserter(returnValue));
    }

    if (m_filter.isEmpty() || modelNode.displayName().contains(m_filter))
        return returnValue;

    return Utils::filtered(returnValue, [this](const PropertyName &name) {
        return name.contains(m_filter.toUtf8()) || name == m_filter.toUtf8();
    });
}

const std::vector<PropertyName> PropertyTreeModel::getDynamicProperties(
    const ModelNode &modelNode) const
{
    QList<PropertyName> list = Utils::transform(modelNode.dynamicProperties(),
                                                [](const AbstractProperty &property) {
                                                    return property.name();
                                                });

    QList<PropertyName> filtered
        = Utils::filtered(list, [this, modelNode](const PropertyName &propertyName) {
              PropertyName propertyType = modelNode.property(propertyName).dynamicTypeName();
              switch (m_type) {
              case AllTypes:
                  return true;
              case NumberType:
                  return propertyType == "float" || propertyType == "double"
                         || propertyType == "int";
              case StringType:
                  return propertyType == "string";
              case UrlType:
                  return propertyType == "url";
              case ColorType:
                  return propertyType == "color";

              case BoolType:
                  return propertyType == "bool";
              default:
                  break;
              }
              return true;
          });

    return Utils::sorted(std::vector<PropertyName>(filtered.begin(), filtered.end()));
}

const std::vector<PropertyName> PropertyTreeModel::sortedAndFilteredPropertyNames(
    const NodeMetaInfo &metaInfo, bool recursive) const
{
    auto filtered = Utils::filtered(metaInfo.properties(),
                                    [this, recursive](const PropertyMetaInfo &metaInfo) {
                                        // if (!metaInfo.isWritable()) - lhs/rhs

                                        const PropertyName name = metaInfo.name();

                                        if (name.contains("."))
                                            return false;

                                        if (name.startsWith("icon."))
                                            return false;
                                        if (name.startsWith("transformOriginPoint."))
                                            return false;

                                        return filterProperty(name, metaInfo, recursive);
                                    });

    auto sorted = Utils::sorted(
        Utils::transform(filtered, [](const PropertyMetaInfo &metaInfo) -> PropertyName {
            return metaInfo.name();
        }));

    std::set<PropertyName> set(std::make_move_iterator(sorted.begin()),
                               std::make_move_iterator(sorted.end()));

    auto checkedPriorityList = Utils::filtered(priorityListProperties,
                                               [&set](const PropertyName &name) {
                                                   auto it = set.find(name);
                                                   const bool b = it != set.end();
                                                   if (b)
                                                       set.erase(it);

                                                   return b;
                                               });

    //const int priorityLength = checkedPriorityList.size(); We eventually require this to get the prioproperties

    std::vector<PropertyName> final(set.begin(), set.end());

    std::move(final.begin(), final.end(), std::back_inserter(checkedPriorityList));

    return checkedPriorityList;
}

const std::vector<PropertyName> PropertyTreeModel::sortedAndFilteredSignalNames(
    const NodeMetaInfo &metaInfo, bool recursive) const
{
    const std::vector<PropertyName> priorityListSignals = {"clicked",
                                                           "doubleClicked",
                                                           "pressed",
                                                           "released",
                                                           "toggled",
                                                           "valueModified",
                                                           "valueChanged",
                                                           "checkedChanged",
                                                           "moved",
                                                           "accepted",
                                                           "editingFinished",
                                                           "entered",
                                                           "exited",
                                                           "canceled",
                                                           "triggered",
                                                           "stateChanged",
                                                           "started",
                                                           "stopped",
                                                           "finished"
                                                           "enabledChanged",
                                                           "visibleChanged",
                                                           "opacityChanged",
                                                           "rotationChanged"};

    auto filtered
        = Utils::filtered(metaInfo.signalNames(), [priorityListSignals](const PropertyName &name) {
              if (std::find(priorityListSignals.begin(), priorityListSignals.end(), name)
                  != priorityListSignals.end())
                  return true;

              if (name.endsWith("Changed")) //option?
                  return false;

              return true;
          });

    auto sorted = Utils::sorted(filtered);

    std::set<PropertyName> set(std::make_move_iterator(sorted.begin()),
                               std::make_move_iterator(sorted.end()));

    auto checkedPriorityList = Utils::filtered(priorityListSignals,
                                               [&set](const PropertyName &name) {
                                                   auto it = set.find(name);
                                                   const bool b = it != set.end();
                                                   if (b)
                                                       set.erase(it);

                                                   return b;
                                               });

    //const int priorityLength = checkedPriorityList.size(); We eventually require this to get the prioproperties

    std::vector<PropertyName> final(set.begin(), set.end());

    std::move(final.begin(), final.end(), std::back_inserter(checkedPriorityList));

    return checkedPriorityList;
}

const std::vector<PropertyName> PropertyTreeModel::sortedAndFilteredSlotNames(
    const NodeMetaInfo &metaInfo, bool recursive) const
{
    auto priorityList = priorityListSlots;
    auto filtered = Utils::filtered(metaInfo.slotNames(), [priorityList](const PropertyName &name) {
        if (std::find(priorityListSlots.begin(), priorityListSlots.end(), name)
            != priorityListSlots.end())
            return true;

        if (name.startsWith("_"))
            return false;
        if (name.startsWith("q_"))
            return false;

        if (name.endsWith("Changed")) //option?
            return false;

        if (std::find(blockListSlots.begin(), blockListSlots.end(), name) != blockListSlots.end())
            return false;

        return true;
    });

    auto sorted = Utils::sorted(filtered);

    std::set<PropertyName> set(std::make_move_iterator(sorted.begin()),
                               std::make_move_iterator(sorted.end()));

    auto checkedPriorityList = Utils::filtered(priorityListSlots, [&set](const PropertyName &name) {
        auto it = set.find(name);
        const bool b = it != set.end();
        if (b)
            set.erase(it);

        return b;
    });

    //const int priorityLength = checkedPriorityList.size(); We eventually require this to get the prioproperties

    std::vector<PropertyName> final(set.begin(), set.end());

    std::move(final.begin(), final.end(), std::back_inserter(checkedPriorityList));

    return checkedPriorityList;
}

const std::vector<PropertyName> PropertyTreeModel::sortedDotPropertyNames(
    const ModelNode &modelNode, const PropertyName &propertyName) const
{
    const PropertyName prefix = propertyName + '.';
    auto filtered = Utils::filtered(modelNode.metaInfo().properties(),
                                    [this, prefix](const PropertyMetaInfo &metaInfo) {
                                        const PropertyName name = metaInfo.name();

                                        if (!name.startsWith(prefix))
                                            return false;

                                        return filterProperty(name, metaInfo, true);
                                    });

    auto sorted = Utils::sorted(
        Utils::transform(filtered, [](const PropertyMetaInfo &metaInfo) -> PropertyName {
            return metaInfo.name();
        }));

    if (sorted.size() == 0
        && modelNode.metaInfo().property(propertyName).propertyType().isQtObject()) {
        return Utils::transform(sortedAndFilteredPropertyNames(modelNode.metaInfo()
                                                                   .property(propertyName)
                                                                   .propertyType(),
                                                               true),
                                [propertyName](const PropertyName &name) -> PropertyName {
                                    return propertyName + "." + name;
                                });
    }

    return sorted;
}

const std::vector<PropertyName> PropertyTreeModel::sortedDotPropertySignals(
    const ModelNode &modelNode, const PropertyName &propertyName) const
{
    return Utils::transform(sortedAndFilteredSignalNames(
                                modelNode.metaInfo().property(propertyName).propertyType(), true),
                            [propertyName](const PropertyName &name) -> PropertyName {
                                return propertyName + "." + name;
                            });
}

const std::vector<PropertyName> PropertyTreeModel::sortedDotPropertySlots(
    const ModelNode &modelNode, const PropertyName &propertyName) const
{
    return Utils::transform(sortedAndFilteredSlotNames(
                                modelNode.metaInfo().property(propertyName).propertyType(), true),
                            [propertyName](const PropertyName &name) -> PropertyName {
                                return propertyName + "." + name;
                            });
}

const std::vector<PropertyName> PropertyTreeModel::sortedDotPropertyNamesSignalsSlots(
    const ModelNode &modelNode, const PropertyName &propertyName) const
{
    if (m_type == SignalType) {
        return sortedDotPropertySignals(modelNode, propertyName);
    } else if (m_type == SlotType) {
        return sortedDotPropertySlots(modelNode, propertyName);
    } else {
        return sortedDotPropertyNames(modelNode, propertyName);
    }
}

bool PropertyTreeModel::filterProperty(const PropertyName &name,
                                       const PropertyMetaInfo &metaInfo,
                                       bool recursive) const
{
    if (std::find(blockListProperties.begin(), blockListProperties.end(), name)
        != blockListProperties.end())
        return false;

    const NodeMetaInfo propertyType = metaInfo.propertyType();

    //We want to keep sub items with matching properties
    if (!recursive && metaInfo.isPointer()
        && sortedAndFilteredPropertyNames(propertyType, true).size() > 0)
        return true;

    //TODO no type relaxation atm...
    switch (m_type) {
    case AllTypes:
        return true;
    case NumberType:
        return propertyType.isFloat() || propertyType.isInteger();
    case StringType:
        return propertyType.isString();
        /*
        return propertyType.isString() || propertyType.isColor() || propertyType.isBool()
               || propertyType.isUrl();
*/
    case UrlType:
        return propertyType.isUrl();
        //return propertyType.isString() || propertyType.isUrl();
    case ColorType:
        return propertyType.isColor();
        //return propertyType.isString() || propertyType.isColor();
    case BoolType:
        return propertyType.isBool();
    default:
        break;
    }
    return true;
}

QHash<int, QByteArray> PropertyTreeModel::roleNames() const
{
    static QHash<int, QByteArray> roleNames{{PropertyNameRole, "propertyName"},
                                            {PropertyPriorityRole, "hasPriority"},
                                            {ExpressionRole, "expression"}};

    return roleNames;
}

PropertyListProxyModel::PropertyListProxyModel(PropertyTreeModel *parent)
    : QAbstractListModel(), m_treeModel(parent)
{}

void PropertyListProxyModel::setRowandInternalId(int row, int internalId)
{
    QTC_ASSERT(m_treeModel, return );

    if (internalId == -1)
        m_parentIndex = m_treeModel->index(0, 0);
    else
        m_parentIndex = m_treeModel->indexForInernalIdAndRow(internalId, row);

    beginResetModel();
    endResetModel();
}

int PropertyListProxyModel::rowCount(const QModelIndex &) const
{
    QTC_ASSERT(m_treeModel, return 0);
    return m_treeModel->rowCount(m_parentIndex);
}

QVariant PropertyListProxyModel::data(const QModelIndex &index, int role) const
{
    QTC_ASSERT(m_treeModel, return 0);

    auto treeIndex = m_treeModel->index(index.row(), 0, m_parentIndex);

    return m_treeModel->data(treeIndex, role);
}

} // namespace QmlDesigner
