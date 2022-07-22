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


#include "consoleengine.h"

#include "consolebackend.h"

#include "debugview.h"

namespace QmlDesigner {

namespace Internal {

ConsoleEngine::ConsoleEngine(DebugView *view)
    : QJSEngine{view}
    , m_backend(new ConsoleBackend(this, view))
{
    QJSValue objectValue = newQObject(m_backend);

    globalObject().setProperty("View", objectValue);

    auto console = new JSConsole(this);
    QJSValue consoleObj = newQObject(console);
    globalObject().setProperty("console", consoleObj);
}

void ConsoleEngine::modelAttached()
{
    m_backend->modelAttached();
}

void ConsoleEngine::selectionChanged()
{
    emit m_backend->selectionChanged();
}

JSConsole::JSConsole(ConsoleEngine *parent)
    : QObject{parent}
    , m_engine(parent)
{}

void JSConsole::log(QString message)
{
    emit m_engine->consoleMesage(message);
}

QJSValue ConsoleEngine::newValueForModelNode(const ModelNode &modelNode) const
{
    ConsoleEngine *that = const_cast<ConsoleEngine *>(this);
    ModelNodeJSWrapper *wrapper = new ModelNodeJSWrapper(that);
    wrapper->setModelNode(modelNode);
    QJSEngine::setObjectOwnership(wrapper, QJSEngine::JavaScriptOwnership);

    return that->newQObject(wrapper);
}

QJSValue ConsoleEngine::newValueForAbstractProperty(const AbstractProperty &abstractProperty) const
{
    ConsoleEngine *that = const_cast<ConsoleEngine *>(this);
    AbstractPropertyJSWrapper *wrapper = new AbstractPropertyJSWrapper(that);
    wrapper->setProperty(abstractProperty);
    QJSEngine::setObjectOwnership(wrapper, QJSEngine::JavaScriptOwnership);

    return that->newQObject(wrapper);
}

QJSValue ConsoleEngine::newValueForVariantProperty(const VariantProperty &variantProperty) const
{
    ConsoleEngine *that = const_cast<ConsoleEngine *>(this);
    VariantPropertyJSWrapper *wrapper = new VariantPropertyJSWrapper(that);
    wrapper->setProperty(variantProperty);
    QJSEngine::setObjectOwnership(wrapper, QJSEngine::JavaScriptOwnership);

    return that->newQObject(wrapper);
}

QJSValue ConsoleEngine::newValueForBindingProperty(const BindingProperty &bindingProperty) const
{
    ConsoleEngine *that = const_cast<ConsoleEngine *>(this);
    BindingPropertyJSWrapper *wrapper = new BindingPropertyJSWrapper(that);
    wrapper->setProperty(bindingProperty);
    QJSEngine::setObjectOwnership(wrapper, QJSEngine::JavaScriptOwnership);

    return that->newQObject(wrapper);
}

QJSValue ConsoleEngine::newValueForNodeAbstractProperty(const NodeAbstractProperty &nodeAbstractProperty) const
{
    ConsoleEngine *that = const_cast<ConsoleEngine *>(this);
    NodeAbstractPropertyJSWrapper *wrapper = new NodeAbstractPropertyJSWrapper(that);
    wrapper->setProperty(nodeAbstractProperty);
    QJSEngine::setObjectOwnership(wrapper, QJSEngine::JavaScriptOwnership);

    return that->newQObject(wrapper);
}

QJSValue ConsoleEngine::newValueForNodeListProperty(const NodeListProperty &nodeListProperty) const
{
    ConsoleEngine *that = const_cast<ConsoleEngine *>(this);
    NodeListPropertyJSWrapper *wrapper = new NodeListPropertyJSWrapper(that);
    wrapper->setProperty(nodeListProperty);
    QJSEngine::setObjectOwnership(wrapper, QJSEngine::JavaScriptOwnership);

    return that->newQObject(wrapper);
}

QJSValue ConsoleEngine::newValueForNodeProperty(const NodeProperty &nodeProperty) const
{
    ConsoleEngine *that = const_cast<ConsoleEngine *>(this);
    NodePropertyJSWrapper *wrapper = new NodePropertyJSWrapper(that);
    wrapper->setProperty(nodeProperty);
    QJSEngine::setObjectOwnership(wrapper, QJSEngine::JavaScriptOwnership);

    return that->newQObject(wrapper);
}

QJSValueList ConsoleEngine::newValueListForModelNodeList(const QList<ModelNode> &nodes) const
{
    QJSValueList list;
    for (const auto &node : nodes) {
        list.append(newValueForModelNode(node));
    }

    return list;
}

ModelNode ConsoleEngine::valueToModelNode(const QJSValue &value)
{
    auto nodeWrapper = qobject_cast<ModelNodeJSWrapper*>(value.toQObject());

    if (!nodeWrapper) {
        emit consoleMesage("ModelNode expected " + value.toString());
        return {};
    }

    return nodeWrapper->modelNode();
}

} // namespace Internal

} // namespace QmlDesigner
