// Copyright  (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0  WITH Qt-GPL-exception-1.0

import QtQuick3D 6.0

View3D {
    id: root
    anchors.fill: parent
    environment: sceneEnv

    property Material previewMaterial

    function fitToViewPort(closeUp)
    {
        // No need to zoom this view, this is here just to avoid runtime warnings
    }

    SceneEnvironment {
        id: sceneEnv
        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.High
    }

    Node {
        DirectionalLight {
            eulerRotation.x: -26
            eulerRotation.y: -57
        }

        PerspectiveCamera {
            y: 125.331
            z: 120
            eulerRotation.x: -31
            clipNear: 1
            clipFar: 1000
        }

        Model {
            id: model
            readonly property bool _edit3dLocked: true // Make this non-pickable

            y: 50
            source: "#Sphere"
            materials: previewMaterial
        }

    }
}
