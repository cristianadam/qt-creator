// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HelperWidgets as HelperWidgets
import StudioTheme as StudioTheme

Rectangle {
    id: itemPane

    width: 320
    height: 400
    color: StudioTheme.Values.themePanelBackground

    Component.onCompleted: Controller.mainScrollView = mainScrollView

    default property alias content: mainColumn.children
    property alias scrollView: mainScrollView

    // Called from C++ to close context menu on focus out
    function closeContextMenu() {
        Controller.closeContextMenu()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: forceActiveFocus()
    }

    Rectangle {
        id: stateSection
        width: itemPane.width
        height: StudioTheme.Values.height + StudioTheme.Values.controlGap * 2
        color: Theme.qmlDesignerBackgroundColorDarkAlternate()
        z: isBaseState ? -1: 10
        SectionLayout {
            y: StudioTheme.Values.controlGap
            x: StudioTheme.Values.controlGap
            PropertyLabel {
                text: qsTr("Current State")
                tooltip: tooltipItem.tooltip
            }

            SecondColumnLayout {

                Spacer { implicitWidth: StudioTheme.Values.actionIndicatorWidth }

                RoundedPanel {
                    implicitWidth: StudioTheme.Values.singleControlColumnWidth
                    height: StudioTheme.Values.height

                    HelperWidgets.Label {
                        anchors.fill: parent
                        anchors.leftMargin: StudioTheme.Values.inputHorizontalPadding
                        anchors.topMargin: StudioTheme.Values.typeLabelVerticalShift
                        text: stateName
                        color: StudioTheme.Values.themeInteraction
                    }

                    ToolTipArea {
                        id: tooltipItem
                        anchors.fill: parent
                        tooltip: qsTr("The current state of the States View.")
                    }

                }

                ExpandingSpacer {}
            }
        }
    }

    HelperWidgets.ScrollView {
        id: mainScrollView
        //clip: true
        anchors.fill: parent
        anchors.topMargin: isBaseState ? 0 : stateSection.height

        interactive: !Controller.contextMenuOpened

        Column {
            id: mainColumn
            y: -1
            width: itemPane.width

            onWidthChanged: StudioTheme.Values.responsiveResize(itemPane.width)
            Component.onCompleted: StudioTheme.Values.responsiveResize(itemPane.width)
        }
    }
}
