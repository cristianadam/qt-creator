// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick
import QtQuick.Controls.Basic as Basic
import StudioTheme 1.0 as StudioTheme

Basic.ScrollView {
    id: control

    property alias horizontalThickness: horizontalScrollBar.height
    property alias verticalThickness: verticalScrollBar.width
    readonly property bool verticalScrollBarVisible: verticalScrollBar.scrollBarVisible
    readonly property bool horizontalScrollBarVisible: horizontalScrollBar.scrollBarVisible
    readonly property bool bothVisible: control.verticalScrollBarVisible
                                        && control.horizontalScrollBarVisible

    property real temporaryHeight: 0

    default property alias content: areaItem.children

    property alias interactive: flickable.interactive
    property alias contentY: flickable.contentY

    property bool adsFocus: false
    // objectName is used by the dock widget to find this particular ScrollView
    // and set the ads focus on it.
    objectName: "__mainSrollView"

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    hoverEnabled: true

    ScrollBar.horizontal: ScrollBar {
        id: horizontalScrollBar
        parent: control
        x: control.leftPadding
        y: control.height - height
        width: control.availableWidth - (verticalScrollBar.isNeeded ? verticalScrollBar.thickness : 0)
        orientation: Qt.Horizontal

        show: (control.hovered || control.focus || control.adsFocus) && horizontalScrollBar.isNeeded
        otherInUse: verticalScrollBar.inUse
    }

    ScrollBar.vertical: ScrollBar {
        id: verticalScrollBar
        parent: control
        x: control.mirrored ? 0 : control.width - width
        y: control.topPadding
        height: control.availableHeight - (horizontalScrollBar.isNeeded ? horizontalScrollBar.thickness : 0)
        orientation: Qt.Vertical

        show: (control.hovered || control.focus || control.adsFocus) && verticalScrollBar.isNeeded
        otherInUse: horizontalScrollBar.inUse
    }

    Flickable {
        id: flickable
        contentWidth: areaItem.childrenRect.width
        contentHeight: Math.max(areaItem.childrenRect.height, control.temporaryHeight)

        boundsMovement: Flickable.StopAtBounds
        boundsBehavior: Flickable.StopAtBounds

        Item { id: areaItem }
    }
}
