# Manual verification rigs

Helper scripts for driving a built Qt Creator **headlessly** and verifying
behavior that unit tests cannot easily observe (editor/debugger/device
interactions), by talking to the in-process `McpServer` plugin.

These are manual-test helpers, not part of the build. Nothing here hard-codes a
machine, user, or absolute path: everything is passed via environment
variables, so the same scripts work on any checkout and against any device you
own. Substitute your own values.

## Layout

    common/        platform-agnostic: launch/stop a headless Creator + MCP client
    linux/         remote-linux (GenericLinux SSH) device + attach/debug on it
    SCENARIOS.md   catalog of build/run targets and how each is driven

## Build vs run scenarios

Two independent axes decide what a rig needs:

- **where you build** - on this host (`local`) or on the device itself
  (`remote`);
- **where you run / debug** - the target platform (linux, windows, macos,
  qnx, ...).

Name a scenario `build-<local|remote>-<buildplatform>-run-<runplatform>`, e.g.
`build-local-linux-run-linux` (host-built, run on a remote Linux device) or
`build-local-linux-run-qnx` (cross-compiled on Linux, run on a QNX target).
In Qt Creator terms these are just different values for the kit's **build
device**, **run device** and **toolchain**:

| scenario                            | build device | run device        | toolchain            |
|-------------------------------------|--------------|-------------------|----------------------|
| build-local-P-run-P (all local)     | Desktop      | Desktop           | host                 |
| build-local-P-run-P (remote device) | Desktop      | remote P device   | host (same ABI)      |
| build-remote-P-run-P                | the device   | the device        | on-device            |
| build-local-P-run-Q (cross)         | Desktop      | remote Q device   | cross-compiler for Q |

The script in `linux/` covers the common **build-local-linux-run-linux**
(remote device) cell. The other cells are the same recipe with different env
values - point the build/run device and toolchain at your target; the driving
(launch + MCP) is identical. Add a `<platform>/` directory when a target needs
its own device/toolchain seeding.

See `SCENARIOS.md` for a per-target catalog (linux, windows, macos, freebsd,
qnx, bare-metal, harmonyos) - device transport, debugger engine, and how each
is driven.

Real hosts, users and keys are always supplied via environment variables and
never committed: there is deliberately no registry of machines in the tree, so
each engineer points a scenario at their own device.

## The common rig

`common/launch-creator.sh` starts Xvfb and a throwaway Qt Creator with
`-load McpServer -mcp-port <PORT>`, waits until the port is listening, and
verifies the listener really is the process it started. `common/mcpcli.py`
sends MCP `tools/call` requests to it. `common/stop-creator.sh` kills only the
PIDs the launcher tracked.

    export QTC_DISPLAY=:9 QTC_MCP_PORT=8769          # unique to your instance!
    export QTC_SETTINGS=$PWD/.rig-run/settings       # throwaway config
    tests/manual/rigs/common/launch-creator.sh
    QTC_MCP_PORT=8769 tests/manual/rigs/common/mcpcli.py \
        'find_widgets:{"class_name":"QPushButton"}'
    tests/manual/rigs/common/stop-creator.sh

Environment variables (all optional, with defaults):

| variable        | meaning                          | default                 |
|-----------------|----------------------------------|-------------------------|
| `QTC_BIN`       | qtcreator executable             | `<repo>/bin/qtcreator`  |
| `QTC_DISPLAY`   | X display to create              | `:9`                    |
| `QTC_MCP_PORT`  | MCP port to bind / connect       | `8765`                  |
| `QTC_SETTINGS`  | throwaway `-settingspath`        | `$QTC_RUN_DIR/settings` |
| `QTC_SCREEN`    | Xvfb geometry                    | `1280x1024x24`          |
| `QTC_RUN_DIR`   | pid/log directory                | `./.rig-run`            |
| `QTC_EXTRA_ARGS`| extra qtcreator arguments        | empty                   |
| `QTC_MCP_HOST`  | host mcpcli.py connects to       | `127.0.0.1`             |

## Why the TCP port, not stdio

The plugin also speaks MCP over stdin/stdout (`-mcp-stdio`) - the transport an
interactive client uses when it spawns Qt Creator as a subprocess and owns the
pipe (e.g. Claude Code running `qtcreator -mcp-stdio`). These rigs use the TCP
port instead, because they fire discrete tool calls from *independent* shell
invocations against a Creator already running in the background: a listening
socket lets each call connect, do the small init handshake, send one
`tools/call`, and disconnect, while the state that matters (debug session, open
dialogs) lives in Creator and persists across those throwaway MCP sessions.
stdio would need a single long-lived client holding the pipe; bridging it to
independent shell calls through a FIFO would require a broker that
demultiplexes responses by id - reimplementing what the port gives for free.
So: `-mcp-stdio` for an interactive client, the port + `mcpcli.py` for
scripting.

## Rules to avoid stepping on other instances

Parallel Creators on one machine collide on X displays and MCP ports, which
silently mixes another session's windows into your captures or points your
client at their build. So:

- Pick a `QTC_DISPLAY` and `QTC_MCP_PORT` **unique to your instance**; never use
  `:0` (the real desktop). The launcher refuses a display whose socket exists
  and fails if the port ends up owned by a different process.
- Cleanup kills only the tracked PIDs. Do **not** `pkill -f qtcreator` /
  `pkill -f Xvfb` - that reaches everyone else's processes too.
- Launch once; avoid kill/relaunch cycles that can orphan windows.

## Dismissing first-run noise

Pre-seed the throwaway settings to suppress nags instead of clicking them away,
e.g. in `$QTC_SETTINGS/QtProject/QtCreator.ini`:

    [General]
    SuppressedWarnings=TakeUITour, LinkWithQtInstallation

## Prerequisites

`Xvfb`, `xdpyinfo`, `ss` and Python 3. The Creator build must include the
`McpServer` plugin.
