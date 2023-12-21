/****************************************************************************
**
** Copyright (C) 2023 The Qt Company Ltd.
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
import WelcomeScreen 1.0

Rectangle {
    id: restart
    height: 36
    color: "#00ffffff"
    radius: 18
    border.color: "#f9f9f9"
    border.width: 3
    state: "normal"

    signal restart()

    Text {
        id: text2
        color: "#ffffff"
        text: qsTrId("Restart")
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: 12
        anchors.horizontalCenter: parent.horizontalCenter
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        Connections {
            target: mouseArea
            onClicked: restart.restart()
        }
    }

    states: [
        State {
            name: "normal"
            when: !mouseArea.containsMouse && !mouseArea.pressed

            PropertyChanges {
                target: text2
                color: "#dedede"
            }

            PropertyChanges {
                target: restart
                border.color: "#dedede"
            }
        },
        State {
            name: "hover"
            when: mouseArea.containsMouse && !mouseArea.pressed

            PropertyChanges {
                target: restart
                color: "#00ffffff"
                border.color: "#ffffff"
            }

            PropertyChanges {
                target: text2
                color: "#ffffff"
            }
        },
        State {
            name: "press"
            when: mouseArea.pressed

            PropertyChanges {
                target: restart
                color: "#ffffff"
                border.color: "#ffffff"
            }

            PropertyChanges {
                target: text2
                color: "#000000"
            }
        }
    ]
}
