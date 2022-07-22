/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include <connectionview.h>
#include <dynamicpropertiesmodel.h>

#include <abstractproperty.h>
#include <bindingeditor.h>
#include <variantproperty.h>
#include <qmldesignerconstants.h>
#include <qmldesignerplugin.h>

#include <utils/qtcassert.h>

using namespace QmlDesigner;

DynamicPropertiesProxyModel::DynamicPropertiesProxyModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(dynamicPropertiesModel(),
            &QAbstractItemModel::modelAboutToBeReset,
            this,
            &QAbstractItemModel::modelAboutToBeReset);
    connect(dynamicPropertiesModel(),
            &QAbstractItemModel::modelReset,
            this,
            &QAbstractItemModel::modelReset);

    connect(dynamicPropertiesModel(),
            &QAbstractItemModel::rowsAboutToBeRemoved,
            this,
            &QAbstractItemModel::rowsAboutToBeRemoved);
    connect(dynamicPropertiesModel(),
            &QAbstractItemModel::rowsRemoved,
            this,
            &QAbstractItemModel::rowsRemoved);
    connect(dynamicPropertiesModel(),
            &QAbstractItemModel::rowsInserted,
            this,
            &QAbstractItemModel::rowsInserted);

    connect(dynamicPropertiesModel(),
            &QAbstractItemModel::dataChanged,
            this,
            [this](const QModelIndex &topLeft, const QModelIndex &, const QList<int> &) {
                emit newPropertyNameChanged();
                emit dataChanged(index(topLeft.row(), 0),
                                 index(topLeft.row(), 0),
                                 {Qt::UserRole + 1,
                                  Qt::UserRole + 2,
                                  Qt::UserRole + 3,
                                  Qt::UserRole + 4});
            });
}

int DynamicPropertiesProxyModel::rowCount(const QModelIndex & /*parent*/) const
{
    return dynamicPropertiesModel()->rowCount();
}

QHash<int, QByteArray> DynamicPropertiesProxyModel::roleNames() const
{
    static QHash<int, QByteArray> roleNames{{Qt::UserRole + 1, "propertyName"},
                                            {Qt::UserRole + 2, "propertyType"},
                                            {Qt::UserRole + 3, "propertyValue"},
                                            {Qt::UserRole + 4, "propertyBinding"}};

    return roleNames;
}

