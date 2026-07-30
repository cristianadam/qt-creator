// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick

Item {
    function compute(value) {
        var doubled = value * 2
        function helper() { return doubled }
        return backend.process(helper()) // MARKER: qml breakpoint line
    }
    // Uncaught by design - only reached once throwTimer fires, well after
    // startup, so it never disturbs compute()'s own breakpoint-hit tests.
    // An uncaught JS exception with no BreakpointAtJavaScriptThrow set is
    // just a runtime warning (confirmed live), not a crash or a debugger
    // stop - only insertsBreakpointAtJavaScriptThrowAndStopsAtIt() actually
    // sets that breakpoint, so every other Qml test using this same
    // executable is unaffected by this call existing at all.
    function throwsError() {
        throw new Error("boom")
    }
    // Real wall-clock delay, not Qt.callLater (which fires on the very next
    // event-loop iteration, together with compute()'s own second call -
    // confirmed live too early for a test that still needs to complete a
    // connect+insert-breakpoint round trip first).
    Timer {
        interval: 1000
        running: true
        onTriggered: throwsError()
    }
    Component.onCompleted: {
        compute(41)
        // A second call on the next event loop iteration - lets a QML
        // breakpoint inserted only once already stopped in QmlEntryPoint::process
        // (the first call's C++ callee) still get a chance to fire.
        Qt.callLater(compute, 41)
    }
}
