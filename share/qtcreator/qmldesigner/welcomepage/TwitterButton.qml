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
import WelcomeScreen 1.0
import StudioTheme 1.0 as StudioTheme

Item {
    id: twitterButton
    state: "darkNormal"

    property bool isHovered: mouseArea.containsMouse

    Image {
        id: twitterDarkNormal
        anchors.fill: parent
        source: "images/twitterDarkNormal.png"
        fillMode: Image.PreserveAspectFit
    }

    Image {
        id: twitterLightNormal
        anchors.fill: parent
        source: "images/twitterLightNormal.png"
        fillMode: Image.PreserveAspectFit
    }

    Image {
        id: twitterHover
        anchors.fill: parent
        source: "images/twitterHover.png"
        fillMode: Image.PreserveAspectFit
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        Connections {
            target: mouseArea
            function onClicked(mouse) { Qt.openUrlExternally("https://twitter.com/qtproject/") }
        }
    }

    states: [
        State {
            name: "darkNormal"
            when: !StudioTheme.Values.isLightTheme && !mouseArea.containsMouse && !mouseArea.pressed

            PropertyChanges {
                target: twitterDarkNormal
                visible: true
            }

            PropertyChanges {
                target: twitterLightNormal
                visible: false
            }

            PropertyChanges {
                target: twitterHover
                visible: false
            }
        },
        State {
            name: "lightNormal"
            when: StudioTheme.Values.isLightTheme && !mouseArea.containsMouse && !mouseArea.pressed

            PropertyChanges {
                target: twitterHover
                visible: false
            }

            PropertyChanges {
                target: twitterLightNormal
                visible: true
            }

            PropertyChanges {
                target: twitterDarkNormal
                visible: false
            }
        },
        State {
            name: "hover"
            when: mouseArea.containsMouse && !mouseArea.pressed

            PropertyChanges {
                target: twitterHover
                visible: true
            }

            PropertyChanges {
                target: twitterLightNormal
                visible: false
            }

            PropertyChanges {
                target: twitterDarkNormal
                visible: false
            }
        },
        State {
            name: "press"
            when: (mouseArea.containsMouse || !mouseArea.containsMouse) && mouseArea.pressed

            PropertyChanges {
                target: twitterHover
                visible: true
                scale: 1.1
            }

            PropertyChanges {
                target: twitterLightNormal
                visible: false
            }

            PropertyChanges {
                target: twitterDarkNormal
                visible: false
            }
        }
    ]
}
