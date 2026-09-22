# cdb dumpers: engine calls per Locals fetch

Measurement of what one `fetchVariables()` of the Python dumpers costs under
cdb, in three states - before the `cdb-lookups` series (the commits on top of
"Debugger: Ask CDB for a type name it can resolve"), at its tip, and with the
follow-up that records the layout of polymorphic classes and types a
dereferenced pointer from its vtable - and how to repeat it.

The unit is *calls into the debugger engine*: every method of `cdbext.Value`
and `cdbext.Type` is at least one dbgeng round trip, and the extension counts
them by engine method. The counts are deterministic for a given program state;
wall time depends on the machine and, on the first run, on the symbol server,
and varies by about a third between runs of the same thing on the same machine.

## Setup

| | |
|---|---|
| cdb | 10.0.26100.8249 AMD64 (Windows Kits 10), default symbol path `srv*`, cache warm |
| Qt of the debuggee | 6.11.2 msvc2022_64 (debug DLLs, `Qt6Cored.dll`) |
| compiler | MSVC 19.51.36256 (VS 18 Insiders), `vcvars64.bat`, qmake debug build |
| extension | `qtcreatorcdbext.dll` Debug build, Python 3.13 (`python313_d.dll`) |
| before | `cc85469fa64`, the parent of "Debugger: Ask CDB for a type name it can resolve", plus the counting instrumentation ported by hand and nothing else |
| series | `57fb52c5c70`, the tip of `cdb-lookups`: the six performance commits plus the instrumentation |
| + vtable | series plus the follow-up: `__vfptr` recorded in a struct's layout, a pointer's target typed from its vtable and RTTI locator instead of a cast expression in the symbol group |
| date | 2026-09-22 |

The instrumentation is the commit "CDB: Count the engine calls a fetch makes":
`countEngineCall("Method")` before every engine call in the Python bridge,
`EngineTimer` around the type search, `AddSymbol()` and `ExpandSymbol()`, and
`cdbext.takeEngineStatistics()`. The cdb bridge resets the counters when a
fetch starts and appends them to its result:

    result={data=[...],partial="0",timings=[],enginecalls={AddSymbol="42",
    AddSymbol_us="1304",ExpandSymbol="53",...,TypeSearch_us="225"},runtime="0.607"}

Keys ending in `_us` are microseconds spent inside the timed step, the others
are call counts. Only those three steps are timed; the cost of the other calls
shows in the runtime alone. The GUI does not read the field; it shows up in the
debugger log of Qt Creator and in the output of a manual cdb session.

## The debuggee

