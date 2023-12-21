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
import QtQuick.Timeline

Rectangle {
    id: root
    width: 872
    height: 860
    radius: 4
    color: "transparent"
    border.color: "#1381e3"
    border.width: 8
    state: "off"

    property bool active: true

    states: [
        State {
            name: "on"
            when: root.active

            PropertyChanges {
                target: root
            }
        },
        State {
            name: "off"
            when: !root.active

            PropertyChanges {
                target: root
                opacity: 0
            }
        }
    ]

    transitions: [
        Transition {
            id: transition
            to: "*"
            from: "*"
            ParallelAnimation {
                SequentialAnimation {
                    PauseAnimation { duration: 0 }

                    PropertyAnimation {
                        target: root
                        property: "opacity"
                        duration: 150
                    }
                }
            }
        }
    ]
}



