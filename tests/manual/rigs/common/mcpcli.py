#!/usr/bin/env python3
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

"""Tiny stdlib-only client for the McpServer plugin's HTTP/SSE endpoint.

Drives a running Qt Creator that was started with
"-load McpServer -mcp-port <PORT>" (see launch-creator.sh). No third-party
deps so it runs anywhere Python 3 does.

Environment:
    QTC_MCP_HOST   host to connect to (default 127.0.0.1)
    QTC_MCP_PORT   port the McpServer is listening on (default 8765)

Usage:
    mcpcli.py 'tool_name:{"arg":"value"}' ['another_tool:{...}'] ...

Each argument is one tool call: the part before the first ':' is the tool
name, the rest (if any) is a JSON object of arguments. Calls run in order
in a single MCP session; the concise result of each is printed.
"""

import sys, json, http.client, os

HOST = os.environ.get("QTC_MCP_HOST", "127.0.0.1")
PORT = int(os.environ.get("QTC_MCP_PORT", "8765"))


def _rpc(sid, method, params, mid):
    conn = http.client.HTTPConnection(HOST, PORT, timeout=120)
    body = {"jsonrpc": "2.0", "method": method, "params": params}
    if mid is not None:
        body["id"] = mid
    headers = {"Content-Type": "application/json",
               "Accept": "application/json, text/event-stream"}
    if sid:
        headers["mcp-session-id"] = sid
    conn.request("POST", "/", json.dumps(body), headers)
    resp = conn.getresponse()
    new_sid = resp.getheader("mcp-session-id") or sid
    raw = resp.read().decode("utf-8", "replace")
    conn.close()
    data = None
    if raw.strip().startswith("{"):
        data = json.loads(raw)
    else:
        # server-sent events: pick the first "data:" line
        for line in raw.splitlines():
            line = line.strip()
            if line.startswith("data:"):
                data = json.loads(line[5:].strip())
    return new_sid, data


def session():
    sid, _ = _rpc(None, "initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "mcpcli", "version": "1"}}, 1)
    _rpc(sid, "notifications/initialized", {}, None)
    return sid


def call(sid, name, args, mid):
    _, data = _rpc(sid, "tools/call", {"name": name, "arguments": args}, mid)
    return data


if __name__ == "__main__":
    sid = session()
    mid = 10
    for arg in sys.argv[1:]:
        name, _, a = arg.partition(":")
        args = json.loads(a) if a else {}
        res = call(sid, name, args, mid)
        mid += 1
        out = res
        try:
            r = res["result"]
            out = r.get("structuredContent", r.get("content", r))
        except Exception:
            pass
        print("### %s(%s)" % (name, a))
        print(json.dumps(out, indent=1)[:4000])
