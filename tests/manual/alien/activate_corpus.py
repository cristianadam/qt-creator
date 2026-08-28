#!/usr/bin/env python3
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
"""Activate every VS Code extension in a directory and report which ones fail.

An extension that reaches for a part of the API the host does not have dies
while it is being activated - a TypeError from its own bundle, before it has
done anything. Nothing in Qt Creator's interface says so: the extension simply
is not there. The only trace is the activate request coming back as an error
over the host connection, which this reads out.

That is how the missing OverviewRulerLane enum was found: cortex-debug threw in
a constructor and never started, and its debugger was therefore never offered.
Run this over an extensions directory to find the same class of gap in bulk,
rather than one extension at a time.

The corpus is whatever is installed on the machine, so this is a measurement
rather than a test: it cannot assert a fixed number of extensions activate.
What it does give is a list, and a non-zero exit when anything on it failed.

Usage:
    activate_corpus.py --qtcreator ../../../bin/qtcreator \\
                       --extensions-dir /path/to/vscode-extensions

Run it under whatever DISPLAY is set up (Xvfb for headless use); it does not
manage the display itself.
"""

import argparse
import http.client
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SETTINGS = """[General]
SuppressedWarnings=TakeUITour, LinkWithQtInstallation
[Alien]
Enable=true
ExtensionsDir={dir}
NodeJsPath={node}
EnabledExtensions="{ids}"
"""


class SweepError(Exception):
    pass


