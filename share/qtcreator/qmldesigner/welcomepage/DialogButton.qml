/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
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
import QtQuick.Templates
import WelcomeScreen 1.0

Button {
    id: root

    implicitWidth: Math.max(
                       background ? background.implicitWidth : 0,
                       textItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(
                        background ? background.implicitHeight : 0,
                        textItem.implicitHeight + topPadding + bottomPadding)
    leftPadding: 4
    rightPadding: 4


    background: Rectangle {
        id: background
        implicitWidth: 80
        implicitHeight: 20
        opacity: enabled ? 1 : 0.3
        radius: 2
        color: Constants.currentPushButtonNormalBackground
        border.color: Constants.currentPushButtonNormalOutline
        anchors.fill: parent
    }

    contentItem: Text {
        id: textItem
        text: root.text
        font.pixelSize: 12
        opacity: enabled ? 1.0 : 0.3
        color: Constants.currentGlobalText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    states: [
        State {
            name: "default"
            when: !root.down && !root.hovered && !root.checked

            PropertyChanges {
                target: background
                color: Constants.currentPushButtonNormalBackground
                border.color: Constants.currentPushButtonNormalOutline
            }

            PropertyChanges {
                target: textItem
                color: Constants.currentGlobalText
            }
        },
        State {
            name: "hover"
            when: root.hovered && !root.checked && !root.down

            PropertyChanges {
                target: textItem
                color: Constants.currentGlobalText
            }

            PropertyChanges {
                target: background
                color: Constants.currentPushButtonHoverBackground
                border.color: Constants.currentPushButtonHoverOutline
            }
        },
        State {
            name: "active"
            when: root.checked || root.down

            PropertyChanges {
                target: textItem
                color: Constants.currentActiveGlobalText
            }

            PropertyChanges {
                target: background
                color: Constants.currentBrand
                border.color: "#00000000"
            }
        }
    ]
}
