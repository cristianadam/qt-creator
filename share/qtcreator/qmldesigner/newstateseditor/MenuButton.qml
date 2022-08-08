import QtQuick
import QtQuick.Controls
//import StatesPrototype
import StudioTheme 1.0 as StudioTheme
import StudioControls 1.0 as StudioControls

Item {
    id: root
    width: 25
    height: 25

    property bool hovered: mouseArea.containsMouse
    property bool checked: false
    signal pressed()

    Rectangle {
        id: background
        color: "transparent"
        anchors.fill: parent
    }

    // Burger menu icon
    Column {
        id: menuIcon
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 3

        property color iconColor: StudioTheme.Values.themeTextColor

        Rectangle {
            id: rectangle
            width: 19
            height: 3
            color: menuIcon.iconColor
        }

        Rectangle {
            id: rectangle1
            width: 19
            height: 3
            color: menuIcon.iconColor
        }

        Rectangle {
            id: rectangle2
            width: 19
            height: 3
            color: menuIcon.iconColor
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true

        Connections {
            target: mouseArea
            function onPressed() { root.pressed() }
        }
    }

    states: [
        State {
            name: "default"
            when: !root.hovered && !root.checked

            PropertyChanges {
                target: background
                color: "transparent"
            }
            PropertyChanges {
                target: menuIcon
                iconColor: StudioTheme.Values.themeTextColor
            }
        },
        State {
            name: "hover"
            when: root.hovered && !root.checked

            PropertyChanges {
                target: background
                color: StudioTheme.Values.themeControlBackgroundHover
            }
            PropertyChanges {
                target: menuIcon
                iconColor: StudioTheme.Values.themeTextColor
            }
        },
        State {
            name: "checked"
            when: !root.hovered && root.checked

            PropertyChanges {
                target: menuIcon
                iconColor: StudioTheme.Values.themeInteraction
            }
        },
        State {
            name: "hoverChecked"
            when: root.hovered && root.checked

            PropertyChanges {
                target: background
                color: StudioTheme.Values.themeControlBackgroundHover
            }
            PropertyChanges {
                target: menuIcon
                iconColor: StudioTheme.Values.themeInteraction
            }
        }
    ]
}

/*##^##
Designer {
    D{i:0;height:25;width:25}
}
##^##*/
