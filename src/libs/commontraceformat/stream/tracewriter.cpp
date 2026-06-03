// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "tracewriter.h"
#include "metadata/metadatawriter.h"
#include <QIODevice>

namespace CommonTraceFormat {

TraceWriter::TraceWriter(Schema schema, QIODevice *metadataDevice)
    : m_schema(std::move(schema))
{
    MetadataWriter mw(metadataDevice);
    if (!mw.write(m_schema))
        m_error = true;
}

TraceWriter::~TraceWriter()
{
    close();
}

DataStreamWriter *TraceWriter::openStream(const QString &streamClassName,
                                          QIODevice *dataDevice)
{
    const DataStreamClass *dsc = nullptr;
    for (const auto &d : m_schema.dataStreamClasses) {
        if (d.name == streamClassName) {
            dsc = &d;
            break;
        }
    }
    return openStreamImpl(dsc, dataDevice);
}

DataStreamWriter *TraceWriter::openStreamById(quint64 dataStreamClassId,
                                              QIODevice *dataDevice)
{
    return openStreamImpl(m_schema.findDataStreamClass(dataStreamClassId), dataDevice);
}

DataStreamWriter *TraceWriter::openStreamImpl(const DataStreamClass *dsc,
                                              QIODevice *dataDevice)
{
    if (!dsc)
        return nullptr;

    const TraceClass *tc = m_schema.traceClass ? &*m_schema.traceClass : nullptr;
    auto writer = std::make_unique<DataStreamWriter>(dataDevice, *dsc, tc, 65536,
                                                     m_schema.uuid);
    m_streams.push_back(std::move(writer));
    return m_streams.back().get();
}

void TraceWriter::close()
{
    for (auto &s : m_streams)
        s->flush();
    m_streams.clear();
}

} // namespace CommonTraceFormat
