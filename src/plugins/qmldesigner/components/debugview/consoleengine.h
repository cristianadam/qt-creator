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

#include <QJSEngine>

namespace QmlDesigner {

class ModelNode;
class AbstractProperty;
class VariantProperty;
class BindingProperty;
class NodeAbstractProperty;
class NodeListProperty;
class NodeProperty;

namespace Internal {

class DebugView;
class ConsoleBackend;

class ConsoleEngine : public QJSEngine
{
    Q_OBJECT
public:
    explicit ConsoleEngine(DebugView *view);

    void modelAttached();
    void selectionChanged();

    QJSValue newValueForModelNode(const ModelNode &modelNode) const;
    QJSValue newValueForAbstractProperty(const AbstractProperty &abstractProperty) const;
    QJSValue newValueForVariantProperty(const VariantProperty &variantProperty) const;
    QJSValue newValueForBindingProperty(const BindingProperty &bindingProperty) const;
    QJSValue newValueForNodeAbstractProperty(const NodeAbstractProperty &nodeAbstractProperty) const;
    QJSValue newValueForNodeListProperty(const NodeListProperty &nodeListProperty) const;
    QJSValue newValueForNodeProperty(const NodeProperty &nodeListProperty) const;
    QJSValueList newValueListForModelNodeList(const QList<ModelNode> &nodes) const;
    ModelNode valueToModelNode(const QJSValue &value);

public: signals:
    void consoleMesage(const QString &message);

private:
    ConsoleBackend *m_backend = nullptr;
};

class JSConsole : public QObject
{
    Q_OBJECT
public:
    explicit JSConsole(ConsoleEngine *parent = 0);

public slots:
    Q_INVOKABLE void log(QString message);

private:
    ConsoleEngine *m_engine;
};

} // namespace Internal

} // namespace QmlDesigner

