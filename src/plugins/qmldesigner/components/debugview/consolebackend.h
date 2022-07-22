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

#pragma once

#include <QJSValue>
#include <QObject>

#include <abstractproperty.h>
#include <bindingproperty.h>
#include <modelnode.h>
#include <nodeabstractproperty.h>
#include <variantproperty.h>
#include <nodelistproperty.h>
#include <nodeproperty.h>

namespace QmlDesigner {

namespace Internal {

class DebugView;
class ConsoleEngine;

//AbstractProperty

class AbstractPropertyJSWrapper : public QObject
{
    Q_OBJECT

public:
    explicit AbstractPropertyJSWrapper(ConsoleEngine *engine, QObject *parent = nullptr);
    void setProperty(const AbstractProperty &property) { m_property = property; }

    AbstractProperty property() const { return m_property; }

    ConsoleEngine *engine() const { return m_engine; }

public slots:
    Q_INVOKABLE QmlDesigner::PropertyName name() const { return property().name(); }

    Q_INVOKABLE bool isValid() const { return property().isValid(); }
    Q_INVOKABLE bool exists() const { return property().exists(); }
    Q_INVOKABLE QJSValue parentModelNode() const;

    Q_INVOKABLE bool isDefaultProperty() const { return property().isDefaultProperty(); }

    Q_INVOKABLE QJSValue toVariantProperty() const;
    Q_INVOKABLE QJSValue toNodeListProperty() const;
    Q_INVOKABLE QJSValue toNodeAbstractProperty() const;
    Q_INVOKABLE QJSValue toBindingProperty() const;
    Q_INVOKABLE QJSValue toNodeProperty() const;
    //SignalHandlerProperty toSignalHandlerProperty() const;
    //SignalDeclarationProperty toSignalDeclarationProperty() const;

    Q_INVOKABLE bool isVariantProperty() const { return property().isVariantProperty(); }
    Q_INVOKABLE bool isNodeListProperty() const { return property().isNodeListProperty(); }
    Q_INVOKABLE bool isNodeAbstractProperty() const { return property().isNodeAbstractProperty(); }
    Q_INVOKABLE bool isBindingProperty() const { return property().isBindingProperty(); }
    Q_INVOKABLE bool isNodeProperty() const { return property().isNodeProperty(); }
    Q_INVOKABLE bool isSignalHandlerProperty() const
    {
        return property().isSignalHandlerProperty();
    }
    Q_INVOKABLE bool isSignalDeclarationProperty() const
    {
        return property().isSignalDeclarationProperty();
    }

    Q_INVOKABLE bool isDynamic() const { return property().isDynamic(); }
    Q_INVOKABLE QmlDesigner::TypeName dynamicTypeName() const
    {
        return property().dynamicTypeName();
    }

private:
    AbstractProperty m_property;
    ConsoleEngine *m_engine = nullptr;
};

class VariantPropertyJSWrapper : public AbstractPropertyJSWrapper
{
    Q_OBJECT

public:
    explicit VariantPropertyJSWrapper(ConsoleEngine *engine, QObject *parent = nullptr)
        : AbstractPropertyJSWrapper(engine, parent)
    {}

public slots:

    Q_INVOKABLE void setValue(const QVariant &value) { variantProperty().setValue(value); }
    Q_INVOKABLE QVariant value() const { return variantProperty().value(); }

    //Q_INVOKABLE void setEnumeration(const EnumerationName &enumerationName);
    Q_INVOKABLE QString enumeration() const { return variantProperty().enumeration().toString(); }
    Q_INVOKABLE bool holdsEnumeration() const { return variantProperty().holdsEnumeration(); }

    Q_INVOKABLE void setDynamicTypeNameAndValue(const QmlDesigner::TypeName &type,
                                                const QVariant &value)
    {
        variantProperty().setDynamicTypeNameAndValue(type, value);
    }
    //void setDynamicTypeNameAndEnumeration(const TypeName &type, const EnumerationName &enumerationName);

private:
    VariantProperty variantProperty() const { return property().toVariantProperty(); }
};

class BindingPropertyJSWrapper : public AbstractPropertyJSWrapper
{
    Q_OBJECT

public:
    explicit BindingPropertyJSWrapper(ConsoleEngine *engine, QObject *parent = nullptr)
        : AbstractPropertyJSWrapper(engine, parent)
    {}

public slots:

    Q_INVOKABLE void setDynamicTypeNameAndExpression(const QmlDesigner::TypeName &type,
                                                     const QString &expression)
    {
        bindingProperty().setDynamicTypeNameAndExpression(type, expression);
    }

    Q_INVOKABLE QJSValue resolveToModelNode() const;
    Q_INVOKABLE QJSValue resolveToProperty() const;
    Q_INVOKABLE bool isList() const { return bindingProperty().isList(); }
    Q_INVOKABLE QJSValueList resolveToModelNodeList() const;

