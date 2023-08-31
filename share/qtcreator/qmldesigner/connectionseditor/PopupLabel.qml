import QtQuick
import QtQuick.Controls
import HelperWidgets as HelperWidgets
import StudioControls as StudioControls
import StudioTheme as StudioTheme

Text {
    width: root.columnWidth
    color: StudioTheme.Values.themeTextColor
    font.pixelSize: StudioTheme.Values.myFontSize
    property alias tooltip: area.tooltip
    ToolTipArea {
        id: area
        anchors.fill: parent
        tooltip: qsTr("missing")
    }
}
