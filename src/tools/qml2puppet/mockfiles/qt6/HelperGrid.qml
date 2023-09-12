// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick 6.0
import QtQuick3D 6.0
import GridGeometry 1.0

Node {
    id: grid

    property alias lines: gridGeometry.lines
    property alias step: gridGeometry.step
    property alias subdivAlpha: subGridMaterial.generalAlpha
    property alias gridColor: mainGridMaterial.color
    property real density: 2500 / gridGeometry.step
    property bool orthoMode: false

    eulerRotation.x: 90

    // Note: Only one instance of HelperGrid is supported, as the geometry names are fixed

    Model { // Main grid lines
        readonly property bool _edit3dLocked: true // Make this non-pickable
        geometry: GridGeometry {
            id: gridGeometry
            name: "3D Edit View Helper Grid"
        }

        materials: [
            GridMaterial {
                id: mainGridMaterial
                color: "#cccccc"
                density: grid.density
                orthoMode: grid.orthoMode
            }
        ]
    }

    Model { // Subdivision lines
        readonly property bool _edit3dLocked: true // Make this non-pickable
        geometry: GridGeometry {
            lines: gridGeometry.lines
            step: gridGeometry.step
            isSubdivision: true
            name: "3D Edit View Helper Grid subdivisions"
        }

        materials: [
            GridMaterial {
                id: subGridMaterial
                color: mainGridMaterial.color
                density: grid.density
                orthoMode: grid.orthoMode
            }
        ]
    }

    Model { // Z Axis
        readonly property bool _edit3dLocked: true // Make this non-pickable
        geometry: GridGeometry {
            lines: gridGeometry.lines
            step: gridGeometry.step
            isCenterLine: true
            name: "3D Edit View Helper Grid Z Axis"
        }
        materials: [
            GridMaterial {
                id: vCenterLineMaterial
                color: "#00a1d2"
                density: grid.density
                orthoMode: grid.orthoMode
            }
        ]
    }
    Model { // X Axis
        readonly property bool _edit3dLocked: true // Make this non-pickable
        eulerRotation.z: 90
        geometry: GridGeometry {
            lines: gridGeometry.lines
            step: gridGeometry.step
            isCenterLine: true
            name: "3D Edit View Helper Grid X Axis"
        }
        materials: [
            GridMaterial {
                id: hCenterLineMaterial
                color: "#cb211a"
                density: grid.density
                orthoMode: grid.orthoMode
            }
        ]
    }
}
