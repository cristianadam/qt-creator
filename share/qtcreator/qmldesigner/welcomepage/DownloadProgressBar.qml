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
import StudioTheme as StudioTheme

Item {
    id: progressBar
    width: 272
    height: 25
    property bool downloadFinished: false
    property int value: 0
    property bool closeButtonVisible
    //property alias numberAnimationRunning: numberAnimation.running

    readonly property int margin: 4

    signal cancelRequested

    Rectangle {
        id: progressBarGroove
        color: Constants.currentNormalThumbnailLabelBackground
        anchors.fill: parent
    }

    Rectangle {
        id: progressBarTrack
        width: progressBar.value * ((progressBar.width - closeButton.width) - 2 * progressBar.margin) / 100
        color: Constants.currentBrand
        border.color: "#002e769e"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: progressBar.margin
    }

    Text {
        id: closeButton
        visible: progressBar.closeButtonVisible
        width: 20
        text: StudioTheme.Constants.closeCross
        color: Constants.currentBrand
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        font.family: StudioTheme.Constants.iconFont.family
        font.pixelSize: StudioTheme.Values.myIconFontSize

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: progressBar.margin

        MouseArea {
            anchors.fill: parent
            onClicked: {
                progressBar.cancelRequested()
            }
        }
    }

/*
    NumberAnimation {
        id: numberAnimation
        target: progressBarTrack
        property: "width"
        duration: 2500
        easing.bezierCurve: [0.197,0.543,0.348,0.279,0.417,0.562,0.437,0.757,0.548,0.731,0.616,0.748,0.728,0.789,0.735,0.982,1,1]
        alwaysRunToEnd: true
        to: progressBar.width
        from: 0
    }

    Connections {
        target: numberAnimation
        onFinished: progressBar.downloadFinished = true
    }
*/
}
