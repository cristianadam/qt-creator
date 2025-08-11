// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick
import QtQuick.Layouts
import StudioControls as StudioControls
import StudioTheme as StudioTheme
import ModelModules as ModelModules
import HelperWidgets as HelperWidgets

Rectangle {
    id: root

    property var rootEditor: shaderEditor

    color: StudioTheme.Values.themeToolbarBackground

    ColumnLayout {
        anchors.fill: parent

        RowLayout {
            Layout.topMargin: StudioTheme.Values.toolbarVerticalMargin
            Layout.leftMargin: StudioTheme.Values.toolbarHorizontalMargin
            Layout.rightMargin: StudioTheme.Values.toolbarHorizontalMargin

            StudioControls.TopLevelComboBox {
                id: nodesComboBox

                style: StudioTheme.Values.toolbarStyle
                Layout.preferredWidth: nodeNamesWidthCalculator.maxWidth
                                       + nodesComboBox.indicator.width
                                       + 20
                Layout.alignment: Qt.AlignVCenter
                model: editableCompositionsModel
                textRole: "display"

                Binding on currentIndex {
                    value: editableCompositionsModel.selectedIndex
                }

                onActivated: (idx) => {
                    shaderEditor.switchToNodeIndex(editableCompositionsModel.sourceIndex(idx))
                }

                ModelModules.ListModelWidthCalculator {
                    id: nodeNamesWidthCalculator

                    model: nodesComboBox.model
                    font: nodesComboBox.font
                    textRole: nodesComboBox.textRole
                }
            }

            Item { // Spacer
                Layout.preferredHeight: 1
                Layout.fillWidth: true
            }

            ColumnChooser {
                table: uniformsView.tableView
                text: qsTr("Columns")
                style: StudioTheme.Values.viewBarControlStyle
                Layout.alignment: Qt.AlignVCenter
            }

            HelperWidgets.AbstractButton {
                id: openHelpButton

                objectName: "btnEffectComposerHelp"
                style: StudioTheme.Values.viewBarButtonStyle
                buttonIcon: StudioTheme.Constants.help
                tooltip: qsTr("Open Effect Composer Help.")

                onClicked: Qt.openUrlExternally("https://doc.qt.io/qtdesignstudio/qtquick-effect-composer-view.html")
            }
        }

        CodeEditorUniformsView {
            id: uniformsView

            Layout.fillWidth: true
            Layout.fillHeight: true
            model: uniformsTableModel
        }
    }
}
