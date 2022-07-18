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

import QtQuick 2.15
import QtQuick.Templates 2.15 as T

T.Button {
    id: control

    implicitWidth: Math.max((background ? background.implicitWidth : 0) + leftInset + rightInset,
                            (contentItem ? contentItem.implicitWidth : 0) + leftPadding + rightPadding)
    implicitHeight: Math.max((background ? background.implicitHeight : 0) + topInset + bottomInset,
                             (contentItem ? contentItem.implicitHeight : 0) + topPadding + bottomPadding)

    leftPadding: 16
    topPadding: 22
    rightPadding: 16
    bottomPadding: 16

    // We use the standard inset, but there is also some extra
    // space on each edge of the background image, so we account for that here
    // to ensure that there is visually an equal inset on each edge.
    leftInset: DefaultStyle.inset - 12
    topInset: DefaultStyle.inset - 6
    rightInset: DefaultStyle.inset - 12
    bottomInset: DefaultStyle.inset - 18

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? DefaultStyle.textColor : DefaultStyle.disabledTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Item {
        implicitWidth: 136
        implicitHeight: 66

        BorderImage {
            border.top: 18
            border.left: 22
            border.right: 22
            border.bottom: 27
            source: control.enabled
                ? (control.down
                   ? "images/button-pressed.png"
                   : "images/button.png")
                : "images/button-disabled.png"
            anchors.fill: parent
        }
    }
}
