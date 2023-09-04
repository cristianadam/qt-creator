import QtQuick
import StudioTheme as StudioTheme

FocusScope {
    id: root

    required property int index
    required property string value
    required property int type

    function setCursorBegin() { textInput.cursorPosition = 0 }
    function setCursorEnd() { textInput.cursorPosition = textInput.text.length }

    function isEditable() { return root.type === ConditionListModel.Intermediate
                                   || root.type === ConditionListModel.Literal }

    function isIntermediate() { return root.type === ConditionListModel.Intermediate }
    function isLiteral() { return root.type === ConditionListModel.Literal }
    function isOperator() { return root.type === ConditionListModel.Operator }
    function isProperty() { return root.type === ConditionListModel.Variable }
    function isShadow() { return root.type === ConditionListModel.Shadow }

    signal remove()
    signal update(var value)
    signal submit()

    readonly property int margin: 4

    width: pill.visible ? textItem.contentWidth + icon.width + root.margin : textInput.width + 1
    height: 20

    onActiveFocusChanged: {
        if (root.activeFocus && root.isEditable())
            textInput.forceActiveFocus()
    }

    Keys.onPressed: function (event) {
        if (!pill.visible)
            return

        if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete)
            root.remove()
    }

    MouseArea {
        id: rootMouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.isEditable() ? Qt.IBeamCursor : Qt.ArrowCursor
        onClicked: root.forceActiveFocus()
    }

    Rectangle {
        id: pill
        anchors.fill: parent
        color: {
            if (type === ConditionListModel.Intermediate)
                return "indigo"
            if (type === ConditionListModel.Invalid)
                return "firebrick"
            if (type === ConditionListModel.Operator)
                return "navy"
            if (type === ConditionListModel.Literal)
                return "forestgreen"
            if (type === ConditionListModel.Variable)
                return "goldenrod"
            if (type === ConditionListModel.Shadow)
                return "fuchsia"
        }
        //root.isShadow() ? StudioTheme.Values.interaction : "black" // TODO colors
        border.color: "white" // TODO colors
        border.width: rootMouseArea.containsMouse || root.focus ? 1 : 0
        radius: 4
        visible: root.isOperator() || root.isProperty() || root.isShadow()

        Row {
            id: row
            anchors.left: parent.left
            anchors.leftMargin: root.margin
            anchors.verticalCenter: parent.verticalCenter

            Text {
                id: textItem
                font.pixelSize: 12
                color: root.isShadow() ? "black" : "white"
                text: root.value
                anchors.verticalCenter: parent.verticalCenter
            }

            Item {
                id: icon
                width: root.isShadow() ? root.margin : 20
                height: 20
                visible: !root.isShadow()

                Text {
                    id: test
                    font.family: StudioTheme.Constants.iconFont.family
                    font.pixelSize: StudioTheme.Values.baseIconFontSize
                    color: StudioTheme.Values.themeIconColor
                    text: StudioTheme.Constants.close_small
                    anchors.centerIn: parent
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    onClicked: root.remove()
                }
            }
        }
    }

    TextInput {
        id: textInput

        property bool dirty: false

        height: 20
        topPadding: 1
        font.pixelSize: StudioTheme.Values.baseFontSize
        color: rootMouseArea.containsMouse || textInput.activeFocus ? "white" : "gainsboro" // TODO colors
        text: root.value
        visible: root.isEditable()
        enabled: root.isEditable()

        validator: RegularExpressionValidator { regularExpression: /^\S.+/ }

        onEditingFinished: {
            if (textInput.dirty === true)
                root.update(textInput.text)

            textInput.dirty = false

            root.submit() // emit
        }

        onTextEdited: textInput.dirty = true

        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_Backspace) {
                console.log("BACKSPACE text input")
                if (textInput.text !== "")
                    return

                root.remove()
            }
        }
    }
}
