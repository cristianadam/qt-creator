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

# Capture the asynchronous breakpoint resolution reports; they carry
# the service breakpoint ids needed for removal.
asyncReports = []
savedReportAsync = d.reportInterpreterAsync
d.reportInterpreterAsync = lambda resdict, asyncclass: asyncReports.append(dict(resdict))

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
check('both breakpoints were resolved',
      len([r for r in asyncReports
           if not r.get('pending') and r.get('number', -1) != -1]) == 2)

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

# The full mixed stack report must de-emphasize the debugger machinery
# frames between the QML frame and the application code.
reports = []
savedReportResult = d.reportResult
d.reportResult = lambda result, args: reports.append(result)
d.fetchStack({'limit': 40, 'nativemixed': 1, 'token': 99,
              'allowinferiorcalls': 1})
d.reportResult = savedReportResult
stackFrames = (reports[0] if reports else '').split('frame={')[1:]

def frames_matching(needle):
    return [f for f in stackFrames if f.find(needle) >= 0]

check('machinery frames are marked',
      all(f.find('usable="0",machinery="1"') >= 0
          for f in frames_matching('qt_qmlDebug')
                 + frames_matching('NativeDebugger::')
                 + frames_matching('QV4::Moth::'))
      and len(frames_matching('machinery="1"')) >= 3)
check('QML frame is not marked',
      all(f.find('machinery="1"') < 0 for f in frames_matching('language="js"'))
      and len(frames_matching('language="js"')) >= 1)
check('application frames are not marked',
      len(frames_matching('QQmlTimer')) >= 1
      and all(f.find('machinery="1"') < 0 for f in frames_matching('QQmlTimer')))

# The second breakpoint, on the next line, must be hit as well.
gdb.execute('continue')
check('stopped at second breakpoint',
      gdb.newest_frame().name() == 'qt_qmlDebugMessageAvailable')
frames = d.extractInterpreterStack().get('frames', [])
check('second breakpoint is on the next line',
      bool(frames) and frames[0].get('line') == BREAK_LINE + 1)

# Stepping: back at the first breakpoint, remove the second breakpoint,
# then a single step must stop on its line nevertheless.
gdb.execute('continue')
frames = d.extractInterpreterStack().get('frames', [])
check('first breakpoint hits again',
      bool(frames) and frames[0].get('line') == BREAK_LINE)

bp2 = -1
for report in asyncReports:
    if report.get('token') == 2:
        bp2 = report.get('number', -1)
check('second breakpoint has a service id', bp2 != -1)
res = d.removeInterpreterBreakpoint({'id': bp2})
check('breakpoint removal acknowledged', res.get('id') == bp2)

d.sendInterpreterRequest('stepin', {})
gdb.execute('continue')
frames = d.extractInterpreterStack().get('frames', [])
check('step lands on the next line',
      gdb.newest_frame().name() == 'qt_qmlDebugMessageAvailable'
      and bool(frames) and frames[0].get('line') == BREAK_LINE + 1)

d.reportInterpreterAsync = savedReportAsync
gdb.execute('kill')

if failures:
    print('=== RESULT: FAIL (%s) ===' % ', '.join(failures))
    gdb.execute('quit 1')
print('=== RESULT: PASS ===')
