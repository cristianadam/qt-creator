// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "nodeinstanceserver.h"
#include "nodeinstancesignalspy.h"

#include "instancecontainer.h"

#include <QPainter>
#include <QSharedPointer>
#include <QWeakPointer>

#include <enumeration.h>

QT_BEGIN_NAMESPACE
class QQmlContext;
class QQmlEngine;
class QQmlProperty;
class QQmlAbstractBinding;
class QQuickItem;
QT_END_NAMESPACE

namespace QmlDesigner {

class NodeInstanceServer;

namespace Internal {

class QmlGraphicsItemNodeInstance;
class GraphicsWidgetNodeInstance;
class GraphicsViewNodeInstance;
class GraphicsSceneNodeInstance;
class ProxyWidgetNodeInstance;
class WidgetNodeInstance;

class ObjectNodeInstance
{
public:
    using Pointer = QSharedPointer<ObjectNodeInstance>;
    using WeakPointer = QWeakPointer<ObjectNodeInstance>;

    virtual ~ObjectNodeInstance();
    void destroy();
    virtual void handleObjectDeletion(QObject *object);

    static Pointer create(QObject *objectToBeWrapped);
    static QObject *createPrimitive(const QString &typeName, int majorNumber, int minorNumber, QQmlContext *context);
    static QObject *createPrimitiveFromSource(const QString &typeName, int majorNumber, int minorNumber, QQmlContext *context);
    static QObject *createCustomParserObject(const QString &nodeSource, const QByteArray &importCode, QQmlContext *context);
    static QObject *createComponent(const QString &componentPath, QQmlContext *context);
    static QObject *createComponent(const QUrl &componentUrl, QQmlContext *context);
    static QObject *createComponentWrap(const QString &nodeSource, const QByteArray &importCode, QQmlContext *context);

    void setInstanceId(qint32 id);
    qint32 instanceId() const;

    NodeInstanceServer *nodeInstanceServer() const;
    void setNodeInstanceServer(NodeInstanceServer *server);
    virtual void initialize(const Pointer &objectNodeInstance, InstanceContainer::NodeFlags flags);
    virtual QImage renderImage() const;
    virtual QImage renderPreviewImage(const QSize &previewImageSize) const;

    virtual QSharedPointer<QQuickItemGrabResult> createGrabResult() const;

    virtual QObject *parent() const;

    Pointer parentInstance() const;

    virtual void reparent(const ObjectNodeInstance::Pointer &oldParentInstance,
                          const PropertyName &oldParentProperty,
                          const ObjectNodeInstance::Pointer &newParentInstance,
                          const PropertyName &newParentProperty);

    virtual void setId(const QString &id);
    virtual QString id() const;

    virtual bool isTransition() const;
    virtual bool isPositioner() const;
    virtual bool isQuickItem() const;
    virtual bool isQuickWindow() const;
    virtual bool isLayoutable() const;
    virtual bool isRenderable() const;
    virtual bool isPropertyChange() const;
    virtual bool isComposedEffect() const;

    virtual bool equalGraphicsItem(QGraphicsItem *item) const;

    virtual QRectF boundingRect() const;
    virtual QRectF contentItemBoundingBox() const;

    virtual QPointF position() const;
    virtual QSizeF size() const;
    virtual QSizeF implicitSize() const;
    virtual QTransform transform() const;
    virtual QTransform contentTransform() const;
    virtual QTransform customTransform() const;
    virtual QTransform contentItemTransform() const;
    virtual QTransform sceneTransform() const;
    virtual double opacity() const;

    virtual int penWidth() const;

    virtual bool hasAnchor(const PropertyName &name) const;
    virtual QPair<PropertyName, ServerNodeInstance> anchor(const PropertyName &name) const;
    virtual bool isAnchoredBySibling() const;
    virtual bool isAnchoredByChildren() const;

    virtual double rotation() const;
    virtual double scale() const;
    virtual QList<QGraphicsTransform *> transformations() const;
    virtual QPointF transformOriginPoint() const;
    virtual double zValue() const;

