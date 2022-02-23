/*
This is a UI file (.ui.qml) that is intended to be edited in Qt Design Studio only.
It is supposed to be strictly declarative and only uses a subset of QML. If you edit
this file manually, you might introduce QML code that is not supported by Qt Design Studio.
Check out https://doc.qt.io/qtcreator/creator-quick-ui-forms.html for details on .ui.qml files.
*/

import QtQuick %{QtQuickVersion}
import QtQuick.Controls %{QtQuickVersion}
import %{ImportModuleName} %{ImportModuleVersion}

Rectangle {
    id: rectangle
    width: Constants.width
    height: Constants.height

    color: Constants.backgroundColor

    Button {
        id: button
        text: qsTr("Press me")
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter

        Connections {
            target: button
            onClicked: rectangle.state = "clicked"
        }
    }

    Text {
        id: text1
        visible: false
        text: qsTr("clicked")
        anchors.top: button.bottom
        font.pixelSize: 12
        anchors.topMargin: 45
        anchors.horizontalCenter: parent.horizontalCenter
    }
    states: [
        State {
            name: "clicked"

            PropertyChanges {
                target: text1
                visible: true
            }
        }
    ]
}
