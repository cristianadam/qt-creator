// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick

Window {
    id: root
    visible: true
    width: 400
    height: 300

    property int counter: 0

    Timer {
        interval: 500
        running: true
        repeat: true
        onTriggered: {
            var message = "tick"
            root.counter = root.counter + 1
        }
    }

    // Used by the breakpoint cost experiment. The two property writes
    // delimit the hot loop for timing from the outside.
    property int hotStart: 0
    property int hotResult: 0
    Component.onCompleted: {
        root.hotStart = 1
        var sum = 0
        for (var i = 0; i < 20000; ++i)
            sum += i
        root.hotResult = sum
    }
}