A console program holding the three things the series is about: many values
of one struct type (layout reuse, symbol group walks), pointers to QObjects
(type lookups by name, dynamic type) and a `QMap<QString, QVariant>` (the
QVariant dumper's inner-type lookups). The sizes are command line arguments,
so the same binary serves the small and the big run.

`main.cpp`:

```cpp
#include <qt_windows.h>
#include <QCoreApplication>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <cstdlib>

struct Foo
{
    int a;
    int b;
    QString s;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const int fooCount = argc > 1 ? atoi(argv[1]) : 1000;
    const int objectCount = argc > 2 ? atoi(argv[2]) : 200;
    QList<Foo> foos;
    for (int i = 0; i < fooCount; ++i)
        foos.append({i, 2 * i, QString::number(i)});
    QList<QObject *> objects;
    for (int i = 0; i < objectCount; ++i) {
        QObject *object = new QObject;
        object->setObjectName(QString("object%1").arg(i));
        objects.append(object);
    }
    QMap<QString, QVariant> map;
    for (int i = 0; i < 100; ++i)
        map.insert(QString::number(i), QVariant(i));
    int total = foos.size() + objects.size() + map.size();
    DebugBreak();
    return total;
}
```

`doit.pro` (what `tst_dumpers` writes for its cdb rows):

```
SOURCES = main.cpp
TARGET = doit
CONFIG -= app_bundle release
CONFIG += debug console utf8_source
QT -= widgets gui
QT += core
TARGET = ../doit
```

Build, from a directory holding the two files:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
C:\Qt\6.11.2\msvc2022_64\bin\qmake.exe
nmake
```

`TARGET = ../doit` lifts the executable out of qmake's `debug\` directory, so
it ends up next to `main.cpp` as `doit.exe`.

## The cdb session

The commands are the ones `tst_dumpers.cpp` sends for a cdb row, with two
differences: the `expanded` dictionary is too long for one cdb input line at
1200 entries, so it comes from a file, and the debuggee is named by its full
path, because this cdb version does not start a program named relative to its
working directory (`Cannot execute 'doit.exe ', Win32 error 0n2`).

One `expanded` file per size and scenario. A scenario expands one of the three
containers and every element in it; `all` expands all three, `none` nothing,
which is the cost of the locals themselves and what a container's own cost is
measured against:

```python
# make_expanded.py: python make_expanded.py
sizes = {'small': (100, 20, 100), 'big': (1000, 200, 100)}
for size, (foos, objects, entries) in sizes.items():
    parts = {
        'foos': ["'local.foos':10000"] + ["'local.foos.%d':100" % i for i in range(foos)],
        'objects': ["'local.objects':10000"] + ["'local.objects.%d':100" % i for i in range(objects)],
        'map': ["'local.map':10000"] + ["'local.map.%d':100" % i for i in range(entries)],
    }
    scenarios = dict(parts)
    scenarios['none'] = []
    scenarios['all'] = parts['foos'] + parts['objects'] + parts['map']
    for scenario, items in scenarios.items():
        open('expanded_%s_%s.py' % (size, scenario), 'w').write('expanded = {%s}\n' % ','.join(items))
```

`input.txt`, the script cdb reads from stdin (`.frame 1` because the stop is
inside `DebugBreak()`, one frame below `main`; `396034` is `0x060b02`, Qt
6.11.2):

```
.symopt+0x8000
!qtcreatorcdbext.script sys.path.insert(1, 'C:/dev/src/qc21/share/qtcreator/debugger')
!qtcreatorcdbext.script from cdbbridge import *
!qtcreatorcdbext.script theDumper = Dumper()
!qtcreatorcdbext.script theDumper.setupDumpers()
!qtcreatorcdbext.script exec(open('C:/path/to/expanded_small_all.py').read())
.frame 1
!qtcreatorcdbext.pid
!qtcreatorcdbext.script -t 42 theDumper.fetchVariables({'token':2,'fancy':1,'forcens':1,'autoderef':1,'dyntype':1,'passexceptions':0,'testing':1,'qobjectnames':1,'qtversion':396034,'qtnamespace':'','expanded':expanded})
q
```

Run (`100 20` for the small size, `1000 200` for the big one):

```bat
set _NT_DEBUGGER_EXTENSION_PATH=C:\dev\build\qc21\QtCreator-dev-Debug\lib\qtcreatorcdbext64
set PATH=C:\Qt\6.11.2\msvc2022_64\bin;%PATH%
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -aqtcreatorcdbext.dll -G -xn 0x4000001f -g C:\path\to\doit.exe 100 20 < input.txt > out.log 2>&1
```

For the "before" numbers the extension directory and the `sys.path.insert`
line point at a checkout of the base commit with the instrumentation applied,
built the same way; for the "series" numbers the `sys.path.insert` line points
at a checkout of the series tip and the extension is the current one, which
differs from the tip's only by a function the tip's bridge does not call.
Nothing else differs.

The result comes back in chunks, one line each, `<qtcreatorcdbext>|R|42|<n>|script|<payload>`,
and a field can be split across two of them. Concatenate the payloads in file
order before looking for `enginecalls=` and `runtime=`:

```python
import re
payload = ''.join(m.group(1) for m in re.finditer(
    r'^<qtcreatorcdbext>\|R\|42\|\d+\|script\|(.*)$', open('out.log').read(), re.M))
print(re.search(r'runtime="[^"]*"', payload).group())
print(re.search(r'enginecalls=\{[^}]*\}', payload).group())
```

## Results

### Small: `doit.exe 100 20`, everything expanded

100 `Foo`, 20 `QObject *`, 100 map entries, `expanded_small_all.py`.

| engine method | before | series | + vtable |
|---|---:|---:|---:|
| GetSymbolOffset | 35 922 | 360 | 87 |
| GetSymbolTypeName | 4 910 | 140 | 64 |
| GetSymbolParameters | 4 281 | 574 | 144 |
| GetSymbolEntryInformation | 2 455 | 70 | 32 |
| ReadVirtual | 1 937 | 1 990 | 1 973 |
| GetSymbolName | 892 | 184 | 68 |
| GetTypeSize | 591 | 36 | 36 |
| GetModuleNameString | 581 | 178 | 140 |
| GetSymbolValueText | 410 | 14 | 14 |
| GetSymbolSize | 405 | 70 | 32 |
| GetNumberSymbols | 311 | 75 | 17 |
| ExpandSymbol | 190 | 53 | 14 |
| GetStackTrace | 142 | 43 | 4 |
| AddSymbol | 141 | 42 | 3 |
| SetExpressionSyntax | 42 | 42 | 2 |
| GetExpressionSyntax | 21 | 21 | 1 |
| GetTypeId | 4 | 7 | 7 |
| GetFieldName | 5 | 5 | 5 |
| GetTypeName | 4 | 4 | 4 |
| GetSymbolTypeId | 4 | 1 | 1 |
| GetNameByOffset | 0 | 0 | 2 |
| GetFieldTypeAndOffset | 2 | 2 | 2 |
| GetNumberModules | 1 | 1 | 1 |
| GetOffsetByName | 1 | 1 | 1 |
| GetScopeSymbolGroup2 | 1 | 1 | 1 |
| **engine calls, total** | **53 253** | **3 914** | **2 655** |
| TypeSearch_us | 355 | 185 | 176 |
| AddSymbol_us | 6 844 | 1 231 | 278 |
| ExpandSymbol_us | 39 690 | 35 928 | 29 839 |
| **fetch runtime** | **2.91 s** | **0.42 s** | **0.22 s** |

Before to series: 13.6 times fewer engine calls, 6.9 times faster. Series to
the follow-up: 1.5 times fewer calls, 1.9 times faster. A second run of each
gave 2.96 s, 0.44 s and 0.22 s.

### Small: one container at a time

The same program, one scenario file each. The `none` row is the fetch with
nothing expanded; the container rows are that scenario minus `none`, so each
is what expanding that one container and all its elements costs on top of the
locals themselves.

| expanded | before | series | + vtable |
|---|---:|---:|---:|
| nothing | 926 calls, 0.05 s | 483 calls, 0.07 s | 483 calls, 0.07 s |
| the 100 `Foo` | 33 801 calls, 0.88 s | 903 calls, 0.05 s | 903 calls, 0.05 s |
| the 20 `QObject *` | 10 009 calls, 0.79 s | 2 056 calls, 0.25 s | 798 calls, 0.05 s |
| the 100 map entries | 517 calls, 0.03 s | 520 calls, 0.03 s | 520 calls, 0.03 s |

The series is about the `Foo` values: 37 times fewer calls for them. It takes
the `QObject *` down by 5 times, and the follow-up by another 2.6; what is left
per object is ReadVirtual and the QObject dumper's Python. The map never
changes: the `QMap<QString, QVariant>` dumper reads keys and values from memory
and makes no symbol group values, so there is neither a scan to index nor a
layout to reuse. Its only share in the before-to-series difference is the
locals themselves, the `nothing` row.

### Big: `doit.exe 1000 200`, everything expanded

1000 `Foo`, 200 `QObject *`, 100 map entries, `expanded_big_all.py`.

| | before | series | + vtable |
|---|---|---:|---:|
| fetch runtime | no result: killed after 1307 s inside `fetchVariables()`, cdb at 17.9 GB and growing (run of 2026-09-21, not repeated) | 33.9 s | 0.86 s |
| engine calls | - | 28 387 | 15 789 |
| ReadVirtual | - | 15 303 | 15 107 |
| GetSymbolParameters | - | 4 354 | 144 |
| GetSymbolOffset | - | 2 700 | 87 |
| GetSymbolName | - | 1 264 | 68 |
| GetSymbolTypeName | - | 860 | 64 |
| GetNumberSymbols | - | 615 | 17 |
| ExpandSymbol | - | 413 | 14 |
| AddSymbol | - | 402 | 3 |
| GetStackTrace | - | 403 | 4 |
| SetExpressionSyntax | - | 402 | 2 |
| GetExpressionSyntax | - | 201 | 1 |
| ExpandSymbol_us | - | 78 640 | 26 771 |
| AddSymbol_us | - | 18 108 | 126 |
| TypeSearch_us | - | 266 | 165 |

The two results describe the same data: the payloads are identical apart from
addresses and the statistics, 200 objects and 1000 `Foo` with their members.

## Which change removes what

Read against the small table.

- **GetSymbolOffset 35 922 to 360, GetNumberSymbols 311 to 75** - "CDB: Find
  the symbol for an address without a scan". `createValue()` walked the whole
  symbol group, `GetSymbolOffset()` and `GetSymbolParameters()` per symbol, for
  every value a dumper made up, and the group grows with every value. That
  walk is the quadratic term that never finishes in the big run before the
  series. The index per symbol group asks each symbol once.
- **GetSymbolTypeName 4 910 to 140, GetSymbolEntryInformation 2 455 to 70,
  GetSymbolParameters 4 281 to 574** - "Debugger: Ask cdb about a value's type
  once" and the `PyValue::type()` memo of "CDB: Walk a value's children once".
  `fromNativeValue()` asked `type()` seven times per value; each answer was
  built anew from `GetSymbolParameters()`, `GetSymbolTypeName()` twice and
  `GetSymbolEntryInformation()`.
- **GetTypeSize 591 to 36, GetModuleNameString 581 to 178** - the
  `from_native_type()` memo of the same commit: name, size and module of a
  type were re-derived for every value of it.
- **GetSymbolName 892 to 184, GetSymbolValueText 410 to 14, GetSymbolSize 405
  to 70, ExpandSymbol 190 to 53, AddSymbol 141 to 42, GetStackTrace 142 to
  43** - "Debugger: Reuse a struct's layout for the next cdb value of its
  type". After the first `Foo` is listed from the symbol group, the other 99
  get their members out of memory: no `AddSymbol()` cast per element, no
  expansion, no walk over the children. `ReadVirtual` stays flat because the
  bytes are read either way.
- **GetSymbolTypeId 4 to 1, TypeSearch_us 355 to 185** - "CDB: Look a type up
  where it is likely to be". Lookups go to the module of the value being
  dumped first; with symbols loaded and the cache warm the search was already
  cheap here. The change matters for a miss on a process with many modules
  and deferred symbols, which this program does not have.
- **AddSymbol 42 to 3, ExpandSymbol 53 to 14, GetStackTrace 43 to 4,
  Get/SetExpressionSyntax 21/42 to 1/2, GetSymbolParameters 574 to 144,
  GetSymbolOffset 360 to 87** - the follow-up, all of it on the 20 objects.
  Each `QObject *` a dumper made up cost a cast expression added to the
  symbol group (`AddSymbol`, the expression syntax switched twice around it,
  `GetStackTrace` to find the group), its expansion for the `__vtcast_` probe
  (`hasChildren()`), and then a second symbol for the object itself with its
  own expansion and child walk - three calls of about 6 ms each in a profile
  of the fetch, 69% of its time. The layout of `QObject` was never recorded
  because cdb reports the `__vfptr` child as the vtable itself, sitting in the
  module, which the layout check took for a member outside the struct; the
  follow-up records it as the pointer slot at offset 0 where memory holds the
  table's address there. The dynamic type comes from the vtable the object
  holds at offset 0 - the name of the symbol at that address, if one sits
  there exactly - and where in the complete object the pointee lies from the
  table's RTTI locator, which is what cdb's `__vtcast_` is computed from as
  well. The first object still goes through the symbol group once, so its
  layout can be recorded; the other 19 come out of memory.
- `ExpandSymbol_us` falls less than the count (39.7, 35.9, 29.8 ms for 190,
  53, 14 expansions): the expansions that remain are of larger symbols, the
  lists themselves and the first `QObject`.

In the big run the objects were the bulk of the 33.9 s, not the `Foo` values
and not `ReadVirtual`: 200 objects put some 3000 symbols into one group, and
the untimed calls on it - `GetSymbolParameters` 4 354, `GetSymbolOffset` 2 700,
the expansions' own walks - grow with the group. Without them the fetch takes
0.86 s with the same 15 000 `ReadVirtual`.

## Caveats

- **Cold symbol cache.** The first run of a scenario on a machine fetches
  PDBs from the Microsoft symbol server (`srv*`); `tst_dumpers` row `QVariant1`
  took 15 s cold and 2.5 s warm with the same code. Compare warm runs only.
- **cdb and relative paths.** cdb 10.0.26100 does not start `-g doit.exe`;
  `tst_dumpers` got a one-line fix for it ("Debugger tests: Name the debuggee
  to cdb by its full path").
- **What the layout reuse cannot see.** A bitfield alone in its storage unit
  while the neighbouring bits happen to be zero when the layout is recorded.
  Members sharing storage, a printed value that differs from memory, enum
  members and `__vtcast_` children all keep the type on the symbol group path,
  as does a `__vfptr` whose table is not what the object holds at offset 0.
- **One name for all of a class's vtables.** dbgeng undecorates
  `??_7Derived@@6BBase1@@@` and `??_7Derived@@6BBase2@@@` both to
  ``Derived::`vftable'``; the ``{for `Base2'}`` never shows. A vtable's symbol
  therefore names the class but not the subobject, and `tst_dumpers` row
  `Bug17823` - a `Base2 *` into the middle of a `Derived` - is what catches a
  bridge that assumes the primary table. The subobject's offset is in the RTTI
  locator the slot before the table points to; a table with a constructor
  displacement (virtual bases) or without a valid locator goes back to the
  symbol group's `__vtcast_`.
