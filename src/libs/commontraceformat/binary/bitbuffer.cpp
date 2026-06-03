// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bitbuffer.h"
#include <QIODevice>
#include <cstring>

namespace CommonTraceFormat {

BitBuffer::BitBuffer(QIODevice *dev)
    : m_dev(dev), m_writeMode(true)
{}

BitBuffer::BitBuffer(QByteArray data)
    : m_buf(std::move(data)), m_writeMode(false)
{}

// ──────────────────────────────────────────────
// Internal bit manipulation helpers
// ──────────────────────────────────────────────

// Spec 6.4.3: decode/encode an L-bit fixed-length bit array field as a single
// pass, handling every byte-order/bit-order combination directly (no separate
// byte-reversal step). Two independent directions are at play:
//
//   * The reading direction within a data-stream byte — which physical bit
//     BIT_I a decode position X selects — depends on BYTE_ORDER:
//       big-endian    → BIT_I = 7 − (X mod 8)   (MSb of the byte first)
//       little-endian → BIT_I = X mod 8         (LSb of the byte first)
//
//   * The order in which decoded bits fill the result (element index DI)
//     depends on BIT_ORDER:
//       first-to-last (LeastSignificantFirst) → DI = I
//       last-to-first (MostSignificantFirst)  → DI = L − 1 − I
//
// Element 0 of the bit array is the least significant bit of the integer
// interpretation (spec 6.4.6), so DI doubles as the result bit position.

static quint64 readField(const uint8_t *buf, int bitOffset, int bitCount,
                         ByteOrder byteOrder, BitOrder bitOrder)
{
    quint64 raw = 0;
    for (int i = 0; i < bitCount; ++i) {
        const int x         = bitOffset + i;
        const int byteIdx   = x / 8;
        const int bitInByte = (byteOrder == ByteOrder::BigEndian) ? 7 - (x % 8) : (x % 8);
        const quint64 bit   = (buf[byteIdx] >> bitInByte) & 1;
        const int di        = (bitOrder == BitOrder::LeastSignificantFirst) ? i : (bitCount - 1 - i);
        raw |= bit << di;
    }
    return raw;
}

static void storeField(uint8_t *buf, int bitOffset, int bitCount, quint64 value,
                       ByteOrder byteOrder, BitOrder bitOrder)
{
    for (int i = 0; i < bitCount; ++i) {
        const int di        = (bitOrder == BitOrder::LeastSignificantFirst) ? i : (bitCount - 1 - i);
        const uint8_t bit   = (value >> di) & 1;
        const int x         = bitOffset + i;
        const int byteIdx   = x / 8;
        const int bitInByte = (byteOrder == ByteOrder::BigEndian) ? 7 - (x % 8) : (x % 8);
        buf[byteIdx] = (buf[byteIdx] & ~(1u << bitInByte)) | (bit << bitInByte);
    }
}

// ──────────────────────────────────────────────
// Write mode
// ──────────────────────────────────────────────

void BitBuffer::writeBits(quint64 value, int bitCount, ByteOrder byteOrder, BitOrder bitOrder)
{
    if (m_error || bitCount <= 0 || bitCount > 64)
        return;

    // Ensure buffer is large enough.
    qint64 lastBit  = m_writeBitOffset + bitCount - 1;
    qint64 lastByte = lastBit / 8;
    while (m_buf.size() <= lastByte)
        m_buf.append('\0');

    auto *buf = reinterpret_cast<uint8_t *>(m_buf.data());
    storeField(buf, static_cast<int>(m_writeBitOffset), bitCount, value, byteOrder, bitOrder);
    m_writeBitOffset += bitCount;
}

void BitBuffer::writeBytes(const QByteArray &bytes)
{
    writeBytes(bytes.constData(), bytes.size());
}

void BitBuffer::writeBytes(const char *data, qsizetype len)
{
    if (m_error || len == 0)
        return;
    Q_ASSERT(m_writeBitOffset % 8 == 0); // must be byte-aligned
    qsizetype bytePos = m_writeBitOffset / 8;
    m_buf.resize(bytePos + len);
    memcpy(m_buf.data() + bytePos, data, len);
    m_writeBitOffset += len * 8;
}

void BitBuffer::alignWriteTo(int alignBits)
{
    if (alignBits <= 1)
        return;
    qint64 rem = m_writeBitOffset % alignBits;
    if (rem != 0)
        writeBits(0, static_cast<int>(alignBits - rem),
                  ByteOrder::LittleEndian, BitOrder::LeastSignificantFirst);
}

void BitBuffer::patchBits(qint64 bitOffset, quint64 value, int bitCount,
                           ByteOrder byteOrder, BitOrder bitOrder)
{
    if (m_error || bitCount <= 0)
        return;

    auto *buf = reinterpret_cast<uint8_t *>(m_buf.data());
    storeField(buf, static_cast<int>(bitOffset), bitCount, value, byteOrder, bitOrder);
}

bool BitBuffer::flush()
{
    if (!m_dev || m_error)
        return false;
    if (m_buf.isEmpty())
        return true;
    qint64 written = m_dev->write(m_buf);
    if (written != m_buf.size()) {
        m_error = true;
        return false;
    }
    m_buf.clear();
    m_writeBitOffset = 0;
    return true;
}

// ──────────────────────────────────────────────
// Read mode
// ──────────────────────────────────────────────

quint64 BitBuffer::readBits(int bitCount, ByteOrder byteOrder, BitOrder bitOrder)
{
    if (m_error || bitCount <= 0 || bitCount > 64)
        return 0;
    // Spec 6.4.3: a fixed-length bit array field must not end past the packet
    // content boundary (strict ">", so a field may end exactly at it), nor past
    // the physical buffer.
    if (m_readBitOffset + bitCount > m_contentEndBit
        || m_readBitOffset + bitCount > m_buf.size() * 8LL) {
        m_error = true;
        return 0;
    }
    // Spec 6.4.3: within a single byte, two fixed-length bit array fields must
    // share the same byte order.
    if (m_readBitOffset % 8 != 0 && m_hasLastByteOrder && byteOrder != m_lastByteOrder) {
        m_error = true;
        return 0;
    }
    m_hasLastByteOrder = true;
    m_lastByteOrder    = byteOrder;

    const auto *buf = reinterpret_cast<const uint8_t *>(m_buf.constData());
    const quint64 raw = readField(buf, static_cast<int>(m_readBitOffset), bitCount, byteOrder, bitOrder);
    m_readBitOffset += bitCount;
    return raw;
}

QByteArray BitBuffer::readBytes(qsizetype byteCount)
{
    if (m_error || byteCount <= 0)
        return {};
    Q_ASSERT(m_readBitOffset % 8 == 0);
    qsizetype bytePos = m_readBitOffset / 8;
    if (m_readBitOffset + byteCount * 8LL > m_contentEndBit
        || bytePos + byteCount > m_buf.size()) {
        m_error = true;
        return {};
    }
    QByteArray result = m_buf.mid(bytePos, byteCount);
    m_readBitOffset += byteCount * 8;
    return result;
}

void BitBuffer::alignReadTo(int alignBits)
{
    if (m_error || alignBits <= 1)
        return;
    qint64 rem = m_readBitOffset % alignBits;
    if (rem != 0)
        m_readBitOffset += alignBits - rem;
    // Spec 6.4.1: after aligning, a position past the packet content boundary
    // (strict ">", so landing exactly at it is allowed) is an error.
    if (m_readBitOffset > m_contentEndBit)
        m_error = true;
}

bool BitBuffer::atEnd() const
{
    return m_readBitOffset >= m_buf.size() * 8LL;
}

} // namespace CommonTraceFormat
