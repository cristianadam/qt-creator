// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "datastreamreader.h"
#include "metadata/metadatareader.h"
#include <QIODevice>

namespace CommonTraceFormat {
using namespace Qt::StringLiterals;

DataStreamReader::DataStreamReader(QIODevice *dev,
                                   const DataStreamClass &dsc,
                                   const TraceClass *tc,
                                   const Schema *schema)
    : m_dev(dev), m_dsc(dsc), m_tc(tc), m_schema(schema)
{}

Utils::Result<> DataStreamReader::openNextPacket()
{
    if (m_dev->atEnd()) {
        m_atEnd = true;
        return Utils::ResultError(u"End of stream"_s);
    }
    m_reader.emplace(m_dev, m_dsc, m_tc, m_schema);
    auto r = m_reader->openPacket();
    if (!r) {
        m_atEnd = true;
        return r;
    }

    // Spec 4.2.1: consecutive packets of a stream carry consecutive sequence
    // numbers, so a gap reveals missing (e.g. overwritten) packets. This is not
    // an error — record the count so a consumer can report it.
    if (const auto seq = m_reader->packetSequenceNumber()) {
        if (m_lastPktSeqNum && *seq > *m_lastPktSeqNum + 1)
            m_lostPacketCount += *seq - *m_lastPktSeqNum - 1;
        m_lastPktSeqNum = seq;
    }
    return Utils::ResultOk;
}

Utils::Result<EventRecord> DataStreamReader::nextEvent()
{
    if (m_atEnd)
        return Utils::ResultError(u"End of stream"_s);

    // Open first packet lazily.
    if (!m_reader)
        if (auto r = openNextPacket(); !r)
            return Utils::ResultError(r.error());

    // Advance past exhausted packets.
    while (m_reader->atEndOfPacket()) {
        if (auto r = openNextPacket(); !r)
            return Utils::ResultError(r.error());
    }

    auto result = m_reader->nextEvent();
    if (!result) {
        if (!m_reader->atEndOfPacket()) {
            m_error = true;
            return result;  // propagate real error
        }
        if (auto r = openNextPacket(); !r)
            return Utils::ResultError(r.error());
        result = m_reader->nextEvent();
    }
    if (!result) {
        m_error = true;
        return result;
    }

    // Spec 4.2.2: an event record's default-clock timestamp MUST be >= that of
    // the previous event record within the same data stream. (When the stream
    // has no default clock, every timestamp is 0 and this trivially holds.)
    if (m_lastTimestamp && result->timestamp < *m_lastTimestamp) {
        m_error = true;
        return Utils::ResultError(
            u"Event record timestamp %1 is less than the previous timestamp %2 (spec 4.2.2)"_s
                .arg(result->timestamp).arg(*m_lastTimestamp));
    }
    m_lastTimestamp = result->timestamp;
    return result;
}

bool DataStreamReader::atEnd() const
{
    return m_atEnd || (m_reader && m_reader->atEnd());
}

} // namespace CommonTraceFormat
