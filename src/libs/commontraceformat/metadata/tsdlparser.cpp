// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "tsdlparser.h"
#include <utils/result.h>
#include "../schema/scalarfieldclasses.h"
#include "../schema/stringfieldclasses.h"
#include "../schema/compoundfieldclasses.h"
#include "../schema/clockclass.h"
#include "../schema/datastreamclass.h"
#include "../schema/eventrecordclass.h"
#include "../schema/traceclass.h"
#include "binary/bitbuffer.h"
#include <QUuid>
#include <QHash>
#include <functional>
#include <cctype>

namespace CommonTraceFormat {
using namespace Qt::StringLiterals;

// ── Lexer ──────────────────────────────────────────────────────────────────

enum class TT {
    Ident, IntLit, StrLit,
    LBrace, RBrace, LBracket, RBracket, LParen, RParen,
    Semi, Colon, Equals, Comma, Dot, Star, Lt, Gt,
    ColonEq,    // :=
    DotDotDot,  // ...
    Eof,
    Error,
};

struct Tok {
    TT      type   = TT::Error;
    QString text;
    qint64  intVal = 0;
};

class Lexer {
public:
    explicit Lexer(const QByteArray &src)
        : m_pos(src.constData()), m_end(m_pos + src.size())
    {
        advance();
    }

