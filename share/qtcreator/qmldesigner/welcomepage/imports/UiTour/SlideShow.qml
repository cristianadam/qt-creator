/****************************************************************************
**
** Copyright (C) 2023 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    width: 1920
    height: 1080
    color: "#00000000"

    property string caption
    property string title

    property int progress: 0
    property int currentSlide: 0

    function next() {
        if (root.currentSlide === (root.progress - 1))
            return

        var index = root.findActive()
        var current = root.children[index]

        root.currentSlide++

        if (current.next()) {
            root.caption = current.caption
            root.title = current.title
            return
        }

        root.children[index].init()
        root.children[index + 1].activate()

        root.caption = root.children[index + 1].caption
        root.title = root.children[index + 1].title
    }

    function prev() {
        if (root.currentSlide === 0)
            return

        var index = root.findActive()
        var current = root.children[index]

        root.currentSlide--

        if (current.prev()) {
            root.caption = current.caption
            root.title = current.title
            return
        }

        root.children[index].init()
        root.children[index - 1].activate()
        root.caption = root.children[index - 1].caption
        root.title = root.children[index - 1].title
    }

    function findActive() {
        for (var i = 0; i < root.children.length; i++) {
            var child = root.children[i]
            if (child.active)
                return i
        }
        return -1
    }

    Component.onCompleted: {
        for (var i = 0; i < root.children.length; i++) {
            var child = root.children[i]
            child.init()
            root.progress += child.states.length
            if (i === 0) {
                child.visible = true
                child.activate()
            }
        }

        root.caption = root.children[0].caption
        root.title = root.children[0].title
    }
}
