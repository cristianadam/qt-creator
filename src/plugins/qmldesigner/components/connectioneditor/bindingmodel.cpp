// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bindingmodel.h"
#include "bindingmodelitem.h"
#include "connectionview.h"
#include "connectioneditorutils.h"

#include <bindingproperty.h>
#include <nodemetainfo.h>
#include <nodeproperty.h>
#include <rewritertransaction.h>
#include <rewritingexception.h>
#include <variantproperty.h>

#include <utils/qtcassert.h>

namespace QmlDesigner {

BindingModel::BindingModel(ConnectionView *parent)
    : QStandardItemModel(parent)
    , m_connectionView(parent)
    , m_delegate(new BindingModelBackendDelegate(this))
{
    setHorizontalHeaderLabels(BindingModelItem::headerLabels());
}

ConnectionView *BindingModel::connectionView() const
{
    return m_connectionView;
}

BindingModelBackendDelegate *BindingModel::delegate() const
{
    return m_delegate;
}

int BindingModel::currentIndex() const
{
    return m_currentIndex;
}

BindingProperty BindingModel::currentProperty() const
{
    return propertyForRow(m_currentIndex);
}

BindingProperty BindingModel::propertyForRow(int row) const
{
    if (!m_connectionView)
        return {};

    if (!m_connectionView->isAttached())
        return {};

    if (auto *item = itemForRow(row)) {
        int internalId = item->internalId();
        if (ModelNode node = m_connectionView->modelNodeForInternalId(internalId); node.isValid())
            return node.bindingProperty(item->targetPropertyName());
    }

    return {};
}

static PropertyName unusedProperty(const ModelNode &modelNode)
{
    if (modelNode.metaInfo().isValid()) {
        for (const auto &property : modelNode.metaInfo().properties()) {
            if (property.isWritable() && !modelNode.hasProperty(property.name()))
                return property.name();
        }
    }
    return "none";
}

void BindingModel::add()
{
    if (const QList<ModelNode> nodes = connectionView()->selectedModelNodes(); nodes.size() == 1) {
        const ModelNode modelNode = nodes.constFirst();
        if (modelNode.isValid()) {
            try {
                PropertyName name = unusedProperty(modelNode);
                modelNode.bindingProperty(name).setExpression(QLatin1String("none.none"));
            } catch (RewritingException &e) {
                showErrorMessage(e.description());
                reset();
            }
        }
    } else {
        qWarning() << __FUNCTION__ << " Requires exactly one selected node";
    }
}

void BindingModel::remove(int row)
{
    if (BindingProperty property = propertyForRow(row); property.isValid()) {
        ModelNode node = property.parentModelNode();
        node.removeProperty(property.name());
    }

    reset();
}

void BindingModel::reset(const QList<ModelNode> &nodes)
{
    if (!connectionView())
        return;

    if (!connectionView()->isAttached())
        return;

    AbstractProperty current = currentProperty();

    clear();

    if (!nodes.isEmpty()) {
        for (const ModelNode &modelNode : nodes)
            addModelNode(modelNode);
    } else {
        for (const ModelNode &modelNode : connectionView()->selectedModelNodes())
            addModelNode(modelNode);
    }

    setCurrentProperty(current);
}

void BindingModel::setCurrentIndex(int i)
{
    if (m_currentIndex != i) {
        m_currentIndex = i;
        emit currentIndexChanged();
    }
    m_delegate->update(currentProperty(), m_connectionView);
}

void BindingModel::setCurrentProperty(const AbstractProperty &property)
{
    if (auto index = rowForProperty(property))
        setCurrentIndex(*index);
}

void BindingModel::updateItem(const BindingProperty &property)
{
    if (auto *item = itemForProperty(property))
        item->updateProperty(property);
    else
        appendRow(new BindingModelItem(property));
}

void BindingModel::removeItem(const AbstractProperty &property)
{

    AbstractProperty current = currentProperty();
    if (auto index = rowForProperty(property))
        static_cast<void>(removeRow(*index));

    setCurrentProperty(current);
    emit currentIndexChanged();
}

void BindingModel::commitExpression(int row, const QString &expression)
{
    QTC_ASSERT(connectionView(), return);

    BindingProperty bindingProperty = propertyForRow(row);
    if (!bindingProperty.isValid())
        return;

    connectionView()->executeInTransaction(__FUNCTION__, [&bindingProperty, expression]() {
        bindingProperty.setExpression(expression.trimmed());
    });
}

QHash<int, QByteArray> BindingModel::roleNames() const
{
    return BindingModelItem::roleNames();
}

std::optional<int> BindingModel::rowForProperty(const AbstractProperty &property) const
{
    PropertyName name = property.name();
    int internalId = property.parentModelNode().internalId();

    for (int i = 0; i < rowCount(); ++i) {
        if (auto *item = itemForRow(i)) {
            if (item->targetPropertyName() == name && item->internalId() == internalId)
                return i;
        }
    }
    return std::nullopt;
}

BindingModelItem *BindingModel::itemForRow(int row) const
{
    if (QModelIndex idx = index(row, 0); idx.isValid())
        return dynamic_cast<BindingModelItem *>(itemFromIndex(idx));
    return nullptr;
}

BindingModelItem *BindingModel::itemForProperty(const AbstractProperty &property) const
{
    if (auto row = rowForProperty(property))
        return itemForRow(*row);
    return nullptr;
}

void BindingModel::addModelNode(const ModelNode &node)
{
    if (!node.isValid())
        return;

    const QList<BindingProperty> bindingProperties = node.bindingProperties();
    for (const BindingProperty &property : bindingProperties)
        appendRow(new BindingModelItem(property));
}

BindingModelBackendDelegate::BindingModelBackendDelegate(BindingModel *parent)
    : QObject(parent)
    , m_targetNode()
    , m_property()
    , m_sourceNode()
    , m_sourceNodeProperty()
{
    connect(&m_sourceNode, &StudioQmlComboBoxBackend::activated, this, [this]() {
        expressionChanged();
    });

    connect(&m_sourceNodeProperty, &StudioQmlComboBoxBackend::activated, this, [this]() {
        expressionChanged();
    });

    connect(&m_property, &StudioQmlComboBoxBackend::activated, this, [this]() {
        handleTargetNameChanged();
    });
}

void BindingModelBackendDelegate::update(const BindingProperty &property, AbstractView *view)
{
    if (!property.isValid())
        return;

    auto addName = [](QStringList&& list, const QString& name) {
        if (!list.contains(name))
            list.prepend(name);
        return std::move(list);
    };

    auto [sourceNodeName, sourcePropertyName] = splitExpression(property.expression());

    QString targetName = QString::fromUtf8(property.name());
    m_targetNode = idOrTypeName(property.parentModelNode());

    auto modelNodes = addName(availableModelNodes(view), sourceNodeName);
    m_sourceNode.setModel(modelNodes);
    m_sourceNode.setCurrentText(sourceNodeName);

    auto sourceproperties = addName(availableSourceProperties(property, view), sourcePropertyName);
    m_sourceNodeProperty.setModel(sourceproperties);
    m_sourceNodeProperty.setCurrentText(sourcePropertyName);

    auto targetProperties = addName(availableTargetProperties(property), targetName);
    m_property.setModel(targetProperties);
    m_property.setCurrentText(targetName);

    emit targetNodeChanged();
}

QString BindingModelBackendDelegate::targetNode() const
{
    return m_targetNode;
}

StudioQmlComboBoxBackend *BindingModelBackendDelegate::property()
{
    return &m_property;
}

StudioQmlComboBoxBackend *BindingModelBackendDelegate::sourceNode()
{
    return &m_sourceNode;
}

StudioQmlComboBoxBackend *BindingModelBackendDelegate::sourceProperty()
{
    return &m_sourceNodeProperty;
}

void BindingModelBackendDelegate::expressionChanged()
{
    BindingModel *model = qobject_cast<BindingModel *>(parent());
    QTC_ASSERT(model, return);

    const QString sourceNode = m_sourceNode.currentText();
    const QString sourceProperty = m_sourceNodeProperty.currentText();

    QString expression;
    if (sourceProperty.isEmpty())
        expression = sourceNode;
    else
        expression = sourceNode + QLatin1String(".") + sourceProperty;

    int row = model->currentIndex();
    model->commitExpression(row, expression);
    setupSourcePropertyNames();
}

void BindingModelBackendDelegate::handleTargetNameChanged()
{
    BindingModel *model = qobject_cast<BindingModel *>(parent());

    QTC_ASSERT(model, return );
    QTC_ASSERT(model->connectionView(), return );

    BindingProperty bindingProperty = model->propertyForRow(model->currentIndex());

    const PropertyName newName = m_property.currentText().toUtf8();
    const QString expression = bindingProperty.expression();
    const PropertyName dynamicPropertyType = bindingProperty.dynamicTypeName();
    ModelNode targetNode = bindingProperty.parentModelNode();

    if (!newName.isEmpty()) {
        model->connectionView()
            ->executeInTransaction("BindingModelBackendDelegate::handleTargetNameChanged", [&]() {
                if (bindingProperty.isDynamic()) {
                    targetNode.bindingProperty(newName)
                        .setDynamicTypeNameAndExpression(dynamicPropertyType, expression);
                } else {
                    targetNode.bindingProperty(newName).setExpression(expression);
                }
                targetNode.removeProperty(bindingProperty.name());
            });
    }
    setupSourcePropertyNames();
}

void BindingModelBackendDelegate::setupSourcePropertyNames()
{
    BindingModel *model = qobject_cast<BindingModel *>(parent());

    QTC_ASSERT(model, return );

    auto view = model->connectionView();

    BindingProperty property = model->propertyForRow(model->currentIndex());

    auto [sourceNodeName, sourcePropertyName] = splitExpression(property.expression());

    QString targetName = QString::fromUtf8(property.name());
    m_targetNode = idOrTypeName(property.parentModelNode());

    auto addName = [](QStringList &&list, const QString &name) {
        if (!list.contains(name))
            list.prepend(name);
        return std::move(list);
    };

    auto sourceproperties = addName(availableSourceProperties(property, view), sourcePropertyName);
    m_sourceNodeProperty.setModel(sourceproperties);
    m_sourceNodeProperty.setCurrentText(sourcePropertyName);

    /*
     *
    QString sourceNodeName;
    QString sourcePropertyName;

    model->getExpressionStrings(bindingProperty, &sourceNodeName, &sourcePropertyName);

    auto properties = model->possibleSourceProperties(bindingProperty);

    if (!properties.contains(sourcePropertyName))
        properties.append(sourcePropertyName);

    m_sourceNodeProperty.setModel(properties);
    m_sourceNodeProperty.setCurrentText(sourcePropertyName);
*/
}

} // namespace QmlDesigner
