// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <modelvisitor.h>

#include <qmljs/qmljsstaticanalysismessage.h>

#include <vector>

namespace QmlDesigner {

class QMLDESIGNERCORE_EXPORT ModelCheck : public ModelVisitor
{
public:
    ModelCheck(const ModelNode &modelNode);

    static QList<QmlJS::StaticAnalysis::Message> checkModel(Model *model);

    QList<QmlJS::StaticAnalysis::Message> operator()();

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
    std::vector<ModelNode> m_path;
    void addMessage(const QmlJS::StaticAnalysis::Message &message);
    void addMessage(QmlJS::StaticAnalysis::Type type,
                    const ModelNode &modelNode,
                    const QString &arg1 = QString(),
                    const QString &arg2 = QString());
    ModelNode m_rootNode;
    QList<QmlJS::StaticAnalysis::Message> m_messages;

    QmlJS::SourceLocation sourceLocationForModelNode(const ModelNode &modelNode);
};

} // namespace QmlDesigner