    Tok peek() const { return m_current; }
    Tok next()       { Tok t = m_current; advance(); return t; }
    bool at(TT t)            const { return m_current.type == t; }
    bool atIdent(const char *s) const
    { return m_current.type == TT::Ident && m_current.text == QLatin1StringView(s); }

private:
    void skipWhitespace()
    {
        while (m_pos < m_end) {
            if (*m_pos == '/' && m_pos + 1 < m_end && m_pos[1] == '*') {
                m_pos += 2;
                while (m_pos + 1 < m_end && !(m_pos[0] == '*' && m_pos[1] == '/'))
                    ++m_pos;
                if (m_pos + 1 < m_end) m_pos += 2;
            } else if (*m_pos == '/' && m_pos + 1 < m_end && m_pos[1] == '/') {
                while (m_pos < m_end && *m_pos != '\n') ++m_pos;
            } else if (std::isspace(static_cast<unsigned char>(*m_pos))) {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    void advance()
    {
        skipWhitespace();
        if (m_pos >= m_end) { m_current = {.type = TT::Eof, .text = {}}; return; }
        const char c = *m_pos;

        if (c == '"') {
            ++m_pos;
            QByteArray s;
            while (m_pos < m_end && *m_pos != '"') {
                if (*m_pos == '\\' && m_pos + 1 < m_end) ++m_pos;
                s.append(*m_pos++);
            }
            if (m_pos < m_end) ++m_pos;
            m_current = {.type = TT::StrLit, .text = QString::fromUtf8(s), .intVal = 0};
            return;
        }

        if (std::isdigit(static_cast<unsigned char>(c))
            || (c == '-' && m_pos + 1 < m_end && std::isdigit(static_cast<unsigned char>(m_pos[1])))) {
            bool neg = (c == '-');
            if (neg) ++m_pos;
            int base = 10;
            if (m_pos + 1 < m_end && *m_pos == '0' && (m_pos[1] == 'x' || m_pos[1] == 'X')) {
                base = 16; m_pos += 2;
            }
            QByteArray digits;
            while (m_pos < m_end && (std::isalnum(static_cast<unsigned char>(*m_pos))))
                digits.append(*m_pos++);
            bool ok;
            quint64 uval = digits.toULongLong(&ok, base);
            qint64 val = neg ? -static_cast<qint64>(uval) : static_cast<qint64>(uval);
            m_current = {.type = TT::IntLit, .text = QString::fromLatin1(digits), .intVal = val};
            return;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            QByteArray id;
            while (m_pos < m_end && (std::isalnum(static_cast<unsigned char>(*m_pos)) || *m_pos == '_'))
                id.append(*m_pos++);
            m_current = {.type = TT::Ident, .text = QString::fromLatin1(id), .intVal = 0};
            return;
        }

        if (c == ':' && m_pos + 1 < m_end && m_pos[1] == '=') { m_pos += 2; m_current = {.type = TT::ColonEq, .text = {}}; return; }
        if (c == '.' && m_pos + 2 < m_end && m_pos[1] == '.' && m_pos[2] == '.') { m_pos += 3; m_current = {.type = TT::DotDotDot, .text = {}}; return; }

        ++m_pos;
        switch (c) {
        case '{': m_current = {.type = TT::LBrace,   .text = {}}; return;
        case '}': m_current = {.type = TT::RBrace,   .text = {}}; return;
        case '[': m_current = {.type = TT::LBracket, .text = {}}; return;
        case ']': m_current = {.type = TT::RBracket, .text = {}}; return;
        case '(': m_current = {.type = TT::LParen,   .text = {}}; return;
        case ')': m_current = {.type = TT::RParen,   .text = {}}; return;
        case ';': m_current = {.type = TT::Semi,     .text = {}}; return;
        case ':': m_current = {.type = TT::Colon,    .text = {}}; return;
        case '=': m_current = {.type = TT::Equals,   .text = {}}; return;
        case ',': m_current = {.type = TT::Comma,    .text = {}}; return;
        case '.': m_current = {.type = TT::Dot,      .text = {}}; return;
        case '*': m_current = {.type = TT::Star,     .text = {}}; return;
        case '<': m_current = {.type = TT::Lt,       .text = {}}; return;
        case '>': m_current = {.type = TT::Gt,       .text = {}}; return;
        default:  m_current = {.type = TT::Error,    .text = {}}; return;
        }
    }

    const char *m_pos;
    const char *m_end;
    Tok         m_current;
};

// ── Type factory ────────────────────────────────────────────────────────────
// Creates a fresh FieldClassPtr each call (avoids sharing modifiable state).

using TypeFactory = std::function<FieldClassPtr()>;

// ── Helpers ─────────────────────────────────────────────────────────────────

// Walk a FieldClass tree and update all FieldLocation origins.
static void updateOrigins(FieldClass *fc, FieldLocation::Origin origin)
{
    if (!fc) return;
    switch (fc->kind) {
    case FieldClassKind::Structure: {
        auto &s = static_cast<StructureFC &>(*fc);
        for (auto &m : s.members) updateOrigins(m.fieldClass.get(), origin);
        break;
    }
    case FieldClassKind::StaticLengthArray:
        updateOrigins(static_cast<StaticLengthArrayFC &>(*fc).elementFieldClass.get(), origin);
        break;
    case FieldClassKind::DynamicLengthArray: {
        auto &a = static_cast<DynamicLengthArrayFC &>(*fc);
        a.lengthFieldLocation.origin = origin;
        updateOrigins(a.elementFieldClass.get(), origin);
        break;
    }
    case FieldClassKind::Variant: {
        auto &v = static_cast<VariantFC &>(*fc);
        v.selectorFieldLocation.origin = origin;
        for (auto &opt : v.options) updateOrigins(opt.fieldClass.get(), origin);
        break;
    }
    case FieldClassKind::Optional: {
        auto &o = static_cast<OptionalFC &>(*fc);
        o.selectorFieldLocation.origin = origin;
        updateOrigins(o.fieldClass.get(), origin);
        break;
    }
    default: break;
    }
}

static void assignPacketContextRoles(FieldClass *fc)
{
    if (!fc || fc->kind != FieldClassKind::Structure) return;
    for (auto &m : static_cast<StructureFC &>(*fc).members) {
        if (!m.fieldClass || m.fieldClass->kind != FieldClassKind::FixedLengthUInt) continue;
        auto &u = static_cast<FixedLengthUIntFC &>(*m.fieldClass);
        if (m.name == u"content_size"_s && !u.roles.contains(UIntRole::PacketContentLength))
            u.roles.append(UIntRole::PacketContentLength);
        else if (m.name == u"packet_size"_s && !u.roles.contains(UIntRole::PacketTotalLength))
            u.roles.append(UIntRole::PacketTotalLength);
        else if (m.name == u"packet_seq_num"_s && !u.roles.contains(UIntRole::PacketSequenceNumber))
            u.roles.append(UIntRole::PacketSequenceNumber);
        else if (m.name == u"events_discarded"_s && !u.roles.contains(UIntRole::DiscardedEventRecordCounterSnapshot))
            u.roles.append(UIntRole::DiscardedEventRecordCounterSnapshot);
        // timestamp_begin seeds the default clock for the packet; timestamp_end
        // bounds it (CTF 1.8 packet.context). Without these the packet-begin
        // clock value is never established and partial event-header timestamps
        // extend against 0, yielding wrong absolute times.
        else if (m.name == u"timestamp_begin"_s && !u.roles.contains(UIntRole::DefaultClockTimestamp))
            u.roles.append(UIntRole::DefaultClockTimestamp);
        else if (m.name == u"timestamp_end"_s && !u.roles.contains(UIntRole::PacketEndDefaultClockTimestamp))
            u.roles.append(UIntRole::PacketEndDefaultClockTimestamp);
    }
}

static void assignPacketHeaderRoles(FieldClass *fc)
{
    if (!fc || fc->kind != FieldClassKind::Structure) return;
    for (auto &m : static_cast<StructureFC &>(*fc).members) {
        if (!m.fieldClass || m.fieldClass->kind != FieldClassKind::FixedLengthUInt) continue;
        auto &u = static_cast<FixedLengthUIntFC &>(*m.fieldClass);
        if (m.name == u"magic"_s && !u.roles.contains(UIntRole::PacketMagicNumber))
            u.roles.append(UIntRole::PacketMagicNumber);
        else if (m.name == u"stream_id"_s && !u.roles.contains(UIntRole::DataStreamClassId))
            u.roles.append(UIntRole::DataStreamClassId);
    }
}

// ── Parser implementation ───────────────────────────────────────────────────

struct StreamDef {
    quint64       id = 0;
    FieldClassPtr eventHeaderFC;
    FieldClassPtr packetContextFC;
    // Clock referenced (via `map = clock.X.value`) by a timestamp field of this
    // stream; empty when the stream has no clocked field.
    QString       clockName;
};

struct EventDef {
    QString       name;
    quint64       id       = 0;
    quint64       streamId = 0;
    FieldClassPtr payload;
    FieldClassPtr specificContext;
};

class TsdlParserImpl {
public:
    explicit TsdlParserImpl(const QByteArray &tsdl) : lex(tsdl) {}

    Utils::Result<Schema> parse()
    {
        while (!lex.at(TT::Eof) && !m_error)
            parseTopDecl();
        if (m_error)
            return Utils::ResultError(m_errorMsg);
        return buildSchema();
    }

private:
    Lexer lex;
    bool   m_error = false;
    QString m_errorMsg;

    ByteOrder m_defaultByteOrder = ByteOrder::LittleEndian;

    // Type alias name → factory (creates a fresh FC per call)
    QHash<QString, TypeFactory> m_typeFactories;
    // Clock-mapped type alias name → clock name
    QHash<QString, QString>     m_clockMappings;
    // Named struct definitions (shared; used once per role assignment)
    QHash<QString, FieldClassPtr> m_namedStructs;

    QHash<quint64, StreamDef> m_streamDefs;
    QList<EventDef>           m_eventDefs;
    QList<ClockClass>         m_clockClasses;
    FieldClassPtr             m_packetHeaderFC;

    // Most recent clock name seen via `map = clock.X.value` while parsing the
    // current stream/event scope; used to link a stream to its default clock.
    QString                   m_pendingClockName;

    // ── Error / consume helpers ────────────────────────────────────────────

    void error(const QString &msg) { if (!m_error) { m_error = true; m_errorMsg = u"TSDL: "_s + msg; } }

    bool expect(TT t, const char *what)
    {
        if (!lex.at(t)) { error(u"expected %1"_s.arg(QString::fromLatin1(what))); return false; }
        lex.next(); return true;
    }

    QString consumeIdent()
    {
        if (!lex.at(TT::Ident)) { error(u"expected identifier"_s); return {}; }
        return lex.next().text;
    }

    qint64 consumeInt()
    {
        if (!lex.at(TT::IntLit)) { error(u"expected integer"_s); return 0; }
        return lex.next().intVal;
    }

    QString consumeStr()
    {
        if (!lex.at(TT::StrLit)) { error(u"expected string"_s); return {}; }
        return lex.next().text;
    }

    QString consumeAttrVal()
    {
        if (lex.at(TT::StrLit)) return lex.next().text;
        if (lex.at(TT::IntLit)) return QString::number(lex.next().intVal);
        if (lex.at(TT::Ident))  return lex.next().text;
        error(u"expected attribute value"_s); return {};
    }

    // Skip tokens until semicolon (brace-balanced, top-level scope)
    void skipToSemi()
    {
        int depth = 0;
        while (!lex.at(TT::Eof)) {
            auto t = lex.next();
            if (t.type == TT::LBrace) ++depth;
            else if (t.type == TT::RBrace) { if (depth) --depth; }
            else if (t.type == TT::Semi && depth == 0) return;
        }
    }

    // ── Top-level dispatch ────────────────────────────────────────────────

    void parseTopDecl()
    {
        if      (lex.atIdent("typealias"))  { parseTypealias(); expect(TT::Semi, ";"); }
        else if (lex.atIdent("typedef"))    { parseTypedef();   expect(TT::Semi, ";"); }
        else if (lex.atIdent("trace"))      { parseTraceBlock(); expect(TT::Semi, ";"); }
        else if (lex.atIdent("env"))        { parseEnvBlock();   expect(TT::Semi, ";"); }
        else if (lex.atIdent("clock"))      { parseClockBlock(); expect(TT::Semi, ";"); }
        else if (lex.atIdent("stream"))     { parseStreamBlock(); expect(TT::Semi, ";"); }
        else if (lex.atIdent("event"))      { parseEventBlock();  expect(TT::Semi, ";"); }
        else if (lex.atIdent("struct"))     { parseNamedStruct(); expect(TT::Semi, ";"); }
        else                                 skipToSemi();
    }

    // ── typealias / typedef ────────────────────────────────────────────────

    void parseTypealias()
    {
        lex.next(); // "typealias"
        QString clockName;
        TypeFactory factory = parseTypeFactory(clockName);
        if (m_error || !factory) return;
        if (!expect(TT::ColonEq, ":=")) return;
        QString alias = consumeIdent(); if (m_error) return;
        while (lex.at(TT::Ident)) alias += u" "_s + lex.next().text;
        m_typeFactories[alias] = factory;
        if (!clockName.isEmpty()) m_clockMappings[alias] = clockName;
    }

    void parseTypedef()
    {
        lex.next(); // "typedef"
        QString clockName;
        TypeFactory factory = parseTypeFactory(clockName);
        if (m_error || !factory) return;
        QString alias = consumeIdent(); if (m_error) return;
        while (lex.at(TT::Ident)) alias += u" "_s + lex.next().text;
        m_typeFactories[alias] = factory;
        if (!clockName.isEmpty()) m_clockMappings[alias] = clockName;
    }

    // ── trace ──────────────────────────────────────────────────────────────

    void parseTraceBlock()
    {
        lex.next();
        if (!expect(TT::LBrace, "{")) return;
        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) {
            if (lex.atIdent("byte_order")) {
                lex.next(); expect(TT::Equals, "=");
                QString v = consumeAttrVal();
                m_defaultByteOrder = (v == u"be"_s || v == u"network"_s)
                                     ? ByteOrder::BigEndian : ByteOrder::LittleEndian;
                expect(TT::Semi, ";");
            } else if (lex.atIdent("packet")) {
                lex.next(); expect(TT::Dot, "."); consumeIdent(); // "header"
                expect(TT::ColonEq, ":=");
                m_packetHeaderFC = parseTypeRef();
                if (!m_error) assignPacketHeaderRoles(m_packetHeaderFC.get());
                expect(TT::Semi, ";");
            } else {
                skipToSemi();
            }
        }
        expect(TT::RBrace, "}");
    }

    // ── env ────────────────────────────────────────────────────────────────

    void parseEnvBlock()
    {
        lex.next();
        expect(TT::LBrace, "{");
        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) skipToSemi();
        expect(TT::RBrace, "}");
    }

    // ── clock ─────────────────────────────────────────────────────────────

    void parseClockBlock()
    {
        lex.next();
        if (!expect(TT::LBrace, "{")) return;
        ClockClass cc;
        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) {
            if (lex.atIdent("name"))        { lex.next(); expect(TT::Equals,"="); cc.name        = consumeAttrVal(); expect(TT::Semi,";"); }
            else if (lex.atIdent("uuid"))   { lex.next(); expect(TT::Equals,"="); cc.uuid        = QUuid::fromString(consumeStr()); expect(TT::Semi,";"); }
            else if (lex.atIdent("description")) { lex.next(); expect(TT::Equals,"="); cc.description = consumeStr(); expect(TT::Semi,";"); }
            else if (lex.atIdent("freq"))   { lex.next(); expect(TT::Equals,"="); cc.frequency   = static_cast<quint64>(consumeInt()); expect(TT::Semi,";"); }
            else if (lex.atIdent("offset")) { lex.next(); expect(TT::Equals,"="); cc.offsetCycles= static_cast<quint64>(consumeInt()); expect(TT::Semi,";"); }
            else if (lex.atIdent("offset_s")) { lex.next(); expect(TT::Equals,"="); cc.offsetSeconds= consumeInt(); expect(TT::Semi,";"); }
            else if (lex.atIdent("absolute")) { lex.next(); expect(TT::Equals,"="); QString v = consumeAttrVal(); if (v==u"true"_s) cc.origin.isUnixEpoch=true; expect(TT::Semi,";"); }
            else if (lex.atIdent("precision")) { lex.next(); expect(TT::Equals,"="); cc.precision = static_cast<quint64>(consumeInt()); expect(TT::Semi,";"); }
            else skipToSemi();
        }
        expect(TT::RBrace, "}");
        if (!cc.name.isEmpty()) m_clockClasses.append(cc);
    }

