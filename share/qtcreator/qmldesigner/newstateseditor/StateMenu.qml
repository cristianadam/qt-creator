import QtQuick
import QtQuick.Controls
//import StatesPrototype
import StudioTheme as StudioTheme
import StudioControls as StudioControls
import QtQuick.Layouts

StudioControls.Menu {
    id: root

    property alias showChangesIsBlocked: showChanges.enabled
    property alias showChangesText: showChanges.text

    property alias extendVisible: extend.visible
    property alias showChangesVisible: showChanges.visible
    property alias deleteStateVisible: deleteState.visible
    property alias cloneVisible: clone.visible

    property bool changes: false

    signal remove()

    closePolicy: Popup.CloseOnReleaseOutside | Popup.CloseOnEscape

    StudioControls.MenuItem {
        id: clone
        text: qsTr("Clone")
        height: clone.visible ? clone.implicitHeight : 0
    }

    StudioControls.MenuItem {
        id: deleteState
        text: qsTr("Delete")
        height: deleteState.visible ? deleteState.implicitHeight : 0
        onTriggered: root.remove()
    }

    StudioControls.MenuItem {
        id: showChanges
        text: qsTr("Show Changes")
        height: showChanges.visible ? showChanges.implicitHeight : 0
        onTriggered: root.changes = !root.changes
    }

    StudioControls.MenuItem {
        id: extend
        text: qsTr("Extend")
        height: extend.visible ? extend.implicitHeight : 0
    }

    StudioControls.MenuItem {
        id: annotate
        text: qsTr("Annotate")
        height: annotate.visible ? annotate.implicitHeight : 0
    }
}
