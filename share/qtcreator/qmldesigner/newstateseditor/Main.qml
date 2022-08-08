

/*
This is a UI file (.ui.qml) that is intended to be edited in Qt Design Studio only.
It is supposed to be strictly declarative and only uses a subset of QML. If you edit
this file manually, you might introduce QML code that is not supported by Qt Design Studio.
Check out https://doc.qt.io/qtcreator/creator-quick-ui-forms.html for details on .ui.qml files.
*/
import QtQuick
import QtQuick.Controls
import StatesEditor
import StudioTheme as StudioTheme

Rectangle {
    id: root

    signal createNewState
    signal deleteState(int internalNodeId)
    signal duplicateCurrentState

    enum Orientation {
        Landscape,
        Portrait
    }

    property int orientation: Main.Orientation.Landscape

    function isLandscape() {
        return root.orientation === Main.Orientation.Landscape
    }

    color: "#252525" // TODO

    onWidthChanged: root.responsiveResize(root.width, root.height)
    onHeightChanged: root.responsiveResize(root.width, root.height)

    Component.onCompleted: root.responsiveResize(root.width, root.height)

    // TODO better name!!!
    function numFit(overall, size, space) {
        let tmpNum = Math.floor(overall / size)
        let spaceLeft = overall - (tmpNum * size)
        return spaceLeft - (space * (tmpNum - 1)) >= 0 ? tmpNum : tmpNum - 1
    }

    function responsiveResize(width, height) {
        console.log("responsiveResize", width, height)

        var numStates = statesRepeater.count - 1 // Subtract base state
        var numRows = 0
        var numColumns = 0

        // Get window orientation (LANDSCAPE, PORTRAIT)
        if (width >= height) {
            root.orientation = Main.Orientation.Landscape
            outerGrid.columns = 3
            outerGrid.rows = 1

            baseStateWrapper.height = height
            root.scrollViewHeight = height
            addWrapper.height = height

            if (height > root.maxThumbSize) {
                // In this case we want to have a multiple row grid in the center
                root.thumbSize = root.maxThumbSize

                let tmpScrollViewWidth = width - root.thumbSize * 1.5 - 2 * root.gridSpacing

                // Inner grid calculation
                numRows = root.numFit(height, root.maxThumbSize,
                                      root.gridSpacing)
                numColumns = Math.min(numStates,
                                      root.numFit(tmpScrollViewWidth,
                                                  root.thumbSize,
                                                  root.gridSpacing))

                let tmpRows = Math.ceil(numStates / numColumns)

                if (tmpRows <= numRows)
                    numRows = tmpRows
                else
                    numColumns = Math.ceil(numStates / numRows)
            } else {
                // This case is for one row layout and small thumb view
                root.thumbSize = Math.max(height, root.minThumbSize)

                // Inner grid calculation
                numColumns = numStates
                numRows = 1
            }

            Constants.thumbnailWH = root.thumbSize

            let tmpWidth = root.thumbSize * numColumns + root.gridSpacing * (numColumns - 1)
            let remainingSpace = width - root.thumbSize - 2 * root.gridSpacing
            let space = remainingSpace - tmpWidth

            if (space >= root.thumbSize) {
                root.scrollViewWidth = tmpWidth
                addWrapper.width = space
            } else {
                addWrapper.width = Math.max(space, 0.5 * root.thumbSize)
                root.scrollViewWidth = remainingSpace - addWrapper.width
            }

            root.topMargin = (root.scrollViewHeight - (root.thumbSize * numRows)
                              - root.gridSpacing * (numRows - 1)) * 0.5

            addCanvas.width = Math.min(addWrapper.width, root.thumbSize)
            addCanvas.height = root.thumbSize

            baseStateWrapper.width = root.thumbSize

            baseStateThumbnail.anchors.verticalCenter = baseStateWrapper.verticalCenter
            baseStateThumbnail.anchors.horizontalCenter = undefined

            addCanvas.anchors.verticalCenter = addWrapper.verticalCenter
            addCanvas.anchors.horizontalCenter = undefined
            addCanvas.anchors.top = undefined
            addCanvas.anchors.left = addWrapper.left

            root.leftMargin = 0 // resetting left margin in case of orientation switch
        } else {
            root.orientation = Main.Orientation.Portrait
            outerGrid.rows = 3
            outerGrid.columns = 1

            baseStateWrapper.width = width
            root.scrollViewWidth = width
            addWrapper.width = width

            if (width > root.maxThumbSize) {
                // In this case we want to have a multiple row grid in the center
                root.thumbSize = root.maxThumbSize

                let tmpScrollViewHeight = height - root.thumbSize * 1.5 - 2 * root.gridSpacing

                // Inner grid calculation
                numRows = Math.min(numStates, root.numFit(tmpScrollViewHeight,
                                                          root.thumbSize,
                                                          root.gridSpacing))
                numColumns = root.numFit(width, root.maxThumbSize,
                                         root.gridSpacing)

                let tmpColumns = Math.ceil(numStates / numRows)

                if (tmpColumns <= numColumns)
                    numColumns = tmpColumns
                else
                    numRows = Math.ceil(numStates / numColumns)
            } else {
                // This case is for one row layout and small thumb view
                root.thumbSize = Math.max(width, root.minThumbSize)

                // Inner grid calculation
                numRows = numStates
                numColumns = 1
            }

            Constants.thumbnailWH = root.thumbSize

            let tmpHeight = root.thumbSize * numRows + root.gridSpacing * (numRows - 1)
            let remainingSpace = height - root.thumbSize - 2 * root.gridSpacing
            let space = remainingSpace - tmpHeight

            if (space >= root.thumbSize) {
                root.scrollViewHeight = tmpHeight
                addWrapper.height = space
            } else {
                addWrapper.height = Math.max(space, 0.5 * root.thumbSize)
                root.scrollViewHeight = remainingSpace - addWrapper.height
            }

            root.leftMargin = (root.scrollViewWidth - (root.thumbSize * numColumns)
                               - root.gridSpacing * (numColumns - 1)) * 0.5

            addCanvas.width = root.thumbSize
            addCanvas.height = Math.min(addWrapper.height, root.thumbSize)

            baseStateWrapper.height = root.thumbSize

            baseStateThumbnail.anchors.verticalCenter = undefined
            baseStateThumbnail.anchors.horizontalCenter = baseStateWrapper.horizontalCenter

            addCanvas.anchors.verticalCenter = undefined
            addCanvas.anchors.horizontalCenter = addWrapper.horizontalCenter
            addCanvas.anchors.top = addWrapper.top
            addCanvas.anchors.left = undefined

            root.topMargin = 0 // resetting top margin in case of orientation switch
        }

        // TODO always assign the bigger one first otherwise there will be console output complaining...
        innerGrid.rows = numRows
        innerGrid.columns = numColumns
    }

    readonly property int minThumbSize: 100
    readonly property int maxThumbSize: 350

    property int thumbSize: 250

    property int scrollViewWidth: 640
    property int scrollViewHeight: 480
    readonly property int gridSpacing: 10

    property int topMargin: 0
    property int leftMargin: 0

    property int currentStateInternalId: 0

    Connections {
        target: statesEditorModel
        function onChangedToState(n) {
            root.currentStateInternalId = n
        }
    }

    Grid {
        id: outerGrid
        columns: 3
        rows: 1
        spacing: root.gridSpacing

        Item {
            id: baseStateWrapper

            StateThumbnail {
                // Base State
                id: baseStateThumbnail
                baseState: true
                defaultChecked: statesEditorModel.baseState.isDefault
                isChecked: root.currentStateInternalId === 0

                thumbnailImageSource: statesEditorModel.baseState.stateImageSource

                onFocusSignal: root.currentStateInternalId = 0
                onDefaultClicked: statesEditorModel.setStateAsDefault(0)
            }
        }

        Item {
            id: scrollViewWrapper
            width: root.isLandscape() ? root.scrollViewWidth : root.width
            height: root.isLandscape() ? root.height : root.scrollViewHeight

            ScrollView {
                anchors.fill: parent
                anchors.topMargin: root.topMargin
                anchors.leftMargin: root.leftMargin

                Flickable {
                    id: frame
                    // Used to center the ScrollView content vertically/horizontally
                    clip: true
                    interactive: true

                    contentWidth: innerGrid.width
                    contentHeight: innerGrid.height

                    Grid {
                        id: innerGrid

                        rows: 1
                        spacing: root.gridSpacing

                        move: Transition {
                            NumberAnimation {
                                properties: "x,y"
                                easing.type: Easing.OutQuad
                            }
                        }

                        Repeater {
                            id: statesRepeater
                            model: statesEditorModel

                            onItemAdded: root.responsiveResize(root.width,
                                                               root.height)
                            onItemRemoved: root.responsiveResize(root.width,
                                                                 root.height)

                            delegate: DropArea {
                                id: delegateRoot

                                required property int index

                                required property string stateName
                                required property var stateImageSource
                                required property int internalNodeId
                                required property var hasWhenCondition
                                required property var whenConditionString
                                required property bool isDefault
                                required property var modelHasDefaultState

                                width: Constants.thumbnailWH
                                height: Constants.thumbnailWH

                                visible: delegateRoot.internalNodeId // Skip base state

                                property int visualIndex: index //DelegateModel.itemsIndex

                                onEntered: function (drag) {
                                    console.log((drag.source as StateThumbnail).visualIndex,
                                                stateThumbnail.visualIndex)
                                    statesEditorModel.move(
                                                (drag.source as StateThumbnail).visualIndex,
                                                stateThumbnail.visualIndex)
                                }

                                StateThumbnail {
                                    id: stateThumbnail
                                    visualIndex: delegateRoot.visualIndex

                                    // Fix ScrollView taking over the dragging event
                                    onGrabbing: frame.interactive = false
                                    onLetGo: frame.interactive = true

                                    // Fix for ScrollView clipping while dragging of StateThumbnail
                                    onDragActiveChanged: {
                                        if (stateThumbnail.dragActive)
                                            parent = scrollViewWrapper
                                        else
                                            parent = delegateRoot
                                    }

                                    stateName: delegateRoot.stateName
                                    thumbnailImageSource: delegateRoot.stateImageSource
                                    whenCondition: delegateRoot.whenConditionString

                                    baseState: !delegateRoot.internalNodeId
                                    defaultChecked: delegateRoot.isDefault
                                    isChecked: root.currentStateInternalId
                                               === delegateRoot.internalNodeId

                                    onFocusSignal: root.currentStateInternalId
                                                   = delegateRoot.internalNodeId
                                    onDefaultClicked: statesEditorModel.setStateAsDefault(
                                                          delegateRoot.internalNodeId)
                                    onRemove: root.deleteState(
                                                  delegateRoot.internalNodeId)

                                    onStateNameFinished: statesEditorModel.renameState(
                                                             delegateRoot.internalNodeId,
                                                             stateThumbnail.stateName)

                                    onWhenConditionFinished: statesEditorModel.setWhenCondition(
                                                                 delegateRoot.internalNodeId,
                                                                 stateThumbnail.whenCondition)
                                }
                            }
                        }
                    }
                }
            }
        }

        Item {
            id: addWrapper

            Canvas {
                id: addCanvas
                width: root.thumbWidth
                height: root.thumbHeight

                onPaint: {
                    var ctx = getContext("2d")

                    ctx.strokeStyle = Qt.rgba(0, 0, 0, 1)
                    ctx.lineWidth = 6

                    var plusExtend = 20
                    var halfWidth = addCanvas.width / 2
                    var halfHeight = addCanvas.height / 2

                    ctx.beginPath()
                    ctx.moveTo(halfWidth, halfHeight - plusExtend)
                    ctx.lineTo(halfWidth, halfHeight + plusExtend)

                    ctx.moveTo(halfWidth - plusExtend, halfHeight)
                    ctx.lineTo(halfWidth + plusExtend, halfHeight)
                    ctx.stroke()

                    ctx.save()
                    ctx.setLineDash([2, 2])
                    ctx.strokeRect(0, 0, addCanvas.width, addCanvas.height)
                    ctx.restore()
                }

                MouseArea {
                    id: addMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.createNewState()
                }

                Rectangle {
                    // temporary hover indicator for add button
                    anchors.fill: parent
                    opacity: 0.1
                    color: addMouseArea.containsMouse ? "#ffffff" : "#000000"
                }
            }
        }
    }
}
