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
import QtQuick.Layouts
import QtQuick.Shapes
import UiTour

Item {
    id: root
    width: 1920
    height: 1080
    visible: true

    property alias slideSource: loader.source
    property alias loaderActive: loader.active
    property var mainScreen: loader.item

    Image {
        id: gradientRect
        anchors.fill: parent
        source: "gradientRect.webp"
        mipmap: true
        fillMode: Image.Stretch
    }

    RowLayout {
        anchors.fill: parent

        Item {
            Layout.preferredWidth: 160
            Layout.maximumWidth: 160
            Layout.fillHeight: true

            SlideNavButton {
                id: prevSlideButton
                dialogButtonRotation: 180
                visible: ((mainScreen.currentSlide + 1) !== 1)
                anchors.fill: parent

                Connections {
                    target: prevSlideButton
                    onButtonClicked: mainScreen.prev()
                }
            }
        }

        Column {
            id: content
            Layout.fillWidth: true
            Layout.preferredWidth: 120

            Item {
                id: titleFrame
                width: content.width
                height: 100

                Text {
                    color: "#ffffff"
                    text: mainScreen.title
                    font.pixelSize: 40
                    font.bold: true
                    wrapMode: Text.WordWrap
                    anchors.centerIn: parent
                }
            }

            Item {
                id: slideFrame
                width: content.width
                height: Math.min(1080 * content.width / 1920,
                                 root.height - (titleFrame.height + captionFrame.height + indicatorFrame.height))

                Loader {
                    id: loader
                    source: "MySlideShow.ui.qml"
                    transformOrigin: Item.Center
                    scale: Math.min(slideFrame.width / 1920, slideFrame.height / 1080)
                    anchors.centerIn: parent
                }
            }

            Item {
                id: captionFrame
                width: content.width
                height: 140

                Text {
                    id: captionText
                    color: "#ffffff"
                    text: mainScreen.caption
                    font.pixelSize: 20
                    font.bold: true
                    wrapMode: Text.WordWrap
                    anchors.fill: parent
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item {
                id: indicatorFrame
                width: content.width
                height: 100

                Text {
                    color: "#ffffff"
                    text: (mainScreen.currentSlide + 1) + "/" + mainScreen.progress
                    font.pixelSize: 20
                    anchors.centerIn: parent
                }
            }
        }

        Item {
            Layout.preferredWidth: 160
            Layout.maximumWidth: 160
            Layout.fillHeight: true

            SlideNavButton {
                id: nextSlideButton
                visible: (mainScreen.progress !== (mainScreen.currentSlide + 1))
                anchors.fill: parent

                Connections {
                    target: nextSlideButton
                    onButtonClicked: mainScreen.next()
                }
            }
        }
    }
}
