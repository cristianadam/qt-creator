// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

import QtQuick
import QtQuick.Templates 2.12 as T
import StudioTheme 1.0 as StudioTheme

T.ComboBox {
    id: control

    editable: false

    property bool hover: (comboBoxInput.hover || actionIndicator.hover
                          || window.visible) && myComboBox.enabled
    property bool edit: false
    property bool open: window.visible

    width: StudioTheme.Values.defaultControlWidth
    height: StudioTheme.Values.defaultControlHeight

    leftPadding: StudioTheme.Values.border
    rightPadding: popupIndicator.width + StudioTheme.Values.border
    font.pixelSize: StudioTheme.Values.myFontSize

    delegate: ItemDelegate {
        width: control.width
        contentItem: Text {
            text: modelData
            color: "#21be2b"
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        highlighted: control.highlightedIndex === index

        required property int index
        required property var modelData
    }

    indicator: CheckIndicator {
        id: popupIndicator
        myControl: control
        myPopup: thePopup
        x: contentItem.x + contentItem.width
        y: StudioTheme.Values.border
        width: StudioTheme.Values.checkIndicatorWidth - StudioTheme.Values.border
        height: StudioTheme.Values.checkIndicatorHeight - (StudioTheme.Values.border * 2)
    }

    contentItem: ComboBoxInput {
        id: comboBoxInput

        myControl: control
        text: control.editText
    }

    background: Rectangle {
        id: comboBoxBackground
        color: StudioTheme.Values.themeControlBackground

        border.color: StudioTheme.Values.themeControlOutline

        border.width: StudioTheme.Values.border
        width: control.width
        height: control.height
    }

    popup: T.Popup {
        id: thePopup
        width: 0
        height: 0
        onAboutToShow: {
            menuDelegate.parent = window.contentItem
            menuDelegate.visible = true

            window.show()
            window.requestActivate()
            window.x = control.mapToGlobal(0,0).x
            window.y = control.mapToGlobal(0,0).y + control.height
            menuDelegate.focus = true
        }
    }

    Window {
        id: window
        width: menuDelegate.width
        height: menuDelegate.height
        visible: false
        //flags: Qt.Popup | Qt.NoDropShadowWindowHint
        flags: Qt.FramelessWindowHint
        modality: Qt.NonModal
        onActiveChanged: {
            print("active chaged " + window.active)
            if (!window.active) {
                window.hide()
                thePopup.close()
            }
        }
        transientParent: control.Window.window
        color: Qt.transparent
    }

    property Menu menuDelegate: Menu {
        //x: 0
        y: 0
        overlap: 0
        id: textEditMenu
        width: control.width
        onActiveFocusChanged: {
            print("focus")
            print("active chaged 2" + window.active)
            if (!textEditMenu.activeFocus) {
                window.hide()
                thePopup.close()
            }
        }

        Repeater {
            model: control.model

            MenuItem {
                x: 0

                id: menuItem
                text: modelData
                enabled: textField.selectedText.length > 0
                onTriggered: {
                    control.currentIndex = index
                    window.hide()
                    textField.cut()
                }

                background: Rectangle {
                    implicitWidth: textLabel.implicitWidth + menuItem.labelSpacing
                                   + shortcutLabel.implicitWidth
                                   + menuItem.leftPadding + menuItem.rightPadding
                    implicitHeight: StudioTheme.Values.height
                    x: StudioTheme.Values.border
                    y: StudioTheme.Values.border
                    width: menuItem.menu.width - (StudioTheme.Values.border * 2)
                    height: menuItem.height - (StudioTheme.Values.border * 2)
                    color: menuItem.highlighted ? StudioTheme.Values.themeInteraction : "transparent"
                }

                contentItem: Item {
                    Text {
                        leftPadding: itemDelegateIconArea.width
                        id: textLabel
                        text: menuItem.text
                        font: control.font
                        color: menuItem.highlighted ? StudioTheme.Values.themeTextSelectedTextColor : StudioTheme.Values.themeTextColor
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Item {
                        id: itemDelegateIconArea
                        width: menuItem.height
                        height: menuItem.height

                        T.Label {
                            id: itemDelegateIcon
                            text: StudioTheme.Constants.tickIcon
                            color: textLabel.color
                            font.family: StudioTheme.Constants.iconFont.family
                            font.pixelSize: StudioTheme.Values.spinControlIconSizeMulti
                            visible: control.currentIndex === index ? true : false
                            anchors.fill: parent
                            renderType: Text.NativeRendering
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }

    states: [
        State {
            name: "default"
            when: control.enabled && !control.hover && !control.edit
                  && !control.open && !control.activeFocus
                  && !control.hasActiveDrag
            PropertyChanges {
                target: control
                wheelEnabled: false
            }
            PropertyChanges {
                target: comboBoxInput
                selectByMouse: false
            }
            PropertyChanges {
                target: comboBoxBackground
                color: StudioTheme.Values.themeControlBackground
            }
        },
        // This state is intended for ComboBoxes which aren't editable, but have focus e.g. via
        // tab focus. It is therefor possible to use the mouse wheel to scroll through the items.
        State {
            name: "focus"
            when: control.enabled && control.activeFocus && !control.editable
                  && !control.open
            PropertyChanges {
                target: myComboBox
                wheelEnabled: true
            }
            PropertyChanges {
                target: comboBoxInput
                focus: true
            }
        },
        State {
            name: "popup"
            when: control.enabled && control.open
            PropertyChanges {
                target: control
                wheelEnabled: true
            }
            PropertyChanges {
                target: comboBoxInput
                selectByMouse: false
                readOnly: true
            }
            PropertyChanges {
                target: comboBoxBackground
                color: StudioTheme.Values.themeControlBackgroundInteraction
                border.color: StudioTheme.Values.themeControlOutlineInteraction
            }
        },
        State {
            name: "disable"
            when: !control.enabled
            PropertyChanges {
                target: comboBoxBackground
                color: StudioTheme.Values.themeControlBackgroundDisabled
                border.color: StudioTheme.Values.themeControlOutlineDisabled
            }
        }
    ]
}
