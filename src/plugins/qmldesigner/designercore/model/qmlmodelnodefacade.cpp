// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "qmlmodelnodefacade.h"
#include "nodeinstanceview.h"

namespace QmlDesigner {

QmlModelNodeFacade::QmlModelNodeFacade() : m_modelNode(ModelNode())
{}

AbstractView *QmlModelNodeFacade::view() const
{
    if (modelNode().isValid())
        return modelNode().view();
    else
        return nullptr;
}

NodeInstanceView *QmlModelNodeFacade::nodeInstanceView(const ModelNode &modelNode)
{
    return modelNode.model()->nodeInstanceView();
}

NodeInstanceView *QmlModelNodeFacade::nodeInstanceView() const
{
    return nodeInstanceView(m_modelNode);
}


QmlModelNodeFacade::QmlModelNodeFacade(const ModelNode &modelNode) : m_modelNode(modelNode)
{}

QmlModelNodeFacade::~QmlModelNodeFacade() = default;

QmlModelNodeFacade::operator ModelNode() const
{
    return m_modelNode;
}

bool QmlModelNodeFacade::hasModelNode() const
{
    return m_modelNode.isValid();
}

bool QmlModelNodeFacade::isValid() const
{
    return isValidQmlModelNodeFacade(m_modelNode);
}

bool QmlModelNodeFacade::isValidQmlModelNodeFacade(const ModelNode &modelNode)
{
    return modelNode.isValid()
#ifndef QMLDESIGNER_TEST //This is a hack to keep tests working without instances
            && nodeInstanceView(modelNode)
            && nodeInstanceView(modelNode)->hasInstanceForModelNode(modelNode)
            && nodeInstanceView(modelNode)->instanceForModelNode(modelNode).isValid();
#else
            ;
#endif
}

bool QmlModelNodeFacade::isRootNode() const
{
    return modelNode().isRootNode();
}
} //QmlDesigner