QVariant DynamicPropertiesProxyModel::data(const QModelIndex &index, int role) const
{
    if (index.isValid() && index.row() < rowCount()) {
        AbstractProperty property = dynamicPropertiesModel()->abstractPropertyForRow(index.row());

        QTC_ASSERT(property.isValid(), return QVariant());

        if (role == Qt::UserRole + 1) {
            return property.name();

        } else if (role == Qt::UserRole + 2) {
            return property.dynamicTypeName();
        } else if (role == Qt::UserRole + 3) {
            QmlObjectNode objectNode = property.parentQmlObjectNode();
            return objectNode.modelValue(property.name());
        } else if (role == Qt::UserRole + 4) {
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
    qmlRegisterType<DynamicPropertiesProxyModel>("HelperWidgets", 2, 0, "DynamicPropertiesModel");
}

QmlDesigner::Internal::DynamicPropertiesModel *DynamicPropertiesProxyModel::dynamicPropertiesModel()
{
    auto *model = QmlDesigner::Internal::ConnectionView::instance()->dynamicPropertiesModel();
    QTC_ASSERT(model, return nullptr);

    return model;
}

QString DynamicPropertiesProxyModel::newPropertyName() const
{
    auto propertiesModel = DynamicPropertiesProxyModel::dynamicPropertiesModel();

    return QString::fromUtf8(propertiesModel->unusedProperty(
        propertiesModel->connectionView()->singleSelectedModelNode()));
}

void DynamicPropertiesProxyModel::createProperty(const QString &name, const QString &type)
{
    QmlDesignerPlugin::emitUsageStatistics(Constants::EVENT_PROPERTY_ADDED);

    auto propertiesModel = DynamicPropertiesProxyModel::dynamicPropertiesModel();
    auto view = propertiesModel->connectionView();
    if (view->selectedModelNodes().count() == 1) {
        const ModelNode modelNode = view->selectedModelNodes().constFirst();
        if (modelNode.isValid()) {
            try {
                modelNode.variantProperty(name.toUtf8())
                    .setDynamicTypeNameAndValue(type.toUtf8(), QLatin1String("none.none"));
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

    connect(DynamicPropertiesProxyModel::dynamicPropertiesModel(),
            &QAbstractItemModel::dataChanged,
            this,
            [this](const QModelIndex &topLeft, const QModelIndex &, const QList<int> &) {
                if (topLeft.row() == m_row)
                    setupBackendValue();
            });

    QObject::connect(m_backendValue,
                     &PropertyEditorValue::valueChanged,
                     this,
                     [this](const QString &, const QVariant &value) { commitValue(value); });

    QObject::connect(m_backendValue,
                     &PropertyEditorValue::expressionChanged,
                     this,
                     [this](const QString &) { commitExpression(m_backendValue->expression()); });
}

void DynamicPropertyRow::registerDeclarativeType()
{
    qmlRegisterType<DynamicPropertyRow>("HelperWidgets", 2, 0, "DynamicPropertyRow");
}

void DynamicPropertyRow::setRow(int r)
{
    if (m_row == r)
        return;

    qDebug() << Q_FUNC_INFO << r;

    m_row = r;
    setupBackendValue();
    emit rowChanged();
}

int DynamicPropertyRow::row() const
{
    return m_row;
}

PropertyEditorValue *DynamicPropertyRow::backendValue() const
{
    return m_backendValue;
}

void DynamicPropertyRow::remove()
{
    DynamicPropertiesProxyModel::dynamicPropertiesModel()->deleteDynamicPropertyByRow(m_row);
}

void DynamicPropertyRow::setupBackendValue()
{
    QmlDesigner::AbstractProperty property = DynamicPropertiesProxyModel::dynamicPropertiesModel()
                                                 ->abstractPropertyForRow(m_row);
    if (!property.isValid())
        return;

    qDebug() << Q_FUNC_INFO << property.name() << m_row;

    m_backendValue->setName(property.name());
    QmlDesigner::QmlObjectNode objectNode = property.parentQmlObjectNode();

    m_backendValue->setModelNode(property.parentModelNode());
    m_backendValue->setValue({});
    m_backendValue->setValue(objectNode.modelValue(property.name()));
    emit m_backendValue->isBoundChanged();

    if (property.isBindingProperty())
        m_backendValue->setExpression(property.toBindingProperty().expression());
}

void DynamicPropertyRow::commitValue(const QVariant &value)
{
    auto propertiesModel = DynamicPropertiesProxyModel::dynamicPropertiesModel();
    VariantProperty variantProperty = propertiesModel->variantPropertyForRow(m_row);

    auto view = propertiesModel->connectionView();
    RewriterTransaction transaction = view->beginRewriterTransaction(
        QByteArrayLiteral("DynamicPropertiesModel::updateValue"));
    try {
        if (view->currentState().isBaseState())
            variantProperty.setDynamicTypeNameAndValue(variantProperty.dynamicTypeName(), value);
        else
            variantProperty.parentQmlObjectNode().setVariantProperty(variantProperty.name(), value);
        transaction.commit(); //committing in the try block
    } catch (Exception &e) {
        e.showException();
    }
}

void DynamicPropertyRow::commitExpression(const QString &expression)
{
    auto propertiesModel = DynamicPropertiesProxyModel::dynamicPropertiesModel();
    BindingProperty bindingProperty = propertiesModel->bindingPropertyForRow(m_row);

    auto view = propertiesModel->connectionView();
    RewriterTransaction transaction = view->beginRewriterTransaction(
        QByteArrayLiteral("DynamicPropertiesModel::updateValue"));
    try {
        if (view->currentState().isBaseState())
            bindingProperty.setDynamicTypeNameAndExpression(bindingProperty.dynamicTypeName(),
                                                            expression);
        else
            bindingProperty.parentQmlObjectNode().setBindingProperty(bindingProperty.name(),
                                                                     expression);
        transaction.commit(); //committing in the try block
    } catch (Exception &e) {
        e.showException();
    }
    return;
}
