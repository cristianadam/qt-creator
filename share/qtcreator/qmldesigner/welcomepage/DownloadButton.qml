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
import WelcomeScreen 1.0
import StudioTheme 1.0 as StudioTheme

Item {
    id: root
    width: 30
    height: 30
    state: "default"

    property bool dowloadPressed: false
    property bool isHovered: mouseArea.containsMouse

    property bool globalHover: false

    property bool alreadyDownloaded: false
    property bool updateAvailable: false
    property bool downloadUnavailable: false

    signal downloadClicked()

    property color currentColor: {
        if (root.updateAvailable)
            return Constants.amberLight
        if (root.alreadyDownloaded)
            return Constants.greenLight
        if (root.downloadUnavailable)
            return Constants.redLight

        return Constants.currentGlobalText
    }

    property string currentIcon: {
        if (root.updateAvailable)
            return StudioTheme.Constants.downloadUpdate
        if (root.alreadyDownloaded)
            return StudioTheme.Constants.downloaded
        if (root.downloadUnavailable)
            return StudioTheme.Constants.downloadUnavailable

        return StudioTheme.Constants.download
    }

    property string currentToolTipText: {
        if (root.updateAvailable)
            return qsTr("Update available.")
        if (root.alreadyDownloaded)
            return qsTr("Example was already downloaded.")
        if (root.downloadUnavailable)
            return qsTr("Network or example is not available or the link is broken.")

        return qsTr("Download the example.")
    }

    Text {
        id: downloadIcon
        color: root.currentColor
        font.family: StudioTheme.Constants.iconFont.family
        text: root.currentIcon
        anchors.fill: parent
        font.pixelSize: 22
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        anchors.bottomMargin: 0
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true

        Connections {
            target: mouseArea
            onClicked: root.downloadClicked()
        }
    }

    ToolTip {
        id: toolTip
        y: -toolTip.height
        visible: mouseArea.containsMouse
        text: root.currentToolTipText
        delay: 1000
        height: 20
        background: Rectangle {
            color: Constants.currentToolTipBackground
            border.color: Constants.currentToolTipOutline
            border.width: 1
        }
        contentItem: Text {
            color: Constants.currentToolTipText
            text: toolTip.text
            verticalAlignment: Text.AlignVCenter
        }
    }

    states: [
        State {
            name: "default"
            when: !mouseArea.pressed && !mouseArea.containsMouse && !root.globalHover
            PropertyChanges {
                target: downloadIcon
                color: root.currentColor
            }
        },
        State {
            name: "pressed"
            when: mouseArea.pressed && mouseArea.containsMouse
            PropertyChanges {
                target: downloadIcon
                color: Constants.currentBrand
                scale: 1.2
            }
        },
        State {
            name: "hover"
            when: mouseArea.containsMouse && !mouseArea.pressed && !root.globalHover
            PropertyChanges {
                target: downloadIcon
                scale: 1.2
            }
        },
        State {
            name: "globalHover"
            extend: "hover"
            when: root.globalHover && !mouseArea.pressed && !mouseArea.containsMouse
        }
    ]
}
