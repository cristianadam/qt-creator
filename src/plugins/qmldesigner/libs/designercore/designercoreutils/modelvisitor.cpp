// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "modelvisitor.h"

#include <bindingproperty.h>
#include <modelnode.h>
#include <nodeabstractproperty.h>
#include <nodelistproperty.h>
#include <nodeproperty.h>
#include <signalhandlerproperty.h>
#include <variantproperty.h>

#include <qmldesignercoreconstants.h>

namespace QmlDesigner {

void ModelVisitor::accept(const ModelNode &modelNode)
{
    enterModelNode(modelNode);
    if (visit(modelNode)) {
        for (const AbstractProperty &property : modelNode.properties()) {
            visit(property);
            if (property.isNodeAbstractProperty()) {
                visit(property.toNodeAbstractProperty());
                enterChildren(property.toNodeAbstractProperty());
                if (property.isNodeProperty())
                    accept(property.toNodeProperty());
                else if (property.isNodeListProperty())
                    accept(property.toNodeListProperty());
                leaveChildren(property.toNodeAbstractProperty());
            } else if (property.isVariantProperty()) {
                visit(property.toVariantProperty());
            } else if (property.isBindingProperty()) {
                visit(property.toBindingProperty());
            } else if (property.isSignalHandlerProperty()) {
                visit(property.toSignalHandlerProperty());
            } else if (property.isSignalDeclarationProperty()) {
                visit(property.toSignalDeclarationProperty());
            }
        }
    }
    leaveModelNode(modelNode);
}

void ModelVisitor::accept(const NodeListProperty &nodeListProperty)
{
    visit(nodeListProperty);
    for (const ModelNode &modelNode : nodeListProperty.toModelNodeList()) {
        accept(modelNode);
    }
}

void ModelVisitor::accept(const NodeProperty &nodeProperty)
{
    visit(nodeProperty);
    accept(nodeProperty.parentModelNode());
}

void PrettyPrinter::print(const ModelNode &modelNode)
{
    PrettyPrinter printer;
    printer.accept(modelNode);
}

bool PrettyPrinter::visit(const ModelNode &modelNode)
{
    return true;
}

void PrettyPrinter::visit(const AbstractProperty &property) {}

void PrettyPrinter::visit(const NodeAbstractProperty &property)
{
    qDebug() << indent() << property;
}

void PrettyPrinter::enterChildren(const NodeAbstractProperty &property)
{
    m_indent++;
}

void PrettyPrinter::leaveChildren(const NodeAbstractProperty &property)
{
    m_indent--;
}

void PrettyPrinter::enterModelNode(const ModelNode &modelNode)
{
    qDebug() << indent() << modelNode;
    m_indent++;
}

void PrettyPrinter::leaveModelNode(const ModelNode &modelNode)
{
    m_indent--;
}

void PrettyPrinter::visit(const NodeListProperty &property) {}

void PrettyPrinter::visit(const NodeProperty &property) {}

void PrettyPrinter::visit(const VariantProperty &property)
{
    qDebug() << indent() << property;
}

void PrettyPrinter::visit(const BindingProperty &property)
{
    qDebug() << indent() << property;
}

void PrettyPrinter::visit(const SignalHandlerProperty &property)
{
    qDebug() << indent() << property;
}

void PrettyPrinter::visit(const SignalDeclarationProperty &property)
{
    qDebug() << indent() << property;
}

QString QmlDesigner::PrettyPrinter::indent() const
{
    QString str;
    for (int i = 0; i < m_indent * 4; i++)
        str.append(" ");

    return str;
}

} // namespace QmlDesigner
