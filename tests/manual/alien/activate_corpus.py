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
LegalNoticeAccepted=true
ExtensionsDir={dir}
NodeJsPath={node}
EnabledExtensions="{ids}"
"""


# The languages nobody contributes because the editor is expected to have them
# already, against a suffix each is recognized by (or a whole file name, where
# that is what names the language). Mirrors the host's own table: an
# "onLanguage:python" cannot fire off a corpus that never mentions python.
BUILTIN_LANGUAGES = {
    "dockerfile": "Dockerfile",
    "c": ".c", "cpp": ".cpp", "cuda-cpp": ".cu", "objective-c": ".m",
    "objective-cpp": ".mm", "python": ".py", "java": ".java", "csharp": ".cs",
    "go": ".go", "rust": ".rs", "swift": ".swift", "lua": ".lua", "ruby": ".rb",
    "perl": ".pl", "php": ".php", "javascript": ".js", "typescript": ".ts",
    "json": ".json", "xml": ".xml", "yaml": ".yaml", "markdown": ".md",
    "html": ".html", "css": ".css", "shellscript": ".sh", "cmake": ".cmake",
    "qml": ".qml", "qdoc": ".qdoc", "plaintext": ".txt",
}


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

    def call(self, tool, arguments, timeout=180):
        text = self._rpc({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                          "params": {"name": tool, "arguments": arguments}},
                         timeout=timeout)
        for line in text.splitlines():
            line = line.strip()
            if line.startswith("data:"):
                line = line[5:].strip()
            if line.startswith("{"):
                answer = json.loads(line)
                if "error" in answer:
                    raise SweepError("the %s tool failed: %s"
                                     % (tool, json.dumps(answer["error"])))
                if "result" in answer:
                    result = answer["result"]
                    # A tool the server does not have is an error in the result
                    # rather than in the reply, and it looks like a no-op here.
                    if result.get("isError"):
                        raise SweepError("the %s tool failed: %s"
                                         % (tool, json.dumps(result)))
                    return result
        raise SweepError("the %s tool gave no answer" % tool)


def manifests(directory):
    """The package.json of every extension in the directory, by publisher.name."""
    found = {}
    for manifest in sorted(Path(directory).glob("*/package.json")):
        try:
            data = json.loads(manifest.read_text(encoding="utf-8", errors="replace"))
        except Exception:
            continue
        publisher, name = data.get("publisher"), data.get("name")
        if not publisher or not name:
            continue
        found["%s.%s" % (publisher, name)] = data
    return found


def activatable(all_manifests):
    """Those with a JS entry point: only they are activated at all."""
    return {i: d for i, d in all_manifests.items() if d.get("main")}


def language_files(all_manifests):
    """A file name per language id, to open so that "onLanguage:" fires.

    What a file's language is comes out of the manifests where one contributes
    it, and out of the editor's own table where none does. An id in neither
    cannot fire, and no name is written for it.
    """
    names = {i: ("language-%s%s" % (i, e) if e.startswith(".") else e)
             for i, e in BUILTIN_LANGUAGES.items()}
    for data in all_manifests.values():
        for language in data.get("contributes", {}).get("languages", []):
            identifier = language.get("id")
            # A contributed language wins: its own name is what the host will
            # report for the file, whatever the editor would have called it.
            if not identifier:
                continue
            for extension in language.get("extensions", []):
                names[identifier] = "language-%s%s" % (identifier, extension)
                break
            else:
                for filename in language.get("filenames", []):
                    names[identifier] = filename
                    break
    return names


def concrete_path(glob):
    """A path matching a "workspaceContains:" glob, to write into the workspace.

    The pattern is what the extension looks for, so the shortest thing matching
    it is what makes the event fire: a group collapses to its first branch, and
    the wildcards to a name of their own.
    """
    while "{" in glob:
        start = glob.index("{")
        end = glob.find("}", start)
        if end < 0:
            return ""
        glob = glob[:start] + glob[start + 1:end].split(",")[0] + glob[end + 1:]
    glob = glob.lstrip("/")
    glob = glob.replace("**/", "deep/").replace("**", "deep")
    glob = glob.replace("*", "sweep").replace("?", "x")
    # A directory of its own is not a file, and neither is an empty pattern.
    if not glob or glob.endswith("/") or ".." in glob:
        return ""
    return glob


def seed_workspace(workspace, all_manifests):
    """Write what the corpus waits for, and return the files to open.

    Without this only the extensions that activate unconditionally are ever
    asked, which is a handful of a real corpus: the rest wait for a language or
    for a file that says what kind of project this is.

    A "workspaceContains:" file only has to be there, so it is written and left
    alone. Opening it would put the sweep at the mercy of whatever editor the
    name pulls in - a designer form, a binary project format - and that is not
    what is being measured.
    """
    names = language_files(all_manifests)
    contained = set()
    languages = set()
    for data in activatable(all_manifests).values():
        for event in data.get("activationEvents", []):
            argument = event.split(":", 1)[1] if ":" in event else ""
            if event.startswith("onLanguage:"):
                name = names.get(argument)
                if name:
                    languages.add(name)
            elif event.startswith("workspaceContains:"):
                relative = concrete_path(argument)
                if relative:
                    contained.add(relative)

    def write(relative):
        path = workspace / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            path.write_text("activation sweep\n", encoding="utf-8")
        except OSError:
            return None
        return path

    for relative in sorted(contained - languages):
        write(relative)
    files = []
    for relative in ["sweep.txt"] + sorted(languages):
        path = write(relative)
        if path:
            files.append(path)
    return files


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

    all_manifests = manifests(args.extensions_dir)
    ids = activatable(all_manifests)
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
    to_open = seed_workspace(workspace, all_manifests)
    print("%d languages to open, in a workspace seeded for the "
          "\"workspaceContains:\" events" % len(to_open))

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
    try:
        client.initialize()
        for path in to_open:
            # Bounded: an extension can put a dialog up over the editor, and a
            # sweep that waits for somebody to answer it measures nothing.
            try:
                client.call("editor_open", {"path": str(path)}, timeout=60)
            except OSError as error:
                raise SweepError("opening %s did not come back (%s)" % (path, error))
        time.sleep(args.settle)
    finally:
        if child is not None and child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=30)
            except subprocess.TimeoutExpired:
                child.kill()

    results = []
    if child is not None:
        results = activations(log_path.read_text(encoding="utf-8", errors="replace"))
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
