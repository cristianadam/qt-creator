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
import QtQuick.Layouts

Item {
    id: root
    height: (repeater.count > root.columnCount) ? root.tagHeight * 2 + root.tagSpacing
                                                : root.tagHeight

    property alias tags: repeater.model
    // private
    property int tagWidth: 75
    property int tagHeight: 25
    property int tagSpacing: 4

    readonly property int columnCount: 3

    Connections {
        target: root
        onWidthChanged: root.tagWidth = Math.floor((root.width - root.tagSpacing
                                                    * (root.columnCount - 1)) / root.columnCount)
    }

    Flow {
        anchors.fill: parent
        spacing: root.tagSpacing

        Repeater {
            id: repeater
            model: ["Qt 6", "Qt for MCU", "3D"]

            Tag {
                text: modelData
                width: root.tagWidth
                height: root.tagHeight
            }
        }
    }
}
