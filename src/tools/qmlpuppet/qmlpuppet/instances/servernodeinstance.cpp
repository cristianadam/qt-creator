// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "servernodeinstance.h"

#include "objectnodeinstance.h"
#include "dummynodeinstance.h"
#include "componentnodeinstance.h"
#include "qmltransitionnodeinstance.h"
#include "qmlpropertychangesnodeinstance.h"
#include "behaviornodeinstance.h"
#include "qmlstatenodeinstance.h"
#include "anchorchangesnodeinstance.h"
#include "positionernodeinstance.h"
#include "layoutnodeinstance.h"
#include "debugoutputcommand.h"
#include "qt3dpresentationnodeinstance.h"

#include "quickitemnodeinstance.h"
#include "quick3dmaterialnodeinstance.h"
#include "quick3dnodeinstance.h"
#include "quick3dtexturenodeinstance.h"

#include "nodeinstanceserver.h"
#include "instancecontainer.h"

#include <qmlprivategate.h>

#include <QtQuick/private/qquickitem_p.h>

#include <QHash>
#include <QSet>
#include <QDebug>
#include <QQuickItem>

#include <QQmlEngine>

/*!
  \class QmlDesigner::NodeInstance
  \ingroup CoreInstance
  \brief NodeInstance is a common handle for the actual object representation of a ModelNode.

   NodeInstance abstracts away the differences e.g. in terms of position and size
   for QWidget, QGraphicsView, QLayout etc objects. Multiple NodeInstance objects can share
   the pointer to the same instance object. The actual instance will be deleted when
   the last NodeInstance object referencing to it is deleted. This can be disabled by
   setDeleteHeldInstance().

   \see QmlDesigner::NodeInstanceView
*/

