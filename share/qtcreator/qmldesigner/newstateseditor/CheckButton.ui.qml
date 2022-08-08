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

import QtQuick 2.15
import QtQuick.Templates 2.5
//import StatesPrototype
import StudioTheme as StudioTheme

Button {
    id: control

    leftPadding: 4
    rightPadding: 4
    text: "My Button"
    checkable: true
    state: "default"

    background: Rectangle {
        id: buttonBackground
        color: "#00000000"
        implicitWidth: 100
        implicitHeight: 40
        opacity: control.enabled ? 1 : 0.3
        border.color: "#047eff"
        anchors.fill: parent
    }

    contentItem: Text {
        id: textItem
        text: control.text
        font.pixelSize: 12
        opacity: control.enabled ? 1.0 : 0.3
        color: "#047eff"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    states: [
        State {
            name: "default"
            when: !control.down && !control.hovered && !control.checked

            PropertyChanges {
                target: buttonBackground
                color: StudioTheme.Values.themeControlBackground
                border.color: StudioTheme.Values.themeControlOutline
            }
            PropertyChanges {
                target: textItem
                color: StudioTheme.Values.themeTextColor
            }
        },
        State {
            name: "hover"
            when: control.hovered && !control.checked && !control.down

            PropertyChanges {
                target: textItem
                color: StudioTheme.Values.themeTextColor
            }
            PropertyChanges {
                target: buttonBackground
                color: StudioTheme.Values.themeControlBackgroundHover
                border.color: StudioTheme.Values.themeControlOutline
            }
        },
        State {
            name: "checked"
            when: (control.checked || control.down)

            PropertyChanges {
                target: textItem
                color: StudioTheme.Values.themeTextSelectedTextColor
            }
            PropertyChanges {
                target: buttonBackground
                color: StudioTheme.Values.themeInteraction
                border.color: "transparent"
            }
        }
    ]
}
