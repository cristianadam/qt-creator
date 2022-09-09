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

#include "dynamicpropertiesproxymodel.h"

#include "propertyeditorvalue.h"

#include <dynamicpropertiesmodel.h>

#include <abstractproperty.h>
#include <bindingeditor.h>
#include <variantproperty.h>
#include <qmldesignerconstants.h>
#include <qmldesignerplugin.h>

#include <utils/qtcassert.h>

using namespace QmlDesigner;

static const int propertyNameRole = Qt::UserRole + 1;
static const int propertyTypeRole = Qt::UserRole + 2;
static const int propertyValueRole = Qt::UserRole + 3;
static const int propertyBindingRole = Qt::UserRole + 4;

DynamicPropertiesProxyModel::DynamicPropertiesProxyModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void DynamicPropertiesProxyModel::initModel(QmlDesigner::Internal::DynamicPropertiesModel *model)
{
    m_model = model;

    connect(m_model, &QAbstractItemModel::modelAboutToBeReset,
            this, &QAbstractItemModel::modelAboutToBeReset);
    connect(m_model, &QAbstractItemModel::modelReset,
            this, &QAbstractItemModel::modelReset);

    connect(m_model, &QAbstractItemModel::rowsAboutToBeRemoved,
            this, &QAbstractItemModel::rowsAboutToBeRemoved);
    connect(m_model, &QAbstractItemModel::rowsRemoved,
            this, &QAbstractItemModel::rowsRemoved);
    connect(m_model, &QAbstractItemModel::rowsInserted,
            this, &QAbstractItemModel::rowsInserted);

    connect(m_model, &QAbstractItemModel::dataChanged,
            this, [this](const QModelIndex &topLeft, const QModelIndex &, const QList<int> &) {
                emit dataChanged(index(topLeft.row(), 0),
                                 index(topLeft.row(), 0),
                                 { propertyNameRole, propertyTypeRole,
                                   propertyValueRole, propertyBindingRole });
            });
}

int DynamicPropertiesProxyModel::rowCount(const QModelIndex &) const
{
    return m_model->rowCount();
}

QHash<int, QByteArray> DynamicPropertiesProxyModel::roleNames() const
{
    static QHash<int, QByteArray> roleNames{{propertyNameRole, "propertyName"},
                                            {propertyTypeRole, "propertyType"},
                                            {propertyValueRole, "propertyValue"},
                                            {propertyBindingRole, "propertyBinding"}};

    return roleNames;
}

QVariant DynamicPropertiesProxyModel::data(const QModelIndex &index, int role) const
{
    if (index.isValid() && index.row() < rowCount()) {
        AbstractProperty property = m_model->abstractPropertyForRow(index.row());

        QTC_ASSERT(property.isValid(), return QVariant());

        if (role == propertyNameRole) {
            return property.name();
        } else if (propertyTypeRole) {
            return property.dynamicTypeName();
        } else if (role == propertyValueRole) {
            QmlObjectNode objectNode = property.parentQmlObjectNode();
            return objectNode.modelValue(property.name());
        } else if (role == propertyBindingRole) {
            if (property.isBindingProperty())
                return property.toBindingProperty().expression();
            return QVariant();
        }
        qWarning() << Q_FUNC_INFO << "invalid role";
    } else {
        qWarning() << Q_FUNC_INFO << "invalid index";
    }

    return QVariant();
}

void DynamicPropertiesProxyModel::registerDeclarativeType()
{
    static bool registered = false;
    if (!registered)
        qmlRegisterType<DynamicPropertiesProxyModel>("HelperWidgets", 2, 0, "DynamicPropertiesModel");
}

QmlDesigner::Internal::DynamicPropertiesModel *DynamicPropertiesProxyModel::dynamicPropertiesModel() const
{
    return m_model;
}

QString DynamicPropertiesProxyModel::newPropertyName() const
{
    auto propertiesModel = dynamicPropertiesModel();

    return QString::fromUtf8(propertiesModel->unusedProperty(
        propertiesModel->singleSelectedNode()));
}

void DynamicPropertiesProxyModel::createProperty(const QString &name, const QString &type)
{
    QmlDesignerPlugin::emitUsageStatistics(Constants::EVENT_PROPERTY_ADDED);

    const auto selectedNodes = dynamicPropertiesModel()->selectedNodes();
    if (selectedNodes.count() == 1) {
        const ModelNode modelNode = selectedNodes.constFirst();
        if (modelNode.isValid()) {
            try {
                if (Internal::DynamicPropertiesModel::isValueType(type.toUtf8())) {
                    QVariant value;
                    if (type == "int")
                        value = 0;
                    else if (type == "real")
                        value = 0.0;
                    else if (type == "color")
                        value = QColor(255, 255, 255);
                    else if (type == "string")
                        value = "";
                    else if (type == "bool")
                        value = false;
                    else if (type == "url")
                        value = "";
                    else if (type == "variant")
                        value = "";

                    modelNode.variantProperty(name.toUtf8())
                        .setDynamicTypeNameAndValue(type.toUtf8(), value);
                } else {
                    QString expression;
                    if (type == "alias")
                        expression = "null";
                    else if (type == "TextureInput")
                        expression = "null";
                    else if (type == "vector2d")
                        expression = "Qt.vector2d(0, 0)";
                    else if (type == "vector3d")
                        expression = "Qt.vector3d(0, 0, 0)";
                    else if (type == "vector4d")
                        expression = "Qt.vector4d(0, 0, 0 ,0)";

                    modelNode.bindingProperty(name.toUtf8())
                        .setDynamicTypeNameAndExpression(type.toUtf8(), expression);
                }
            } catch (Exception &e) {
                e.showException();
            }
        }
    } else {
        qWarning() << " BindingModel::addBindingForCurrentNode not one node selected";
    }
}