    // ── stream ────────────────────────────────────────────────────────────

    void parseStreamBlock()
    {
        lex.next();
        if (!expect(TT::LBrace, "{")) return;
        StreamDef sd;
        m_pendingClockName.clear();
        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) {
            if (lex.atIdent("id")) {
                lex.next(); expect(TT::Equals,"="); sd.id = static_cast<quint64>(consumeInt()); expect(TT::Semi,";");
            } else if (lex.atIdent("event")) {
                lex.next(); expect(TT::Dot,"."); QString sub = consumeIdent(); expect(TT::ColonEq,":=");
                if (sub == u"header"_s) sd.eventHeaderFC = parseTypeRef(FieldLocation::Origin::EventRecordHeader);
                else parseTypeRef(); // ignore event.context
                expect(TT::Semi,";");
            } else if (lex.atIdent("packet")) {
                lex.next(); expect(TT::Dot,"."); consumeIdent(); expect(TT::ColonEq,":=");
                sd.packetContextFC = parseTypeRef(FieldLocation::Origin::PacketContext);
                expect(TT::Semi,";");
            } else {
                skipToSemi();
            }
        }
        expect(TT::RBrace, "}");
        sd.clockName = m_pendingClockName;
        m_streamDefs[sd.id] = sd;
    }

    // ── event ─────────────────────────────────────────────────────────────

