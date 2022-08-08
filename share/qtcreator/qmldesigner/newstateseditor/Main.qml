import QtQuick
import QtQuick.Controls
import StatesEditor
import StudioTheme as StudioTheme

Rectangle {
    id: root

    signal createNewState
    signal cloneState(int internalNodeId)
    signal extendState(int internalNodeId)
    signal deleteState(int internalNodeId)

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

    Component.onCompleted: {
        console.log("HAS EXTEND", statesEditorModel.hasExtend)

        root.responsiveResize(root.width, root.height)
    }

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

        // Size extension in case of extend groups are shown
        var sizeExtension = root.showExtendGroups ? root.extend : 0
        var doubleSizeExtension = root.showExtendGroups ? 2 * root.extend : 0

        // Get view orientation (LANDSCAPE, PORTRAIT)
        if (width >= height) {
            root.orientation = Main.Orientation.Landscape
            outerGrid.columns = 3
            outerGrid.rows = 1
            // Three outer section height (base state, middle, plus button)
            baseStateWrapper.height = height
            root.scrollViewHeight = height
            addWrapper.height = height

            height -= doubleSizeExtension

            if (height > root.maxThumbSize) {
                // In this case we want to have a multi row grid in the center
                root.thumbSize = root.maxThumbSize

                let tmpScrollViewWidth = width - root.thumbSize * 1.5 - 2 * root.outerGridSpacing

                // Inner grid calculation
                numRows = root.numFit(height, root.maxThumbSize, root.innerGridSpacing)
                numColumns = Math.min(numStates, root.numFit(tmpScrollViewWidth, root.thumbSize,
                                                             root.innerGridSpacing))

                let tmpRows = Math.ceil(numStates / numColumns)

                if (tmpRows <= numRows)
                    numRows = tmpRows
                else
                    numColumns = Math.ceil(numStates / numRows)
            } else {
                // This case is for single row layout and small thumb view
                root.thumbSize = Math.max(height, root.minThumbSize)

                // Inner grid calculation
                numColumns = numStates
                numRows = 1
            }

            Constants.thumbnailWH = root.thumbSize

            let tmpWidth = root.thumbSize * numColumns
                + root.innerGridSpacing * (numColumns - 1) + doubleSizeExtension
            let remainingSpace = width - root.thumbSize - 2 * root.outerGridSpacing
            let space = remainingSpace - tmpWidth

            if (space >= root.thumbSize) {
                root.scrollViewWidth = tmpWidth
                addWrapper.width = space
            } else {
                addWrapper.width = Math.max(space, 0.5 * root.thumbSize)
                root.scrollViewWidth = remainingSpace - addWrapper.width
            }

            root.topMargin = (root.scrollViewHeight - (root.thumbSize * numRows)
                              - root.innerGridSpacing * (numRows - 1)) * 0.5 - sizeExtension

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
            // Three outer section width (base state, middle, plus button)
            baseStateWrapper.width = width
            root.scrollViewWidth = width
            addWrapper.width = width

            width -= doubleSizeExtension

            if (width > root.maxThumbSize) {
                // In this case we want to have a multi column grid in the center
                root.thumbSize = root.maxThumbSize

                let tmpScrollViewHeight = height - root.thumbSize * 1.5 - 2 * root.outerGridSpacing

                // Inner grid calculation
                numRows = Math.min(numStates, root.numFit(tmpScrollViewHeight, root.thumbSize,
                                                          root.innerGridSpacing))
                numColumns = root.numFit(width, root.maxThumbSize, root.innerGridSpacing)

                let tmpColumns = Math.ceil(numStates / numRows)

                if (tmpColumns <= numColumns)
                    numColumns = tmpColumns
                else
                    numRows = Math.ceil(numStates / numColumns)
            } else {
                // This case is for single column layout and small thumb view
                root.thumbSize = Math.max(width, root.minThumbSize)

                // Inner grid calculation
                numRows = numStates
                numColumns = 1
            }

            Constants.thumbnailWH = root.thumbSize

            let tmpHeight = root.thumbSize * numRows
                + root.innerGridSpacing * (numRows - 1) + doubleSizeExtension
            let remainingSpace = height - root.thumbSize - 2 * root.outerGridSpacing
            let space = remainingSpace - tmpHeight

            if (space >= root.thumbSize) {
                root.scrollViewHeight = tmpHeight
                addWrapper.height = space
            } else {
                addWrapper.height = Math.max(space, 0.5 * root.thumbSize)
                root.scrollViewHeight = remainingSpace - addWrapper.height
            }

            root.leftMargin = (root.scrollViewWidth - (root.thumbSize * numColumns)
                               - root.innerGridSpacing * (numColumns - 1)) * 0.5 - sizeExtension

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

    // These function assume that the order of the states is as follows:
    // State A, State B (extends State A), ... so the extended state always comes first
    function isInRange(i) {
        return i >= 0 && i < statesRepeater.count
    }

    function nextStateHasExtend(i) {
        let next = i + 1
        return root.isInRange(next) ? statesEditorModel.get(next).hasExtend : false
    }

    function previousStateHasExtend(i) {
        let prev = i - 1
        return root.isInRange(prev) ? statesEditorModel.get(prev).hasExtend : false
    }

    property bool showExtendGroups: statesEditorModel.hasExtend

    property int extend: 16

    readonly property int minThumbSize: 100
    readonly property int maxThumbSize: 350

    property int thumbSize: 250

    property int scrollViewWidth: 640
    property int scrollViewHeight: 480
    property int outerGridSpacing: 10
    property int innerGridSpacing: root.showExtendGroups ? 40 : root.outerGridSpacing

    // These margins are used to push the inner grid down or to the left depending on the views
    // orientation to align to the outer grid
    property int topMargin: 0
    property int leftMargin: 0

    property int currentStateInternalId: 0

    // TODO make a function and execute it on item added (didn't work properly)
    onCurrentStateInternalIdChanged: {
        // Move the current state into view if outside
        if (root.currentStateInternalId === 0) // Not for base state
            return;

        var x = 0
        var y = 0
        for (let i = 0; i < statesRepeater.count; ++i) {
            let item = statesRepeater.itemAt(i)
            if (item.internalNodeId === root.currentStateInternalId) {
                x = item.x
                y = item.y
                break
            }
        }

        console.log(x, y)
        console.log(frame.contentX,
                    frame.contentX + root.scrollViewWidth - root.thumbSize)

        // Check if it is in view
        if (x <= frame.contentX || x >= (frame.contentX + root.scrollViewWidth - root.thumbSize))
            frame.contentX = x - root.scrollViewWidth * 0.5 + root.thumbSize * 0.5

        if (y <= frame.contentY || y >= (frame.contentY + root.scrollViewHeight - root.thumbSize))
            frame.contentY = y - root.scrollViewHeight * 0.5 + root.thumbSize * 0.5

        console.log(frame.contentX)
    }

    Grid {
        id: outerGrid
        columns: 3
        rows: 1
        spacing: root.outerGridSpacing

        Item {
            id: baseStateWrapper

            StateThumbnail {
                // Base State
                id: baseStateThumbnail
                width: Constants.thumbnailWH
                height: Constants.thumbnailWH
                baseState: true
                defaultChecked: !statesEditorModel.baseState.modelHasDefaultState // TODO Make this one a model property
                isChecked: root.currentStateInternalId === 0
                thumbnailImageSource: statesEditorModel.baseState.stateImageSource // TODO Get rid of the QVariantMap

                onFocusSignal: root.currentStateInternalId = 0
                onDefaultClicked: statesEditorModel.resetDefaultState()
            }
        }

        Item {
            id: scrollViewWrapper
            width: root.isLandscape() ? root.scrollViewWidth : root.width
            height: root.isLandscape() ? root.height : root.scrollViewHeight
            clip: true

            ScrollView {
                anchors.fill: parent
                anchors.topMargin: root.topMargin
                anchors.leftMargin: root.leftMargin

                Flickable {
                    id: frame
                    boundsMovement: Flickable.StopAtBounds
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: true
                    contentWidth: {
                        let ext = root.showExtendGroups ? (2 * root.extend) : 0
                        return innerGrid.width + ext
                    }
                    contentHeight: {
                        let ext = root.showExtendGroups ? (2 * root.extend) : 0
                        return innerGrid.height + ext
                    }

                    Behavior on contentY {
                        NumberAnimation {
                            duration: 1000
                            easing.type: Easing.InOutCubic
                        }
                    }

                    Behavior on contentX {
                        NumberAnimation {
                            duration: 1000
                            easing.type: Easing.InOutCubic
                        }
                    }

                    Grid {
                        id: innerGrid

                        x: root.showExtendGroups ? root.extend : 0
                        y: root.showExtendGroups ? root.extend : 0

                        rows: 1
                        spacing: root.innerGridSpacing

                        move: Transition {
                            NumberAnimation {
                                properties: "x,y"
                                easing.type: Easing.OutQuad
                            }
                        }

                        Repeater {
                            id: statesRepeater

                            property int grabIndex: -1

                            function executeDrop(from, to) {
                                statesEditorModel.drop(from, to)
                                statesRepeater.grabIndex = -1
                            }

                            model: statesEditorModel

                            onItemAdded: root.responsiveResize(root.width, root.height)
                            onItemRemoved: root.responsiveResize(root.width, root.height)

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
                                required property bool hasExtend
                                required property var extendString

                                property bool extendedState: statesEditorModel.extendedStates.includes(
                                                                 delegateRoot.stateName)

                                width: Constants.thumbnailWH
                                height: Constants.thumbnailWH

                                visible: delegateRoot.internalNodeId // Skip base state

                                property int visualIndex: index //DelegateModel.itemsIndex

                                onEntered: function (drag) {
                                    let dragSource = (drag.source as StateThumbnail)

                                    console.log("dragSource", dragSource, drag.source)

                                    if (dragSource === undefined)
                                        return

                                    console.log(dragSource.hasExtend, stateThumbnail.hasExtend)

                                    if (dragSource.extendString !== stateThumbnail.extendString
                                            || delegateRoot.extendedState)
                                        return

                                    statesEditorModel.move(
                                                (drag.source as StateThumbnail).visualIndex,
                                                stateThumbnail.visualIndex)
                                }

                                onDropped: function (drop) {
                                    statesRepeater.executeDrop(statesRepeater.grabIndex,
                                                               stateThumbnail.visualIndex)
                                }

                                // Extend Groups Visualization
                                Rectangle {
                                    id: extendBackground
                                    x: -root.extend
                                    y: -root.extend
                                    width: Constants.thumbnailWH + 2 * root.extend
                                    height: Constants.thumbnailWH + 2 * root.extend
                                    color: "#727272"

                                    radius: {
                                        if (root.nextStateHasExtend(delegateRoot.index))
                                            return delegateRoot.hasExtend ? 0 : root.extend

                                        return root.extend
                                    }

                                    visible: {
                                        if (delegateRoot.hasExtend || delegateRoot.extendedState)
                                            return true

                                        return false
                                    }
                                }
                                // Fill the gap between extend group states and also cover up radius
                                // of start and end states of an extend group in case of line break
                                Rectangle {
                                    id: extendGap
                                    x: {
                                        if (root.previousStateHasExtend(delegateRoot.index))
                                            return -root.innerGridSpacing
                                        if (root.nextStateHasExtend(delegateRoot.index))
                                            return Constants.thumbnailWH

                                        return 0
                                    }
                                    y: -root.extend
                                    width: root.innerGridSpacing
                                    height: Constants.thumbnailWH + 2 * root.extend
                                    color: "#727272"
                                    visible: extendBackground.radius !== 0
                                             && extendBackground.visible
                                }

                                StateThumbnail {
                                    id: stateThumbnail
                                    width: Constants.thumbnailWH
                                    height: Constants.thumbnailWH
                                    visualIndex: delegateRoot.visualIndex
                                    internalNodeId: delegateRoot.internalNodeId

                                    hasExtend: delegateRoot.hasExtend
                                    extendString: delegateRoot.extendString

                                    // Fix ScrollView taking over the dragging event
                                    onGrabbing: {
                                        frame.interactive = false
                                        console.log("onGrabbing", stateThumbnail.visualIndex)
                                        statesRepeater.grabIndex = stateThumbnail.visualIndex
                                    }
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
                                    isChecked: root.currentStateInternalId === delegateRoot.internalNodeId

                                    onFocusSignal: root.currentStateInternalId = delegateRoot.internalNodeId
                                    onDefaultClicked: statesEditorModel.setStateAsDefault(
                                                          delegateRoot.internalNodeId)

                                    onClone: root.cloneState(delegateRoot.internalNodeId)
                                    onExtend: root.extendState(delegateRoot.internalNodeId)
                                    onRemove: root.deleteState(delegateRoot.internalNodeId)

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
