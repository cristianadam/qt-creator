// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "fieldreader.h"
#include <utils/result.h>
#include "../schema/scalarfieldclasses.h"
#include "../schema/stringfieldclasses.h"
#include "../schema/blobfieldclasses.h"
#include "../schema/compoundfieldclasses.h"
#include "util/leb128.h"
#include <QStringConverter>
#include <QFloat16>
#include <cstring>
#include <cmath>
#include <limits>
#include <vector>
#include <algorithm>

namespace CommonTraceFormat {
using namespace Qt::StringLiterals;

// Convert an IEEE 754-2008 binary128 value (1 sign, 15-bit exponent, 112-bit
// mantissa), given as its high and low 64-bit halves, to the nearest double.
// Precision beyond double's 52-bit mantissa is necessarily lost.
static double binary128ToDouble(quint64 hi, quint64 lo)
{
    const int     sign     = (hi >> 63) & 1;
    const int     exponent = (hi >> 48) & 0x7FFF;            // bits 112..126
    const quint64 mantHi   = hi & 0xFFFFFFFFFFFFULL;         // mantissa bits 64..111
    const quint64 mantLo   = lo;                             // mantissa bits 0..63

    double result;
    if (exponent == 0x7FFF) {
        result = (mantHi == 0 && mantLo == 0)
                     ? std::numeric_limits<double>::infinity()
                     : std::numeric_limits<double>::quiet_NaN();
    } else {
        // Mantissa as a fraction in [0, 1): mantissa / 2^112.
        const double mant = std::ldexp(static_cast<double>(mantHi), 64)
                            + static_cast<double>(mantLo);
        const double frac = std::ldexp(mant, -112);
        result = (exponent == 0)
                     ? std::ldexp(frac, 1 - 16383)           // subnormal / zero
                     : std::ldexp(1.0 + frac, exponent - 16383);
    }
    return sign ? -result : result;
}

// Extract `count` (<= 64) bits from a big-endian bit array (be[0] is the most
// significant byte), starting at bit index `startFromMsb` counted from the MSB.
static quint64 bitsFromMsb(const std::vector<uint8_t> &be, int startFromMsb, int count)
{
    quint64 v = 0;
    for (int i = 0; i < count; ++i) {
        const int b         = startFromMsb + i;
        const int byteIdx   = b / 8;
        const int bitInByte = 7 - (b % 8);
        v = (v << 1) | ((be[byteIdx] >> bitInByte) & 1);
    }
    return v;
}

// Convert an IEEE 754-2008 binaryK interchange value (K a multiple of 32, K >=
// 128), given as a big-endian byte array, to the nearest double. The exponent
// field width follows the interchange format: w = round(4·log2(K)) − 13, the
// trailing significand is t = K − w − 1, and the bias is 2^(w−1) − 1. Precision
// beyond double's 52-bit significand is necessarily lost.
static double binaryKToDouble(const std::vector<uint8_t> &be, int k)
{
    const int    w    = static_cast<int>(std::lround(4.0 * std::log2(double(k)))) - 13;
    const int    t    = k - w - 1;
    const int    sign = (be[0] >> 7) & 1;
    const quint64 exponent = bitsFromMsb(be, 1, w);

    // Top significand bits drive the double; scan the remainder only to tell a
    // NaN from an infinity when the exponent field is all ones.
    const int    topBits = std::min(t, 53);
    const quint64 topMant = bitsFromMsb(be, 1 + w, topBits);
    bool mantNonZero = topMant != 0;
    for (int i = topBits; i < t && !mantNonZero; ++i)
        mantNonZero = bitsFromMsb(be, 1 + w + i, 1) != 0;

    const quint64 maxExp = (w >= 64) ? ~quint64(0) : ((quint64(1) << w) - 1);
    const double  bias   = std::ldexp(1.0, w - 1) - 1.0; // 2^(w-1) - 1

    double result;
    if (exponent == maxExp) {
        result = mantNonZero ? std::numeric_limits<double>::quiet_NaN()
                             : std::numeric_limits<double>::infinity();
    } else {
        const double frac = std::ldexp(double(topMant), -topBits); // [0, 1)
        result = (exponent == 0)
                     ? std::ldexp(frac, static_cast<int>(1 - bias))           // subnormal / zero
                     : std::ldexp(1.0 + frac, static_cast<int>(double(exponent) - bias));
    }
    return sign ? -result : result;
}

// Number of bytes per code unit for a string encoding (spec 6.4.11/12/14).
static int codeUnitLength(StringEncoding enc)
{
    switch (enc) {
    case StringEncoding::Utf8:    return 1;
    case StringEncoding::Utf16Be:
    case StringEncoding::Utf16Le: return 2;
    case StringEncoding::Utf32Be:
    case StringEncoding::Utf32Le: return 4;
    }
    return 1;
}

static QString decodeStringBytes(const QByteArray &bytes, StringEncoding enc)
{
    switch (enc) {
    case StringEncoding::Utf8:    return QString::fromUtf8(bytes);
    case StringEncoding::Utf16Be: return QStringDecoder(QStringDecoder::Utf16BE)(bytes);
    case StringEncoding::Utf16Le: return QStringDecoder(QStringDecoder::Utf16LE)(bytes);
    case StringEncoding::Utf32Be: return QStringDecoder(QStringDecoder::Utf32BE)(bytes);
    case StringEncoding::Utf32Le: return QStringDecoder(QStringDecoder::Utf32LE)(bytes);
    }
    return QString::fromUtf8(bytes);
}

// Validate a located length field (spec 5.3.14/5.3.17/5.3.21: the length field
// MUST be an unsigned integer). A signed located field carrying a negative
// value would otherwise wrap to a huge unsigned length.
static Utils::Result<void> checkUnsignedLength(quint64 value, bool isSigned, QStringView what)
{
    if (isSigned && static_cast<qint64>(value) < 0)
        return Utils::ResultError(
            QStringLiteral("%1 length field is negative").arg(what));
    return Utils::ResultOk;
}

// True if any byte of the code unit [data+off, off+ul) is non-zero.
static bool codeUnitHasSetBit(const QByteArray &data, int off, int ul)
{
    for (int i = 0; i < ul; ++i) {
        if (static_cast<uint8_t>(data[off + i]) != 0)
            return true;
    }
    return false;
}

FieldReader::FieldReader(BitBuffer &buf, FieldLocationResolver resolver,
                         FieldLocation::Origin currentOrigin)
    : m_buf(buf), m_resolver(std::move(resolver)), m_currentOrigin(currentOrigin)
{}

Utils::Result<FieldValue> FieldReader::read(const FieldClass &fc)
{
    if (m_buf.hasError())
        return Utils::ResultError(u"BitBuffer error before read"_s);

    switch (fc.kind) {
    case FieldClassKind::FixedLengthBitArray:
    case FieldClassKind::FixedLengthBitMap: {
        const auto &f = static_cast<const FixedLengthBitArrayFC &>(fc);
        m_buf.alignReadTo(f.alignment);
        const quint64 v = m_buf.readBits(f.length, f.byteOrder, f.bitOrder);
        // Spec 6.4.3: surface a read past the packet content boundary as an
        // error and abort, rather than returning a zero-filled value.
        if (m_buf.hasError())
            return Utils::ResultError(u"Read past packet content"_s);
        return v;
    }
    case FieldClassKind::FixedLengthBool: {
        const auto &f = static_cast<const FixedLengthBoolFC &>(fc);
        m_buf.alignReadTo(f.alignment);
        const quint64 v = m_buf.readBits(f.length, f.byteOrder, f.bitOrder);
        if (m_buf.hasError())
            return Utils::ResultError(u"Read past packet content"_s);
        return bool(v != 0);
    }
    case FieldClassKind::FixedLengthUInt: {
        const auto &f = static_cast<const FixedLengthUIntFC &>(fc);
        m_buf.alignReadTo(f.alignment);
        const quint64 v = m_buf.readBits(f.length, f.byteOrder, f.bitOrder);
        if (m_buf.hasError())
            return Utils::ResultError(u"Read past packet content"_s);
        return v;
    }
    case FieldClassKind::FixedLengthSInt: {
        const auto &f = static_cast<const FixedLengthSIntFC &>(fc);
        m_buf.alignReadTo(f.alignment);
        quint64 raw = m_buf.readBits(f.length, f.byteOrder, f.bitOrder);
        if (m_buf.hasError())
            return Utils::ResultError(u"Read past packet content"_s);
        int bits = f.length;
        if (bits < 64 && (raw & (quint64(1) << (bits - 1))))
            raw |= ~((quint64(1) << bits) - 1);
        return qint64(raw);
    }
    case FieldClassKind::FixedLengthFloat: {
        const auto &f = static_cast<const FixedLengthFloatFC &>(fc);
        m_buf.alignReadTo(f.alignment);
        if (f.length == 16) {
            quint16 bits = static_cast<quint16>(m_buf.readBits(16, f.byteOrder, f.bitOrder));
            if (m_buf.hasError())
                return Utils::ResultError(u"Read past packet content"_s);
            qfloat16 hv;
            memcpy(&hv, &bits, 2);
            return double(float(hv));
        } else if (f.length == 32) {
            quint32 bits = static_cast<quint32>(m_buf.readBits(32, f.byteOrder, f.bitOrder));
            if (m_buf.hasError())
                return Utils::ResultError(u"Read past packet content"_s);
            float fv;
            memcpy(&fv, &bits, 4);
            return double(fv);
        } else if (f.length == 64) {
            quint64 bits = m_buf.readBits(64, f.byteOrder, f.bitOrder);
            if (m_buf.hasError())
                return Utils::ResultError(u"Read past packet content"_s);
            double dv;
            memcpy(&dv, &bits, 8);
            return dv;
        } else if (f.length == 128) {
            // Read two 64-bit words. Only the natural byte/bit-order combinations
            // (LE + first-to-last, BE + last-to-first) are supported, since
            // mixed-order assembly across the two words isn't handled here.
            const bool le = f.byteOrder == ByteOrder::LittleEndian
                            && f.bitOrder == BitOrder::LeastSignificantFirst;
            const bool be = f.byteOrder == ByteOrder::BigEndian
                            && f.bitOrder == BitOrder::MostSignificantFirst;
            if (!le && !be)
                return Utils::ResultError(
                    u"Unsupported 128-bit float byte/bit-order combination"_s);
            const quint64 w0 = m_buf.readBits(64, f.byteOrder, f.bitOrder);
            const quint64 w1 = m_buf.readBits(64, f.byteOrder, f.bitOrder);
            if (m_buf.hasError())
                return Utils::ResultError(u"Read past packet content"_s);
            // LE: first word holds bits 0..63 (low); BE: first word holds the
            // most significant 64 bits (high).
            const quint64 lo = le ? w0 : w1;
            const quint64 hi = le ? w1 : w0;
            return binary128ToDouble(hi, lo);
        } else if (f.length > 128 && f.length % 32 == 0) {
            // binaryK (K a multiple of 32, K > 128). Read 32-bit words and
            // assemble a big-endian byte array (be[0] is the most significant
            // byte). As with binary128, only the natural byte/bit-order combos
            // are handled, since mixed-order cross-word assembly isn't.
            const bool le = f.byteOrder == ByteOrder::LittleEndian
                            && f.bitOrder == BitOrder::LeastSignificantFirst;
            const bool be = f.byteOrder == ByteOrder::BigEndian
                            && f.bitOrder == BitOrder::MostSignificantFirst;
            if (!le && !be)
                return Utils::ResultError(
                    u"Unsupported %1-bit float byte/bit-order combination"_s.arg(f.length));
            const int nWords = f.length / 32;
            std::vector<uint8_t> bytes(f.length / 8, 0);
            for (int i = 0; i < nWords; ++i) {
                const quint32 word = static_cast<quint32>(
                    m_buf.readBits(32, f.byteOrder, f.bitOrder));
                if (m_buf.hasError())
                    return Utils::ResultError(u"Read past packet content"_s);
                // LE: word 0 is the least significant, so it lands last in the
                // big-endian layout; BE: word 0 is the most significant (first).
                const int wordPos = le ? (nWords - 1 - i) : i;
                bytes[wordPos * 4 + 0] = (word >> 24) & 0xFF;
                bytes[wordPos * 4 + 1] = (word >> 16) & 0xFF;
                bytes[wordPos * 4 + 2] = (word >> 8) & 0xFF;
                bytes[wordPos * 4 + 3] = word & 0xFF;
            }
            return binaryKToDouble(bytes, f.length);
        }
        return Utils::ResultError(
            u"Unsupported floating point length: %1"_s.arg(f.length));
    }
    case FieldClassKind::VariableLengthUInt: {
        m_buf.alignReadTo(8);
        QByteArray encoded;
        while (true) {
            // readU8 enforces the packet content boundary (spec 6.4.9: error if
            // the next byte would extend past PKT_CONTENT_LEN).
            uint8_t byte = m_buf.readU8();
            if (m_buf.hasError())
                return Utils::ResultError(u"Truncated ULEB128"_s);
            encoded.append(static_cast<char>(byte));
            if (!(byte & 0x80))
                break;
        }
        m_lastVlByteCount = static_cast<int>(encoded.size());
        auto [value, n] = decodeULeb128(std::span(encoded.constData(), encoded.size()));
        if (n < 0)
            return Utils::ResultError(u"Invalid ULEB128"_s);
        return quint64(value);
    }
    case FieldClassKind::VariableLengthSInt: {
        m_buf.alignReadTo(8);
        QByteArray encoded;
        while (true) {
            uint8_t byte = m_buf.readU8();
            if (m_buf.hasError())
                return Utils::ResultError(u"Truncated SLEB128"_s);
            encoded.append(static_cast<char>(byte));
            if (!(byte & 0x80))
                break;
        }
        m_lastVlByteCount = static_cast<int>(encoded.size());
        auto [value, n] = decodeSLeb128(std::span(encoded.constData(), encoded.size()));
        if (n < 0)
            return Utils::ResultError(u"Invalid SLEB128"_s);
        return qint64(value);
    }
    case FieldClassKind::NullTerminatedString: {
        const auto &f = static_cast<const NullTerminatedStringFC &>(fc);
        m_buf.alignReadTo(8);
        return readNullTerminatedString(f.encoding);
    }
    case FieldClassKind::StaticLengthString: {
        const auto &f = static_cast<const StaticLengthStringFC &>(fc);
        m_buf.alignReadTo(8);
        return readLengthPrefixedString(f.length, f.encoding);
    }
    case FieldClassKind::DynamicLengthString: {
        const auto &f = static_cast<const DynamicLengthStringFC &>(fc);
        m_buf.alignReadTo(8);
        auto len = resolveLocation(f.lengthFieldLocation);
        if (!len)
            return Utils::ResultError(len.error());
        if (auto r = checkUnsignedLength(len->value, len->isSigned, u"dynamic-length string"); !r)
            return Utils::ResultError(r.error());
        return readLengthPrefixedString(static_cast<qsizetype>(len->value), f.encoding);
    }
    case FieldClassKind::StaticLengthBlob: {
        const auto &f = static_cast<const StaticLengthBlobFC &>(fc);
        m_buf.alignReadTo(8);
        const QByteArray bytes = m_buf.readBytes(f.length);
        if (m_buf.hasError())
            return Utils::ResultError(u"Read past packet content in BLOB"_s);
        return bytes;
    }
    case FieldClassKind::DynamicLengthBlob: {
        const auto &f = static_cast<const DynamicLengthBlobFC &>(fc);
        m_buf.alignReadTo(8);
        auto len = resolveLocation(f.lengthFieldLocation);
        if (!len)
            return Utils::ResultError(len.error());
        if (auto r = checkUnsignedLength(len->value, len->isSigned, u"dynamic-length BLOB"); !r)
            return Utils::ResultError(r.error());
        const QByteArray bytes = m_buf.readBytes(static_cast<qsizetype>(len->value));
        if (m_buf.hasError())
            return Utils::ResultError(u"Read past packet content in BLOB"_s);
        return bytes;
    }
    case FieldClassKind::Structure:
        return readStructure(static_cast<const StructureFC &>(fc));
    case FieldClassKind::StaticLengthArray:
        return readStaticArray(static_cast<const StaticLengthArrayFC &>(fc));
    case FieldClassKind::DynamicLengthArray:
        return readDynamicArray(static_cast<const DynamicLengthArrayFC &>(fc));
    case FieldClassKind::Variant:
        return readVariant(static_cast<const VariantFC &>(fc));
    case FieldClassKind::Optional:
        return readOptional(static_cast<const OptionalFC &>(fc));
    }
    return Utils::ResultError(u"Unknown field class kind"_s);
}

Utils::Result<FieldValue> FieldReader::readStructure(const StructureFC &fc)
{
    m_buf.alignReadTo(fc.minimumAlignment);
    auto sv = std::make_shared<StructureValue>();

    // Push a scope for this structure so the field location procedure can find
    // its members (prior siblings) and navigate to/from its parent.
    Scope scope;
    scope.sv             = sv.get();
    scope.parent         = m_scopes.isEmpty() ? -1 : static_cast<int>(m_scopes.size()) - 1;
    scope.memberInParent = m_pendingChildMember;
    m_pendingChildMember.clear();
    const int  scopeIdx = static_cast<int>(m_scopes.size());
    const bool isRoot   = scopeIdx == 0;
    m_scopes.append(scope);

    for (const auto &member : fc.members) {
        m_pendingChildMember = member.name;
        auto result = read(*member.fieldClass);
        m_pendingChildMember.clear();
        if (!result) {
            m_scopes.removeLast();
            return Utils::ResultError(result.error());
        }
        // Surface a latent buffer error (e.g. an alignment that overran the
        // packet content boundary, spec 6.4.1) instead of letting it slip past.
        if (m_buf.hasError()) {
            m_scopes.removeLast();
            return Utils::ResultError(u"Read past packet content"_s);
        }

        // Record the clock length L (spec 6.3) for top-level integer members.
        if (isRoot) {
            const FieldClass *mfc = member.fieldClass.get();
            if (mfc->kind == FieldClassKind::FixedLengthUInt)
                m_rootMemberClockBits[member.name] =
                    static_cast<const FixedLengthUIntFC &>(*mfc).length;
            else if (mfc->kind == FieldClassKind::VariableLengthUInt)
                m_rootMemberClockBits[member.name] = m_lastVlByteCount * 7;
        }

        sv->set(member.name, std::move(*result));
    }

    m_scopes.removeLast();
    return FieldValue{sv};
}

Utils::Result<FieldValue> FieldReader::readStaticArray(const StaticLengthArrayFC &fc)
{
    m_buf.alignReadTo(fc.minimumAlignment);
    // Each element structure is labelled with the array's own member name, so a
    // field location can reach the element currently being decoded.
    const QString memberName = m_pendingChildMember;
    auto av = std::make_shared<ArrayValue>();
    for (qint64 i = 0; i < fc.length; ++i) {
        m_pendingChildMember = memberName;
        auto result = read(*fc.elementFieldClass);
        if (!result)
            return Utils::ResultError(result.error());
        av->elements.append(std::move(*result));
    }
    m_pendingChildMember.clear();
    return FieldValue{av};
}

Utils::Result<FieldValue> FieldReader::readDynamicArray(const DynamicLengthArrayFC &fc)
{
    m_buf.alignReadTo(fc.minimumAlignment);
    const QString memberName = m_pendingChildMember;
    auto len = resolveLocation(fc.lengthFieldLocation);
    if (!len)
        return Utils::ResultError(len.error());
    if (auto r = checkUnsignedLength(len->value, len->isSigned, u"dynamic-length array"); !r)
        return Utils::ResultError(r.error());
    auto av = std::make_shared<ArrayValue>();
    for (quint64 i = 0; i < len->value; ++i) {
        m_pendingChildMember = memberName;
        auto result = read(*fc.elementFieldClass);
        if (!result)
            return Utils::ResultError(result.error());
        av->elements.append(std::move(*result));
    }
    m_pendingChildMember.clear();
    return FieldValue{av};
}

Utils::Result<FieldValue> FieldReader::readVariant(const VariantFC &fc)
{
    // The selected option's structure (if any) is labelled with the variant's
    // own member name for the field location procedure.
    const QString memberName = m_pendingChildMember;
    auto sel = resolveLocation(fc.selectorFieldLocation);
    if (!sel)
        return Utils::ResultError(sel.error());
    // Spec 5.3.23 / 6.4.20: a variant selector field MUST be an integer; a
    // boolean selector is not allowed.
    if (sel->isBoolean)
        return Utils::ResultError(u"Variant selector field must be an integer, not boolean"_s);
    const quint64 selector = sel->value;
    // A signed selector is compared in the signed domain so that ranges spanning
    // negative values match correctly (spec 5.3.23); otherwise compare unsigned.
    const bool   selSigned = sel->isSigned;
    const qint64 sSelector = static_cast<qint64>(selector);
    const auto eqVal = [&](qint64 v) {
        return selSigned ? (sSelector == v) : (selector == static_cast<quint64>(v));
    };
    const auto inRange = [&](qint64 lo, qint64 hi) {
        return selSigned ? (sSelector >= lo && sSelector <= hi)
                         : (selector >= static_cast<quint64>(lo)
                            && selector <= static_cast<quint64>(hi));
    };
    for (int oi = 0; oi < fc.options.size(); ++oi) {
        const auto &opt = fc.options[oi];
        bool matched = false;
        for (auto sv : opt.selectorValues) {
            if (eqVal(sv)) { matched = true; break; }
        }
        if (!matched) {
            for (const auto &r : opt.selectorRanges) {
                if (inRange(r.lo, r.hi)) { matched = true; break; }
            }
        }
        if (matched) {
            m_pendingChildMember = memberName;
            auto result = read(*opt.fieldClass);
            if (!result)
                return Utils::ResultError(result.error());
            auto vv = std::make_shared<VariantValue>();
            vv->selectedOption = opt.name;
            vv->selectedIndex  = oi;
            vv->value = std::make_unique<FieldValue>(std::move(*result));
            return FieldValue{vv};
        }
    }
    return Utils::ResultError(u"Variant selector %1 matched no option"_s.arg(selector));
}

Utils::Result<FieldValue> FieldReader::readOptional(const OptionalFC &fc)
{
    const QString memberName = m_pendingChildMember;
    auto sel = resolveLocation(fc.selectorFieldLocation);
    if (!sel)
        return Utils::ResultError(sel.error());
    const quint64 selector = sel->value;
    bool present;
    // Spec 6.4.19: a boolean selector makes the field present iff true; an
    // integer selector iff its value is in the range set. Prefer the located
    // field's runtime type; fall back to schema shape (no ranges => boolean)
    // when the type is unknown (e.g. resolved across roots).
    const bool booleanSelector =
        sel->isBoolean || (fc.selectorValues.isEmpty() && fc.selectorRanges.isEmpty());
    if (booleanSelector) {
        present = selector != 0;
    } else {
        // A signed selector is compared in the signed domain so that ranges
        // spanning negative values match correctly (spec 5.3.22).
        const bool   selSigned = sel->isSigned;
        const qint64 sSelector = static_cast<qint64>(selector);
        const auto eqVal = [&](qint64 v) {
            return selSigned ? (sSelector == v) : (selector == static_cast<quint64>(v));
        };
        const auto inRange = [&](qint64 lo, qint64 hi) {
            return selSigned ? (sSelector >= lo && sSelector <= hi)
                             : (selector >= static_cast<quint64>(lo)
                                && selector <= static_cast<quint64>(hi));
        };
        present = false;
        for (qint64 v : fc.selectorValues) {
            if (eqVal(v)) { present = true; break; }
        }
        if (!present) {
            for (const auto &r : fc.selectorRanges) {
                if (inRange(r.lo, r.hi)) { present = true; break; }
            }
        }
    }
    if (!present)
        return std::monostate{};
    m_pendingChildMember = memberName;
    return read(*fc.fieldClass);
}

// Field location procedure (spec 6.4.2). Picks the starting structure, then
// hands off to walkLocation to follow the path to the located scalar field.
Utils::Result<FieldReader::LocatedValue> FieldReader::resolveLocation(const FieldLocation &loc)
{
    if (loc.path.isEmpty())
        return Utils::ResultError(u"Field location has an empty path"_s);

    if (loc.hasOrigin && loc.origin != m_currentOrigin) {
        // The location names another root, which has already been fully decoded:
        // defer to the external resolver over those completed root structures.
        // The resolver yields only the integer value, not the field's type.
        if (m_resolver) {
            if (auto v = m_resolver(loc))
                return LocatedValue{*v, false};
        }
        return Utils::ResultError(u"Field location: cannot resolve cross-root origin"_s);
    }

    if (m_scopes.isEmpty())
        return Utils::ResultError(u"Field location: no enclosing structure"_s);

    // An explicit origin naming this very root starts at the root scope (0);
    // a location without an origin starts at the immediate parent (top scope).
    const int start = loc.hasOrigin ? 0 : static_cast<int>(m_scopes.size()) - 1;
    return walkLocation(start, loc.path);
}

Utils::Result<FieldReader::LocatedValue> FieldReader::walkLocation(int startScope,
                                                 const QList<std::optional<QString>> &path)
{
    // The current position is either an in-progress scope (scopeIdx >= 0) or a
    // value inside an already-decoded subtree (completed != nullptr).
    int               scopeIdx  = startScope;
    const FieldValue *completed = nullptr;

    for (int i = 0; i < path.size(); ++i) {
        const bool isLast = i == path.size() - 1;

        if (!path[i].has_value()) { // null: ascend to the immediate parent structure
            if (completed)
                return Utils::ResultError(
                    u"Field location: cannot ascend out of a decoded field"_s);
            const int parent = m_scopes[scopeIdx].parent;
            if (parent < 0)
                return Utils::ResultError(u"Field location: no parent structure"_s);
            scopeIdx = parent;
            continue;
        }

        const QString &name  = *path[i];
        const FieldValue *found = nullptr;

        if (!completed) {
            // In-progress scope: a prior sibling is already in the structure;
            // the member currently being decoded lives in a child scope.
            found = m_scopes[scopeIdx].sv->get(name);
            if (!found) {
                int child = -1;
                for (int s = scopeIdx + 1; s < m_scopes.size(); ++s) {
                    if (m_scopes[s].parent == scopeIdx
                        && m_scopes[s].memberInParent == name) {
                        child = s;
                        break;
                    }
                }
                if (child < 0)
                    return Utils::ResultError(
                        u"Field location: no decoded member named '%1'"_s.arg(name));
                if (isLast)
                    return Utils::ResultError(
                        u"Field location: '%1' is a structure, not a scalar"_s.arg(name));
                scopeIdx = child; // descend into the in-progress child structure
                continue;
            }
        } else {
            if (!isStructure(*completed))
                return Utils::ResultError(
                    u"Field location: '%1' has no members"_s.arg(name));
            found = asStructure(*completed).get(name);
            if (!found)
                return Utils::ResultError(
                    u"Field location: no member named '%1'"_s.arg(name));
        }

        // Unwrap variants to the value of their selected option.
        const FieldValue *v = found;
        while (std::holds_alternative<std::shared_ptr<VariantValue>>(*v)) {
            const auto &vv = *std::get<std::shared_ptr<VariantValue>>(*v);
            if (!vv.value)
                return Utils::ResultError(u"Field location: empty variant"_s);
            v = vv.value.get();
        }

        if (isLast)
            return scalarValue(*v);

        if (!isStructure(*v))
            return Utils::ResultError(
                u"Field location: '%1' is not a structure"_s.arg(name));
        completed = v;
    }

    return Utils::ResultError(u"Field location did not resolve to a scalar field"_s);
}

Utils::Result<FieldReader::LocatedValue> FieldReader::scalarValue(const FieldValue &fv)
{
    if (std::holds_alternative<quint64>(fv))
        return LocatedValue{std::get<quint64>(fv), /*isBoolean=*/false, /*isSigned=*/false};
    if (std::holds_alternative<qint64>(fv))
        return LocatedValue{static_cast<quint64>(std::get<qint64>(fv)),
                            /*isBoolean=*/false, /*isSigned=*/true};
    if (std::holds_alternative<bool>(fv))
        return LocatedValue{std::get<bool>(fv) ? quint64(1) : quint64(0),
                            /*isBoolean=*/true, /*isSigned=*/false};
    return Utils::ResultError(
        u"Field location: located field is not an integer or boolean"_s);
}

Utils::Result<QString> FieldReader::readNullTerminatedString(StringEncoding enc)
{
    const int ul = codeUnitLength(enc);
    QByteArray content;
    while (true) {
        QByteArray unit = m_buf.readBytes(ul);
        if (m_buf.hasError())
            return Utils::ResultError(u"Read past packet content in null-terminated string"_s);
        if (!codeUnitHasSetBit(unit, 0, ul))
            break; // terminating (all-zero) code unit
        content.append(unit);
    }
    return decodeStringBytes(content, enc);
}

// Read `length` bytes for a static- or dynamic-length string, taking the
// content as the prefix up to (but excluding) the first all-zero code unit
// (spec 6.4.12 / 6.4.14). The full `length` bytes are always consumed.
Utils::Result<QString> FieldReader::readLengthPrefixedString(qsizetype length, StringEncoding enc)
{
    if (length < 0)
        return Utils::ResultError(u"Negative string length"_s);

    const int ul = codeUnitLength(enc);
    // Spec 6.4.12 / 6.4.14: the length MUST be a whole number of code units.
    if (ul > 1 && (length % ul) != 0)
        return Utils::ResultError(
            u"String length is not a whole number of code units"_s);

    QByteArray bytes = m_buf.readBytes(length);
    if (m_buf.hasError())
        return Utils::ResultError(u"Read past packet content in string"_s);

    int contentLen = 0;
    while (contentLen + ul <= bytes.size()) {
        if (!codeUnitHasSetBit(bytes, contentLen, ul))
            break;
        contentLen += ul;
    }
    return decodeStringBytes(bytes.left(contentLen), enc);
}

} // namespace CommonTraceFormat