DynamicPropertyRow::DynamicPropertyRow(QObject *parent)
{
    m_backendValue = new PropertyEditorValue(this);

    QObject::connect(m_backendValue,
                     &PropertyEditorValue::valueChanged,
                     this,
                     [this](const QString &, const QVariant &value) { commitValue(value); });

    QObject::connect(m_backendValue,
                     &PropertyEditorValue::expressionChanged,
                     this,
                     [this](const QString &) { commitExpression(m_backendValue->expression()); });
}

DynamicPropertyRow::~DynamicPropertyRow()
{
    clearProxyBackendValues();
}

void DynamicPropertyRow::registerDeclarativeType()
{
    qmlRegisterType<DynamicPropertyRow>("HelperWidgets", 2, 0, "DynamicPropertyRow");
}

void DynamicPropertyRow::setRow(int r)
{
    if (m_row == r)
        return;

    m_row = r;
    setupBackendValue();
    emit rowChanged();
}

int DynamicPropertyRow::row() const
{
    return m_row;
}

void DynamicPropertyRow::setModel(DynamicPropertiesProxyModel *model)
{
    if (model == m_model)
        return;

    if (m_model) {
        disconnect(m_model, &QAbstractItemModel::dataChanged,
                   this, &DynamicPropertyRow::handleDataChanged);
    }

    m_model = model;

    if (m_model) {
        connect(m_model, &QAbstractItemModel::dataChanged,
                this, &DynamicPropertyRow::handleDataChanged);

        if (m_row != -1)
            setupBackendValue();
    }

    emit modelChanged();
}

DynamicPropertiesProxyModel *DynamicPropertyRow::model() const
{
    return m_model;
}

PropertyEditorValue *DynamicPropertyRow::backendValue() const
{
    return m_backendValue;
}

void DynamicPropertyRow::remove()
{
    m_model->dynamicPropertiesModel()->deleteDynamicPropertyByRow(m_row);
}

PropertyEditorValue *DynamicPropertyRow::createProxyBackendValue()
{

    PropertyEditorValue *newValue = new PropertyEditorValue(this);
    m_proxyBackendValues.append(newValue);

    return newValue;
}

void DynamicPropertyRow::clearProxyBackendValues()
{
    qDeleteAll(m_proxyBackendValues);
    m_proxyBackendValues.clear();
}

void DynamicPropertyRow::setupBackendValue()
{
    if (!m_model)
        return;

    QmlDesigner::AbstractProperty property = m_model->dynamicPropertiesModel()->abstractPropertyForRow(m_row);
    if (!property.isValid())
        return;

    if (m_backendValue->name() != property.name())
        m_backendValue->setName(property.name());

    ModelNode node = property.parentModelNode();
    if (node != m_backendValue->modelNode())
        m_backendValue->setModelNode(node);

    QVariant modelValue = property.parentQmlObjectNode().modelValue(property.name());
    if (modelValue != m_backendValue->value()) {
        m_backendValue->setValue({});
        m_backendValue->setValue(modelValue);
    }

    if (property.isBindingProperty()) {
        QString expression = property.toBindingProperty().expression();
        if (m_backendValue->expression() != expression)
            m_backendValue->setExpression(expression);
    }

    emit m_backendValue->isBoundChanged();
}

void DynamicPropertyRow::commitValue(const QVariant &value)
{
    auto propertiesModel = m_model->dynamicPropertiesModel();
    VariantProperty variantProperty = propertiesModel->variantPropertyForRow(m_row);

    if (!Internal::DynamicPropertiesModel::isValueType(variantProperty.dynamicTypeName()))
        return;

    auto view = propertiesModel->view();
    RewriterTransaction transaction = view->beginRewriterTransaction(
        QByteArrayLiteral("DynamicPropertiesModel::commitValue"));
    try {
        if (view->currentState().isBaseState()) {
        if (variantProperty.value() != value)
            variantProperty.setDynamicTypeNameAndValue(variantProperty.dynamicTypeName(), value);
        } else {
            QmlObjectNode objectNode = variantProperty.parentQmlObjectNode();
            QTC_CHECK(objectNode.isValid());
            PropertyName name = variantProperty.name();
            if (objectNode.isValid() && objectNode.modelValue(name) != value)
                objectNode.setVariantProperty(name, value);
        }
        transaction.commit(); //committing in the try block
    } catch (Exception &e) {
        e.showException();
    }
}

void DynamicPropertyRow::commitExpression(const QString &expression)
{
    auto propertiesModel = m_model->dynamicPropertiesModel();
    BindingProperty bindingProperty = propertiesModel->bindingPropertyForRow(m_row);

    auto view = propertiesModel->view();
    RewriterTransaction transaction = view->beginRewriterTransaction(
        QByteArrayLiteral("DynamicPropertiesModel::commitExpression"));
    try {
        QString theExpression = expression;
        if (theExpression.isEmpty())
            theExpression = "null";

        if (bindingProperty.expression() != theExpression) {
            bindingProperty.setDynamicTypeNameAndExpression(bindingProperty.dynamicTypeName(),
                                                            theExpression);
        }
        transaction.commit(); //committing in the try block
    } catch (Exception &e) {
        e.showException();
    }
    return;
}

void DynamicPropertyRow::handleDataChanged(const QModelIndex &topLeft, const QModelIndex &, const QList<int> &)
{
    if (topLeft.row() == m_row)
        setupBackendValue();
}
