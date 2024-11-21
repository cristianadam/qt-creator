// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <modelnode.h>

namespace QmlDesigner {

class QMLDESIGNERCORE_EXPORT ModelVisitor
{
public:
    ModelVisitor() = default;

    void accept(const ModelNode &modelNode);
    void accept(const NodeListProperty &nodeListProperty);
    void accept(const NodeProperty &nodeProperty);

    virtual bool visit(const ModelNode &node) { return true; };

    virtual void enterChildren(const NodeAbstractProperty &property) {};
    virtual void leaveChildren(const NodeAbstractProperty &property) {};

    virtual void enterModelNode(const ModelNode &property) {};
    virtual void leaveModelNode(const ModelNode &property) {};

    virtual void visit(const AbstractProperty &property) {};
    virtual void visit(const NodeAbstractProperty &property) {};

    virtual void visit(const NodeListProperty &property) {};
    virtual void visit(const NodeProperty &property) {};
    virtual void visit(const VariantProperty &property) {};
    virtual void visit(const BindingProperty &property) {};
    virtual void visit(const SignalHandlerProperty &property) {};
    virtual void visit(const SignalDeclarationProperty &property) {};
};

class QMLDESIGNERCORE_EXPORT PrettyPrinter : public ModelVisitor
{
public:
    PrettyPrinter() = default;

    static void print(const ModelNode &modelNode);

    bool visit(const ModelNode &modelNode) override;

    void enterChildren(const NodeAbstractProperty &property) override;
    void leaveChildren(const NodeAbstractProperty &property) override;

    void enterModelNode(const ModelNode &modelNode) override;
    void leaveModelNode(const ModelNode &modelNode) override;

    void visit(const AbstractProperty &property) override;
    void visit(const NodeAbstractProperty &property) override;

    void visit(const NodeListProperty &property) override;
    void visit(const NodeProperty &property) override;

    void visit(const VariantProperty &property) override;
    void visit(const BindingProperty &property) override;
    void visit(const SignalHandlerProperty &property) override;
    void visit(const SignalDeclarationProperty &property) override;

private:
    QString indent() const;
    int m_indent = 0;
};

} // namespace QmlDesigner
