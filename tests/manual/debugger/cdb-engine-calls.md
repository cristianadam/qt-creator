# cdb dumpers: engine calls per Locals fetch

Measurement of what one `fetchVariables()` of the Python dumpers costs under
cdb, before and after the `cdb-lookups` series (the commits on top of
"Debugger: Ask CDB for a type name it can resolve"), and how to repeat it.

The unit is *calls into the debugger engine*: every method of `cdbext.Value`
and `cdbext.Type` is at least one dbgeng round trip, and the extension counts
them by engine method. The counts are deterministic for a given program state;
wall time depends on the machine and, on the first run, on the symbol server.

## Setup

| | |
|---|---|
| cdb | 10.0.26100.6584 AMD64 (Windows Kits 10), default symbol path `srv*`, cache warm |
| Qt of the debuggee | 6.11.1 msvc2022_64 (debug DLLs, `Qt6Cored.dll`) |
| compiler | MSVC 19.51 (VS 18 Enterprise), `vcvars64.bat`, qmake debug build |
| extension | `qtcreatorcdbext.dll` Debug build, Python 3.13 (`python313_d.dll`) |
| before | `d018cd76fc8` plus the counting instrumentation and nothing else |
| after | `cdb-lookups` tip, i.e. the six performance commits plus the instrumentation |
| date | 2026-09-21 |

The instrumentation is the commit "CDB: Count the engine calls a fetch makes":
`countEngineCall("Method")` before every engine call in the Python bridge,
`EngineTimer` around the type search, `AddSymbol()` and `ExpandSymbol()`, and
`cdbext.takeEngineStatistics()`. The cdb bridge resets the counters when a
fetch starts and appends them to its result:

    result={data=[...],partial="0",timings=[],enginecalls={AddSymbol="42",
    AddSymbol_us="1304",ExpandSymbol="53",...,TypeSearch_us="225"},runtime="0.607"}

Keys ending in `_us` are microseconds spent inside the timed step, the others
are call counts. The GUI does not read the field; it shows up in the debugger
log of Qt Creator and in the output of a manual cdb session.

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
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
C:\Qt\6.11.1\msvc2022_64\bin\qmake.exe
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

`expanded.py` asks for every element of the two lists and the map to be
expanded, which is what makes each `Foo` and each `QObject` get its members
listed:

```python
# make_expanded.py: python make_expanded.py
foos, objects = 1000, 200
entries = ["'local.foos':10000", "'local.objects':10000", "'local.map':10000"]
entries += ["'local.foos.%d':100" % i for i in range(foos)]
entries += ["'local.objects.%d':100" % i for i in range(objects)]
open('expanded.py', 'w').write('expanded = {%s}\n' % ','.join(entries))
```

`input.txt`, the script cdb reads from stdin (`.frame 1` because the stop is
inside `DebugBreak()`, one frame below `main`; `396033` is `0x060b01`, Qt
6.11.1):

```
.symopt+0x8000
!qtcreatorcdbext.script sys.path.insert(1, 'C:/dev/src/qc21/share/qtcreator/debugger')
!qtcreatorcdbext.script from cdbbridge import *
!qtcreatorcdbext.script theDumper = Dumper()
!qtcreatorcdbext.script theDumper.setupDumpers()
!qtcreatorcdbext.script exec(open('C:/path/to/expanded.py').read())
.frame 1
!qtcreatorcdbext.pid
!qtcreatorcdbext.script -t 42 theDumper.fetchVariables({'token':2,'fancy':1,'forcens':1,'autoderef':1,'dyntype':1,'passexceptions':0,'testing':1,'qobjectnames':1,'qtversion':396033,'qtnamespace':'','expanded':expanded})
q
```

Run:

```bat
set _NT_DEBUGGER_EXTENSION_PATH=C:\dev\build\qc21\QtCreator-dev-Debug\lib\qtcreatorcdbext64
set PATH=C:\Qt\6.11.1\msvc2022_64\bin;%PATH%
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -aqtcreatorcdbext.dll -G -xn 0x4000001f -g C:\path\to\doit.exe 1000 200 < input.txt > out.log 2>&1
```

For the "before" numbers the extension directory and the `sys.path.insert`
line point at a checkout of the base commit with the instrumentation applied;
nothing else differs.

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

### Small: `doit.exe 100 20`

100 `Foo`, 20 `QObject *`, 100 map entries, everything expanded.

