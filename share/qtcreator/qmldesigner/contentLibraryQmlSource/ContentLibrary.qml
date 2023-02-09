// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuickDesignerTheme
import HelperWidgets as HelperWidgets
import StudioControls as StudioControls
import StudioTheme as StudioTheme

Item {
    id: root

    property int currIndex: 0

    // Called also from C++ to close context menu on focus out
    function closeContextMenu()
    {
        materialsView.closeContextMenu()
        texturesView.closeContextMenu()
        environmentsView.closeContextMenu()
    }

    // Called from C++
    function clearSearchFilter()
    {
        searchBox.clear();
    }

    Column {
        id: col
        //y: 5 // align to top
        spacing: 5

//        StudioControls.SearchBox {
//            id: searchBox

//            width: root.width
//            enabled: {
//                if (tabBar.currIndex == 0) { // Materials tab
//                    materialsModel.matBundleExists
//                            && rootView.hasMaterialLibrary
//                            && materialsModel.hasRequiredQuick3DImport
//                } else { // Textures / Environments tabs
//                    texturesModel.texBundleExists
//                }
//            }

//            onSearchChanged: (searchText) => {
//                rootView.handleSearchFilterChanged(searchText)

//                // make sure categories with matches are expanded
//                materialsView.expandVisibleSections()
//                texturesView.expandVisibleSections()
//                environmentsView.expandVisibleSections()
//            }
//        }

        Rectangle {
            width: parent.width
            height: StudioTheme.Values.doubleToolbarHeight
            color: StudioTheme.Values.themeToolbarBackground

            Column {
                anchors.fill: parent
                padding: 6
                spacing: 12

                    StudioControls.SearchBox {
                        id: searchBox
                        width: parent.width - (parent.padding * 2)
                        style: StudioTheme.Values.searchControlStyle
                    }

                    Row {
                        id: searchRow
                        width: parent.width - (parent.padding * 2)
                        height: StudioTheme.Values.toolbarHeight
                        leftPadding: 6
                        rightPadding: 6
                        spacing: 6

                    HelperWidgets.AbstractButton {
                        id: materialLibrary
                        style: StudioTheme.Values.viewBarButtonStyle
                        buttonIcon: StudioTheme.Constants.material_medium
                        tooltip: qsTr("Material Library.")
                        checkable: true
                        checked: true
                        checkedInverted: true //todo --- swap inverted and make this default?
                        autoExclusive: true
                    }

                    Text {
                        height: StudioTheme.Values.statusbarButtonStyle.controlSize.height
                        color: StudioTheme.Values.themeTextColor
                        text: qsTr("Materials")
                        font.pixelSize: StudioTheme.Values.baseFontSize
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    HelperWidgets.AbstractButton {
                        id: texttureLibrary
                        style: StudioTheme.Values.viewBarButtonStyle
                        buttonIcon: StudioTheme.Constants.textures_medium
                        tooltip: qsTr("Texture Library.")
                        checkable: true
                        checkedInverted: true
                        autoExclusive: true
                    }

                    Text {
                        height: StudioTheme.Values.statusbarButtonStyle.controlSize.height
                        color: StudioTheme.Values.themeTextColor
                        text: qsTr("Textures")
                        font.pixelSize: StudioTheme.Values.baseFontSize
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    HelperWidgets.AbstractButton {
                        id: environmentLibrary
                        style: StudioTheme.Values.viewBarButtonStyle
                        buttonIcon: StudioTheme.Constants.languageList_medium // icon should be moved to general and given general name.
                        tooltip: qsTr("Environment Library.")
                        checkable: true
                        checkedInverted: true
                        autoExclusive: true
                    }

                    Text {
                        height: StudioTheme.Values.statusbarButtonStyle.controlSize.height
                        color: StudioTheme.Values.themeTextColor
                        text: qsTr("Environments")
                        font.pixelSize: StudioTheme.Values.baseFontSize
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }


        UnimportBundleMaterialDialog {
            id: confirmUnimportDialog
        }

//        ContentLibraryTabBar {
//            id: tabBar
//             // TODO: update icons
//            tabsModel: [{name: qsTr("Materials"),    icon: StudioTheme.Constants.gradient},
//                        {name: qsTr("Textures"),     icon: StudioTheme.Constants.materialPreviewEnvironment},
//                        {name: qsTr("Environments"), icon: StudioTheme.Constants.translationSelectLanguages}]
//        }

        StackLayout {
            width: root.width
            height: root.height - y
            currentIndex: tabBar.currIndex

            ContentLibraryMaterialsView {
                id: materialsView

                width: root.width

                searchBox: searchBox

                onUnimport: (bundleMat) => {
                    confirmUnimportDialog.targetBundleMaterial = bundleMat
                    confirmUnimportDialog.open()
                }
            }

            ContentLibraryTexturesView {
                id: texturesView

                width: root.width
                model: texturesModel

                searchBox: searchBox
            }

            ContentLibraryTexturesView {
                id: environmentsView

                width: root.width
                model: environmentsModel

                searchBox: searchBox
            }
        }
    }
}
