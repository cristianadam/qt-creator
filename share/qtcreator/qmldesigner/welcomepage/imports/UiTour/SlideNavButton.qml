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
import QtQuick.Controls
import WelcomeScreen 1.0
import StudioTheme 1.0 as StudioTheme
import UiTour

Item {
    id: tourButton
    width: 120
    height: 120
    property alias dialogButtonRotation: dialogButton.rotation
    property alias dialogButtonFontpixelSize: dialogButton.font.pixelSize
    property alias dialogButtonText: dialogButton.text

    signal buttonClicked

    Text {
        id: dialogButton
        color: "#ffffff"
        text: StudioTheme.Constants.nextFile_large
        font.family: StudioTheme.Constants.iconFont.family
        font.pixelSize: 32
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        Connections {
            target: mouseArea
            onClicked: tourButton.buttonClicked()
        }
    }

    states: [
        State {
            name: "normal"
            when: !mouseArea.containsMouse && !mouseArea.pressed

            PropertyChanges {
                target: dialogButton
                color: "#ecebeb"
                font.pixelSize: 92
            }
        },
        State {
            name: "hover"
            when: mouseArea.containsMouse && !mouseArea.pressed

            PropertyChanges {
                target: dialogButton
                font.pixelSize: 96
            }
        },
        State {
            name: "press"
            when: mouseArea.pressed

            PropertyChanges {
                target: dialogButton
                font.pixelSize: 98
            }
        }
    ]
}