| engine method | before | after |
|---|---:|---:|
| GetSymbolOffset | 35 922 | 360 |
| GetSymbolTypeName | 4 910 | 140 |
| GetSymbolParameters | 4 281 | 574 |
| GetSymbolEntryInformation | 2 455 | 70 |
| ReadVirtual | 1 937 | 1 990 |
| GetModuleNameString | 1 096 | 180 |
| GetSymbolName | 892 | 184 |
| GetTypeSize | 594 | 36 |
| GetSymbolValueText | 410 | 14 |
| GetSymbolSize | 405 | 70 |
| GetNumberSymbols | 311 | 75 |
| ExpandSymbol | 190 | 53 |
| GetStackTrace | 142 | 43 |
| AddSymbol | 141 | 42 |
| SetExpressionSyntax | 42 | 42 |
| GetExpressionSyntax | 21 | 21 |
| GetSymbolTypeId | 7 | 1 |
| GetTypeId | 7 | 7 |
| GetFieldName | 5 | 5 |
| GetTypeName | 4 | 4 |
| GetFieldTypeAndOffset | 2 | 2 |
| GetNumberModules | 1 | 1 |
| GetOffsetByName | 1 | 1 |
| GetScopeSymbolGroup2 | 1 | 1 |
| **engine calls, total** | **53 777** | **3 916** |
| TypeSearch_us | 921 | 225 |
| AddSymbol_us | 8 311 | 1 304 |
| ExpandSymbol_us | 48 562 | 71 836 |
| **fetch runtime** | **3.86 s** | **0.61 s** |

13.7 times fewer engine calls, 6.3 times faster.

### Big: `doit.exe 1000 200`

1000 `Foo`, 200 `QObject *`, 100 map entries, everything expanded.

| | before | after |
|---|---|---|
| fetch runtime | no result: killed after 1307 s inside `fetchVariables()`, cdb at 17.9 GB and growing | 43.8 s |
| engine calls | - | 28 356 |
| TypeSearch_us | - | 1 019 |
| AddSymbol | - | 402 |
| ExpandSymbol | - | 413 |
| GetSymbolParameters | - | 4 348 |
| ReadVirtual | - | 15 300 |

## Which change removes what

Read against the small table.

- **GetSymbolOffset 35 922 to 360, GetNumberSymbols 311 to 75** - "CDB: Find
  the symbol for an address without a scan". `createValue()` walked the whole
  symbol group, `GetSymbolOffset()` and `GetSymbolParameters()` per symbol, for
  every value a dumper made up, and the group grows with every value. That
  walk is the quadratic term that never finishes in the big run. The index per
  symbol group asks each symbol once.
- **GetSymbolTypeName 4 910 to 140, GetSymbolEntryInformation 2 455 to 70,
  GetSymbolParameters 4 281 to 574** - "Debugger: Ask cdb about a value's type
  once" and the `PyValue::type()` memo of "CDB: Walk a value's children once".
  `fromNativeValue()` asked `type()` seven times per value; each answer was
  built anew from `GetSymbolParameters()`, `GetSymbolTypeName()` twice and
  `GetSymbolEntryInformation()`.
- **GetTypeSize 594 to 36, GetModuleNameString 1 096 to 180** - the
  `from_native_type()` memo of the same commit: name, size and module of a
  type were re-derived for every value of it.
- **GetSymbolName 892 to 184, GetSymbolValueText 410 to 14, GetSymbolSize 405
  to 70, ExpandSymbol 190 to 53, AddSymbol 141 to 42, GetStackTrace 142 to
  43** - "Debugger: Reuse a struct's layout for the next cdb value of its
  type". After the first `Foo` is listed from the symbol group, the other 99
  get their members out of memory: no `AddSymbol()` cast per element, no
  expansion, no walk over the children. `ReadVirtual` stays flat because the
  bytes are read either way.
- **GetSymbolTypeId 7 to 1, TypeSearch_us 921 to 225** - "CDB: Look a type up
  where it is likely to be". Lookups go to the module of the value being
  dumped first; with symbols loaded and the cache warm the search was already
  cheap here. The change matters for a miss on a process with many modules
  and deferred symbols, which this program does not have.
- `ExpandSymbol_us` went up (48 to 72 ms) while the count went down; the 53
  remaining expansions are of larger symbols (the lists themselves, the
  QObjects). Noise level either way.

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
  members and `__vtcast_` children all keep the type on the symbol group path.
- **Where the rest of the time goes.** In the big run the engine calls are no
  longer the bulk of the 43.8 s: `ReadVirtual` is 15 300 (one read per member,
  the `QString` dumper reads again for each string) and the rest is Python.
  The counters make that a follow-up, not a guess.

## Regression check

`tst_debugger_dumpers` against cdb, same machine, 52 rows covering the changed
paths (`Struct`, `Bitfields`, `Union`, `AnonymousStruct`, `Inheritance`,
`QObject1-3`, `QVariant1/2/4`, `QVariantMap/Hash`, `QList`, `QMap`, `QHash`,
`QSet`, the std containers): 49 passed, 0 failed, 6 skipped by the test for
cdb. Run with

```
QTC_DEBUGGER_PATH_FOR_TEST=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe
QTC_QMAKE_PATH_FOR_TEST=C:/Qt/6.11.1/msvc2022_64/bin/qmake.exe
QTC_MSVC_ENV_BAT=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat
QTC_CDBEXT_PATH=<build>\lib\qtcreatorcdbext64
tst_debugger_dumpers.exe dumper:Struct dumper:Bitfields ...
```

with the Qt DLLs, the Creator `bin` and `lib\qtcreator\plugins` directories on
`PATH`.
