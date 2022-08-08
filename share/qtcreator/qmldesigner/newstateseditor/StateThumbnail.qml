import QtQuick
import QtQuick.Controls
//import StatesPrototype
import StudioTheme 1.0 as StudioTheme
import StudioControls 1.0 as StudioControls
import QtQuick.Layouts 6.0

Item {
    id: root
    clip: true

    anchors {
        horizontalCenter: parent.horizontalCenter
        verticalCenter: parent.verticalCenter
    }

    property alias thumbnailImageSource: thumbnailImage.source
    property alias stateName: stateNameField.text
    property alias whenCondition: whenCondition.text

    property alias defaultChecked: defaultButton.checked
    property alias menuChecked: menuButton.checked
    property bool baseState: false
    property bool isSmall: root.width <= Constants.thumbnailBreak
    property bool showPropertyChanges: stateMenu.changes
    property bool isChecked: false

    property bool hasExtend: false
    property string extendString: ""

    property int visualIndex: 0

    property int internalNodeId

    signal focusSignal()
    signal defaultClicked()
    signal clone()
    signal extend()
    signal remove()
    signal stateNameFinished()
    signal whenConditionFinished()

    signal grabbing()
    signal letGo()

    property alias dragActive: dragHandler.active

    onIsSmallChanged: {
        if (root.isSmall) {
            buttonGrid.rows = 2
            buttonGrid.columns = 1
        } else {
            buttonGrid.columns = 2
            buttonGrid.rows = 1
        }
    }

    DragHandler {
        id: dragHandler
        enabled: !root.baseState
        onGrabChanged: function(transition, point) {
            console.log("GRAB", transition, point)

            if (transition === PointerDevice.GrabPassive
                    || transition === PointerDevice.GrabExclusive)
                root.grabbing()

            if (transition === PointerDevice.UngrabPassive
                    || transition === PointerDevice.CancelGrabPassive
                    || transition === PointerDevice.UngrabExclusive
                    || transition === PointerDevice.CancelGrabExclusive)
                root.letGo()
        }
    }

    onDragActiveChanged: {
        if (root.dragActive)
            Drag.start()
        else
            Drag.drop()
    }

    Drag.active: dragHandler.active
    Drag.source: root
    Drag.hotSpot.x: root.width * 0.5
    Drag.hotSpot.y: root.height * 0.5

    Rectangle {
        id: stateBackground
        color: StudioTheme.Values.themeControlBackground
        border.color: StudioTheme.Values.themeInteraction
        border.width: root.isChecked ? 4 : 0
        anchors.fill: parent

        readonly property int controlHeight: 25
        readonly property int thumbPadding: 10
        readonly property int thumbSpacing: 7

        property int innerWidth: root.width - 2 * stateBackground.thumbPadding
        property int innerHeight: root.height - 2 * stateBackground.thumbPadding
                                  - 2 * stateBackground.thumbSpacing - 2 * stateBackground.controlHeight

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            enabled: true
            hoverEnabled: true
            propagateComposedEvents: true
            onClicked: root.focusSignal()
        }

        Column {
            padding: stateBackground.thumbPadding
            spacing: stateBackground.thumbSpacing

            Grid {
                id: buttonGrid
                columns: 2
                rows: 1
                spacing: stateBackground.thumbSpacing

                StudioControls.AbstractButton {
                    id: defaultButton
                    width: 50
                    height: stateBackground.controlHeight
                    checkedInverted: true
                    buttonIcon: qsTr("Default")
                    iconFont: StudioTheme.Constants.font
                    onClicked: {
                        root.defaultClicked()
                        root.focusSignal()
                    }
                }

                StudioControls.TextField {
                    id: stateNameField

                    property string previousText

                    // This is the width for the "big" state
                    property int bigWidth: stateBackground.innerWidth - 2 * stateBackground.thumbSpacing
                                           - defaultButton.width - menuButton.width

                    width: root.isSmall ? stateBackground.innerWidth : stateNameField.bigWidth
                    height: stateBackground.controlHeight
                    actionIndicatorVisible: false
                    translationIndicatorVisible: false
                    placeholderText: qsTr("State Name")
                    visible: !root.baseState

                    onActiveFocusChanged: {
                        if (stateNameField.activeFocus)
                             root.focusSignal()
                    }

                    onEditingFinished: {
                        if (stateNameField.previousText === stateNameField.text)
                            return

                        stateNameField.previousText = stateNameField.text
                        root.stateNameFinished()
                    }

                    Component.onCompleted: stateNameField.previousText = stateNameField.text
                }

                Text {
                    id: baseStateLabel
                    width: root.isSmall ? stateBackground.innerWidth : stateNameField.bigWidth
                    height: stateBackground.controlHeight
                    visible: root.baseState
                    color: StudioTheme.Values.themeTextColor
                    text: qsTr("Base State")
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 5
                    elide: Text.ElideRight
                }
            }

            Item {
                visible: !root.isSmall && !root.showPropertyChanges
                width: stateBackground.innerWidth
                height: stateBackground.innerHeight

                Image {
                    anchors.fill: stateImageBackground
                    source: "images/checkers.png"
                    fillMode: Image.Tile
                }

                Rectangle {
                    id: stateImageBackground
                    anchors.centerIn: parent
                    width: Math.round(thumbnailImage.paintedWidth) + 2 * StudioTheme.Values.border
                    height: Math.round(thumbnailImage.paintedHeight) + 2 * StudioTheme.Values.border
                    color: "transparent"
                    border.width: StudioTheme.Values.border
                    border.color: StudioTheme.Values.themeStatePreviewOutline
                }

                Image {
                    id: thumbnailImage
                    anchors.centerIn: parent
                    anchors.fill: parent
                    source: "images/622cce7514a72.jpg"
                    sourceSize.width: 230
                    sourceSize.height: 160
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                }
            }

            ScrollView {
                id: scrollView

                visible: !root.isSmall && root.showPropertyChanges
                width: stateBackground.innerWidth
                height: stateBackground.innerHeight

                Column {
                    id: column

                    // Grid sizes
                    property int gridSpacing: 20
                    property int gridRowSpacing: 5
                    property int gridPadding: 5
                    property int col1Width: 60 // labels
                    property int col2Width: stateBackground.innerWidth - column.gridSpacing
                                            - 2 * column.gridPadding - column.col1Width // controls


                    width: stateBackground.innerWidth
                    spacing: stateBackground.thumbSpacing

                    PropertyChangesModel {
                        id: propertyChangesModel
                        modelNodeBackendProperty: statesEditorModel.stateModelNode(root.internalNodeId)
                    }

                    Repeater {
                        model: propertyChangesModel

                        delegate: Rectangle {
                            id: propertyChanges

                            required property string target
                            required property bool explicit
                            required property bool restoreEntryValues
                            required property var propertyModelNode

                            width: column.width
                            height: propertyColumn.height
                            color: "black"

                            PropertyModel {
                                id: propertyModel
                                modelNodeBackendProperty: propertyChanges.propertyModelNode
                            }

                            Column {
                                id: propertyColumn

                                Repeater {
                                    model: propertyModel

                                    delegate: Row {
                                        id: myProperty

                                        required property string name
                                        required property var value
                                        required property string type

                                        spacing: 10

                                        Text {
                                            text: myProperty.name
                                            color: "white"
                                        }

                                        Text {
                                            text: myProperty.value
                                            color: "white"
                                        }

                                        Text {
                                            text: myProperty.type
                                            color: "white"
                                        }
                                    }
                                }
                            }
                        }
                    }

/*
                    Rectangle {
                        color: "green"
                        width: stateBackground.innerWidth
                        height: childrenRect.height

                        Grid {
                            width: stateBackground.innerWidth
                            columns: 2
                            padding: column.gridPadding
                            spacing: column.gridSpacing
                            rowSpacing: column.gridRowSpacing
                            verticalItemAlignment: Grid.AlignVCenter

                            Text {
                                color: StudioTheme.Values.themeTextColor
                                text: qsTr("Target")
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                                width: column.col1Width
                            }

                            StudioControls.ComboBox{
                                width: column.col2Width
                                actionIndicatorVisible: false
                            }

                            Item {
                                width: column.col1Width
                                height: 10
                            }

                            Flow {
                                width: column.col2Width

                                Text {
                                    color: StudioTheme.Values.themeTextColor
                                    text: qsTr("restore values")
                                    font.pixelSize: 12
                                }

                                StudioControls.Switch {
                                    actionIndicatorVisible: false
                                }

                                Text {
                                    color: StudioTheme.Values.themeTextColor
                                    text: qsTr("explicit")
                                    font.pixelSize: 12
                                    horizontalAlignment: Text.AlignRight
                                }

                                StudioControls.Switch {
                                    actionIndicatorVisible: false
                                }
                            }

                            Text {
                                color: StudioTheme.Values.themeTextColor
                                text: qsTr("color")
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                                width: column.col1Width
                            }

                            StudioControls.SpinBox {
                                width: column.col2Width
                                actionIndicatorVisible: false
                            }
                        }
                    }

                    Rectangle {
                        color: "red"
                        width: stateBackground.innerWidth
                        height: childrenRect.height

                        Grid {
                            width: stateBackground.innerWidth
                            columns: 2
                            padding: column.gridPadding
                            spacing: column.gridSpacing
                            rowSpacing: column.gridRowSpacing
                            verticalItemAlignment: Grid.AlignVCenter

                            Text {
                                color: StudioTheme.Values.themeTextColor
                                text: qsTr("Target")
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                                width: column.col1Width
                            }

                            StudioControls.ComboBox{
                                width: column.col2Width
                                actionIndicatorVisible: false
                            }

                            Item {
                                width: column.col1Width
                                height: 10
                            }

                            Flow {
                                width: column.col2Width

                                Text {
                                    color: StudioTheme.Values.themeTextColor
                                    text: qsTr("restore values")
                                    font.pixelSize: 12
                                }

                                StudioControls.Switch {
                                    actionIndicatorVisible: false
                                }

                                Text {
                                    color: StudioTheme.Values.themeTextColor
                                    text: qsTr("explicit")
                                    font.pixelSize: 12
                                    horizontalAlignment: Text.AlignRight
                                }

                                StudioControls.Switch {
                                    actionIndicatorVisible: false
                                }
                            }

                            Text {
                                color: StudioTheme.Values.themeTextColor
                                text: qsTr("Property #1")
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                                width: column.col1Width
                            }

                            StudioControls.SpinBox {
                                width: column.col2Width
                                actionIndicatorVisible: false
                            }

                            Text {
                                color: StudioTheme.Values.themeTextColor
                                text: qsTr("Property #2")
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                                width: column.col1Width
                            }

                            StudioControls.ComboBox{
                                width: column.col2Width
                                actionIndicatorVisible: false
                            }

                            Text {
                                color: StudioTheme.Values.themeTextColor
                                text: qsTr("Property #3")
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                                width: column.col1Width
                            }

                            StudioControls.SpinBox {
                                width: column.col2Width
                                actionIndicatorVisible: false
                            }
                        }
                    }
*/
                }
            }

            StudioControls.TextField {
                id: whenCondition

                property string previousCondition

                width: stateBackground.innerWidth
                height: stateBackground.controlHeight

                visible: !root.baseState
                actionIndicatorVisible: false
                placeholderText: qsTr("When Condition")

                onActiveFocusChanged: {
                    if (whenCondition.activeFocus)
                         root.focusSignal()
                }

                onEditingFinished: {
                    if (whenCondition.previousCondition === whenCondition.text)
                        return

                    whenCondition.previousCondition = whenCondition.text
                    root.whenConditionFinished()()
                }

                Component.onCompleted: whenCondition.previousCondition = whenCondition.text
            }
        }

        MenuButton {
            id: menuButton
            anchors.top: parent.top
            anchors.topMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 10

            visible: !root.baseState

            checked: stateMenu.opened

            onPressed: {
                if (!stateMenu.opened)
                    stateMenu.popup()

                root.focusSignal()
            }
        }

        Text {
            id: stateDegugLabel
            visible: true
            color: "#e6cbcb"
            text: root.state
            font.pixelSize: 16
            anchors.top: defaultButtonHoverDebugLabel.bottom
            anchors.topMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            id: defaultButtonHoverDebugLabel
            visible: false
            color: "#e6cbcb"
            text: defaultButton.hovered
            font.pixelSize: 16
            anchors.verticalCenter: parent.verticalCenter
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    StateMenu {
        id: stateMenu
        x: 56
        showChangesVisible: !root.baseState
        deleteStateVisible: !root.baseState
        extendVisible: !root.hasExtend
        cloneVisible: true
        showChangesText: stateMenu.changes ? qsTr("Show Thumbnail") : qsTr("Show Changes")

        onClone: root.clone()
        onExtend: root.extend()
        onRemove: root.remove()
    }

    property bool anyControlHovered: defaultButton.hovered || menuButton.hovered
                                     || scrollView.hovered
                                     || stateNameField.hover || whenCondition.hover

    states: [
        State {
            name: "default"
            when: !mouseArea.containsMouse
                  && !root.anyControlHovered
                  && !dragHandler.active
                  && !root.baseState

              PropertyChanges {
                  target: stateBackground
                  color: StudioTheme.Values.themeControlBackground
              }
        },
        State {
            name: "hover"
            when: (mouseArea.containsMouse || root.anyControlHovered)
                  && !dragHandler.active

            PropertyChanges {
                target: stateBackground
                color: StudioTheme.Values.themeControlBackgroundHover
            }
        },
        State {
            name: "baseState"
            when: root.baseState
                  && !mouseArea.containsMouse
                  && !root.anyControlHovered
                  && !dragHandler.active

            PropertyChanges {
                target: stateBackground
                color: "#5c5c5c"
            }
        },
        State {
            name: "drag"
            when: dragHandler.active

            AnchorChanges {
                target: root
                anchors.horizontalCenter: undefined
                anchors.verticalCenter: undefined
            }
        }
    ]
}

/*##^##
Designer {
    D{i:0;height:250;width:250}
}
##^##*/
