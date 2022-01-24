import QtQuick
import QtQuick.Controls
import Loginui1

Rectangle {
    id: rectangle
    width: Constants.width
    height: Constants.height

    color: Constants.backgroundColor
    state: "login"

    Image {
        id: adventurePage
        anchors.fill: parent
        source: "images/adventurePage.jpg"
        fillMode: Image.PreserveAspectFit
    }

    Image {
        id: qt_logo_green_128x128px
        anchors.top: parent.top
        source: "images/qt_logo_green_128x128px.png"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 40
        fillMode: Image.PreserveAspectFit
    }
    Text {
        id: tagLine
        color: "#ffffff"
        text: qsTr("Are you ready to explore?")
        anchors.top: qt_logo_green_128x128px.bottom
        font.pixelSize: 50
        anchors.topMargin: 40
        anchors.horizontalCenter: parent.horizontalCenter
        font.family: "Titillium Web ExtraLight"
        anchors.horizontalCenterOffset: 0
    }

    Column {
        id: fields
        anchors.top: tagLine.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 170
        spacing: 20

        EntryField {
            id: username
            text: qsTr("Username or Email")
        }

        EntryField {
            id: password
            text: qsTr("Password")
        }

        EntryField {
            id: repeatPassword
            text: qsTr("Repeat Password")
        }
    }

    Column {
        id: buttons
        y: 944
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 50
        spacing: 20

        PushButton {
            id: login
            text: qsTr("Continue")
        }

        PushButton {
            id: createAccount
            text: qsTr("Create Account")

            Connections {
                target: createAccount
                onClicked: rectangle.state = "createAccount"
            }
        }
    }
    states: [
        State {
            name: "login"

            PropertyChanges {
                target: repeatPassword
                visible: false
            }
        },
        State {
            name: "createAccount"

            PropertyChanges {
                target: createAccount
                visible: false
            }
        }
    ]
}

/*##^##
Designer {
    D{i:0;formeditorZoom:0.5}D{i:1}D{i:2}D{i:3}D{i:5}D{i:6}D{i:7}D{i:4}D{i:9}D{i:11}D{i:10}
D{i:8}
}
##^##*/

