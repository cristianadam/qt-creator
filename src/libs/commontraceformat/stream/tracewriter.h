// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../commontraceformat_global.h"
#include "../schema/schema.h"

#include "datastreamwriter.h"

#include <memory>
#include <vector>

class QIODevice;

namespace CommonTraceFormat {

// Top-level trace producer. Writes the metadata stream on construction,
// then provides per-stream DataStreamWriter instances.
class CMNTRCEFMT_EXPORT TraceWriter
{
public:
    // Writes metadata immediately. Call openStream() for each data stream.
    TraceWriter(Schema schema, QIODevice *metadataDevice);
    ~TraceWriter();

    // Returns a DataStreamWriter for the named data stream class.
    // The caller owns the QIODevice. Returns nullptr if name not found.
    DataStreamWriter *openStream(const QString &streamClassName, QIODevice *dataDevice);

    // Open a stream by data-stream-class id. Unlike the name-based overload this
    // is unambiguous when several classes share a name (or have none). Returns
    // nullptr if no class has the given id.
    DataStreamWriter *openStreamById(quint64 dataStreamClassId, QIODevice *dataDevice);

    void close();
    bool hasError() const { return m_error; }

private:
    DataStreamWriter *openStreamImpl(const DataStreamClass *dsc, QIODevice *dataDevice);

    Schema                              m_schema;
    std::vector<std::unique_ptr<DataStreamWriter>> m_streams;
    bool m_error = false;
};

} // namespace CommonTraceFormat
