#!/bin/bash
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#
# Stop the Qt Creator + Xvfb that launch-creator.sh started, killing ONLY the
# exact PIDs it tracked. Never uses a broad pattern kill (pkill -f qtcreator /
# pkill -f Xvfb) - that would also reach other instances on this machine.
#
#   QTC_RUN_DIR   pid/log dir used by launch-creator.sh (default: ./.rig-run)
#   QTC_DISPLAY   the display to remove the socket for  (default: :9)
set -u

QTC_RUN_DIR=${QTC_RUN_DIR:-$PWD/.rig-run}
QTC_DISPLAY=${QTC_DISPLAY:-:9}

for f in qtcreator.pid xvfb.pid; do
    pid=$(cat "$QTC_RUN_DIR/$f" 2>/dev/null) || continue
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null && echo "killed $f=$pid"
    fi
    rm -f "$QTC_RUN_DIR/$f"
done

sleep 1
rm -f "/tmp/.X11-unix/X${QTC_DISPLAY#:}" 2>/dev/null
echo "stopped."
