import QtQuick
import QtQuick.Templates as T
import StudioTheme as StudioTheme

T.TextField {
    id: search

    property bool empty: search.text === ""

    horizontalAlignment: Qt.AlignLeft
    verticalAlignment: Qt.AlignVCenter
    leftPadding: 30
    rightPadding: 30
    placeholderText: qsTr("Search")

    font.pixelSize: StudioTheme.Values.baseFontSize

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 30
        color: "#161616"
    }

    Item {
        width: 20
        height: 20
        anchors.left: parent.left
        anchors.leftMargin: 5
        anchors.verticalCenter: parent.verticalCenter

        Text {
            id: searchIcon
            font.family: StudioTheme.Constants.iconFont.family
            font.pixelSize: StudioTheme.Values.baseIconFontSize
            color: search.text === "" ? search.palette.placeholderText : "white" // TODO colors
            text: StudioTheme.Constants.search_small
            anchors.centerIn: parent
        }
    }

    Item {
        width: 30
        height: 30
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter

        Text {
            id: closeIcon
            font.family: StudioTheme.Constants.iconFont.family
            font.pixelSize: StudioTheme.Values.baseIconFontSize
            color: search.text === "" ? search.palette.placeholderText : "white" // TODO colors
            text: StudioTheme.Constants.close_small
            anchors.centerIn: parent
        }

        MouseArea {
            anchors.fill: parent
            onClicked: function() { search.clear() }
        }
    }
}
