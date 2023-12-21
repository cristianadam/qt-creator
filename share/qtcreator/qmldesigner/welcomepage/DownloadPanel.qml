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

Rectangle {
    id: root
    color: Constants.currentNormalThumbnailBackground

    property alias value: progressBar.value
    property alias text: progressLabel.text
    property alias allowCancel: progressBar.closeButtonVisible

    readonly property int pixelSize: 12
    readonly property int textMargin: 5

    signal cancelRequested

    DownloadProgressBar {
        id: progressBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: pushButton.top
        anchors.bottomMargin: 40
        anchors.rightMargin: 10
        anchors.leftMargin: 10

        onCancelRequested: root.cancelRequested()

        Text {
            id: progressLabel
            color: Constants.currentGlobalText
            text: qsTr("Progress:")
            anchors.top: parent.bottom
            anchors.topMargin: root.textMargin
            anchors.left: parent.left
            font.pixelSize: root.pixelSize
        }

        Text {
            id: progressAmount
            color: Constants.currentGlobalText
            text: stringMapper.text
            anchors.top: parent.bottom
            anchors.topMargin: root.textMargin
            anchors.right: percentSign.left
            anchors.rightMargin: root.textMargin
            font.pixelSize: root.pixelSize
        }

        Text {
            id: percentSign
            color: Constants.currentGlobalText
            text: qsTr("%")
            anchors.right: parent.right
            anchors.top: parent.bottom
            anchors.topMargin: root.textMargin
            font.pixelSize: root.pixelSize
        }
    }

    PushButton {
        id: pushButton
        y: 177
        visible: progressBar.downloadFinished
        text: qsTr("Open")
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 40
    }

    StringMapper {
        id: stringMapper
        decimals: 1
        input: root.value
    }
}