    Q_INVOKABLE bool isAlias() const { return bindingProperty().isAlias(); }
    Q_INVOKABLE bool isAliasExport() const { return bindingProperty().isAliasExport(); }

private:
    BindingProperty bindingProperty() const { return property().toBindingProperty(); }
};

//AbstractProperty

class NodeAbstractPropertyJSWrapper : public AbstractPropertyJSWrapper
{
    Q_OBJECT

public:
    explicit NodeAbstractPropertyJSWrapper(ConsoleEngine *engine, QObject *parent = nullptr)
        : AbstractPropertyJSWrapper(engine, parent)
    {}

public slots:
    Q_INVOKABLE void reparentHere(const QJSValue &value);
    Q_INVOKABLE bool isEmpty() const { return property().toNodeAbstractProperty().isEmpty(); }
    Q_INVOKABLE int count() const { return property().toNodeAbstractProperty().count(); }

    Q_INVOKABLE int indexOf(const QJSValue &node) const;
    Q_INVOKABLE QJSValue parentProperty() const;

    Q_INVOKABLE QJSValueList allSubNodes();
    Q_INVOKABLE QJSValueList directSubNodes() const;
};

class NodeListPropertyJSWrapper : public NodeAbstractPropertyJSWrapper
{
    Q_OBJECT

        public:
                 explicit NodeListPropertyJSWrapper(ConsoleEngine *engine, QObject *parent = nullptr)
        : NodeAbstractPropertyJSWrapper(engine, parent)
    {}

public slots:
    Q_INVOKABLE QJSValueList toModelNodeList() const;
    //QList<QmlObjectNode> toQmlObjectNodeList() const;
    Q_INVOKABLE void slide(int a, int b) const { property().toNodeListProperty().slide(a, b); }
    Q_INVOKABLE void swap(int a, int b) const { property().toNodeListProperty().swap(a, b); }
    Q_INVOKABLE QJSValue at(int index) const;
};

class NodePropertyJSWrapper : public NodeAbstractPropertyJSWrapper
{
    Q_OBJECT

        public:
                 explicit NodePropertyJSWrapper(ConsoleEngine *engine, QObject *parent = nullptr)
        : NodeAbstractPropertyJSWrapper(engine, parent)
    {}

public slots:
    Q_INVOKABLE void setDynamicTypeNameAndsetModelNode(const QmlDesigner::TypeName &typeName,
                                                       const QJSValue &modelNode);
};

class ModelNodeJSWrapper : public QObject
{
    Q_OBJECT

public:
    explicit ModelNodeJSWrapper(ConsoleEngine *engine, QObject *parent = nullptr);

    void setModelNode(const ModelNode &modelNode) { m_modelNode = modelNode; }

    ModelNode modelNode() const { return m_modelNode; }

public slots:
    Q_INVOKABLE QmlDesigner::TypeName type() const { return m_modelNode.type(); }
    Q_INVOKABLE QString simplifiedTypeName() const { return m_modelNode.simplifiedTypeName(); }
    Q_INVOKABLE QString displayName() const { return m_modelNode.displayName(); }
    Q_INVOKABLE int minorVersion() const { return m_modelNode.minorVersion(); }
    Q_INVOKABLE int majorVersion() const { return m_modelNode.majorVersion(); }

    Q_INVOKABLE bool isValid() const { return m_modelNode.isValid(); }
    Q_INVOKABLE bool isInHierarchy() const { return m_modelNode.isInHierarchy(); }

    Q_INVOKABLE QString id() const { return m_modelNode.id(); }
    Q_INVOKABLE QString validId() { return m_modelNode.validId(); }

    Q_INVOKABLE bool hasId() const { return m_modelNode.hasId(); }

    Q_INVOKABLE bool hasMetaInfo() const { return m_modelNode.hasMetaInfo(); }

    Q_INVOKABLE bool isSelected() const { return m_modelNode.isSelected(); }
    Q_INVOKABLE bool isRootNode() const { return m_modelNode.isRootNode(); }

    Q_INVOKABLE qint32 internalId() const { return m_modelNode.internalId(); }

    Q_INVOKABLE QString nodeSource() const { return m_modelNode.nodeSource(); }

    Q_INVOKABLE bool isComponent() const { return m_modelNode.isComponent(); }

    Q_INVOKABLE QString behaviorPropertyName() const { return m_modelNode.behaviorPropertyName(); }

