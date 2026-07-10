// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick

Item {
    function compute(value) {
        var doubled = value * 2
        function helper() { return doubled }
        return backend.process(helper()) // MARKER: qml breakpoint line
    }
    Component.onCompleted: {
        compute(41)
        // A second call on the next event loop iteration - lets a QML
        // breakpoint inserted only once already stopped in QmlEntryPoint::process
        // (the first call's C++ callee) still get a chance to fire.
        Qt.callLater(compute, 41)
    }
}