namespace QmlDesigner {

/*!
\brief Constructor - creates a invalid NodeInstance


\see NodeInstanceView
*/
ServerNodeInstance::ServerNodeInstance()
{
}

/*!
\brief Destructor

*/
ServerNodeInstance::~ServerNodeInstance()
{
}

/*!
\brief Constructor - creates a valid NodeInstance

*/
ServerNodeInstance::ServerNodeInstance(const Internal::ObjectNodeInstance::Pointer &abstractInstance)
  : m_nodeInstance(abstractInstance)
{

}


ServerNodeInstance::ServerNodeInstance(const ServerNodeInstance &other)
  : m_nodeInstance(other.m_nodeInstance)
{
}

ServerNodeInstance &ServerNodeInstance::operator=(const ServerNodeInstance &other)
{
    m_nodeInstance = other.m_nodeInstance;
    return *this;
}

QImage ServerNodeInstance::renderImage() const
{
    return m_nodeInstance->renderImage();
}

QImage ServerNodeInstance::renderPreviewImage(const QSize &previewImageSize) const
{
    return m_nodeInstance->renderPreviewImage(previewImageSize);
}

QSharedPointer<QQuickItemGrabResult> ServerNodeInstance::createGrabResult() const
{
    return m_nodeInstance->createGrabResult();
}

bool ServerNodeInstance::isRootNodeInstance() const
{
    return isValid() && m_nodeInstance->isRootNodeInstance();
}

bool ServerNodeInstance::isSubclassOf(QObject *object, const QByteArray &superTypeName)
{
    return  Internal::QmlPrivateGate::isSubclassOf(object, superTypeName);
}

QRectF ServerNodeInstance::effectAdjustedBoundingRect(QQuickItem *item)
{
    if (item) {
        QQuickItemPrivate *pItem = QQuickItemPrivate::get(item);

        QQmlProperty prop(item, "__effect");

        if (pItem && pItem->layer() && pItem->layer()->sourceRect().isValid()) {
            return pItem->layer()->sourceRect();
        } else if (prop.read().toBool()) {
            prop = QQmlProperty(item, "effectBoundingBox");
            QRectF rect = prop.read().toRectF().adjusted(-40, -40, 40, 40);
            if (rect.isValid())
                return rect;
            return item->boundingRect();
        } else {
            return item->boundingRect();
        }
    }
    return {};
}

void ServerNodeInstance::setModifiedFlag(bool b)
{
    m_nodeInstance->setModifiedFlag(b);
}

void ServerNodeInstance::setNodeSource(const QString &source)
{
    m_nodeInstance->setNodeSource(source);
}

bool ServerNodeInstance::holdsGraphical() const
{
    return m_nodeInstance->isRenderable();
}

bool ServerNodeInstance::isComponentWrap() const
{
    return m_nodeInstance->isComponentWrap();
}

bool ServerNodeInstance::isComposedEffect() const
{
    return m_nodeInstance->isComposedEffect();
}

QQuickItem *ServerNodeInstance::contentItem() const
{
    return m_nodeInstance->contentItem();
}

bool ServerNodeInstance::hasProperty(const PropertyName &name)
{
    return propertyNames().contains(name);
}

void ServerNodeInstance::createNewDynamicProperty(const PropertyName &name)
{
    auto nameStr = QString::fromUtf8(name);
    auto *context = QQmlEngine::contextForObject(internalObject());
    Internal::QmlPrivateGate::createNewDynamicProperty(internalObject(), context->engine(), nameStr);
    internalInstance()->handleNewDynamicProperty(name);
}

void ServerNodeInstance::updateDirtyNodeRecursive()
{
    m_nodeInstance->updateAllDirtyNodesRecursive();
}

bool ServerNodeInstance::isSubclassOf(const QString &superTypeName) const
{
    return isSubclassOf(internalObject(), superTypeName.toUtf8());
}

/*!
\brief Creates a new NodeInstace for this NodeMetaInfo

\param metaInfo MetaInfo for which a Instance should be created
\param context QQmlContext which should be used
\returns Internal Pointer of a NodeInstance
\see NodeMetaInfo
*/
Internal::ObjectNodeInstance::Pointer ServerNodeInstance::createInstance(QObject *objectToBeWrapped)
{
    Internal::ObjectNodeInstance::Pointer instance;

    if (objectToBeWrapped == nullptr)
        instance = Internal::DummyNodeInstance::create();
    else if (isSubclassOf(objectToBeWrapped, "Q3DSPresentationItem"))
        instance = Internal::Qt3DPresentationNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuickBasePositioner"))
        instance = Internal::PositionerNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuickLayout"))
        instance = Internal::LayoutNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuickItem"))
        instance = Internal::QuickItemNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuick3DTexture"))
        instance = Internal::Quick3DTextureNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuick3DNode"))
        instance = Internal::Quick3DNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuick3DMaterial"))
        instance = Internal::Quick3DMaterialNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQmlComponent"))
        instance = Internal::ComponentNodeInstance::create(objectToBeWrapped);
    else if (objectToBeWrapped->inherits("QQmlAnchorChanges"))
        instance = Internal::AnchorChangesNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuickPropertyChanges"))
        instance = Internal::QmlPropertyChangesNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuickState"))
        instance = Internal::QmlStateNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuickTransition"))
        instance = Internal::QmlTransitionNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QQuickBehavior"))
        instance = Internal::BehaviorNodeInstance::create(objectToBeWrapped);
    else if (isSubclassOf(objectToBeWrapped, "QObject"))
        instance = Internal::ObjectNodeInstance::create(objectToBeWrapped);
    else
        instance = Internal::DummyNodeInstance::create();


    return instance;
}

QString static getErrorString(QQmlEngine *engine, const QString &componentPath)
{
    QQmlComponent component(engine, componentPath);

    QObject *o = component.create(nullptr);
    delete o;
    QString s;
    for (const QQmlError &error : component.errors())
        s.append(error.toString());
    return s;
}

bool isInPathList(const QStringList &pathList, const QString &componentPath)
{
    if (componentPath.indexOf("qml/QtQuick/Controls") > 0)
        return true;

    return std::any_of(pathList.begin(), pathList.end(), [&](auto &&path) {
        return componentPath.startsWith(path);
    });
}

bool canBeCreatedAsPrimitive(const QStringList &pathList,
                             const InstanceContainer &instanceContainer,
                             QQmlContext *context,
                             QObject *&object)
{
    if (isInPathList(pathList, instanceContainer.componentPath())) {
        object = Internal::ObjectNodeInstance::createPrimitive(QString::fromUtf8(
                                                                   instanceContainer.type()),
                                                               instanceContainer.majorNumber(),
                                                               instanceContainer.minorNumber(),
                                                               context);

        if (object)
            return true;

    }
    return false;
}

ServerNodeInstance ServerNodeInstance::create(NodeInstanceServer *nodeInstanceServer,
                                              const InstanceContainer &instanceContainer,
                                              ComponentWrap componentWrap)
{
    Q_ASSERT(instanceContainer.instanceId() != -1);
    Q_ASSERT(nodeInstanceServer);

    QObject *object = nullptr;
    if (componentWrap == WrapAsComponent) {
        object = Internal::ObjectNodeInstance::createComponentWrap(instanceContainer.nodeSource(), nodeInstanceServer->importCode(), nodeInstanceServer->context());
    } else if (!instanceContainer.nodeSource().isEmpty()) {
        object = Internal::ObjectNodeInstance::createCustomParserObject(instanceContainer.nodeSource(), nodeInstanceServer->importCode(), nodeInstanceServer->context());
        if (object == nullptr)
            nodeInstanceServer->sendDebugOutput(DebugOutputCommand::ErrorType, QLatin1String("Custom parser object could not be created."), instanceContainer.instanceId());
    } else if (!instanceContainer.componentPath().isEmpty()
               && !canBeCreatedAsPrimitive(nodeInstanceServer->engine()->importPathList(),
                                instanceContainer, nodeInstanceServer->context(), object)) {
        object = Internal::ObjectNodeInstance::createComponent(instanceContainer.componentPath(), nodeInstanceServer->context());
        if (object == nullptr) {
            object = Internal::ObjectNodeInstance::createPrimitive(QString::fromUtf8(instanceContainer.type()), instanceContainer.majorNumber(), instanceContainer.minorNumber(), nodeInstanceServer->context());
            if (object == nullptr) {
                const QString errors = getErrorString(nodeInstanceServer->engine(), instanceContainer.componentPath());
                const QString message = QString("Component with path %1 could not be created.\n\n").arg(instanceContainer.componentPath());
                nodeInstanceServer->sendDebugOutput(DebugOutputCommand::ErrorType, message + errors, instanceContainer.instanceId());
            }
        }
    } else if (!object) {
        object = Internal::ObjectNodeInstance::createPrimitive(QString::fromUtf8(instanceContainer.type()), instanceContainer.majorNumber(), instanceContainer.minorNumber(), nodeInstanceServer->context());
        if (object == nullptr)
            nodeInstanceServer->sendDebugOutput(DebugOutputCommand::ErrorType, QLatin1String("Item could not be created."), instanceContainer.instanceId());
    }

    if (object == nullptr) {
        if (instanceContainer.metaType() == InstanceContainer::ItemMetaType) { //If we cannot instanciate the object but we know it has to be an Ttem, we create an Item instead.
            object = Internal::ObjectNodeInstance::createPrimitive("QtQuick/Item", 2, 0, nodeInstanceServer->context());

            if (object == nullptr)
                object = new QQuickItem;
        } else {
            object = Internal::ObjectNodeInstance::createPrimitive("QML/QtObject",
                                                                   1,
                                                                   0,
                                                                   nodeInstanceServer->context());
            if (object == nullptr) //Fallback for Qt 5
                object = Internal::ObjectNodeInstance::createPrimitive("QtQml/QtObject",
                                                                       2,
                                                                       0,
                                                                       nodeInstanceServer->context());
        }
   }

    Internal::QmlPrivateGate::getPropertyCache(object, nodeInstanceServer->engine());

    ServerNodeInstance instance(createInstance(object));

    instance.internalInstance()->setNodeInstanceServer(nodeInstanceServer);

    instance.internalInstance()->setInstanceId(instanceContainer.instanceId());

    instance.internalInstance()->setComponentWrap(componentWrap == WrapAsComponent);

    instance.internalInstance()->initialize(instance.m_nodeInstance, instanceContainer.metaFlags());

    nodeInstanceServer->handlePickTarget(instance);

    return instance;
}

void ServerNodeInstance::reparent(const ServerNodeInstance &oldParentInstance, const PropertyName &oldParentProperty, const ServerNodeInstance &newParentInstance, const PropertyName &newParentProperty)
{
    m_nodeInstance->reparent(oldParentInstance.m_nodeInstance, oldParentProperty, newParentInstance.m_nodeInstance, newParentProperty);
}

/*!
\brief Returns the parent NodeInstance of this NodeInstance.

    If there is not parent than the parent is invalid.

\returns Parent NodeInstance.
*/
ServerNodeInstance ServerNodeInstance::parent() const
{
    return m_nodeInstance->parentInstance();
}

bool ServerNodeInstance::hasParent() const
{
    return m_nodeInstance->parent();
}

bool ServerNodeInstance::isValid() const
{
    return m_nodeInstance && m_nodeInstance->isValid();
}


/*!
\brief Returns if the NodeInstance is a QGraphicsItem.
\returns true if this NodeInstance is a QGraphicsItem
*/
bool ServerNodeInstance::equalGraphicsItem(QGraphicsItem *item) const
{
    return m_nodeInstance->equalGraphicsItem(item);
}

/*!
\brief Returns the bounding rect of the NodeInstance.
\returns QRectF of the NodeInstance
*/
QRectF ServerNodeInstance::boundingRect() const
{
    QRectF boundingRect(m_nodeInstance->boundingRect());

//
//    if (modelNode().isValid()) { // TODO implement recursiv stuff
//        if (qFuzzyIsNull(boundingRect.width()))
//            boundingRect.setWidth(nodeState().property("width").value().toDouble());
//
//        if (qFuzzyIsNull(boundingRect.height()))
//            boundingRect.setHeight(nodeState().property("height").value().toDouble());
//    }

    return boundingRect;
}

QRectF ServerNodeInstance::contentItemBoundingRect() const
{
    return m_nodeInstance->contentItemBoundingBox();
}

void ServerNodeInstance::setPropertyVariant(const PropertyName &name, const QVariant &value)
{
    m_nodeInstance->setPropertyVariant(name, value);

}

void ServerNodeInstance::setPropertyBinding(const PropertyName &name, const QString &expression)
{
    m_nodeInstance->setPropertyBinding(name, expression);
}

void ServerNodeInstance::setHiddenInEditor(bool b)
{
    m_nodeInstance->setHiddenInEditor(b);
    m_nodeInstance->nodeInstanceServer()->handleInstanceHidden(*this, b, true);
}

void ServerNodeInstance::setLockedInEditor(bool b)
{
    m_nodeInstance->setLockedInEditor(b);
    m_nodeInstance->nodeInstanceServer()->handleInstanceLocked(*this, b, true);
}

void ServerNodeInstance::resetProperty(const PropertyName &name)
{
    m_nodeInstance->resetProperty(name);
}

void ServerNodeInstance::refreshProperty(const PropertyName &name)
{
    m_nodeInstance->refreshProperty(name);
}

void ServerNodeInstance::setId(const QString &id)
{
    m_nodeInstance->setId(id);
}

/*!
\brief Returns the property value of the property of this NodeInstance.
\returns QVariant value
*/
QVariant ServerNodeInstance::property(const PropertyName &name) const
{
    return m_nodeInstance->property(name);
}

PropertyNameList ServerNodeInstance::propertyNames() const
{
    return m_nodeInstance->propertyNames();
}

bool ServerNodeInstance::hasBindingForProperty(const PropertyName &name, bool *hasChanged) const
{
    return m_nodeInstance->hasBindingForProperty(name, hasChanged);
}

/*!
\brief Returns the property default value of the property of this NodeInstance.
\returns QVariant default value which is the reset value to
*/
QVariant ServerNodeInstance::defaultValue(const PropertyName &name) const
{
    return m_nodeInstance->resetValue(name);
}

/*!
\brief Returns the type of the property of this NodeInstance.
*/
QString ServerNodeInstance::instanceType(const PropertyName &name) const
{
    return m_nodeInstance->instanceType(name);
}

void ServerNodeInstance::makeInvalid()
{
    if (m_nodeInstance)
        m_nodeInstance->destroy();
    m_nodeInstance.clear();
}

bool ServerNodeInstance::hasContent() const
{
    return m_nodeInstance->hasContent();
}

bool ServerNodeInstance::isResizable() const
{
    return m_nodeInstance->isResizable();
}

bool ServerNodeInstance::isMovable() const
{
    return m_nodeInstance->isMovable();
}

bool ServerNodeInstance::isInLayoutable() const
{
    return m_nodeInstance->isInLayoutable();
}

bool ServerNodeInstance::hasAnchor(const PropertyName &name) const
{
    return m_nodeInstance->hasAnchor(name);
}

int ServerNodeInstance::penWidth() const
{
    return m_nodeInstance->penWidth();
}

bool ServerNodeInstance::isAnchoredBySibling() const
{
    return m_nodeInstance->isAnchoredBySibling();
}

bool ServerNodeInstance::isAnchoredByChildren() const
{
    return m_nodeInstance->isAnchoredByChildren();
}

QPair<PropertyName, ServerNodeInstance> ServerNodeInstance::anchor(const PropertyName &name) const
{
    return m_nodeInstance->anchor(name);
}

QDebug operator <<(QDebug debug, const ServerNodeInstance &instance)
{
    if (instance.isValid()) {
        debug.nospace() << "ServerNodeInstance("
                << instance.instanceId() << ", "
                << instance.internalObject() << ", "
                << instance.id() << ", "
                << instance.parent() << ')';
    } else {
        debug.nospace() << "ServerNodeInstance(invalid)";
    }

    return debug.space();
}

ServerNodeInstance::QHashValueType qHash(const ServerNodeInstance &instance)
{
    return ::qHash(instance.instanceId());
}

bool operator ==(const ServerNodeInstance &first, const ServerNodeInstance &second)
{
    return first.instanceId() == second.instanceId();
}

bool operator <(const ServerNodeInstance &first, const ServerNodeInstance &second)
{
    return first.instanceId() < second.instanceId();
}


bool ServerNodeInstance::isWrappingThisObject(QObject *object) const
{
    return internalObject() && internalObject() == object;
}

/*!
\brief Returns the position in parent coordiantes.
\returns QPointF of the position of the instance.
*/
QPointF ServerNodeInstance::position() const
{
    return m_nodeInstance->position();
}

/*!
\brief Returns the size in local coordiantes.
\returns QSizeF of the size of the instance.
*/
QSizeF ServerNodeInstance::size() const
{
    QSizeF instanceSize = m_nodeInstance->size();

    return instanceSize;
}

QSizeF ServerNodeInstance::implicitSize() const
{
    return m_nodeInstance->implicitSize();
}

QTransform ServerNodeInstance::transform() const
{
    return m_nodeInstance->transform();
}

/*!
\brief Returns the transform matrix of the instance.
\returns QTransform of the instance.
*/
QTransform ServerNodeInstance::customTransform() const
{
    return m_nodeInstance->customTransform();
}

QTransform ServerNodeInstance::sceneTransform() const
{
    return m_nodeInstance->sceneTransform();
}

QTransform ServerNodeInstance::contentTransform() const
{
    return m_nodeInstance->contentTransform();
}

QTransform ServerNodeInstance::contentItemTransform() const
{
    return m_nodeInstance->contentItemTransform();
}

double ServerNodeInstance::rotation() const
{
    return m_nodeInstance->rotation();
}

double ServerNodeInstance::scale() const
{
    return m_nodeInstance->scale();
}

QList<QGraphicsTransform *> ServerNodeInstance::transformations() const
{
    return m_nodeInstance->transformations();
}

QPointF ServerNodeInstance::transformOriginPoint() const
{
    return m_nodeInstance->transformOriginPoint();
}

double ServerNodeInstance::zValue() const
{
    return m_nodeInstance->zValue();
}

/*!
\brief Returns the opacity of the instance.
\returns 0.0 mean transparent and 1.0 opaque.
*/
double ServerNodeInstance::opacity() const
{
    return m_nodeInstance->opacity();
}


void ServerNodeInstance::setDeleteHeldInstance(bool deleteInstance)
{
    m_nodeInstance->setDeleteHeldInstance(deleteInstance);
}


void ServerNodeInstance::paintUpdate()
{
    m_nodeInstance->paintUpdate();
}

QObject *ServerNodeInstance::internalObject() const
{
    if (m_nodeInstance.isNull())
        return nullptr;

    return m_nodeInstance->object();
}

void ServerNodeInstance::activateState()
{
    m_nodeInstance->activateState();
}

void ServerNodeInstance::deactivateState()
{
    m_nodeInstance->deactivateState();
}

bool ServerNodeInstance::updateStateVariant(const ServerNodeInstance &target, const PropertyName &propertyName, const QVariant &value)
{
    return m_nodeInstance->updateStateVariant(target.internalInstance(), propertyName, value);
}

bool ServerNodeInstance::updateStateBinding(const ServerNodeInstance &target, const PropertyName &propertyName, const QString &expression)
{
    return m_nodeInstance->updateStateBinding(target.internalInstance(), propertyName, expression);
}

QVariant ServerNodeInstance::resetVariant(const PropertyName &propertyName) const
{
    return m_nodeInstance->resetValue(propertyName);
}

bool ServerNodeInstance::resetStateProperty(const ServerNodeInstance &target, const PropertyName &propertyName, const QVariant &resetValue)
{
    return m_nodeInstance->resetStateProperty(target.internalInstance(), propertyName, resetValue);
}

/*!
 Makes types used in node instances known to the Qml engine. To be called once at initialization time.
*/
void ServerNodeInstance::registerQmlTypes()
{
//    qmlRegisterType<QmlDesigner::Internal::QmlPropertyChangesObject>();
}

void ServerNodeInstance::doComponentComplete()
{
    m_nodeInstance->doComponentComplete();
}

QList<ServerNodeInstance> ServerNodeInstance::childItems() const
{
    return m_nodeInstance->childItems();
}

QQuickItem *ServerNodeInstance::rootQuickItem() const
{
    return qobject_cast<QQuickItem*>(internalObject());
}

QList<QQuickItem *> ServerNodeInstance::allItemsRecursive() const
{
    return m_nodeInstance->allItemsRecursive();
}

QString ServerNodeInstance::id() const
{
    if (isValid())
        return m_nodeInstance->id();

    return {};
}

qint32 ServerNodeInstance::instanceId() const
{
    if (isValid())
        return m_nodeInstance->instanceId();

    return -1;
}

QList<ServerNodeInstance> ServerNodeInstance::stateInstances() const
{
    return m_nodeInstance->stateInstances();
}

QStringList ServerNodeInstance::allStates() const
{
    if (isValid())
        return m_nodeInstance->allStates();

    return {};
}

Internal::ObjectNodeInstance::Pointer ServerNodeInstance::internalInstance() const
{
    return m_nodeInstance;
}

} // namespace QmlDesigner
