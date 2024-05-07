// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "qt5import3dnodeinstanceserver.h"

#include "createscenecommand.h"
#include "view3dactioncommand.h"

#include "imagecontainer.h"
#include "nodeinstanceclientinterface.h"
#include "puppettocreatorcommand.h"

#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlEngine>

#include <QQmlProperty>

#include <private/qquick3dnode_p.h>
#include <private/qquick3dviewport_p.h>
#include <private/qquickdesignersupport_p.h>

namespace QmlDesigner {

Qt5Import3dNodeInstanceServer::Qt5Import3dNodeInstanceServer(NodeInstanceClientInterface *nodeInstanceClient) :
    Qt5NodeInstanceServer(nodeInstanceClient)
{
    setSlowRenderTimerInterval(100000000);
    setRenderTimerInterval(20);

#ifdef QUICK3D_MODULE
    m_generalHelper = new Internal::GeneralHelper();
    QObject::connect(m_generalHelper, &Internal::GeneralHelper::requestRender, this, [this]() {
        startRenderTimer();
    });
#endif
}

Qt5Import3dNodeInstanceServer::~Qt5Import3dNodeInstanceServer()
{
    cleanup();
}

void Qt5Import3dNodeInstanceServer::createScene(const CreateSceneCommand &command)
{
    initializeView();
    registerFonts(command.resourceUrl);
    setTranslationLanguage(command.language);
    setupScene(command);
    startRenderTimer();
}

void Qt5Import3dNodeInstanceServer::view3DAction([[maybe_unused]] const View3DActionCommand &command)
{
    switch (command.type()) {
    case View3DActionType::Import3dUpdatePreviewImage: {
        QObject *obj = rootItem();
        if (obj) {
            QSize size =  command.value().toSize();
            QQmlProperty wProp(obj, "width", context());
            QQmlProperty hProp(obj, "height", context());
            wProp.write(size.width());
            hProp.write(size.height());
            resizeCanvasToRootItem();

            startRenderTimer();
        }
        break;
    }
    default:
        break;
    }
}

void Qt5Import3dNodeInstanceServer::startRenderTimer()
{
    if (timerId() != 0)
        killTimer(timerId());

    int timerId = startTimer(renderTimerInterval());

    setTimerId(timerId);
}

void Qt5Import3dNodeInstanceServer::cleanup()
{
#ifdef QUICK3D_MODULE
    delete m_previewNode;
    delete m_generalHelper;
#endif
}

void Qt5Import3dNodeInstanceServer::finish()
{
    cleanup();
}

void Qt5Import3dNodeInstanceServer::collectItemChangesAndSendChangeCommands()
{
    static bool inFunction = false;

    if (!rootNodeInstance().holdsGraphical())
        return;

    if (!inFunction) {
        inFunction = true;

        QQuickDesignerSupport::polishItems(quickWindow());

        render();

        inFunction = false;
    }
}

void Qt5Import3dNodeInstanceServer::render()
{
#ifdef QUICK3D_MODULE
    ++m_renderCount;

    if (m_renderCount == 1) {
        QObject *obj = rootItem();
        QQmlProperty viewProp(obj, "view3d", context());
        QObject *viewObj = viewProp.read().value<QObject *>();
        m_view3D = qobject_cast<QQuick3DViewport *>(viewObj);
        if (m_view3D) {
            QQmlProperty sceneModelNameProp(obj, "sceneModelName", context());
            QString sceneModelName = sceneModelNameProp.read().toString();
            QFileInfo fi(fileUrl().toLocalFile());
            QString compPath = fi.absolutePath() + '/' + sceneModelName + ".qml";
            QQmlComponent comp(engine(), compPath, QQmlComponent::PreferSynchronous);
            m_previewNode = qobject_cast<QQuick3DNode *>(comp.create(context()));
            if (m_previewNode) {
                engine()->setObjectOwnership(m_previewNode, QJSEngine::CppOwnership);
                m_previewNode->setParent(m_view3D);
                m_view3D->setImportScene(m_previewNode);
            }
        }
    }

    // Render scene once to ensure geometries are intialized so bounds calculations work correctly
    if (m_renderCount == 2) {
        if (m_view3D) {
            m_generalHelper->calculateNodeBoundsAndFocusCamera(m_view3D->camera(), m_previewNode,
                                                               m_view3D, 1040, false);
        }
    }

    rootNodeInstance().updateDirtyNodeRecursive();
    QImage renderImage = grabWindow();

    // static int counter = 0;
    // renderImage.save(QStringLiteral("c:/dev/debug_images/import_prev_%1.png").arg(counter++));

    if (m_renderCount >= 2) {
        ImageContainer imgContainer(0, renderImage, m_renderCount);
        nodeInstanceClient()->handlePuppetToCreatorCommand(
            {PuppetToCreatorCommand::Import3DPreviewImage,
             QVariant::fromValue(imgContainer)});
        slowDownRenderTimer(); // No more renders needed for now
    }
#else
    slowDownRenderTimer();
#endif
}

} // namespace QmlDesigner
