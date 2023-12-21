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
import QtQuick.Layouts
import WelcomeScreen 1.0
import projectmodel 1.0
import DataModels 1.0
import UiTour
import StudioControls as StudioControls

Item {
    id: thumbnails
    width: 1500
    height: 800
    clip: true

    property alias stackLayoutCurrentIndex: gridStackLayout.currentIndex
    property var projectModel: Constants.projectModel

    Rectangle {
        id: thumbnailGridBack
        color: Constants.currentThumbnailGridBackground
        anchors.fill: parent

        HoverHandler { id: hoverHandler }

        StackLayout {
            id: gridStackLayout
            visible: !Constants.isListView
            anchors.fill: parent
            anchors.margins: Constants.gridMargins
            currentIndex: 0

            CustomGrid {
                id: recentGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                hover: hoverHandler.hovered
                model: thumbnails.projectModel
                delegate: ThumbnailDelegate {
                    id: delegate
                    type: ThumbnailDelegate.Type.RecentProject
                    hasPath: true
                    thumbnailPlaceholderSource: previewUrl
                    onClicked: projectModel.openProjectAt(index)
                    onRightClicked: {
                        removeMenuItem.index = index
                        contextMenu.popup(delegate)
                    }
                }

                Text {
                    text: qsTr("Create a new project using the \"<b>Create Project</b>\" or open an existing project using the \"<b>Open Project</b>\" option. ")
                    font.pixelSize: 18
                    color: Constants.currentGlobalText
                    anchors.centerIn: parent
                    width: recentGrid.width
                    horizontalAlignment: Text.AlignHCenter
                    leftPadding: 20
                    rightPadding: 20
                    wrapMode: Text.WordWrap
                    visible: projectModel.count === 0
                }

                StudioControls.Menu {
                    id: contextMenu

                    StudioControls.MenuItem {
                        id: removeMenuItem

                        property int index: -1

                        text: qsTr("Remove Project from Recent Projects")
                        onTriggered: projectModel.removeFromRecentProjects(removeMenuItem.index)
                    }

                    StudioControls.MenuItem {
                        text: qsTr("Clear Recent Project List")
                        onTriggered: projectModel.clearRecentProjects()
                    }
                }
            }

            CustomGrid {
                id: examplesGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                hover: hoverHandler.hovered
                model: ExamplesModel { id: examplesModel}
                delegate: ThumbnailDelegate {
                    type: ThumbnailDelegate.Type.Example
                    downloadable: showDownload
                    hasUpdate: showUpdate
                    downloadUrl: url
                    thumbnailPlaceholderSource: examplesModel.resolveUrl(thumbnail)
                    onClicked: projectModel.openExample(targetPath,
                                                        projectName,
                                                        qmlFileName,
                                                        explicitQmlproject)
                }
            }

            CustomGrid {
                id: tutorialsGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                hover: hoverHandler.hovered
                model: TutorialsModel { id: tutorialsModel}
                delegate: ThumbnailDelegate {
                    type: ThumbnailDelegate.Type.Tutorial
                    thumbnailPlaceholderSource: tutorialsModel.resolveUrl(thumbnail)
                    onClicked: Qt.openUrlExternally(url)
                }
            }

            CustomGrid {
                id: tourGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                hover: hoverHandler.hovered
                model: TourModel {}
                delegate: TourThumbnailDelegate {
                    id: thumbnailDelegate
                    visible: !slidePlayer.visible
                    enabled: !slidePlayer.visible

                    Connections {
                        target: thumbnailDelegate
                        onClicked: tourGrid.startTour(qmlFileName)
                    }
                }

                function startTour(url) {
                    slidePlayer.visible = true
                    slidePlayer.slideSource = Qt.resolvedUrl(url)
                }

                SlidePlayer {
                    id: slidePlayer
                    anchors.fill: parent
                    visible: false
                }

                TourDialogButton {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 16
                    anchors.topMargin: 16
                    visible: slidePlayer.visible

                    Connections {
                        target: closeButton
                        onButtonClicked: {
                            slidePlayer.visible = false
                            slidePlayer.loaderActive = false
                            slidePlayer.loaderActive = true
                        }
                    }
                }
            }
        }
    }
}
