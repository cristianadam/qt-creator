// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "modelcheck.h"
#include "modelutils.h"

#include <bindingproperty.h>
#include <model.h>
#include <modelnode.h>
#include <nodeabstractproperty.h>
#include <nodelistproperty.h>
#include <nodemetainfo.h>
#include <nodeproperty.h>
#include <rewriterview.h>
#include <signalhandlerproperty.h>
#include <variantproperty.h>

#include <qmldesignercoreconstants.h>

namespace QmlDesigner {

ModelCheck::ModelCheck(const ModelNode &modelNode)
    : m_rootNode(modelNode)
{}

QList<QmlJS::StaticAnalysis::Message> ModelCheck::checkModel(Model *model)
{
    QTC_ASSERT(model && model->rewriterView(), return {});

    ModelCheck modelCheck(model->rewriterView()->rootModelNode());
    QList<QmlJS::StaticAnalysis::Message> messages = modelCheck();

    return messages;
}

QList<QmlJS::StaticAnalysis::Message> ModelCheck::operator()()
{
    qDebug() << Q_FUNC_INFO << m_rootNode;
    accept(m_rootNode);

    return m_messages;
}

bool ModelCheck::visit(const ModelNode &modelNode)
{
    qDebug() << Q_FUNC_INFO << m_path.size() << modelNode;

    QTC_ASSERT(modelNode.model()->rewriterView(), return false);

    // TODO use type cache
    if (m_path.empty() && ModelUtils::isUnsupportedQMLRootType(modelNode.type())) {
        addMessage(QmlJS::StaticAnalysis::ErrUnsupportedRootTypeInVisualDesigner,
                   modelNode,
                   QString::fromUtf8(modelNode.type()));
    }

    if (ModelUtils::isUnsupportedQMLType(modelNode.type())) {
        addMessage(QmlJS::StaticAnalysis::ErrUnsupportedTypeInQmlUi,
                   modelNode,
                   QString::fromUtf8(modelNode.type()));
    }

    m_path.push_back(modelNode);
    return true;
}

void ModelCheck::visit(const AbstractProperty &property)
{
    if (property.isDefaultProperty())
        return;

    const ModelNode modelNode = property.parentModelNode();

    // TODO use type cache
    if (modelNode.type() == "Connections")
        return;

    if (modelNode.type() == "PropertyChanges")
        return;

    if (property.isDynamic())
        return;

    if (!modelNode.metaInfo().hasProperty(property.name())) {
        addMessage(QmlJS::StaticAnalysis::ErrInvalidPropertyName,
                   modelNode,
                   QString::fromUtf8(property.name()));
    }
}

void ModelCheck::visit(const NodeAbstractProperty &property) {}

void ModelCheck::enterChildren(const NodeAbstractProperty &property) {}

void ModelCheck::leaveChildren(const NodeAbstractProperty &property) {}

void ModelCheck::enterModelNode(const ModelNode &modelNode)
{
   
}

void ModelCheck::leaveModelNode(const ModelNode &modelNode)
{
    m_path.pop_back();
}

void ModelCheck::visit(const NodeListProperty &property) {}

void ModelCheck::visit(const NodeProperty &property) {}

void ModelCheck::visit(const VariantProperty &property) {}

void ModelCheck::visit(const BindingProperty &property) {}

void ModelCheck::visit(const SignalHandlerProperty &property) {}

void ModelCheck::visit(const SignalDeclarationProperty &property) {}

void ModelCheck::addMessage(const QmlJS::StaticAnalysis::Message &message)
{
    m_messages.append(message);
}

void ModelCheck::addMessage(QmlJS::StaticAnalysis::Type type,
                            const ModelNode &modelNode,
                            const QString &arg1,
                            const QString &arg2)
{
    addMessage(QmlJS::StaticAnalysis::Message(type, sourceLocationForModelNode(modelNode), arg1, arg2));
}

QmlJS::SourceLocation ModelCheck::sourceLocationForModelNode(const ModelNode &modelNode)
{
    QTC_ASSERT(modelNode.model()->rewriterView(), return QmlJS::SourceLocation());

    auto rewriterView = static_cast<RewriterView *>(modelNode.model()->rewriterView());
    int offset = rewriterView->nodeOffset(modelNode);
    int length = rewriterView->nodeLength(modelNode);
    int line, column;
    rewriterView->convertPosition(offset, &line, &column);
    QmlJS::SourceLocation loc(offset, length, line, column);

    return loc;
}

} // namespace QmlDesigner
