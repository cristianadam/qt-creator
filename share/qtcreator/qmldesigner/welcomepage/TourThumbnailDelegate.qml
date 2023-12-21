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
import WelcomeScreen 1.0

Item {
    id: root
    width: Constants.thumbnailSize
    height: Constants.thumbnailSize
    state: "normal"
    clip: true

    property bool complete: root.currentSlide === root.endSlide

    // Needs to be set from the current slide show and user progress
    property int currentSlide: 0
    property int endSlide: 10

    signal clicked()

    Rectangle {
        id: background
        radius: 10
        color: Constants.currentNormalThumbnailBackground
        anchors.fill: parent

        property bool multiline: (tourNameLabelMetric.width >= tourNameLabel.width)

        TextMetrics {
            id: tourNameLabelMetric
            text: tourNameLabel.text
            font: tourNameLabel.font
        }

        Image {
            id: thumbnailPlaceholder
            source: thumbnail
            anchors.fill: parent
            anchors.bottomMargin: Constants.imageBottomMargin
            anchors.rightMargin: Constants.thumbnailMargin
            anchors.leftMargin: Constants.thumbnailMargin
            anchors.topMargin: Constants.thumbnailMargin
            fillMode: Image.PreserveAspectFit
            verticalAlignment: Image.AlignTop
            mipmap: true
        }

        Rectangle {
            id: tourNameBackground
            height: background.multiline ? Constants.titleHeightMultiline : Constants.titleHeight
            color: "#e5b0e4"
            radius: 3
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: thumbnailPlaceholder.bottom
            anchors.topMargin: Constants.titleBackgroundTopMargin
            anchors.leftMargin: Constants.thumbnailMargin
            anchors.rightMargin: Constants.thumbnailMargin

            Text {
                id: tourNameLabel
                color: Constants.currentGlobalText
                font.pixelSize: 16
                text: title
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                clip: false
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Constants.titleMargin
                anchors.rightMargin: Constants.titleMargin
            }
        }

        Text {
            id: subtitleCaption
            color: Constants.currentGlobalText
            text: subtitle
            renderType: Text.NativeRendering
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: tourNameBackground.bottom
            anchors.topMargin: 5
            anchors.rightMargin: Constants.thumbnailMargin
            anchors.leftMargin: Constants.thumbnailMargin
            leftPadding: 5
            rightPadding: 5
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true

            Connections {
                target: mouseArea
                onClicked: root.clicked()
            }
        }
    }

    states: [
        State {
            name: "normal"
            when: root.enabled && !mouseArea.containsMouse && !mouseArea.pressed && !root.complete

            PropertyChanges {
                target: background
                color: Constants.currentNormalThumbnailBackground
                border.width: 0
            }
            PropertyChanges {
                target: tourNameBackground
                color: Constants.currentNormalThumbnailLabelBackground
            }
            PropertyChanges {
                target: mouseArea
                enabled: true
            }
        },
        State {
            name: "hover"
            when: root.enabled && mouseArea.containsMouse && !mouseArea.pressed && !root.complete

            PropertyChanges {
                target: background
                color: Constants.currentHoverThumbnailBackground
                border.width: 0
            }
            PropertyChanges {
                target: tourNameBackground
                color: Constants.currentHoverThumbnailLabelBackground
            }
            PropertyChanges {
                target: mouseArea
                enabled: true
            }
        },
        State {
            name: "press"
            when: root.enabled && mouseArea.pressed && !root.complete

            PropertyChanges {
                target: background
                color: Constants.currentHoverThumbnailBackground
                border.color: Constants.currentBrand
                border.width: 2
            }
            PropertyChanges {
                target: tourNameBackground
                color: Constants.currentBrand
            }
            PropertyChanges {
                target: mouseArea
                enabled: true
            }
        }
    ]
}
