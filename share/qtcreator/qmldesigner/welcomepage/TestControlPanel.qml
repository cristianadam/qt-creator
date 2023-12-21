/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
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

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WelcomeScreen 1.0
import StudioTheme 1.0 as Theme

Rectangle {
    id: controlPanel
    width: 220
    height: 80
    color: "#9b787878"
    radius: 10

    property bool closeOpen: true

    Text {
        id: closeOpen
        x: 203
        color: "#ffffff"
        text: qsTr("X")
        anchors.right: parent.right
        anchors.top: parent.top
        font.pixelSize: 12
        horizontalAlignment: Text.AlignRight
        anchors.rightMargin: 9
        anchors.topMargin: 6

        MouseArea {
            id: mouseArea
            anchors.fill: parent

            Connections {
                target: mouseArea
                function onClicked(mouse) { controlPanel.closeOpen = !controlPanel.closeOpen }
            }
        }
    }

    Text {
        id: themeSwitchLabel
        x: 8
        y: 50
        color: "#ffffff"
        text: qsTr("Theme")
        font.pixelSize: 12
        horizontalAlignment: Text.AlignRight
        anchors.rightMargin: 6
    }

    Text {
        id: lightLabel
        x: 172
        y: 26
        color: "#ffffff"
        text: qsTr("light")
        font.pixelSize: 12
        horizontalAlignment: Text.AlignRight
    }

    Text {
        id: darkLabel
        x: 65
        y: 26
        color: "#ffffff"
        text: qsTr("dark")
        font.pixelSize: 12
        horizontalAlignment: Text.AlignRight
    }

    Slider {
        id: themeSlider
        x: 58
        y: 44
        width: 145
        height: 28
        snapMode: RangeSlider.SnapAlways
        value: 0
        from: 0
        to: 1
        stepSize: 1

        Connections {
            target: themeSlider
            function onValueChanged(value) { Theme.Values.style = themeSlider.value }
        }
    }

    CheckBox {
        id: basicCheckBox
        x: 60
        y: 79
        text: qsTr("")
        checked: true
        onToggled: { Constants.basic = !Constants.basic }
    }

    CheckBox {
        id: communityCheckBox
        x: 174
        y: 79
        text: qsTr("")
        checked: false
        onToggled: { Constants.communityEdition = !Constants.communityEdition }
    }

    Text {
        id: basicEditionLabel
        x: 8
        y: 92
        color: "#ffffff"
        text: qsTr("Basic")
        font.pixelSize: 12
        horizontalAlignment: Text.AlignRight
        anchors.rightMargin: 6
    }

    Text {
        id: communityEditionLabel
        x: 116
        y: 92
        color: "#ffffff"
        text: qsTr("Community")
        font.pixelSize: 12
        horizontalAlignment: Text.AlignRight
        anchors.rightMargin: 6
    }

    states: [
        State {
            name: "open"
            when: controlPanel.closeOpen
        },
        State {
            name: "close"
            when: !controlPanel.closeOpen

            PropertyChanges {
                target: closeOpen
                text: qsTr("<")
            }

            PropertyChanges {
                target: controlPanel
                width: 28
                height: 26
                clip: true
            }
        }
    ]
}
