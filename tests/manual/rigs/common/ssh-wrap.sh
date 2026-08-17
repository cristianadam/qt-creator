#!/bin/bash
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#
# Drop-in ssh replacement that bypasses an unusable system ssh configuration.
# On some CI/sandbox hosts /etc/ssh/ssh_config.d/* is owned by nobody:nogroup,
# so plain ssh aborts with "Bad owner or permissions on ...". "-F /dev/null"
# ignores the system and per-user config; StrictHostKeyChecking=accept-new and
# BatchMode=yes keep it non-interactive.
#
# Point Qt Creator at this wrapper (Preferences > Devices > SSH > "Path to ssh
# executable", i.e. the SshSettings/SshFilePath key) when the plain ssh fails;
# the per-device SshExecutable is NOT used for every device operation, so the
# global setting is the one that matters.
exec /usr/bin/ssh -F /dev/null \
    -o StrictHostKeyChecking=accept-new \
    -o BatchMode=yes \
    "$@"
