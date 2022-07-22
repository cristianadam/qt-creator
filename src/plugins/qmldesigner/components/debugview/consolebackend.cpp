/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "consolebackend.h"
#include "consoleengine.h"
#include "debugview.h"

#include <utils/qtcassert.h>

namespace QmlDesigner {

namespace Internal {

ConsoleBackend::ConsoleBackend(ConsoleEngine *engine, DebugView *view)
    : QObject{view}
    , m_debugView(view)
    , m_engine(engine)
{
    m_rootNodeObject = new ModelNodeJSWrapper(engine, this);

    m_rootModelNode = engine->newQObject(m_rootNodeObject);
}

void ConsoleBackend::modelAttached()
{
    QTC_ASSERT(m_debugView->model(), return );
    m_rootNodeObject->setModelNode(m_debugView->rootModelNode());
}

QJSValueList ConsoleBackend::selectedModelNodes() const
{
    return m_engine->newValueListForModelNodeList(m_debugView->selectedModelNodes());
}

QJSValue ConsoleBackend::firstSelectedModelNode() const
{
    return m_engine->newValueForModelNode(m_debugView->firstSelectedModelNode());
}

QJSValue ConsoleBackend::singleSelectedModelNode() const
{
    return m_engine->newValueForModelNode(m_debugView->singleSelectedModelNode());
}

QJSValue ConsoleBackend::modelNodeForId(const QString &id)
{
    return m_engine->newValueForModelNode(m_debugView->modelNodeForId(id));
}

bool ConsoleBackend::hasId(const QString &id) const
{
    return m_debugView->hasId(id);
}

QJSValue ConsoleBackend::modelNodeForInternalId(qint32 internalId) const
{
    return m_engine->newValueForModelNode(m_debugView->modelNodeForInternalId(internalId));
}

bool ConsoleBackend::hasModelNodeForInternalId(qint32 internalId) const
{
    return m_debugView->hasModelNodeForInternalId(internalId);
}

QJSValueList ConsoleBackend::allModelNodes() const
{
    return m_engine->newValueListForModelNodeList(m_debugView->allModelNodes());
}

QJSValueList ConsoleBackend::allModelNodesOfType(const TypeName &typeName) const
{
    return m_engine->newValueListForModelNodeList(m_debugView->allModelNodesOfType(typeName));
}

ModelNodeJSWrapper::ModelNodeJSWrapper(ConsoleEngine *engine, QObject *parent)
    : QObject{parent}
    , m_engine(engine)
{}

QJSValueList ModelNodeJSWrapper::directSubModelNodes() const
{
    return m_engine->newValueListForModelNodeList(m_modelNode.directSubModelNodes());
}

QJSValueList ModelNodeJSWrapper::directSubModelNodesOfType(const TypeName &typeName) const
{
    return m_engine->newValueListForModelNodeList(m_modelNode.subModelNodesOfType(typeName));
}

QJSValueList ModelNodeJSWrapper::subModelNodesOfType(const TypeName &typeName) const
{
    return m_engine->newValueListForModelNodeList(m_modelNode.subModelNodesOfType(typeName));
}

QJSValueList ModelNodeJSWrapper::allSubModelNodes() const
{
    return m_engine->newValueListForModelNodeList(m_modelNode.allSubModelNodes());
}

QJSValueList ModelNodeJSWrapper::allSubModelNodesAndThisNode() const
{
    return m_engine->newValueListForModelNodeList(m_modelNode.allSubModelNodesAndThisNode());
}

QJSValueList ModelNodeJSWrapper::properties() const
{
    QJSValueList list;
    const auto properties = m_modelNode.properties();
    for (const auto &property : properties) {
        list.append(m_engine->newValueForAbstractProperty(property));
    }

    return list;
}

QJSValueList ModelNodeJSWrapper::variantProperties() const
{
    QJSValueList list;
    const auto properties = m_modelNode.variantProperties();
    for (const auto &property : properties) {
        list.append(m_engine->newValueForVariantProperty(property));
    }

    return list;
}

QJSValueList ModelNodeJSWrapper::nodeAbstractProperties() const
{
    QJSValueList list;
    const auto properties = m_modelNode.nodeAbstractProperties();
    for (const auto &property : properties) {
        list.append(m_engine->newValueForNodeAbstractProperty(property));
    }

    return list;
}

QJSValueList ModelNodeJSWrapper::nodeProperties() const
{
    QJSValueList list;
    const auto properties = m_modelNode.nodeProperties();
    for (const auto &property : properties) {
        list.append(m_engine->newValueForNodeProperty(property));
    }

    return list;
}

QJSValueList ModelNodeJSWrapper::nodeListProperties() const
{
    QJSValueList list;
    const auto properties = m_modelNode.nodeListProperties();
    for (const auto &property : properties) {
        list.append(m_engine->newValueForNodeListProperty(property));
    }

    return list;
}

QJSValueList ModelNodeJSWrapper::bindingProperties() const
{
    QJSValueList list;
    const auto properties = m_modelNode.bindingProperties();
    for (const auto &property : properties) {
        list.append(m_engine->newValueForBindingProperty(property));
    }

    return list;
}

QJSValueList ModelNodeJSWrapper::propertyNames() const
{
    QJSValueList list;
    const auto names = m_modelNode.propertyNames();
    for (const auto &name : names) {
        list.append(QJSValue(QString::fromUtf8(name)));
    }
    return list;
}

AbstractPropertyJSWrapper::AbstractPropertyJSWrapper(ConsoleEngine *engine, QObject *parent)
    : QObject{parent}
    , m_engine(engine)
{}

QJSValue AbstractPropertyJSWrapper::parentModelNode() const
{
    return m_engine->newValueForModelNode(property().parentModelNode());
}

QJSValue AbstractPropertyJSWrapper::toVariantProperty() const
{
    return engine()->newValueForVariantProperty(m_property.toVariantProperty());
}

QJSValue AbstractPropertyJSWrapper::toNodeListProperty() const
{
    return engine()->newValueForNodeListProperty(m_property.toNodeListProperty());
}

QJSValue AbstractPropertyJSWrapper::toNodeAbstractProperty() const
{
   return engine()->newValueForNodeAbstractProperty(m_property.toNodeAbstractProperty());
}

QJSValue AbstractPropertyJSWrapper::toBindingProperty() const
{
    return engine()->newValueForBindingProperty(m_property.toBindingProperty());
}

QJSValue AbstractPropertyJSWrapper::toNodeProperty() const
{
    return engine()->newValueForNodeProperty(m_property.toNodeProperty());
}

QJSValue BindingPropertyJSWrapper::resolveToModelNode() const
{
    return engine()->newValueForModelNode(bindingProperty().resolveToModelNode());
}

QJSValue BindingPropertyJSWrapper::resolveToProperty() const
{
    return engine()->newValueForAbstractProperty(property().toBindingProperty().resolveToProperty());
}

QJSValueList BindingPropertyJSWrapper::resolveToModelNodeList() const
{
    return engine()->newValueListForModelNodeList(bindingProperty().resolveToModelNodeList());
}

void NodeAbstractPropertyJSWrapper::reparentHere(const QJSValue &value)
{
    const ModelNode node = engine()->valueToModelNode(value);

    if (node.isValid())
        return property().toNodeAbstractProperty().reparentHere(node);
}

int NodeAbstractPropertyJSWrapper::indexOf(const QJSValue &value) const
{
    const ModelNode node = engine()->valueToModelNode(value);

    if (node.isValid())
        return property().toNodeAbstractProperty().indexOf(node);

    return -1;
}

QJSValue NodeAbstractPropertyJSWrapper::parentProperty() const
{
    return engine()->newValueForAbstractProperty(
        property().toNodeAbstractProperty().parentProperty());
}

QJSValueList NodeAbstractPropertyJSWrapper::allSubNodes()
{
    return engine()->newValueListForModelNodeList(
        property().toNodeAbstractProperty().allSubNodes());
}

QJSValueList NodeAbstractPropertyJSWrapper::directSubNodes() const
{
    return engine()->newValueListForModelNodeList(
        property().toNodeAbstractProperty().directSubNodes());
}

QJSValueList NodeListPropertyJSWrapper::toModelNodeList() const
{
    return engine()->newValueListForModelNodeList(
        property().toNodeListProperty().toModelNodeList());
}

QJSValue NodeListPropertyJSWrapper::at(int index) const
{
    return engine()->newValueForModelNode(
        property().toNodeListProperty().at(index));
}

void NodePropertyJSWrapper::setDynamicTypeNameAndsetModelNode(const QmlDesigner::TypeName &typeName, const QJSValue &value)
{
    const ModelNode node = engine()->valueToModelNode(value);
    if (node.isValid())
        property().toNodeProperty().setDynamicTypeNameAndsetModelNode(typeName, node);
}

} // namespace Internal

} // namespace QmlDesigner
