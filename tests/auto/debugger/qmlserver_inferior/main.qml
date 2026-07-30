// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import QtQuick

QtObject {
    id: root

    // Same name and value as the C++/Python inferiors' own file-scope
    // globalValue, so the watcher tests can use one expression for every
    // backend. A property, not a local or parameter of compute() below, so it
    // can only ever surface through the watchers mechanism.
    property int globalValue: 41

    function compute(value) {
        // An object local, so the Locals view has something with members -
        // v8 reports only a property count for it up front, and the members
        // need a separate lookup once expanded (see QmlImpl::handleScopeReply).
        // "inner" is itself an object, so expanding it needs a further lookup
        // round of its own (see QmlImpl::lookupHandles).
        var nested = ({ alpha: 1, beta: "two", inner: ({ deep: 7 }) })
        var doubled = value * 2 // breakpoint line
        // Mirrors the C++/Python inferiors' bump(): globalValue is still 41 at
        // the breakpoint above and 42 by the one below, which is what the
        // watcher tests check for before and after.
        globalValue = value
        return doubled // second breakpoint line
    }

    // Uncaught by design, and fired after compute()'s own Timer below so it
    // never disturbs the breakpoint tests: an uncaught JS exception with no
    // BreakpointAtJavaScriptThrow set is only a runtime warning, not a debugger
    // stop, and insertsBreakpointAtJavaScriptThrowAndStopsAtIt() is the only
    // test that sets that breakpoint. Still early enough to land inside that
    // test's own s_timeout wait, which starts once the attach completes.
    function throwsError() {
        throw new Error("boom")
    }
    property Timer throwTimer: Timer {
        interval: 4000
        running: true
        onTriggered: root.throwsError()
    }

    // Reproduces, without needing Qt Quick, the one case the QmlDebugger
    // service's own object list cannot report: LIST_OBJECTS walks contexts from
    // the root but only ever emits objects from the *root* context's instance
    // list (buildObjectList()/QQmlContextPrivate::appendInstance(), which
    // appends to the creating context, not to the root). So an object created
    // from inside another created object's context is absent from the reported
    // tree entirely, and with a null parent it has no QObject parent to be
    // found under either - which is exactly why Qt Quick delegates
    // (Repeater/ListView, whose visual parent is not a QObject parent) go
    // missing there. It can only reach the Inspector tree through a fetch by
    // debug id, off the OBJECT_CREATED it announced itself with.
    // See QmlImpl::m_knownDelegateIds and reportsInspectorObjectTree().
    //
    // Both objects are held in properties so they stay alive; that does not
    // give either a QObject parent. The inner one carries an id because that,
    // not objectName, is what the Inspector tree names an object by
    // (QmlInspectorAgent::addWatchData()'s chain prefers the QML id, then the
    // class name, and only then objectName).
    property QtObject orphanHost: null
    property Component orphanComponent: Component {
        id: orphanComponent
        QtObject {
            id: orphanHost
            property QtObject orphan: null
            property Component innerComponent: Component {
                id: innerComponent
                QtObject {
                    id: orphanObject
                    property int orphanValue: 7
                }
            }
            Component.onCompleted: orphanHost.orphan = innerComponent.createObject(null)
        }
    }
    Component.onCompleted: root.orphanHost = orphanComponent.createObject(null)

    // Delayed rather than called from Component.onCompleted directly - a
    // standalone V8Debugger attach (unlike native-mixed debugging's own
    // process-level pause) has nothing holding execution once the initial
    // debug-connection handshake completes, so an immediate call would
    // race ahead of a test's own connect+insert-breakpoint round trips
    // (confirmed live: qmlstack_inferior's own compute(), called from
    // Component.onCompleted, already finished before a standalone
    // attach's slower setbreakpoint request even reached it).
    property Timer timer: Timer {
        interval: 3000
        running: true
        // globalValue + 1, matching the C++/Python inferiors' own
        // bump(globalValue + 1) call.
        onTriggered: root.compute(root.globalValue + 1)
    }
}
