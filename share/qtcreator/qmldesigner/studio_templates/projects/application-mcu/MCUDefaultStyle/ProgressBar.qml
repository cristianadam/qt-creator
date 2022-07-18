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

T.ProgressBar {
    id: root

    implicitWidth: Math.max((background ? background.implicitWidth : 0) + leftInset + rightInset,
                            (contentItem ? contentItem.implicitWidth : 0) + leftPadding + rightPadding)
    implicitHeight: Math.max((background ? background.implicitHeight : 0) + topInset + bottomInset,
                             (contentItem ? contentItem.implicitHeight : 0) + topPadding + bottomPadding)

    leftInset: DefaultStyle.inset
    topInset: DefaultStyle.inset
    rightInset: DefaultStyle.inset
    bottomInset: DefaultStyle.inset
    leftPadding: DefaultStyle.padding
    topPadding: DefaultStyle.padding
    rightPadding: DefaultStyle.padding
    bottomPadding: DefaultStyle.padding
    background: Item {
        implicitWidth: 200
        implicitHeight: 16
        BorderImage {
            width: parent.width; height: 16
            anchors.verticalCenter: parent.verticalCenter
            border.left: 7
            border.right: 7
            border.top: 7
            border.bottom: 7
            source: "images/progressbar-background.png"
        }
    }
    contentItem: Item {
        implicitWidth: 200
        implicitHeight: 16
        BorderImage {
            width: parent.width * root.position; height: 16
            anchors.verticalCenter: parent.verticalCenter
            border.left: 6
            border.right: 6
            border.top: 7
            border.bottom: 7
            source: "images/progressbar-progress.png"
        }
    }
}