    virtual void setPropertyVariant(const PropertyName &name, const QVariant &value);
    virtual void setPropertyBinding(const PropertyName &name, const QString &expression);
    virtual QVariant property(const PropertyName &name) const;
    virtual void resetProperty(const PropertyName &name);
    virtual void refreshProperty(const PropertyName &name);
    virtual QString instanceType(const PropertyName &name) const;
    PropertyNameList propertyNames() const;

    virtual QList<ServerNodeInstance> childItems() const;
    virtual QList<QQuickItem*> allItemsRecursive() const;

    void setDeleteHeldInstance(bool deleteInstance);
    bool deleteHeldInstance() const;

    virtual void updateAnchors();
    virtual void paintUpdate();

    virtual void activateState();
    virtual void deactivateState();
    virtual QStringList allStates() const;

    void populateResetHashes();
    bool hasValidResetBinding(const PropertyName &propertyName) const;
    QVariant resetValue(const PropertyName &propertyName) const;

    QObject *object() const;
    virtual QQuickItem *contentItem() const;

    virtual bool hasContent() const;
    virtual bool isResizable() const;
    virtual bool isMovable() const;
    bool isInLayoutable() const;
    void setInLayoutable(bool isInLayoutable);
    virtual void refreshLayoutable();

    bool hasBindingForProperty(const PropertyName &propertyName, bool *hasChanged = nullptr) const;

    QQmlContext *context() const;
    QQmlEngine *engine() const;

    virtual bool updateStateVariant(const ObjectNodeInstance::Pointer &target,
                                    const PropertyName &propertyName,
                                    const QVariant &value);

    virtual bool updateStateBinding(const ObjectNodeInstance::Pointer &target,
                                    const PropertyName &propertyName,
                                    const QString &expression);

    virtual bool resetStateProperty(const ObjectNodeInstance::Pointer &target,
                                    const PropertyName &propertyName,
                                    const QVariant &resetValue);

    bool isValid() const;
    bool isRootNodeInstance() const;

    virtual void doComponentComplete();

    virtual QList<ServerNodeInstance> stateInstances() const;

    virtual void setNodeSource(const QString &source);

    virtual void updateAllDirtyNodesRecursive();

    virtual PropertyNameList ignoredProperties() const;

    virtual void setHiddenInEditor(bool b);
    bool isHiddenInEditor() const;

    virtual void setLockedInEditor(bool b);
    bool isLockedInEditor() const;

    bool isComponentWrap() const;
    void setComponentWrap(bool wrap);

    void setModifiedFlag(bool b);

    void handleNewDynamicProperty(const PropertyName &name);

protected:
    explicit ObjectNodeInstance(QObject *object);
    void doResetProperty(const PropertyName &propertyName);
    void removeFromOldProperty(QObject *object, QObject *oldParent, const PropertyName &oldParentProperty);
    void addToNewProperty(QObject *object, QObject *newParent, const PropertyName &newParentProperty);
    void deleteObjectsInList(const QQmlProperty &metaProperty);
    QVariant convertSpecialCharacter(const QVariant& value) const;
    QVariant convertEnumToValue(const QVariant &value, const PropertyName &name);
    static QObject *parentObject(QObject *object);
    static QVariant enumationValue(const Enumeration &enumeration);

    void initializePropertyWatcher(const ObjectNodeInstance::Pointer &objectNodeInstance);
    void watchProperty(const PropertyName &name);
    void ensureVector3DDotProperties(PropertyNameList &list) const;
    void ensureValueTypeProperties(PropertyNameList &list) const;

private:
    QString m_id;

    QPointer<NodeInstanceServer> m_nodeInstanceServer;
    PropertyName m_parentProperty;

    QPointer<QObject> m_object;

    NodeInstanceSignalSpy m_signalSpy;

    qint32 m_instanceId;
    bool m_deleteHeldInstance;
    bool m_isInLayoutable;
    bool m_isModified = false;
    bool m_isLockedInEditor = false;
    bool m_isHiddenInEditor = false;
    bool m_isComponentWrap = false;
    static QHash<EnumerationName, QVariant> m_enumationValueHash;
};

} // namespace Internal
} // namespace QmlDesigner
