// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../commontraceformat_global.h"

#include "../schema/datastreamclass.h"
#include "../schema/traceclass.h"
#include "../binary/packetreader.h"

#include <utils/result.h>

#include <optional>

class QIODevice;

namespace CommonTraceFormat {

struct Schema;

// Iterates all event records across all packets of one CTF2 data stream.
class CMNTRCEFMT_EXPORT DataStreamReader
{
public:
    // `dsc` is the default class; when `schema` is given, each packet's class is
    // re-selected from its header data-stream-class-id role (spec 6.1).
    DataStreamReader(QIODevice *dev,
                     const DataStreamClass &dsc,
                     const TraceClass *tc = nullptr,
                     const Schema *schema = nullptr);

    // Returns the next event, or error. Check atEnd() to distinguish EOF from error.
    Utils::Result<EventRecord> nextEvent();

    bool atEnd() const;
    bool hasError() const { return m_error; }

    // Number of packets missing from the stream, inferred from gaps in packet
    // sequence numbers (spec 4.2.1). 0 when no gaps (or no sequence numbers).
    quint64 lostPacketCount() const { return m_lostPacketCount; }

private:
    Utils::Result<> openNextPacket();

    QIODevice             *m_dev;
    const DataStreamClass &m_dsc;
    const TraceClass      *m_tc;
    const Schema          *m_schema;

    std::optional<PacketReader> m_reader;
    bool m_atEnd = false;
    bool m_error = false;
    // Default-clock timestamp of the previous event record, to enforce the
    // monotonicity requirement across the whole data stream (spec 4.2.2).
    std::optional<quint64> m_lastTimestamp;
    // Packet-sequence-number tracking for missing-packet detection (spec 4.2.1).
    std::optional<quint64> m_lastPktSeqNum;
    quint64                m_lostPacketCount = 0;
};

} // namespace CommonTraceFormat
