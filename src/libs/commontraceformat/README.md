# CommonTraceFormat

A library for reading and writing [Common Trace Format version 2 (CTF2)](https://diamon.org/ctf/#_whats_ctf2) traces, plus a command-line inspection tool.

CTF version 1.8 is also supported to some extend.

## Overview

CTF2 is a binary trace format designed for fast, efficient event recording. It has two distinct parts:

- **Metadata stream** — RFC 7464 JSON text sequences describing the binary layout
- **Data stream(s)** — binary packets containing event records, schema-driven

This library provides an API for both producing and consuming CTF2 traces without external dependencies.

## Library API

### Writing a trace

```cpp
#include <stream/tracewriter.h>
#include <schema/scalarfieldclasses.h>
#include <schema/stringfieldclasses.h>
#include <schema/compoundfieldclasses.h>
#include <schema/clockclass.h>

using namespace CommonTraceFormat;

// Build schema
Schema schema;

ClockClass cc;
cc.name      = u"mono"_s;
cc.frequency = 1'000'000'000ULL;
cc.origin.isUnixEpoch = true;
schema.clockClasses.append(cc);

DataStreamClass dsc;
dsc.id   = 0;
dsc.name = u"events"_s;
dsc.defaultClockClassName = u"mono"_s;

// Event record header: { uint64 id }
auto hdr   = std::make_shared<StructureFC>();
auto idFc  = std::make_shared<FixedLengthUIntFC>();
idFc->length = 64;
idFc->roles  = {UIntRole::EventRecordClassId};
hdr->members.append({u"id"_s, std::move(idFc)});
dsc.eventRecordHeaderFieldClass = std::move(hdr);

// Event class 0: payload { uint32 value, string name }
EventRecordClass erc;
erc.id   = 0;
erc.name = u"sample"_s;
auto payload = std::make_shared<StructureFC>();
auto vf = std::make_shared<FixedLengthUIntFC>(); vf->length = 32;
payload->members.append({u"value"_s, std::move(vf)});
payload->members.append({u"name"_s,  std::make_shared<NullTerminatedStringFC>()});
erc.payloadFieldClass = std::move(payload);
dsc.eventRecordClasses.append(std::move(erc));
schema.dataStreamClasses.append(std::move(dsc));

// Write
QFile metaFile(u"trace/metadata"_s); metaFile.open(QIODevice::WriteOnly);
QFile dataFile(u"trace/stream_0"_s); dataFile.open(QIODevice::WriteOnly);

TraceWriter writer(schema, &metaFile);
DataStreamWriter *stream = writer.openStream(u"events"_s, &dataFile);

StructureValue payload;
payload.set(u"value"_s, quint64(42));
payload.set(u"name"_s,  QString(u"hello"_s));
stream->writeEvent(0, payload);

writer.close();
```

### Reading a trace

```cpp
#include <stream/tracereader.h>

QFile metaFile(u"trace/metadata"_s); metaFile.open(QIODevice::ReadOnly);
auto readerResult = TraceReader::open(&metaFile);
if (!readerResult) {
    qWarning() << readerResult.error().message;
    return;
}
TraceReader &reader = *readerResult;

const DataStreamClass &dsc = reader.schema().dataStreamClasses[0];
QFile dataFile(u"trace/stream_0"_s); dataFile.open(QIODevice::ReadOnly);
DataStreamReader *stream = reader.openStream(dsc, &dataFile);

while (auto rec = stream->nextEvent()) {
    qDebug() << rec->eventClass->name
             << std::get<quint64>(*rec->payload.get(u"value"_s));
}
```

### Reading a trace directory

`TraceReader` works on individual `QIODevice`s. To open a whole trace stored as
a directory — handling the storage-layout conventions CTF 2 leaves unspecified
(metadata in `kernel`/`ust`/per-PID/session-rotation subdirectories, data
streams split across rotated `<channel>_<cpu>_<fileindex>` tracefiles, and LTTng
`index/*.idx` files) — use `TraceDirectory`:

```cpp
#include <stream/tracedirectory.h>

auto result = TraceDirectory::open(u"/path/to/trace-dir"_s);
if (!result)
    return;
for (const TraceDirectory::Trace &trace : result->traces()) {
    for (const TraceDirectory::Stream &s : trace.streams) {
        while (auto rec = s.reader->nextEvent())
            /* ... */;
    }
}
```

Each packet's data stream class is still resolved from its packet header
(spec 6.1), so `Stream::dsc` is only a default/hint.

## ctf2tool

A command-line tool for inspecting CTF2 trace directories.

```
Usage: ctf2tool <command> <trace-dir> [options]

Commands:
  list        List all events with timestamps
  dump-meta   Print a human-readable schema summary
  to-json     Convert all events to JSON (--out <file> to write to file)
```

### Examples

```bash
# Show schema
ctf2tool dump-meta path/to/trace

# List events
ctf2tool list path/to/trace

# Export to JSON
ctf2tool to-json path/to/trace --out events.json
```

The tool looks for data stream files named `stream_<id>`, `channel<id>`, or `data` alongside the `metadata` file.

## Architecture

```
src/lib/
├── schema/       Pure data model (FieldClass hierarchy, Schema, DataStreamClass, …)
├── metadata/     RFC 7464 metadata writer + reader (Schema ↔ JSON text sequences)
├── binary/       Bit-level I/O, schema-driven field encoder/decoder, packet framing
└── stream/       High-level producer/consumer API (TraceWriter, DataStreamReader, …)
```

**Key design decisions:**

- Field class dispatch via `FieldClassKind` enum + `static_cast` — no virtual dispatch, no RTTI.
- `std::expected<T, ParseError>` throughout all reader paths.
- `FieldClassPtr = std::shared_ptr<FieldClass>` so schema types remain copyable in `QList`.
- Structure member order is preserved by serializing `member-classes` as a JSON array (not object).
- Byte/bit order is per-field; `BitBuffer` handles all four combinations of `ByteOrder × BitOrder`.

## License

LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