class McpClient:
    """Minimal MCP streamable-HTTP client for Qt Creator's McpServer."""

    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.session_id = None

    def _rpc(self, obj, timeout=180):
        conn = http.client.HTTPConnection(self.host, self.port, timeout=timeout)
        headers = {"Content-Type": "application/json",
                   "Accept": "application/json, text/event-stream"}
        if self.session_id:
            headers["mcp-session-id"] = self.session_id
        body = json.dumps(obj).encode()
        headers["Content-Length"] = str(len(body))
        conn.request("POST", "/", body, headers)
        resp = conn.getresponse()
        sid = resp.getheader("mcp-session-id")
        if sid:
            self.session_id = sid
        text = resp.read().decode(errors="replace")
        conn.close()
        return text

    def initialize(self):
        self._rpc({"jsonrpc": "2.0", "id": 0, "method": "initialize",
                   "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                              "clientInfo": {"name": "activate_corpus", "version": "1"}}})
        if not self.session_id:
            raise SweepError("the MCP server did not hand out a session")
        self._rpc({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def call(self, tool, arguments):
        text = self._rpc({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                          "params": {"name": tool, "arguments": arguments}})
        for line in text.splitlines():
            line = line.strip()
            if line.startswith("data:"):
                line = line[5:].strip()
            if line.startswith("{"):
                answer = json.loads(line)
                if "result" in answer:
                    return answer["result"]
        return {}


def extension_ids(directory):
    """The publisher.name of every extension in the directory, newest first."""
    ids = {}
    for manifest in sorted(Path(directory).glob("*/package.json")):
        try:
            data = json.loads(manifest.read_text(encoding="utf-8", errors="replace"))
        except Exception:
            continue
        publisher, name = data.get("publisher"), data.get("name")
        if not publisher or not name:
            continue
        # Only extensions with a JS entry point are activated at all.
        if not data.get("main"):
            continue
        ids["%s.%s" % (publisher, name)] = manifest.parent
    return ids


def wait_for_port(host, port, timeout, child=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            conn = http.client.HTTPConnection(host, port, timeout=2)
            conn.connect()
            conn.close()
            return True
        except OSError:
            if child is not None and child.poll() is not None:
                raise SweepError("Qt Creator exited before the MCP port opened "
                                 "(exit {}).".format(child.returncode))
            time.sleep(0.2)
    return False


def activations(log_text):
    """What each activate request was answered with: id -> None or the error.

    The log quotes each message as a Qt debug string, so the JSON in it arrives
    escaped. Unescaping first is what makes the patterns below match at all -
    against the raw text they find nothing, and nothing is indistinguishable
    from an extension that was never asked.
    """
    log_text = log_text.replace('\\"', '"')
    requested = {}
    for match in re.finditer(r'\{"id":(\d+),"jsonrpc":"2\.0","method":"activate",'
                             r'"params":\{"id":"([^"]+)"', log_text):
        requested[match.group(1)] = match.group(2)
    failed = {}
    for match in re.finditer(r'\{"id":(\d+),"error":\{"code":-?\d+,"message":"(.*?)(?<!\\)"',
                             log_text):
        failed[match.group(1)] = match.group(2)
    return [(name, failed.get(seq)) for seq, name in sorted(requested.items(),
                                                            key=lambda kv: int(kv[0]))]


def main():
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--extensions-dir", required=True,
                        help="Directory holding the unpacked extensions")
    parser.add_argument("--qtcreator", help="Qt Creator to launch (otherwise --port is used)")
    parser.add_argument("--port", type=int, default=8766, help="MCP port (default 8766)")
    parser.add_argument("--node", default="/usr/bin/node", help="Node.js for the host")
    parser.add_argument("--settle", type=float, default=25.0,
                        help="Seconds to let activations finish (default 25)")
    parser.add_argument("--keep", action="store_true", help="Keep the log and settings")
    args = parser.parse_args()

    ids = extension_ids(args.extensions_dir)
    if not ids:
        raise SweepError("no extension with a JS entry point in %s" % args.extensions_dir)
    print("%d extensions to activate from %s" % (len(ids), args.extensions_dir))

    work = tempfile.mkdtemp(prefix="alien-sweep-")
    settings = Path(work) / "settings"
    (settings / "QtProject").mkdir(parents=True)
    (settings / "QtProject" / "QtCreator.ini").write_text(
        SETTINGS.format(dir=args.extensions_dir, node=args.node,
                        ids="\\n".join(sorted(ids))), encoding="utf-8")

    # Activation waits for a workspace folder, so there has to be something
    # open. A folder of its own keeps the sweep away from any real project.
    workspace = Path(work) / "workspace"
    workspace.mkdir()
    (workspace / "sweep.txt").write_text("activation sweep\n", encoding="utf-8")

    log_path = Path(work) / "creator.log"
    child = None
    if args.qtcreator:
        env = dict(os.environ)
        env["QT_LOGGING_RULES"] = "qtc.alien.host.debug=true"
        with open(log_path, "wb") as log:
            child = subprocess.Popen(
                [args.qtcreator, "-load", "Alien", "-load", "McpServer",
                 "-mcp-port", str(args.port), "-settingspath", str(settings)],
                stdout=log, stderr=subprocess.STDOUT, env=env)
    if not wait_for_port("127.0.0.1", args.port, 120, child):
        raise SweepError("no MCP server answered on port %d" % args.port)

    client = McpClient("127.0.0.1", args.port)
    client.initialize()
    client.call("open_file", {"path": str(workspace / "sweep.txt")})
    time.sleep(args.settle)

    results = []
    if child is not None:
        results = activations(log_path.read_text(encoding="utf-8", errors="replace"))
        child.terminate()
        try:
            child.wait(timeout=30)
        except subprocess.TimeoutExpired:
            child.kill()
    else:
        print("attached to a running Creator: read its own log for the activations")

    ok = [name for name, error in results if not error]
    bad = [(name, error) for name, error in results if error]
    print("\nactivated: %d" % len(ok))
    for name in ok:
        print("  ok    %s" % name)
    print("failed: %d" % len(bad))
    for name, error in bad:
        print("  FAIL  %s\n          %s" % (name, error[:200]))
    never = sorted(set(ids) - {name for name, _ in results})
    if never:
        print("never asked to activate: %d" % len(never))
        for name in never:
            print("  ----  %s" % name)

    if args.keep:
        print("\nkept: %s" % work)

    # Nothing activated is a broken sweep rather than a clean one: the host did
    # not start, no extension had its activation event, or the log was not read.
    if child is not None and not results:
        raise SweepError("no extension was asked to activate; nothing was measured")
    return 1 if bad else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SweepError as error:
        print("error: %s" % error, file=sys.stderr)
        sys.exit(2)
