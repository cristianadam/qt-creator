// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0
import QtQuick 2.15
import QtQuick.Controls 2.15

import StudioControls
import StudioTheme 1.0 as StudioTheme

import ToolBar 1.0

Rectangle {

    id: toolbarContainer
    color: "#2d2d2d"
    border.color: "#00000000"

    height: 56
    width: 2024

    ToolBarBackend {
        id: backend
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: "black"
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 2
        color: "black"
    }

    Item {
        id: topToolbarOtherMode
        anchors.fill: parent
        visible: !backend.isInDesignMode

        ToolbarButton {
            id: homeOther
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            textText: StudioTheme.Constants.topToolbar_designMode
            anchors.leftMargin: 10
            onClicked: backend.triggerModeChange()

            enabled: backend.isDesignModeEnabled
            tooltip: qsTr("Switch to Design Mode")
        }
    }

    Item {
        id: topToolbar
        anchors.fill: parent
        visible: backend.isInDesignMode

        SuggestedButton {
            id: livePreviewButton
            height: 36
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: home1.right
            anchors.leftMargin: 10
            onIsCheckedChanged: livePreview.trigger()
            ActionSubscriber {
                id: livePreview
                actionId: "LivePreview"
            }
        }

        TopLevelComboBoxLarge {
            id: currentFile
            width: 320
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: livePreviewButton.right
            anchors.leftMargin: 10
            model: backend.documentModel
            currentIndex: backend.currentDocumentIndex
            onActivated: {
                backend.openFileByIndex(index)
            }
        }

        ToolbarButton {
            id: backButton
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: currentFile.right
            anchors.leftMargin: 10
            enabled: backend.canGoBack
            onClicked: backend.goBackward()
            tooltip: qsTr("Go Back")
            textText: StudioTheme.Constants.topToolbar_navFile
            textRotation: 0
        }

        ToolbarButton {
            id: forwardButton
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: backButton.right
            textRotation: 180
            textText: StudioTheme.Constants.topToolbar_navFile
            anchors.leftMargin: 10
            enabled: backend.canGoForward
            onClicked: backend.goForward()
            tooltip: qsTr("Go Forward")
        }

        ToolbarButton {
            id: closeButton
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: forwardButton.right
            textText: StudioTheme.Constants.topToolbar_closeFile
            anchors.leftMargin: 10
            onClicked: backend.closeCurrentDocument()
            tooltip: qsTr("Close")
        }

        SuggestedButton {
            id: shareButton
            x: 1816
            height: 36
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            isChecked: false
            labelText: "Share"
            anchors.bottomMargin: 17
            anchors.rightMargin: 8
            anchors.topMargin: 17
            onClicked: backend.shareApplicationOnline()
        }

        ToolbarButton {
            id: annotations
            width: 36
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: shareButton.left
            anchors.rightMargin: 10
            textText: StudioTheme.Constants.topToolbar_annotations

            onClicked: backend.editGlobalAnnoation()
            tooltip: qsTr("Edit Annotations")
        }

        TopLevelComboBoxLarge {
            id: views
            width: 210
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: annotations.left
            anchors.rightMargin: 10
            model: backend.workspaces
            onCurrentTextChanged: backend.setCurrentWorkspace(views.currentText)
        }

        ToolbarButton {
            id: enterComponent
            width: 36
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: views.left
            textText: StudioTheme.Constants.topToolbar_enterComponent
            anchors.rightMargin: 10

            onClicked: goIntoComponentBackend.trigger()
            enabled: goIntoComponentBackend.available
            tooltip: goIntoComponentBackend.tooltip

            ActionSubscriber {
                id: goIntoComponentBackend
                actionId: "GoIntoComponent"
            }
        }

        ToolbarButton {
            id: createComponent
            width: 36
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: enterComponent.left
            textText: StudioTheme.Constants.topToolbar_makeComponent
            anchors.rightMargin: 10
            onClicked: moveToComponentBackend.trigger()
            enabled: moveToComponentBackend.available
            tooltip: moveToComponentBackend.tooltip

            ActionSubscriber {
                id: moveToComponentBackend
                actionId: "MoveToComponent"
            }
        }

        CrumbleBar {
            id: flickable

            height: 35
            anchors.left: closeButton.right
            anchors.right: createComponent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 10

            onClicked: crumbleBarModel.onCrumblePathElementClicked(index)

            model: CrumbleBarModel {
                id: crumbleBarModel
            }
        }

        ToolbarButton {
            id: home
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            textText: StudioTheme.Constants.topToolbar_home
            anchors.leftMargin: 10
            onClicked: {
                backend.triggerModeChange()
            }
        }

        ToolbarButton {
            id: home1
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: home.right
            anchors.leftMargin: 10
            textText: StudioTheme.Constants.topToolbar_runProject
            color: "#649a5d"
            onClicked: backend.runProject()
        }
    }
}
