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
import QtQuick.Timeline

Item {
    id: root
    width: 1920
    height: 1080

    property string caption: "This is a string"
    property string title: "this is a string"
    property bool active: false

    function prev() {
        var states = root.stateNames()

        if (states.length === 0)
           return false

        if (root.state === "")
            return false

        var index = states.indexOf(root.state)

        // base state is not in the list
        if (index > 0) {
            root.state = states[index - 1]
            return true
        }

        return false
    }

    function next() {
        var states = root.stateNames()

        if (states.length === 0)
           return false

        if (root.state === "") {
            root.state = states[0]
            return true
        }

        var index = states.indexOf(root.state)

        if (index < (states.length - 1)) {
            root.state = states[index + 1]
            return true
        }

        return false
    }

    function stateNames() {
        var states = []

        for (var i = 0; i < root.states.length; i++) {
            var state = root.states[i]
            states.push(state.name)
        }

        return states
    }

    signal activated

    function activate() {
        root.active = true
        stateGroup.state = "active"
        root.activated()
    }

    function done() {
        stateGroup.state = "done"
    }

    function init() {
        root.active = false
        stateGroup.state = "inactive"
    }

    StateGroup {
        id: stateGroup
        states: [
            State {
                name: "active"

                PropertyChanges {
                    target: root
                    opacity: 1
                    visible: true
                }
            },
            State {
                name: "inactive"

                PropertyChanges {
                    target: root
                    opacity: 0
                    visible: true
                }
            },
            State {
                name: "done"

                PropertyChanges {
                    target: root
                    opacity: 1
                    visible: true
                }
            }
        ]
    }
}
