// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "ctf1packets.h"
#include <utils/result.h>
#include <QIODevice>
#include <cstring>

namespace CommonTraceFormat {
using namespace Qt::StringLiterals;

// CTF 1.8 metadata packet header (37 bytes total, all fields LE unless magic == BE magic)
// magic[4] uuid[16] checksum[4] content_size[4] packet_size[4]
// compression_scheme[1] encryption_scheme[1] checksum_scheme[1] major[1] minor[1]
static constexpr quint32 CTF1_META_MAGIC_LE = 0x75D11D57u;
static constexpr quint32 CTF1_META_MAGIC_BE = 0x571DD175u;
static constexpr int CTF1_PACKET_HDR_SIZE   = 37;

bool isCTF1Metadata(QIODevice *dev)
{
    qint64 savedPos = dev->pos();
    QByteArray hdr = dev->read(4);
    dev->seek(savedPos);
    if (hdr.size() < 4)
        return false;
    quint32 magic;
    memcpy(&magic, hdr.constData(), 4);
    return magic == CTF1_META_MAGIC_LE || magic == CTF1_META_MAGIC_BE;
}

Utils::Result<QByteArray> readCTF1TsdlText(QIODevice *dev)
{
    QByteArray tsdl;

    while (!dev->atEnd()) {
        QByteArray hdr = dev->read(CTF1_PACKET_HDR_SIZE);
        if (hdr.size() < CTF1_PACKET_HDR_SIZE)
            break;

        quint32 magic;
        memcpy(&magic, hdr.constData(), 4);

        bool bigEndian = (magic == CTF1_META_MAGIC_BE);
        if (magic != CTF1_META_MAGIC_LE && magic != CTF1_META_MAGIC_BE)
            return Utils::ResultError(u"CTF1 metadata: bad magic in packet"_s);

        auto readU32 = [&](int offset) -> quint32 {
            quint32 v;
            memcpy(&v, hdr.constData() + offset, 4);
            if (bigEndian)
                return __builtin_bswap32(v);
            return v;
        };

        quint32 contentBits = readU32(24);
        quint32 packetBits  = readU32(28);
        quint32 contentBytes = contentBits / 8;
        quint32 packetBytes  = packetBits  / 8;

        if (packetBytes < CTF1_PACKET_HDR_SIZE || contentBytes < CTF1_PACKET_HDR_SIZE)
            return Utils::ResultError(u"CTF1 metadata: invalid packet size"_s);

        quint32 tsdlBytes = contentBytes - CTF1_PACKET_HDR_SIZE;
        QByteArray chunk = dev->read(tsdlBytes);
        tsdl += chunk;

        // Skip padding to reach packet boundary
        quint32 paddingBytes = packetBytes - CTF1_PACKET_HDR_SIZE - tsdlBytes;
        if (paddingBytes > 0)
            dev->skip(paddingBytes);
    }

    return tsdl;
}

} // namespace CommonTraceFormat
