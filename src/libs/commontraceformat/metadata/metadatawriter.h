// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../commontraceformat_global.h"
#include "../schema/fieldlocation.h"
#include "../schema/schema.h"
#include <QJsonObject>

class QIODevice;

namespace CommonTraceFormat {

// Serializes a Schema to a CTF2 metadata stream (RFC 7464 JSON text sequences).
// Each fragment is: RS (0x1E) + JSON object + LF (0x0A).
// Fragment order: preamble, field-class-aliases, trace-class, clock-classes,
//                data-stream-classes (each immediately followed by its event-record-classes).
class CMNTRCEFMT_EXPORT MetadataWriter
{
public:
    explicit MetadataWriter(QIODevice *dev);

    bool write(const Schema &schema);
    bool hasError() const { return m_error; }

private:
    bool writeFragment(const QByteArray &json);

    bool writePreamble(const Schema &schema);
    bool writeFieldClassAlias(const QString &name, const FieldClass &fc);
    bool writeTraceClass(const TraceClass &tc);
    bool writeClockClass(const ClockClass &cc);
    bool writeDataStreamClass(const DataStreamClass &dsc);
    bool writeEventRecordClass(const EventRecordClass &erc, quint64 dataStreamClassId);

    QJsonObject fieldClassToJson(const FieldClass &fc) const;
    QJsonObject fieldLocationToJson(const FieldLocation &loc) const;

    QIODevice *m_dev;
    bool       m_error = false;
};

} // namespace CommonTraceFormat
