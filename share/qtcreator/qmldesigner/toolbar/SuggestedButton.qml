// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

import QtQuick
import QtQuick.Controls
import StudioControls

Item {
    id: item1
    width: 96
    height: 38
    property bool isChecked: false
    property alias labelText: label.text
    signal clicked
    Rectangle {
        id: suggestedButton
        color: "#57b9fc"
        radius: 10
        anchors.fill: parent

        Text {
            id: label
            text: qsTr("Live Preview")
            anchors.fill: parent
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.styleName: "Regular"
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        Connections {
            target: mouseArea
            onClicked: {
                item1.isChecked = !item1.isChecked
                item1.clicked()
            }

        }
    }
    states: [
        State {
            name: "normal"
            when: !mouseArea.containsMouse && !mouseArea.pressed && !item1.isChecked
        },
        State {
            name: "hover"
            when: mouseArea.containsMouse

            PropertyChanges {
                target: suggestedButton
                color: "#1c95e7"
            }

        },
        State {
            name: "checked"
            when: item1.isChecked

            PropertyChanges {
                target: suggestedButton
                color: "#191b1c"
            }

            PropertyChanges {
                target: label
                color: "#ffffff"
            }

        }
    ]
}
