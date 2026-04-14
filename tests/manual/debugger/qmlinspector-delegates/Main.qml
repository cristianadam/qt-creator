// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// Reproducer for QTCREATORBUG-34170.
//
// Four groups of MyItem instances, created four different ways:
//
//   grid1 - Repeater        (delegate-based, invisible before fix)
//   grid2 - ListView        (delegate-based, invisible before fix)
//   grid3 - createObject()  (visible before fix)
//   grid4 - static QML      (visible before fix)
//
// Set a breakpoint anywhere, or simply pause, then inspect the Locals tree.
// After the fix all four groups should be visible and expandable.

import QtQuick

Window {
    id: root
    width: 500
    height: 340
    visible: true
    title: "QTCREATORBUG-34170 reproducer"

    property list<QtObject> createdItems

    Component { id: myItemComponent; MyItem {} }

    Component.onCompleted: {
        // grid3: instantiate via createObject()
        for (var i = 0; i < 3; ++i)
            createdItems.push(myItemComponent.createObject(grid3, { label: "c" + i }))
    }

    Column {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // grid1: Repeater (delegate-based)
        Row {
            id: grid1
            spacing: 4
            Repeater {
                model: 3
                MyItem { label: "r" + index }
            }
        }

        // grid2: ListView (delegate-based)
        ListView {
            id: grid2
            width: parent.width
            height: 70
            clip: true
            orientation: ListView.Horizontal
            spacing: 4
            model: 3
            delegate: MyItem { label: "l" + index }
        }

        // grid3: createObject() (populated in Component.onCompleted above)
        Row {
            id: grid3
            spacing: 4
        }

        // grid4: static QML declarations
        Row {
            id: grid4
            spacing: 4
            MyItem { label: "s0" }
            MyItem { label: "s1" }
            MyItem { label: "s2" }
        }
    }
}