    Q_INVOKABLE bool hasProperty(const QmlDesigner::PropertyName &name) const
    {
        return m_modelNode.hasProperty(name);
    }
    Q_INVOKABLE bool hasVariantProperty(const QmlDesigner::PropertyName &name) const
    {
        return m_modelNode.hasVariantProperty(name);
    }
    Q_INVOKABLE bool hasBindingProperty(const QmlDesigner::PropertyName &name) const
    {
        return m_modelNode.hasBindingProperty(name);
    }
    Q_INVOKABLE bool hasNodeAbstractProperty(const QmlDesigner::PropertyName &name) const
    {
        return m_modelNode.hasNodeAbstractProperty(name);
    }
    Q_INVOKABLE bool hasDefaultNodeAbstractProperty() const
    {
        return m_modelNode.hasDefaultNodeAbstractProperty();
    }
    Q_INVOKABLE bool hasDefaultNodeListProperty() const
    {
        return m_modelNode.hasDefaultNodeListProperty();
    }
    Q_INVOKABLE bool hasDefaultNodeProperty() const { return m_modelNode.hasDefaultNodeProperty(); }
    Q_INVOKABLE bool hasNodeProperty(const QmlDesigner::PropertyName &name) const
    {
        return m_modelNode.hasNodeProperty(name);
    }
    Q_INVOKABLE bool hasNodeListProperty(const QmlDesigner::PropertyName &name) const
    {
        return m_modelNode.hasNodeListProperty(name);
    }

    Q_INVOKABLE bool hasAnySubModelNodes() const { return m_modelNode.hasAnySubModelNodes(); }

    Q_INVOKABLE bool hasParentProperty() const { return m_modelNode.hasParentProperty(); }

    //Q_INVOKABLE bool isAncestorOf(const ModelNode &node) const  { return m_modelNode.isAncestorOf(); }
    Q_INVOKABLE void selectNode() { m_modelNode.selectNode(); }
    Q_INVOKABLE void deselectNode() { m_modelNode.deselectNode(); }

    Q_INVOKABLE bool isSubclassOf(const QmlDesigner::TypeName &typeName,
                                  int majorVersion = -1,
                                  int minorVersion = -1) const
    {
        return m_modelNode.isSubclassOf(typeName, majorVersion, minorVersion);
    }

    Q_INVOKABLE void setIdWithRefactoring(const QString &id)
    {
        m_modelNode.setIdWithRefactoring(id);
    }
    Q_INVOKABLE void setIdWithoutRefactoring(const QString &id)
    {
        m_modelNode.setIdWithoutRefactoring(id);
    }

    Q_INVOKABLE QJSValueList directSubModelNodes() const;
    Q_INVOKABLE QJSValueList directSubModelNodesOfType(const QmlDesigner::TypeName &typeName) const;
    Q_INVOKABLE QJSValueList subModelNodesOfType(const QmlDesigner::TypeName &typeName) const;

    Q_INVOKABLE QJSValueList allSubModelNodes() const;
    Q_INVOKABLE QJSValueList allSubModelNodesAndThisNode() const;

    Q_INVOKABLE bool locked() const { return m_modelNode.locked(); }
    Q_INVOKABLE void setLocked(bool value) { m_modelNode.setLocked(value); }

    Q_INVOKABLE void destroy() { m_modelNode.destroy(); }

    QJSValueList properties() const;
    QJSValueList variantProperties() const;
    QJSValueList nodeAbstractProperties() const;
    QJSValueList nodeProperties() const;
    QJSValueList nodeListProperties() const;
    QJSValueList bindingProperties() const;
    //QJSValueList signalProperties() const;
    QJSValueList propertyNames() const;

private:
    ModelNode m_modelNode;
    ConsoleEngine *m_engine = nullptr;
};

class ConsoleBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QJSValue rootModelNode READ rootModelNode CONSTANT)

public:
    explicit ConsoleBackend(ConsoleEngine *engine, DebugView *view);

    void modelAttached();

signals:
    void selectionChanged();

public slots:
    Q_INVOKABLE QJSValueList selectedModelNodes() const;
    Q_INVOKABLE QJSValue firstSelectedModelNode() const;
    Q_INVOKABLE QJSValue singleSelectedModelNode() const;

    Q_INVOKABLE QJSValue modelNodeForId(const QString &id);
    Q_INVOKABLE bool hasId(const QString &id) const;

    Q_INVOKABLE QJSValue modelNodeForInternalId(qint32 internalId) const;
    Q_INVOKABLE bool hasModelNodeForInternalId(qint32 internalId) const;

    Q_INVOKABLE QJSValueList allModelNodes() const;
    Q_INVOKABLE QJSValueList allModelNodesOfType(const QmlDesigner::TypeName &typeName) const;

private:
    DebugView *m_debugView = nullptr;
    ConsoleEngine *m_engine = nullptr;
    QJSValue rootModelNode() const { return m_rootModelNode; }
    QJSValue m_rootModelNode;
    ModelNodeJSWrapper *m_rootNodeObject = nullptr;
};

} // namespace Internal

} // namespace QmlDesigner