    void parseEventBlock()
    {
        lex.next();
        if (!expect(TT::LBrace, "{")) return;
        EventDef ed;
        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) {
            if (lex.atIdent("name"))      { lex.next(); expect(TT::Equals,"="); ed.name     = consumeStr();   expect(TT::Semi,";"); }
            else if (lex.atIdent("id"))   { lex.next(); expect(TT::Equals,"="); ed.id       = static_cast<quint64>(consumeInt()); expect(TT::Semi,";"); }
            else if (lex.atIdent("stream_id")) { lex.next(); expect(TT::Equals,"="); ed.streamId = static_cast<quint64>(consumeInt()); expect(TT::Semi,";"); }
            else if (lex.atIdent("fields")) {
                lex.next(); expect(TT::ColonEq,":=");
                ed.payload = parseTypeRef(FieldLocation::Origin::EventRecordPayload);
                expect(TT::Semi,";");
            } else if (lex.atIdent("context")) {
                lex.next(); expect(TT::ColonEq,":=");
                ed.specificContext = parseTypeRef(FieldLocation::Origin::EventRecordSpecificContext);
                expect(TT::Semi,";");
            } else {
                skipToSemi();
            }
        }
        expect(TT::RBrace, "}");
        m_eventDefs.append(ed);
    }

    // ── named struct ──────────────────────────────────────────────────────

    void parseNamedStruct()
    {
        lex.next(); // "struct"
        if (!lex.at(TT::Ident)) { error(u"expected struct name"_s); return; }
        QString name = lex.next().text;
        if (!lex.at(TT::LBrace)) { error(u"expected '{'"_s); return; }
        auto fc = parseStructBody(FieldLocation::Origin::EventRecordPayload);
        if (!m_error && lex.atIdent("align")) {
            lex.next(); expect(TT::LParen,"("); qint64 al = consumeInt(); expect(TT::RParen,")");
            if (fc && fc->kind == FieldClassKind::Structure)
                static_cast<StructureFC &>(*fc).minimumAlignment = static_cast<int>(al);
        }
        m_namedStructs[name] = fc;
    }

    // ── Type reference (for := assignments) ──────────────────────────────

    FieldClassPtr parseTypeRef(FieldLocation::Origin origin = FieldLocation::Origin::EventRecordPayload)
    {
        if (lex.atIdent("struct")) {
            lex.next();
            if (lex.at(TT::LBrace)) {
                auto fc = parseStructBody(origin);
                if (!m_error && lex.atIdent("align")) {
                    lex.next(); expect(TT::LParen,"("); qint64 al = consumeInt(); expect(TT::RParen,")");
                    if (fc && fc->kind == FieldClassKind::Structure)
                        static_cast<StructureFC &>(*fc).minimumAlignment = static_cast<int>(al);
                }
                return fc;
            }
            if (!lex.at(TT::Ident)) { error(u"expected struct name"_s); return {}; }
            QString name = lex.next().text;
            auto it = m_namedStructs.find(name);
            if (it == m_namedStructs.end()) { error(u"unknown struct '%1'"_s.arg(name)); return {}; }
            // Update origins for this use of the named struct
            updateOrigins(it->get(), origin);
            return *it;
        }
        // Fall through to factory-based type expression
        QString clockName;
        TypeFactory f = parseTypeFactory(clockName, origin);
        if (!f) return {};
        return f();
    }

    // ── Factory-based type expression ─────────────────────────────────────
    // Returns a TypeFactory (callable → fresh FieldClassPtr each invocation).

    TypeFactory parseTypeFactory(QString &clockNameOut,
                                  FieldLocation::Origin origin = FieldLocation::Origin::EventRecordPayload)
    {
        if (lex.atIdent("integer") || lex.atIdent("int"))
            return parseIntegerFactory(clockNameOut);
        if (lex.atIdent("floating_point") || lex.atIdent("float"))
            return parseFloatFactory();
        if (lex.atIdent("string"))
            return parseStringFactory();
        if (lex.atIdent("enum"))
            return parseEnumFactory(origin);
        if (lex.atIdent("struct")) {
            lex.next();
            if (lex.at(TT::LBrace)) {
                auto fc = parseStructBody(origin);
                if (!m_error && lex.atIdent("align")) {
                    lex.next(); expect(TT::LParen,"("); qint64 al = consumeInt(); expect(TT::RParen,")");
                    if (fc && fc->kind == FieldClassKind::Structure)
                        static_cast<StructureFC &>(*fc).minimumAlignment = static_cast<int>(al);
                }
                return [fc] { return fc; }; // struct is already unique per parse
            }
            if (!lex.at(TT::Ident)) { error(u"expected struct name"_s); return {}; }
            QString name = lex.next().text;
            auto it = m_namedStructs.find(name);
            if (it == m_namedStructs.end()) { error(u"unknown struct '%1'"_s.arg(name)); return {}; }
            auto fc = *it;
            return [fc] { return fc; };
        }
        if (lex.atIdent("variant"))
            return parseVariantFactory(origin);

        // Type alias lookup (possibly multi-word like "unsigned long")
        if (lex.at(TT::Ident)) {
            QString name = lex.next().text;
            if (lex.at(TT::Ident)) {
                QString combined = name + u" "_s + lex.peek().text;
                if (m_typeFactories.contains(combined)) { lex.next(); name = combined; }
            }
            auto fit = m_typeFactories.find(name);
            if (fit != m_typeFactories.end()) {
                clockNameOut = m_clockMappings.value(name);
                return *fit; // return the factory (caller will invoke it)
            }
            auto sit = m_namedStructs.find(name);
            if (sit != m_namedStructs.end()) {
                auto fc = *sit;
                return [fc] { return fc; };
            }
            error(u"unknown type '%1'"_s.arg(name));
            return {};
        }
        error(u"expected type expression"_s);
        return {};
    }

    // ── integer factory ───────────────────────────────────────────────────

    TypeFactory parseIntegerFactory(QString &clockNameOut)
    {
        lex.next(); // "integer" or "int"
        if (!expect(TT::LBrace, "{")) return {};

        int size = 32, align = 1;
        bool isSigned = false;
        ByteOrder bo = m_defaultByteOrder;
        DisplayBase base = DisplayBase::Decimal;
        QString clockName;

        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) {
            QString attr = consumeIdent(); if (m_error) break;
            if (!expect(TT::Equals, "=")) break;
            if (attr == u"size"_s)        size    = static_cast<int>(consumeInt());
            else if (attr == u"align"_s)  align   = static_cast<int>(consumeInt());
            else if (attr == u"signed"_s) {
                if (lex.at(TT::IntLit)) isSigned = (lex.next().intVal != 0);
                else { QString v = consumeAttrVal(); isSigned = (v == u"true"_s || v == u"1"_s); }
            }
            else if (attr == u"byte_order"_s) {
                QString v = consumeAttrVal();
                bo = (v == u"be"_s || v == u"network"_s) ? ByteOrder::BigEndian : ByteOrder::LittleEndian;
            }
            else if (attr == u"base"_s) {
                QString v = consumeAttrVal();
                if (v==u"hex"_s||v==u"hexadecimal"_s||v==u"16"_s)     base = DisplayBase::Hexadecimal;
                else if (v==u"oct"_s||v==u"octal"_s||v==u"8"_s)       base = DisplayBase::Octal;
                else if (v==u"bin"_s||v==u"binary"_s||v==u"2"_s)      base = DisplayBase::Binary;
                else                                                    base = DisplayBase::Decimal;
            }
            else if (attr == u"encoding"_s) consumeAttrVal();
            else if (attr == u"map"_s) {
                // map = clock.X.value
                QString v = consumeAttrVal();
                if (v == u"clock"_s) {
                    if (lex.at(TT::Dot)) { lex.next(); clockName = consumeIdent(); }
                    if (lex.at(TT::Dot)) { lex.next(); consumeAttrVal(); } // "value"
                }
            }
            else consumeAttrVal();
            if (!lex.at(TT::RBrace)) expect(TT::Semi, ";");
        }
        expect(TT::RBrace, "}");
        if (m_error) return {};

        clockNameOut = clockName;
        bool isClocked = !clockName.isEmpty();
        if (isClocked)
            m_pendingClockName = clockName;

        if (isSigned) {
            return [size, align, bo, base]() -> FieldClassPtr {
                auto fc = std::make_shared<FixedLengthSIntFC>();
                fc->length = size; fc->alignment = align;
                fc->byteOrder = bo; fc->displayBase = base;
                return fc;
            };
        } else {
            return [size, align, bo, base, isClocked]() -> FieldClassPtr {
                auto fc = std::make_shared<FixedLengthUIntFC>();
                fc->length = size; fc->alignment = align;
                fc->byteOrder = bo; fc->displayBase = base;
                if (isClocked)
                    fc->roles.append(UIntRole::DefaultClockTimestamp);
                return fc;
            };
        }
    }

    // ── float factory ─────────────────────────────────────────────────────

    TypeFactory parseFloatFactory()
    {
        lex.next();
        if (!expect(TT::LBrace, "{")) return {};
        int expDig = 8, mantDig = 24, align = 1;
        ByteOrder bo = m_defaultByteOrder;
        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) {
            QString attr = consumeIdent(); if (m_error) break;
            if (!expect(TT::Equals,"=")) break;
            if (attr==u"exp_dig"_s) expDig = static_cast<int>(consumeInt());
            else if (attr==u"mant_dig"_s) mantDig = static_cast<int>(consumeInt());
            else if (attr==u"align"_s) align = static_cast<int>(consumeInt());
            else if (attr==u"byte_order"_s) { QString v=consumeAttrVal(); bo=(v==u"be"_s)?ByteOrder::BigEndian:ByteOrder::LittleEndian; }
            else consumeAttrVal();
            if (!lex.at(TT::RBrace)) expect(TT::Semi,";");
        }
        expect(TT::RBrace, "}");
        int len = expDig + mantDig;
        return [len, align, bo]() -> FieldClassPtr {
            auto fc = std::make_shared<FixedLengthFloatFC>();
            fc->length = len; fc->alignment = align; fc->byteOrder = bo;
            return fc;
        };
    }

    // ── string factory ────────────────────────────────────────────────────

    TypeFactory parseStringFactory()
    {
        lex.next(); // "string"
        if (lex.at(TT::LBrace)) {
            lex.next();
            while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) skipToSemi();
            expect(TT::RBrace, "}");
        }
        return []() -> FieldClassPtr { return std::make_shared<NullTerminatedStringFC>(); };
    }

    // ── enum factory ──────────────────────────────────────────────────────
    // Returns a factory producing the base-type FC, with enum mappings attached.
    // Also stores label→ranges in m_lastEnumLabels for the surrounding struct context.

    QHash<QString, QList<QPair<qint64,qint64>>> m_lastEnumLabels;

    TypeFactory parseEnumFactory(FieldLocation::Origin origin)
    {
        lex.next(); // "enum"
        if (lex.at(TT::Ident)) lex.next(); // optional enum name
        if (!expect(TT::Colon, ":")) return {};
        QString dummy;
        TypeFactory baseFactory = parseTypeFactory(dummy, origin);
        if (m_error || !baseFactory) return {};

        if (!expect(TT::LBrace, "{")) return {};

        QHash<QString, QList<QPair<qint64,qint64>>> labels;
        qint64 nextVal = 0;
        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) {
            QString label;
            if (lex.at(TT::Ident))  label = lex.next().text;
            else if (lex.at(TT::StrLit)) label = lex.next().text;
            else { error(u"expected enum label"_s); break; }
            if (m_error) break;
            if (lex.at(TT::Equals)) {
                lex.next();
                qint64 lo = consumeInt();
                if (lex.at(TT::DotDotDot)) {
                    lex.next(); qint64 hi = consumeInt();
                    labels[label].append({lo, hi}); nextVal = hi + 1;
                } else {
                    labels[label].append({lo, lo}); nextVal = lo + 1;
                }
            } else {
                labels[label].append({nextVal, nextVal}); ++nextVal;
            }
            if (lex.at(TT::Comma)) lex.next();
        }
        expect(TT::RBrace, "}");
        m_lastEnumLabels = labels;

        // Build IntMappings for display
        IntMappings mappings;
        for (auto it = labels.begin(); it != labels.end(); ++it) {
            QList<IntMappingRange> ranges;
            for (const auto &r : it.value())
                ranges.append({static_cast<qint64>(r.first), static_cast<qint64>(r.second)});
            mappings[it.key()] = ranges;
        }

        return [baseFactory, mappings]() -> FieldClassPtr {
            auto fc = baseFactory();
            if (fc && fc->kind == FieldClassKind::FixedLengthUInt)
                static_cast<FixedLengthUIntFC &>(*fc).mappings = mappings;
            else if (fc && fc->kind == FieldClassKind::FixedLengthSInt)
                static_cast<FixedLengthSIntFC &>(*fc).mappings = mappings;
            return fc;
        };
    }

    // ── struct body ───────────────────────────────────────────────────────

    FieldClassPtr parseStructBody(FieldLocation::Origin origin)
    {
        if (!expect(TT::LBrace, "{")) return {};
        auto sfc = std::make_shared<StructureFC>();

        // Track enum labels per field for variant resolution
        QHash<QString, QHash<QString, QList<QPair<qint64,qint64>>>> fieldEnums;

        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) {
            m_lastEnumLabels.clear();

            QString clockName;
            TypeFactory factory = parseTypeFactory(clockName, origin);
            if (m_error || !factory) { skipToSemi(); continue; }

            // Capture enum labels (m_lastEnumLabels set by parseEnumFactory)
            auto capturedEnum = m_lastEnumLabels;

            if (!lex.at(TT::Ident)) { error(u"expected field name"_s); return {}; }
            QString fieldName = lex.next().text;

            if (!capturedEnum.isEmpty())
                fieldEnums[fieldName] = capturedEnum;

            // Create the field's FC
            FieldClassPtr typeFC = factory();
            if (m_error || !typeFC) { skipToSemi(); continue; }

            // Clock role already embedded by factory; also annotate struct members
            // inside variant option structs later. Nothing more needed here.

            // Array suffix
            if (lex.at(TT::LBracket)) {
                lex.next();
                if (lex.at(TT::IntLit)) {
                    int count = static_cast<int>(consumeInt());
                    expect(TT::RBracket, "]");
                    auto arr = std::make_shared<StaticLengthArrayFC>();
                    arr->elementFieldClass = typeFC;
                    arr->length = count;
                    sfc->members.append({fieldName, arr, {}});
                } else if (lex.at(TT::Ident)) {
                    QString lenField = lex.next().text;
                    expect(TT::RBracket, "]");
                    auto arr = std::make_shared<DynamicLengthArrayFC>();
                    arr->elementFieldClass = typeFC;
                    arr->lengthFieldLocation.origin = origin;
                    arr->lengthFieldLocation.path = {std::optional<QString>(lenField)};
                    sfc->members.append({fieldName, arr, {}});
                } else {
                    error(u"expected array length"_s); return {};
                }
            } else {
                sfc->members.append({fieldName, typeFC, {}});
            }
            expect(TT::Semi, ";");
        }
        expect(TT::RBrace, "}");

        // Fix up variant selector values from collected enum labels
        for (auto &m : sfc->members) {
            if (!m.fieldClass || m.fieldClass->kind != FieldClassKind::Variant) continue;
            auto &vfc = static_cast<VariantFC &>(*m.fieldClass);
            if (vfc.selectorFieldLocation.path.isEmpty()) continue;
            const auto &selectorName = vfc.selectorFieldLocation.path.last();
            if (!selectorName) continue;
            auto it = fieldEnums.find(*selectorName);
            if (it == fieldEnums.end()) continue;
            const auto &enumLabels = *it;
            for (auto &opt : vfc.options) {
                auto elit = enumLabels.find(opt.name);
                if (elit == enumLabels.end()) continue;
                for (const auto &r : *elit) {
                    if (r.first == r.second) opt.selectorValues.append(r.first);
                    else                     opt.selectorRanges.append({r.first, r.second});
                }
            }
        }

        return sfc;
    }

    // ── variant factory ───────────────────────────────────────────────────

    TypeFactory parseVariantFactory(FieldLocation::Origin origin)
    {
        lex.next(); // "variant"
        if (!expect(TT::Lt, "<")) return {};
        QString selectorName = consumeIdent();
        if (!expect(TT::Gt, ">")) return {};
        if (lex.at(TT::Ident)) lex.next(); // optional variant name

        if (!expect(TT::LBrace, "{")) return {};

        auto vfc = std::make_shared<VariantFC>();
        vfc->selectorFieldLocation.origin = origin;
        vfc->selectorFieldLocation.path = {std::optional<QString>(selectorName)};

        while (!lex.at(TT::RBrace) && !lex.at(TT::Eof) && !m_error) {
            QString clockName;
            TypeFactory f = parseTypeFactory(clockName, origin);
            if (m_error || !f) { skipToSemi(); continue; }
            if (!lex.at(TT::Ident)) { error(u"expected variant option name"_s); return {}; }
            QString optName = lex.next().text;
            VariantOption opt;
            opt.fieldClass = f();
            opt.name = optName;
            vfc->options.append(std::move(opt));
            expect(TT::Semi, ";");
        }
        expect(TT::RBrace, "}");

        return [vfc] { return vfc; };
    }

    // ── Finalize / build Schema ───────────────────────────────────────────

    // Mark the CTF 1.8 event-header fields with their CTF2 roles so the binary
    // reader can locate them by role rather than by name: the top-level `id`
    // and, inside the compact/extended variant, the per-option `id` and
    // `timestamp` (CTF 1.8 §6.1 recommended event-header layout).
    void markEventHeaderId(FieldClass *fc)
    {
        if (!fc || fc->kind != FieldClassKind::Structure) return;
        for (auto &m : static_cast<StructureFC &>(*fc).members) {
            if (m.name == u"id"_s && m.fieldClass
                && m.fieldClass->kind == FieldClassKind::FixedLengthUInt) {
                auto &u = static_cast<FixedLengthUIntFC &>(*m.fieldClass);
                if (!u.roles.contains(UIntRole::EventRecordClassId))
                    u.roles.append(UIntRole::EventRecordClassId);
            } else if (m.fieldClass && m.fieldClass->kind == FieldClassKind::Variant) {
                auto &vfc = static_cast<VariantFC &>(*m.fieldClass);
                for (auto &opt : vfc.options) {
                    if (!opt.fieldClass || opt.fieldClass->kind != FieldClassKind::Structure)
                        continue;
                    for (auto &im : static_cast<StructureFC &>(*opt.fieldClass).members) {
                        if (!im.fieldClass || im.fieldClass->kind != FieldClassKind::FixedLengthUInt)
                            continue;
                        auto &iu = static_cast<FixedLengthUIntFC &>(*im.fieldClass);
                        if (im.name == u"id"_s && !iu.roles.contains(UIntRole::EventRecordClassId))
                            iu.roles.append(UIntRole::EventRecordClassId);
                        else if (im.name == u"timestamp"_s
                                 && !iu.roles.contains(UIntRole::DefaultClockTimestamp))
                            iu.roles.append(UIntRole::DefaultClockTimestamp);
                    }
                }
            }
        }
    }

    Utils::Result<Schema> buildSchema()
    {
        Schema schema;
        schema.clockClasses = m_clockClasses;

        if (m_packetHeaderFC) {
            schema.traceClass.emplace();
            schema.traceClass->packetHeaderFieldClass = m_packetHeaderFC;
        }

        // Build DataStreamClasses from stream definitions
        for (auto it = m_streamDefs.begin(); it != m_streamDefs.end(); ++it) {
            const StreamDef &sd = it.value();
            DataStreamClass dsc;
            dsc.id = sd.id;

            if (sd.packetContextFC) {
                updateOrigins(sd.packetContextFC.get(), FieldLocation::Origin::PacketContext);
                assignPacketContextRoles(sd.packetContextFC.get());
                dsc.packetContextFieldClass = sd.packetContextFC;
            }

            if (sd.eventHeaderFC) {
                updateOrigins(sd.eventHeaderFC.get(), FieldLocation::Origin::EventRecordHeader);
                markEventHeaderId(sd.eventHeaderFC.get());
                dsc.eventRecordHeaderFieldClass = sd.eventHeaderFC;
            }

            // Default clock: the clock this stream's timestamp field maps to,
            // falling back to the first declared clock class.
            if (!sd.clockName.isEmpty())
                dsc.defaultClockClassName = sd.clockName;
            else if (!m_clockClasses.isEmpty())
                dsc.defaultClockClassName = m_clockClasses.first().name;

            schema.dataStreamClasses.append(dsc);
        }

        // If no streams defined, create default stream 0
        if (schema.dataStreamClasses.isEmpty()) {
            DataStreamClass dsc;
            dsc.id = 0;
            if (!m_clockClasses.isEmpty())
                dsc.defaultClockClassName = m_clockClasses.first().name;
            schema.dataStreamClasses.append(dsc);
        }

        // Add event record classes to their respective streams
        for (const auto &ed : m_eventDefs) {
            DataStreamClass *dsc = nullptr;
            for (auto &d : schema.dataStreamClasses)
                if (d.id == ed.streamId) { dsc = &d; break; }
            if (!dsc) {
                DataStreamClass newDsc;
                newDsc.id = ed.streamId;
                if (!m_clockClasses.isEmpty())
                    newDsc.defaultClockClassName = m_clockClasses.first().name;
                schema.dataStreamClasses.append(newDsc);
                dsc = &schema.dataStreamClasses.last();
            }
            EventRecordClass erc;
            erc.id   = ed.id;
            erc.name = ed.name;
            erc.payloadFieldClass         = ed.payload;
            erc.specificContextFieldClass = ed.specificContext;
            dsc->eventRecordClasses.append(erc);
        }

        return schema;
    }
};

// ── Public API ──────────────────────────────────────────────────────────────

Utils::Result<Schema> TsdlParser::parse(const QByteArray &tsdl)
{
    TsdlParserImpl impl(tsdl);
    return impl.parse();
}

} // namespace CommonTraceFormat
