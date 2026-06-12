# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

# Drives the native combined (C++ + QML) debugging machinery through
# GDB without a Qt Creator frontend, mimicking the commands the GDB
# engine would send.
#
# Usage:
#     qt-cmake -S . -B build && cmake --build build
#     QMLMIX_EXECUTABLE=build/qmlmixtest gdb -batch -x qmlmix-gdb-test.py
#
# The Qt used to build the application needs the qmldbg_native and
# qmldbg_nativedebugger plugins.

import os
import sys

test_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(
    os.path.join(test_dir, '..', '..', '..', '..', 'share', 'qtcreator', 'debugger')))

import gdbbridge

# The 'var message' line in Main.qml.
BREAK_LINE = 19

failures = []

def check(name, cond):
    print('=== %s: %s ===' % ('PASS' if cond else 'FAIL', name))
    if not cond:
        failures.append(name)

executable = os.environ.get('QMLMIX_EXECUTABLE',
                            os.path.join(test_dir, 'build', 'qmlmixtest'))
if not os.path.isfile(executable):
    print('=== FAIL: executable not found: %s ===' % executable)
    print('=== Set QMLMIX_EXECUTABLE or build into ./build first ===')
    gdb.execute('quit 1')

d = gdbbridge.theDumper

gdb.execute('set confirm off')
gdb.execute('set breakpoint pending on')
try:
    gdb.execute('set debuginfod enabled off')
except gdb.error:
    pass
gdb.execute('file %s' % executable)
gdb.execute('set args -qmljsdebugger=native,services:NativeQmlDebugger')
gdb.execute('set environment QV4_FORCE_INTERPRETER 1')
gdb.execute('set environment QT_QPA_PLATFORM offscreen')

# Insertion before the program runs is expected to fail directly and to
# create one pending resolver breakpoint on qt_qmlDebugConnectorOpen()
# per interpreter breakpoint. Both must get resolved on the single stop
# there.
d.insertInterpreterBreakpoint({
    'file': os.path.join(test_dir, 'Main.qml'),
    'line': BREAK_LINE,
    'type': 'breakpoint',
    'token': 1,
})
d.insertInterpreterBreakpoint({
    'file': os.path.join(test_dir, 'Main.qml'),
    'line': BREAK_LINE + 1,
    'type': 'breakpoint',
    'token': 2,
})
check('pending resolvers created', len(d.interpreterBreakpointResolvers) == 2)

gdb.execute('run')

frame = gdb.newest_frame()
check('stopped at qt_qmlDebugMessageAvailable',
      frame.name() == 'qt_qmlDebugMessageAvailable')

stack = d.extractInterpreterStack()
frames = stack.get('frames', [])
check('interpreter stack has frames', bool(frames))

ctx = ''
if frames:
    top = frames[0]
    ctx = top.get('context', '')
    check('top frame is the onTriggered expression',
          top.get('language') == 'js'
          and top.get('line') == BREAK_LINE
          and top.get('function', '').find('onTriggered') >= 0)
    check('top frame has a context', bool(ctx))

if ctx:
    (ok, res) = d.tryFetchInterpreterVariables({
        'nativemixed': 1,
        'context': ctx,
        'expanded': {'local.this': 100, 'local.this.parent': 100},
    })
    check('interpreter variables fetched', ok)
    check('message is undefined before assignment',
          res.find('name="message"') >= 0
          and res.find('valueencoded="undefined"') >= 0)
    check('this expands to Timer properties',
          res.find('iname="local.this.interval"') >= 0)
    check('this.parent expands one level deeper',
          res.find('iname="local.this.parent.children"') >= 0)

# The second breakpoint, on the next line, must be hit as well.
gdb.execute('continue')
check('stopped at second breakpoint',
      gdb.newest_frame().name() == 'qt_qmlDebugMessageAvailable')
frames = d.extractInterpreterStack().get('frames', [])
check('second breakpoint is on the next line',
      bool(frames) and frames[0].get('line') == BREAK_LINE + 1)

gdb.execute('kill')

if failures:
    print('=== RESULT: FAIL (%s) ===' % ', '.join(failures))
    gdb.execute('quit 1')
print('=== RESULT: PASS ===')
