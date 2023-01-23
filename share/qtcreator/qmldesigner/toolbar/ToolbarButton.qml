// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

import QtQuick
import QtQuick.Controls
import StudioControls
import StudioTheme 1.0 as StudioTheme
import HelperWidgets 2.0

Item {
    id: item1
    property bool isBlocked: false
    property bool isHovered: false
    property bool isChecked: false
    signal clicked()
    property alias textText: text1.text
    width: 36
    height: 36
    property alias textFontpixelSize: text1.font.pixelSize
    state: "normal"
    property alias textRotation: text1.rotation

    property bool enabled: true

    property alias tooltip: mouseArea.tooltip

    property alias color: text1.color

    Rectangle {
        id: backButton
        color: "#2b2a2a"
        border.color: "#1f1f1f"

        anchors.fill: parent

        Text {
            id: text1
            anchors.fill: parent
            color: item1.enabled ? StudioTheme.Values.themeTextColor : StudioTheme.Values.themeTextColorDisabled
            text: StudioTheme.Constants.upDownSquare2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            //rotation: 90
            rotation: 0
            font.pixelSize: StudioTheme.Values.topLevelComboIcon
            font.family: StudioTheme.Constants.iconFont.family
        }
    }

    ToolTipArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        Connections {
            target: mouseArea
            onClicked: item1.clicked()
        }
    }
    states: [
        State {
            name: "normal"
            when: !mouseArea.containsMouse && !mouseArea.pressed && !item1.isBlocked && !item1.isChecked

            PropertyChanges {
                target: backButton
                border.color: "#001f1f1f"
            }
        },
        State {
            name: "hover"
            when: mouseArea.containsMouse && !mouseArea.pressed && !item1.isBlocked && !item1.isChecked

            PropertyChanges {
                target: backButton
                color: "#403f3f"
            }

            PropertyChanges {
                target: item1
                isHovered: true
            }
        },
        State {
            name: "pressed"
            when: mouseArea.pressed && !item1.isBlocked && !item1.isChecked

            PropertyChanges {
                target: backButton
                color: "#57b9fc"
                border.color: "#57b9fc"
            }

            PropertyChanges {
                target: text1
                color: "#111111"
            }
        },
        State {
            name: "blocked"
            when: item1.isBlocked

            PropertyChanges {
                target: backButton
                border.color: "#001f1f1f"
            }

            PropertyChanges {
                target: text1
                color: "#6d6c6c"
            }
        },
        State {
            name: "checked"
            when: !item1.isBlocked && item1.isChecked
            PropertyChanges {
                target: backButton
                border.color: "#001f1f1f"
            }

            PropertyChanges {
                target: text1
                color: "#57b9fc"
            }
        }
    ]
}
