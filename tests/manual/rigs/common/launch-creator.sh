#!/bin/bash
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#
# Start a throwaway Qt Creator headlessly under Xvfb with the McpServer plugin
# loaded, so it can be driven in-process via common/mcpcli.py. Tracks the exact
# PIDs it starts and waits until the MCP port is actually listening.
#
# Every setting is an environment variable so no machine, user or absolute path
# is baked in:
#   QTC_BIN        qtcreator executable      (default: <repo>/bin/qtcreator)
#   QTC_DISPLAY    X display to create       (default: :9)
#   QTC_MCP_PORT   MCP port to bind          (default: 8765)
#   QTC_SETTINGS   throwaway -settingspath   (default: $QTC_RUN_DIR/settings)
#   QTC_SCREEN     Xvfb screen geometry      (default: 1280x1024x24)
#   QTC_RUN_DIR    where pid/log files go    (default: ./.rig-run)
#   QTC_EXTRA_ARGS extra qtcreator arguments (default: empty)
#
# IMPORTANT: parallel instances collide on X displays and ports. Pick a
# DISPLAY and PORT unique to your instance and never reuse :0 (the real
# desktop). This script verifies the listener really is the process it
# started before returning success.
set -u

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../../../.." && pwd)

QTC_BIN=${QTC_BIN:-$repo/bin/qtcreator}
QTC_DISPLAY=${QTC_DISPLAY:-:9}
QTC_MCP_PORT=${QTC_MCP_PORT:-8765}
QTC_SCREEN=${QTC_SCREEN:-1280x1024x24}
QTC_RUN_DIR=${QTC_RUN_DIR:-$PWD/.rig-run}
QTC_SETTINGS=${QTC_SETTINGS:-$QTC_RUN_DIR/settings}
QTC_EXTRA_ARGS=${QTC_EXTRA_ARGS:-}

mkdir -p "$QTC_RUN_DIR" "$QTC_SETTINGS"

[ -x "$QTC_BIN" ] || { echo "ERROR: QTC_BIN not executable: $QTC_BIN" >&2; exit 1; }
dpynum=${QTC_DISPLAY#:}
if [ -e "/tmp/.X11-unix/X$dpynum" ]; then
    echo "ERROR: display $QTC_DISPLAY already in use - pick a free one" >&2
    exit 1
fi

export DISPLAY=$QTC_DISPLAY
Xvfb "$QTC_DISPLAY" -screen 0 "$QTC_SCREEN" >"$QTC_RUN_DIR/xvfb.log" 2>&1 &
echo $! >"$QTC_RUN_DIR/xvfb.pid"
for _ in $(seq 1 50); do xdpyinfo -display "$QTC_DISPLAY" >/dev/null 2>&1 && break; sleep 0.1; done

"$QTC_BIN" -settingspath "$QTC_SETTINGS" -load McpServer -mcp-port "$QTC_MCP_PORT" \
    $QTC_EXTRA_ARGS >"$QTC_RUN_DIR/qtcreator.log" 2>&1 &
qtc_pid=$!
echo "$qtc_pid" >"$QTC_RUN_DIR/qtcreator.pid"

for _ in $(seq 1 120); do
    ss -ltn 2>/dev/null | grep -q ":$QTC_MCP_PORT " && break
    kill -0 "$qtc_pid" 2>/dev/null || { echo "ERROR: qtcreator exited early, see $QTC_RUN_DIR/qtcreator.log" >&2; exit 1; }
    sleep 0.5
done

owner=$(ss -ltnp 2>/dev/null | grep ":$QTC_MCP_PORT " | grep -o 'pid=[0-9]*' | head -1 | cut -d= -f2)
if [ "$owner" != "$qtc_pid" ]; then
    echo "ERROR: port $QTC_MCP_PORT is owned by pid ${owner:-none}, not our qtcreator ($qtc_pid)." >&2
    echo "       Another instance likely already uses it; pick a unique QTC_MCP_PORT." >&2
    exit 1
fi

echo "qtcreator=$qtc_pid xvfb=$(cat "$QTC_RUN_DIR/xvfb.pid") display=$QTC_DISPLAY mcp_port=$QTC_MCP_PORT"
echo "settings=$QTC_SETTINGS run_dir=$QTC_RUN_DIR"
echo "drive it with: QTC_MCP_PORT=$QTC_MCP_PORT $here/mcpcli.py '<tool>:{...}'"