- **Where the rest of the time goes.** With the follow-up the big run is
  0.86 s for 15 107 `ReadVirtual` (one read per member, the `QString` dumper
  reads again for each string) and Python; the engine calls that scale with
  the number of values are gone.
- **`tst_debugger_dumpers` writes nothing to a redirected stdout.** Run it
  with `-o <file>,txt`; a plain `> log` ends with the QML debugging banner and
  no test output at all.

## Regression check

`tst_debugger_dumpers` against cdb, same machine, with the follow-up: Struct,
Bitfields, Union, AnonymousStruct, Inheritance, Gdb13393, Bug17823, Bug6933,
QObject1-3, QVariant1/2/4, QVariantMap/Hash, QList, QMap, QHash, QSet,
StdUniquePtr, StdSharedPtr, StdMap, StdVector: 23 rows passed, 0 failed,
Gdb13393 skipped by the test for cdb. The other cdb rows whose code has
virtual functions, QSharedPointer, StdList, StdSharedPtr2 and Internal4, pass
as well; WriteMemory is skipped by the test for cdb. Run with

```
QTC_DEBUGGER_PATH_FOR_TEST=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe
QTC_QMAKE_PATH_FOR_TEST=C:/Qt/6.11.2/msvc2022_64/bin/qmake.exe
QTC_MSVC_ENV_BAT=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat
QTC_CDBEXT_PATH=<build>\lib\qtcreatorcdbext64
tst_debugger_dumpers.exe -o results.txt,txt dumper:Struct dumper:Bitfields ...
```

with the DLLs of the Qt that Qt Creator was built against, the Creator `bin`
and `lib\qtcreator\plugins` directories on `PATH`. The debuggee's Qt is found
through `QTC_QMAKE_PATH_FOR_TEST`.
