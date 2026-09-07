#!/usr/bin/env python3
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

"""Measures what an unresolved cdb breakpoint costs per loaded module.

Generates an application that loads a configurable number of DLLs, then
times cdb.exe starting it with file-and-line breakpoints that are either
plain or scoped to the module the source file was built into.

Needs a Visual Studio environment (cl, link and ninja on PATH) and
cdb.exe from the Windows SDK.
"""

import argparse
import os
import re
import subprocess
import sys
import time

STATEMENTS = 24
FIRST_BREAK_LINE = 4


def generate(directory, dll_count):
    """Writes the fixture sources and a ninja file building them."""
    ninja = ["rule cc",
             "  command = cl /nologo /c /Z7 /Od /Fo$out $in",
             "  description = CC $out",
             "",
             "rule dll",
             "  command = link /nologo /DLL /DEBUG /OUT:$out $in",
             "  description = DLL $out",
             "",
             "rule exe",
             "  command = link /nologo /DEBUG /OUT:$out $in",
             "  description = EXE $out",
             ""]

    for i in range(dll_count):
        body = ["extern \"C\" int work_%03d(int x)" % i, "{", "    int v = x;"]
        for s in range(STATEMENTS):
            body.append("    v = v + %d;" % (s + 1))
        body += ["    return v;",
                 "}",
                 "",
                 "extern \"C\" __declspec(dllexport) int entry_%03d(int x)" % i,
                 "{",
                 "    return work_%03d(x);" % i,
                 "}"]
        name = "dll_%03d" % i
        write(os.path.join(directory, name + ".cpp"), body)
        # A drive letter's colon has to be escaped in a ninja path. The
        # sources are named absolutely so that the pdb records the same
        # path that the breakpoint expression uses.
        source = os.path.join(directory, name + ".cpp").replace(":", "$:")
        ninja.append("build obj/%s.obj: cc %s" % (name, source.replace("\\", "/")))
        ninja.append("build %s.dll: dll obj/%s.obj" % (name, name))
        ninja.append("")

    main = ["#include <windows.h>",
            "#include <stdio.h>",
            "",
            "int main()",
            "{",
            "    int total = 0;",
            "    for (int i = 0; i < %d; ++i) {" % dll_count,
            "        char name[64];",
            "        sprintf_s(name, \"dll_%03d.dll\", i);",
            "        HMODULE handle = LoadLibraryA(name);",
            "        if (!handle) {",
            "            printf(\"cannot load %s\\n\", name);",
            "            return 1;",
            "        }",
            "        char symbol[64];",
            "        sprintf_s(symbol, \"entry_%03d\", i);",
            "        typedef int (*Entry)(int);",
            "        Entry entry = (Entry) GetProcAddress(handle, symbol);",
            "        if (entry)",
            "            total += entry(i);",
            "    }",
            "    printf(\"total=%d\\n\", total);",
            "    return 0;",
            "}"]
    write(os.path.join(directory, "main.cpp"), main)
    source = os.path.join(directory, "main.cpp").replace(":", "$:")
    ninja.append("build obj/main.obj: cc %s" % source.replace("\\", "/"))
    ninja.append("build bpapp.exe: exe obj/main.obj")
    ninja.append("")
    ninja.append("default bpapp.exe " + " ".join("dll_%03d.dll" % i
                                                 for i in range(dll_count)))
    write(os.path.join(directory, "build.ninja"), ninja)


def write(path, lines):
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def breakpoints(directory, count, dll_count, scoped):
    """Breakpoints in the last loaded module, so that they stay unresolved
    for the whole load sequence - the case a large application hits."""
    module = "dll_%03d" % (dll_count - 1)
    source = os.path.join(directory, module + ".cpp")
    result = []
    for line in range(FIRST_BREAK_LINE, FIRST_BREAK_LINE + count):
        location = "%s!%s:%d" % (module, source, line) if scoped \
            else "%s:%d" % (source, line)
        result.append("bu `%s`" % location)
    return result


def measure(cdb, directory, bps, repeats):
    commands = ";".join(bps + ["g", "bl", "q"])
    environment = dict(os.environ, _NT_SYMBOL_PATH=directory)
    times = []
    resolved = 0
    hit = False
    for _ in range(repeats):
        start = time.time()
        process = subprocess.run([cdb, "-c", commands,
                                  os.path.join(directory, "bpapp.exe")],
                                 cwd=directory, env=environment,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT)
        times.append(time.time() - start)
        output = process.stdout.decode("latin-1")
        resolved = len(re.findall(r"^\s*\d+ e ", output, re.MULTILINE))
        hit = "Breakpoint 0 hit" in output
    return times, resolved, hit


def main():
    if os.name != "nt":
        sys.exit("cdb runs on Windows only.")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", default=os.path.join(os.getcwd(), "cdbbp"),
                        help="where to generate and build the fixture")
    parser.add_argument("--dlls", type=int, default=200,
                        help="number of dynamically loaded libraries")
    parser.add_argument("--breakpoints", default="1,10,40",
                        help="comma separated breakpoint counts to measure")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--cdb", default=r"C:\Program Files (x86)\Windows Kits"
                                         r"\10\Debuggers\x64\cdb.exe")
    parser.add_argument("--keep", action="store_true",
                        help="reuse an already generated fixture")
    arguments = parser.parse_args()

    if not os.path.exists(arguments.cdb):
        sys.exit("no cdb.exe at %s, pass --cdb" % arguments.cdb)

    directory = os.path.abspath(arguments.directory)
    if not arguments.keep:
        os.makedirs(directory, exist_ok=True)
        generate(directory, arguments.dlls)
        if subprocess.call("ninja", cwd=directory, shell=True) != 0:
            sys.exit("building the fixture failed; is this a VS environment?")

    counts = [int(c) for c in arguments.breakpoints.split(",")]
    if max(counts) > STATEMENTS:
        sys.exit("at most %d breakpoints fit into the generated function"
                 % STATEMENTS)

    print("%d modules, %d repeats, times in seconds"
          % (arguments.dlls, arguments.repeats))
    print("%-14s %-22s %s" % ("breakpoints", "state", "runs"))
    report(arguments, directory, "none", [])
    for count in counts:
        for scoped in (False, True):
            bps = breakpoints(directory, count, arguments.dlls, scoped)
            label = "%d %s" % (count, "scoped" if scoped else "plain")
            report(arguments, directory, label, bps)


def report(arguments, directory, label, bps):
    times, resolved, hit = measure(arguments.cdb, directory, bps,
                                   arguments.repeats)
    state = "resolved=%d hit=%s" % (resolved, "yes" if hit else "no") if bps \
        else "-"
    print("%-14s %-22s %s" % (label, state,
                              " ".join("%.2f" % t for t in times)))


if __name__ == "__main__":
    main()
