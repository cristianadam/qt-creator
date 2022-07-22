import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import HelperWidgets 2.0
import QtQuick.Templates 2.15 as T
import StudioControls 1.0 as StudioControls
import StudioTheme 1.0 as StudioTheme

Section {
    anchors.left: parent.left
    anchors.right: parent.right
    caption: qsTr("Dynamic Properties")


    property Component colorEditor: Component {
        id: colorEditor
        ColorEditor {
            id: colorEditorControl
            signal remove
            property string propertyType
            //backendValue: dummyBackendValue
            supportGradient: false
            spacer.visible: false

            Spacer { implicitWidth: StudioTheme.Values.twoControlColumnGap }

            IconIndicator {
                Layout.alignment: Qt.AlignLeft

                icon: StudioTheme.Constants.closeCross
                onClicked: colorEditorControl.remove()
            }
            ExpandingSpacer {}
        }
    }

    property Component intEditor: Component {
        id: intEditor
        SecondColumnLayout {
            id: layoutInt
            property var backendValue
            signal remove
            property string propertyType

            SpinBox {
                maximumValue: 9999999
                minimumValue: -9999999
                backendValue:  layoutInt.backendValue
                implicitWidth: StudioTheme.Values.twoControlColumnWidth
                               + StudioTheme.Values.actionIndicatorWidth
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            Item {
                height: 10
                implicitWidth: {
                    return StudioTheme.Values.twoControlColumnWidth
                            + StudioTheme.Values.actionIndicatorWidth
                }
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            IconIndicator {
                Layout.alignment: Qt.AlignLeft
                icon: StudioTheme.Constants.closeCross
                onClicked: layoutInt.remove()
            }

            ExpandingSpacer {}
        }
    }

    property Component realEditor: Component {
        id: realEditor
        SecondColumnLayout {
            id: layoutReal
            property var backendValue
            signal remove
            property string propertyType

            SpinBox {
                backendValue: layoutReal.backendValue
                minimumValue: -9999999
                maximumValue: 9999999
                decimals: 2
                stepSize: 0.1
                implicitWidth: StudioTheme.Values.twoControlColumnWidth
                               + StudioTheme.Values.actionIndicatorWidth
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            Item {
                height: 10
                implicitWidth: {
                    return StudioTheme.Values.twoControlColumnWidth
                            + StudioTheme.Values.actionIndicatorWidth
                }
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            IconIndicator {
                Layout.alignment: Qt.AlignLeft
                icon: StudioTheme.Constants.closeCross
                onClicked: layoutReal.remove()
            }

            ExpandingSpacer {}
        }
    }

    property Component stringEditor: Component {
        id: stringEditor
        SecondColumnLayout {
            id: layoutString
            property var backendValue
            signal remove
            property string propertyType

            LineEdit {
                backendValue:  layoutString.backendValue
                implicitWidth: StudioTheme.Values.twoControlColumnWidth
                               + StudioTheme.Values.actionIndicatorWidth
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            Item {
                height: 10
                implicitWidth: {
                    return StudioTheme.Values.twoControlColumnWidth
                            + StudioTheme.Values.actionIndicatorWidth
                }
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            IconIndicator {
                Layout.alignment: Qt.AlignLeft
                icon: StudioTheme.Constants.closeCross
                onClicked: layoutString.remove()
            }

            ExpandingSpacer {}
        }
    }

    property Component boolEditor: Component {
        id: boolEditor
        SecondColumnLayout {
            id: layoutBool
            property var backendValue
            signal remove
            property string propertyType

            CheckBox {
                implicitWidth: StudioTheme.Values.twoControlColumnWidth
                               + StudioTheme.Values.actionIndicatorWidth
                text: layoutBool.backendValue.value
                backendValue:  layoutBool.backendValue
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            Item {
                height: 10
                implicitWidth: {
                    return StudioTheme.Values.twoControlColumnWidth
                            + StudioTheme.Values.actionIndicatorWidth
                }
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            IconIndicator {
                Layout.alignment: Qt.AlignLeft
                icon: StudioTheme.Constants.closeCross
                onClicked: layoutBool.remove()
            }

            ExpandingSpacer {}
        }
    }


    property Component urlEditor: Component {
        id: urlEditor
        SecondColumnLayout {
            id: layoutUrl
            property var backendValue
            signal remove
            property string propertyType

            UrlChooser {

                backendValue:  layoutUrl.backendValue
                comboBox.implicitWidth: StudioTheme.Values.twoControlColumnWidth
                                        + StudioTheme.Values.actionIndicatorWidth
                spacer.implicitWidth: StudioTheme.Values.controlLabelGap
            }

            Item {
                height: 10
                implicitWidth: {
                    return StudioTheme.Values.twoControlColumnWidth
                            + StudioTheme.Values.actionIndicatorWidth
                }
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            IconIndicator {
                Layout.alignment: Qt.AlignLeft
                icon: StudioTheme.Constants.closeCross
                onClicked: layoutUrl.remove()
            }

            ExpandingSpacer {}
        }
    }

    property Component readonlyEditor: Component {
        id: readonlyEditor
        SecondColumnLayout {
            id: layoutReadonly
            property var backendValue
            signal remove
            property string propertyType

            PropertyLabel {
                tooltip: layoutReadonly.propertyType
                horizontalAlignment: Text.AlignHCenter
                text: layoutReadonly.propertyType

                width: StudioTheme.Values.twoControlColumnWidth
                       + StudioTheme.Values.actionIndicatorWidth
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            Item {
                height: 10
                implicitWidth: {
                    return StudioTheme.Values.twoControlColumnWidth
                            + StudioTheme.Values.actionIndicatorWidth
                }
            }

            Spacer {
                implicitWidth: StudioTheme.Values.twoControlColumnGap
            }

            IconIndicator {
                Layout.alignment: Qt.AlignLeft
                icon: StudioTheme.Constants.closeCross
                onClicked: layoutReadonly.remove()
            }

            ExpandingSpacer {}
        }
    }

    Column {
        width: parent.width
        spacing: StudioTheme.Values.sectionRowSpacing

        Repeater {
            id: repeater
            model: DynamicPropertiesModel {
                id: propertiesModel
            }


            property bool loadActive: true
            onCountChanged: {
                repeater.loadActive = false
                repeater.loadActive = true
            }

            SectionLayout {
                DynamicPropertyRow {
                    id: propertyRow
                    row: index
                }
                PropertyLabel {
                    text: propertyName
                    tooltip: propertyRow.backendValue.valueToString
                }

                Loader {
                    id: loader
                    asynchronous: true
                    active: repeater.loadActive
                    //active: false
                    width: loader.item ? loader.item.width : 0
                    height: loader.item ? loader.item.height : 0

                    sourceComponent:  {
                        if (propertyType == "color")
                            return stringEditor
                            //return colorEditor
                        if (propertyType == "int")
                            return intEditor
                        if (propertyType == "real")
                            return realEditor
                        if (propertyType == "string")
                            return stringEditor
                        if (propertyType == "bool")
                            return boolEditor
                        if (propertyType == "url")
                            return urlEditor

                        return readonlyEditor
                    }

                    onLoaded: {
                        print("loaded " + index + " " + propertyRow.backendValue.value)
                        loader.item.backendValue = propertyRow.backendValue
                        loader.item.propertyType = propertyType
                    }



                     Connections {
                         target: propertyRow
                         function onRowChanged() {
                             //loader.active = false
                             //loader.item.backendValue = propertyRow.backendValue
                             //loader.active = true
                         }
                     }

                    Connections {
                        target: loader.item
                        function onRemove() {
                            propertyRow.remove()
                        }
                    }

                }
            }
        }


        SectionLayout {
            PropertyLabel {
                text: ""
                tooltip: ""
            }

            SecondColumnLayout {
                Spacer { implicitWidth: StudioTheme.Values.actionIndicatorWidth }

                StudioControls.AbstractButton {

                    id: plusButton
                    buttonIcon: StudioTheme.Constants.plus
                    onClicked: {
                        cePopup.opened ? cePopup.close() : cePopup.open()
                        forceActiveFocus()
                    }
                }

                ExpandingSpacer {}
            }
        }

    }

    property T.Popup popup: T.Popup {
        id: cePopup



        onOpened: {
            cePopup.setPopupY()
            cePopup.setMainScrollViewHeight()
        }

        function setMainScrollViewHeight() {
            if (Controller.mainScrollView === null)
                return

            var mapped = plusButton.mapToItem(Controller.mainScrollView.contentItem, cePopup.x, cePopup.y)
            Controller.mainScrollView.temporaryHeight = mapped.y + cePopup.height
                    + StudioTheme.Values.colorEditorPopupMargin
        }

        function setPopupY() {
            if (Controller.mainScrollView === null)
                return

            var tmp = plusButton.mapToItem(Controller.mainScrollView.contentItem, plusButton.x, plusButton.y)
            print(tmp)
            cePopup.y = Math.max(-tmp.y + StudioTheme.Values.colorEditorPopupMargin,
                                 cePopup.__defaultY)

            textField.text = propertiesModel.newPropertyName
        }

        onClosed: Controller.mainScrollView.temporaryHeight = 0

        property real __defaultX: (Controller.mainScrollView.contentItem.width - StudioTheme.Values.colorEditorPopupWidth * 1.5) / 2 //- StudioTheme.Values.colorEditorPopupWidth * 0.5 + plusButton.width * 0.5

        property real __defaultY: - StudioTheme.Values.colorEditorPopupPadding
                                  - (StudioTheme.Values.colorEditorPopupSpacing * 2)
                                  - StudioTheme.Values.defaultControlHeight
                                  - StudioTheme.Values.colorEditorPopupLineHeight
                                  + plusButton.width * 0.5

        x: cePopup.__defaultX
        y: cePopup.__defaultY

        width: StudioTheme.Values.colorEditorPopupWidth * 2
        height: 160

        padding: StudioTheme.Values.border
        margins: 2 // If not defined margin will be -1

        closePolicy: T.Popup.CloseOnPressOutside | T.Popup.CloseOnPressOutsideParent

        contentItem: Item {
            id: content
            Column {
                spacing: StudioTheme.Values.sectionRowSpacing
                RowLayout {
                    width: content.width - 2
                    PropertyLabel {
                        text: "Add New Property"
                        Layout.preferredWidth: StudioTheme.Values.twoControlColumnWidth
                    }
                    IconIndicator {
                        id: closeIndicator
                        icon: StudioTheme.Constants.colorPopupClose
                        pixelSize: StudioTheme.Values.myIconFontSize * 1.4
                        onClicked: cePopup.close()
                        Layout.alignment: Qt.AlignRight
                    }
                }
                RowLayout {
                    PropertyLabel {
                        text: "Name"

                    }
                    StudioControls.TextField {
                        id: textField
                        actionIndicator.visible: false
                        translationIndicatorVisible: false
                        //text: propertiesModel.newPropertyName
                    }
                }
                RowLayout {
                    PropertyLabel {
                        text: "Type"
                    }
                    StudioControls.ComboBox {
                        id: comboBox
                        actionIndicator.visible: false
                        model: ["int", "real", "color", "string", "bool", "url", "variant", "alias"]
                    }
                }
                Item {
                    width: StudioTheme.Values.sectionRowSpacing
                    height: StudioTheme.Values.sectionRowSpacing
                }

                RowLayout {
                    width: content.width - 2



                    StudioControls.AbstractButton {

                        id: acceptButton

                        buttonIcon: qsTr("Add Property")
                        iconFont: StudioTheme.Constants.font
                        Layout.preferredWidth: StudioTheme.Values.twoControlColumnWidth

                        onClicked: {
                            propertiesModel.createProperty(textField.text, comboBox.currentText)
                            cePopup.close()
                        }
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

            }
        }
        background: Rectangle {
            color: StudioTheme.Values.themeControlBackground
            border.color: StudioTheme.Values.themeInteraction
            border.width: StudioTheme.Values.border
        }

        enter: Transition {}
        exit: Transition {}
    }
}
