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
import QtQuick.Templates as T
import WelcomeScreen 1.0
import StudioTheme 1.0 as StudioTheme

T.ScrollBar {
    id: control

    property bool show: false
    property bool otherInUse: false
    property bool isNeeded: control.size < 1.0
    property bool inUse: control.hovered || control.pressed
    property int thickness: control.inUse || control.otherInUse ? 10 : 8

    property bool scrollBarVisible: parent.childrenRect.height > parent.height

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    hoverEnabled: true
    padding: 0
    minimumSize: orientation === Qt.Horizontal ? height / width : width / height

    opacity: 0.0

    contentItem: Rectangle {
        implicitWidth: control.thickness
        implicitHeight: control.thickness
        radius: width / 2
        color: control.inUse ? Constants.currentScrollBarHandle
                             : Constants.currentScrollBarHandle_idle
    }

    background: Rectangle {
        id: controlTrack
        color: Constants.currentScrollBarTrack
        opacity: control.inUse || control.otherInUse ? 0.3 : 0.0
        radius: width / 2

        Behavior on opacity {
            PropertyAnimation {
                duration: 100
                easing.type: Easing.InOutQuad
            }
        }
    }

    states: [
        State {
            name: "show"
            when: control.show
            PropertyChanges {
                target: control
                opacity: 1.0
            }
        },
        State {
            name: "hide"
            when: !control.show
            PropertyChanges {
                target: control
                opacity: 0.0
            }
        }
    ]

    transitions: Transition {
        from: "show"
        SequentialAnimation {
            PauseAnimation { duration: 450 }
            NumberAnimation {
                target: control
                duration: 200
                property: "opacity"
                to: 0.0
            }
        }
    }

    Behavior on thickness {
        PropertyAnimation {
            duration: 100
            easing.type: Easing.InOutQuad
        }
    }

    Behavior on x {
        PropertyAnimation {
            duration: 100
            easing.type: Easing.InOutQuad
        }
    }
}
