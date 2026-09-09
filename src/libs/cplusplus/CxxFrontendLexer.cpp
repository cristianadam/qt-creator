// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendLexer.h"

#include <cplusplus/Lexer.h>
#include <cplusplus/SimpleLexer.h>

#include <cxx/lexer.h>
#include <cxx/token.h>

#include <QHash>

#include <cctype>
#include <memory>
#include <optional>

using namespace CPlusPlus;

namespace {

// What the previous chunk of text left unfinished, laid out the way
// SimpleLexer lays out the state it hands to its caller: the kind of the
// token we are in the middle of, and whether the chunk ended on a backslash.
struct ResumeState
{
    int kind = T_EOF_SYMBOL;
    bool newlineExpected = false;

    static ResumeState fromInt(int state)
    {
        if (state <= 0)
            return {};
        return {state & 0x7f, (state & 0x80) != 0};
    }

    int toInt() const { return (kind & 0x7f) | (newlineExpected ? 0x80 : 0); }
};

bool isComment(int kind)
{
    return kind == T_COMMENT || kind == T_DOXY_COMMENT || kind == T_CPP_COMMENT
           || kind == T_CPP_DOXY_COMMENT;
}

bool isRawString(int kind)
{
    return kind >= T_FIRST_RAW_STRING_LITERAL && kind <= T_LAST_RAW_STRING_LITERAL;
}

// Maps punctuation and operators between the two token sets by how they are
// spelled, so that neither list has to be written out here. Keywords are left
// to Lexer::classify(), which is the only one of the two that knows the Qt and
// Objective-C ones and which of them the language features in effect allow.
auto punctuationMap() -> const QHash<QByteArray, int> &
{
    static const QHash<QByteArray, int> map = [] {
        QHash<QByteArray, int> result;
        for (int kind = T_FIRST_PUNCTUATION_OR_OPERATOR;
             kind <= T_LAST_PUNCTUATION_OR_OPERATOR; ++kind) {
            result.insert(QByteArray(Token::name(kind)), kind);
        }
        return result;
    }();
    return map;
}

bool isIdentifierStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'
           || static_cast<unsigned char>(c) >= 0x80;
}

// A block comment is doxygen when it opens with /** or /*!, optionally
// followed by <, and then whitespace or nothing at all. The empty /**/ is
// not, and neither is a banner such as /*******.
int blockCommentKind(QByteArrayView text)
{
    if (text.size() < 3)
        return T_COMMENT;

    const char marker = text[2];
    if (marker != '*' && marker != '!')
        return T_COMMENT;

    qsizetype i = 3;
    if (marker == '*' && i < text.size() && text[i] == '/')
        return T_COMMENT;
    if (i < text.size() && text[i] == '<')
        ++i;
    if (i >= text.size())
        return T_DOXY_COMMENT;
    return std::isspace(static_cast<unsigned char>(text[i])) ? T_DOXY_COMMENT : T_COMMENT;
}

// /// and //! are doxygen, // is not.
int lineCommentKind(QByteArrayView text)
{
    const bool doxygen = text.size() > 2 && (text[2] == '/' || text[2] == '!');
    return doxygen ? T_CPP_DOXY_COMMENT : T_CPP_COMMENT;
}

int commentKind(QByteArrayView text)
{
    return text.startsWith("/*") ? blockCommentKind(text) : lineCommentKind(text);
}

// R"delim( ... )delim" -- the delimiter, or nothing if this is not raw.
std::optional<QByteArray> rawStringDelimiter(QByteArrayView text)
{
    const qsizetype quote = text.indexOf('"');
    if (quote <= 0 || text[quote - 1] != 'R')
        return std::nullopt;
    const qsizetype paren = text.indexOf('(', quote);
    if (paren < 0)
        return QByteArray();        // R"delim, still short of the (
    return QByteArray(text.mid(quote + 1, paren - quote - 1));
}

// One past \a what, searching from \a from, or -1.
qsizetype afterMatch(QByteArrayView text, QByteArrayView what, qsizetype from)
{
    const qsizetype at = text.indexOf(what, from);
    return at < 0 ? -1 : at + what.size();
}

// One past the end of a raw string whose delimiter is not known: the first
// quote that follows a closing parenthesis, which is the rule the built-in
// lexer uses when it has nothing better. It ends "...)..." on the ", so a
// literal whose body contains a parenthesis and a quote ends early -- and
// that is the answer the callers already read.
qsizetype looseRawStringEnd(QByteArrayView text, qsizetype from)
{
    bool passedParen = false;
    for (qsizetype i = from; i < text.size(); ++i) {
        if (text[i] == ')')
            passedParen = true;
        else if (passedParen && text[i] == '"')
            return i + 1;
    }
    return -1;
}

int stringLiteralKind(QByteArrayView text, bool raw)
{
    const QByteArrayView prefix = text.left(text.indexOf(raw ? 'R' : '"'));
    if (prefix == "L")
        return raw ? T_RAW_WIDE_STRING_LITERAL : T_WIDE_STRING_LITERAL;
    if (prefix == "u8")
        return raw ? T_RAW_UTF8_STRING_LITERAL : T_UTF8_STRING_LITERAL;
    if (prefix == "u")
        return raw ? T_RAW_UTF16_STRING_LITERAL : T_UTF16_STRING_LITERAL;
    if (prefix == "U")
        return raw ? T_RAW_UTF32_STRING_LITERAL : T_UTF32_STRING_LITERAL;
    return raw ? T_RAW_STRING_LITERAL : T_STRING_LITERAL;
}

int charLiteralKind(QByteArrayView text)
{
    const QByteArrayView prefix = text.left(text.indexOf('\''));
    if (prefix == "L")
        return T_WIDE_CHAR_LITERAL;
    if (prefix == "u")
        return T_UTF16_CHAR_LITERAL;
    if (prefix == "U")
        return T_UTF32_CHAR_LITERAL;
    return T_CHAR_LITERAL;
}

// Where the literal proper ends, that is one past its closing quote, or -1 if
// the chunk of text ran out before the quote did. Everything after it is the
// ud-suffix. Raw strings end at )delim", the others at an unescaped quote.
qsizetype literalEnd(QByteArrayView text, char quote, bool raw)
{
    if (raw) {
        const QByteArray terminator = ')' + rawStringDelimiter(text).value_or(QByteArray()) + '"';
        const qsizetype open = text.indexOf('(');
        if (open < 0)
            return -1;
        const qsizetype close = text.indexOf(terminator, open);
        return close < 0 ? -1 : close + terminator.size();
    }

    const qsizetype open = text.indexOf(quote);
    if (open < 0)
        return -1;
    for (qsizetype i = open + 1; i < text.size(); ++i) {
        if (text[i] == '\\')
            ++i;
        else if (text[i] == quote)
            return i + 1;
    }
    return -1;
}

// A ud-suffix is what follows the closing quote of a finished literal.
bool hasUserDefinedSuffix(QByteArrayView text, char quote, bool raw)
{
    const qsizetype end = literalEnd(text, quote, raw);
    return end > 0 && end < text.size();
}

// A numeric literal carries a ud-suffix if what trails the digits is not one
// of the suffixes the language itself defines. The set mirrors the one
// Lexer::scanOptionalIntegerSuffix and scanOptionalFloatingSuffix accept.
bool numericLiteralHasUserDefinedSuffix(QByteArrayView text, LanguageFeatures features)
{
    if (!features.cxx11Enabled)
        return false;

    // Find where the trailing run of letters starts. Digits, the radix
    // prefix, the exponent and the C++14 digit separators are not it.
    qsizetype suffix = text.size();
    while (suffix > 0) {
        const char c = text[suffix - 1];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            --suffix;
        else
            break;
    }
    if (suffix == 0 || suffix == text.size())
        return false;

    // A hexadecimal literal ends in letters that are digits, and its exponent
    // is introduced by p rather than e; back off over those.
    const bool hex = text.startsWith("0x") || text.startsWith("0X");
    if (hex) {
        suffix = qMax(suffix, qsizetype(2));    // the x of 0x is not a suffix
        while (suffix < text.size()) {
            const char c = text[suffix];
            const bool hexDigit = std::isdigit(static_cast<unsigned char>(c))
                                  || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!hexDigit)
                break;
            ++suffix;
        }
    }

    const QByteArray rest = QByteArray(text.mid(suffix)).toLower();
    if (rest.isEmpty())
        return false;

    static const QByteArrayList standard = {
        "f", "l",                                       // floating point
        "u", "ll", "lu", "llu", "ul", "ull", "i64",     // integer
    };
    if (standard.contains(rest))
        return false;
    if (features.cxx23Enabled && (rest == "z" || rest == "zu" || rest == "uz"))
        return false;
    return true;
}

// Walks the UTF-8 bytes once and records, for every byte offset, how many
// UTF-16 code units precede it. Qt Creator addresses text in UTF-16, the
// scanner in bytes.
class Utf16Offsets
{
public:
    explicit Utf16Offsets(QByteArrayView bytes)
    {
        m_offsets.resize(bytes.size() + 1);
        int utf16 = 0;
        for (qsizetype i = 0; i < bytes.size(); ++i) {
            m_offsets[i] = utf16;
            const auto c = static_cast<unsigned char>(bytes[i]);
            if (c < 0x80 || c >= 0xc0)          // not a continuation byte
                utf16 += c >= 0xf0 ? 2 : 1;     // outside the BMP: a surrogate pair
        }
        m_offsets[bytes.size()] = utf16;
    }

    int at(qsizetype byteOffset) const
    {
        if (byteOffset <= 0)
            return 0;
        if (byteOffset >= m_offsets.size())
            return m_offsets.last();
        return m_offsets.at(byteOffset);
    }

private:
    QList<int> m_offsets;
};

} // namespace

namespace CPlusPlus {

// Lexes one chunk of text. Split out from CxxFrontendLexer so that the state
// belonging to a single run is not mixed up with the state that survives
// between runs.
class CxxFrontendLexerRun
{
public:
    CxxFrontendLexerRun(QByteArrayView bytes,
                        LanguageFeatures features,
                        bool skipComments,
                        bool ppMode)
        : m_bytes(bytes)
        , m_utf16(bytes)
        , m_features(features)
        , m_skipComments(skipComments)
        , m_ppMode(ppMode)
    {}

    Tokens run(ResumeState resume, const QByteArray &expectedRawStringSuffix);

    ResumeState endState() const { return m_state; }
    QByteArray expectedRawStringSuffix() const { return m_expectedRawStringSuffix; }
    bool endedJoined() const { return m_endedJoined; }

private:
    // Emits the token the previous chunk left unfinished. Returns the byte
    // offset to carry on scanning from, or -1 if this whole chunk was that
    // token.
    qsizetype resumeUnfinishedToken(ResumeState resume, Tokens &tokens);

    // Works out whether a token that runs to the end of the chunk is in fact
    // unfinished, and records what the next chunk has to resume.
    void recordEndState(int kind, QByteArrayView text);

    // A chunk may end on a line splice. If its newline is in this chunk too
    // the splice is complete and the caller is told the chunk ended joined;
    // if the backslash is the last thing there, the newline is in the next
    // chunk and that is what the state has to say.
    void finishSpliceState();

    void appendToken(Tokens &tokens, int kind, qsizetype begin, qsizetype end,
                     bool userDefinedLiteral = false);
    void skipToken(qsizetype end);

    // What lies between the end of the previous token and the start of this
    // one. Worked out here rather than taken from the scanner's own flags, so
    // that the answer stays right where this class restarts the scanner part
    // way through the buffer.
    struct LeadingFlags
    {
        bool newline = false;
        bool whitespace = false;
        bool joined = false;
    };
    LeadingFlags leadingFlags(qsizetype begin) const;

    QByteArrayView tokenBytes(qsizetype begin, qsizetype end) const
    { return m_bytes.mid(begin, end - begin); }

    QByteArrayView m_bytes;
    Utf16Offsets m_utf16;
    LanguageFeatures m_features;
    bool m_skipComments = false;
    bool m_ppMode = false;

    qsizetype m_previousEnd = 0;
    bool m_atStart = true;
    // The previous chunk ended on a backslash, so the newline that begins
    // this one belongs to the splice: the first token here does not start a
    // line, it continues the previous one.
    bool m_resumedAfterSplice = false;
    ResumeState m_state;
    QByteArray m_expectedRawStringSuffix;
    bool m_endedJoined = false;
};

auto CxxFrontendLexerRun::leadingFlags(qsizetype begin) const -> LeadingFlags
{
    // Mirrors what the built-in lexer does while skipping whitespace: a
    // backslash means the next newline is spliced away rather than ending a
    // line, and each newline settles the two flags afresh -- so a splice
    // followed by a blank line does start a line after all.
    bool newlineExpected = m_resumedAfterSplice;

    LeadingFlags flags;
    flags.newline = m_atStart && !m_resumedAfterSplice;
    flags.joined = m_atStart && m_resumedAfterSplice;

    for (qsizetype i = m_previousEnd; i < begin; ++i) {
        const char c = m_bytes[i];
        if (c == '\\') {
            newlineExpected = true;
        } else if (c == '\n') {
            flags.joined = newlineExpected;
            flags.newline = !newlineExpected;
            newlineExpected = false;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            flags.whitespace = true;
        }
    }

    return flags;
}

void CxxFrontendLexerRun::appendToken(Tokens &tokens, int kind, qsizetype begin,
                                      qsizetype end, bool userDefinedLiteral)
{
    Token token;
    token.f.kind = unsigned(kind);
    token.f.userDefinedLiteral = userDefinedLiteral;
    token.byteOffset = unsigned(begin);
    token.f.bytes = unsigned(end - begin);
    token.utf16charOffset = unsigned(m_utf16.at(begin));
    token.f.utf16chars = unsigned(m_utf16.at(end) - m_utf16.at(begin));

    const LeadingFlags flags = leadingFlags(begin);
    token.f.newline = flags.newline;
    token.f.whitespace = flags.whitespace;
    token.f.joined = flags.joined;

    m_previousEnd = end;
    m_atStart = false;

    tokens.append(token);
}

void CxxFrontendLexerRun::skipToken(qsizetype end)
{
    m_previousEnd = end;
    m_atStart = false;
}

void CxxFrontendLexerRun::recordEndState(int kind, QByteArrayView text)
{
    m_state = {};
    m_expectedRawStringSuffix.clear();

    if (kind == T_COMMENT || kind == T_DOXY_COMMENT) {
        if (!text.endsWith("*/") || text.size() < 4)
            m_state.kind = kind;
    } else if (kind == T_CPP_COMMENT || kind == T_CPP_DOXY_COMMENT) {
        if (text.endsWith('\\'))
            m_state.kind = kind;
    } else if (isRawString(kind)) {
        if (literalEnd(text, '"', true) < 0) {
            m_state.kind = kind;
            // The terminator this literal is waiting for, ")delim", rather
            // than the delimiter on its own. That is what SimpleLexer reports
            // and what a caller carrying it from one line to the next reads:
            // the highlighter uses it both to tell that the next line
            // continues a raw string and to find the closing delimiter in it.
            if (const std::optional<QByteArray> delimiter = rawStringDelimiter(text))
                m_expectedRawStringSuffix = ')' + *delimiter + '"';
        }
    } else if (kind >= T_FIRST_STRING_LITERAL && kind <= T_LAST_STRING_LITERAL) {
        if (literalEnd(text, '"', false) < 0)
            m_state.kind = kind;
    }
}

void CxxFrontendLexerRun::finishSpliceState()
{
    m_state.newlineExpected = false;
    m_endedJoined = false;

    const qsizetype backslash = m_bytes.lastIndexOf('\\');
    if (backslash < 0)
        return;

    bool sawNewline = false;
    for (qsizetype i = backslash + 1; i < m_bytes.size(); ++i) {
        if (!std::isspace(static_cast<unsigned char>(m_bytes[i])))
            return;                 // the backslash is not the last thing here
        sawNewline = sawNewline || m_bytes[i] == '\n';
    }

    if (sawNewline)
        m_endedJoined = true;
    else
        m_state.newlineExpected = true;
}

qsizetype CxxFrontendLexerRun::resumeUnfinishedToken(ResumeState resume, Tokens &tokens)
{
    if (resume.kind == T_EOF_SYMBOL)
        return 0;

    // Whitespace in front of what is being resumed is not part of it: the
    // built-in lexer skips it before the token starts, and an editor line
    // inside a block comment is usually indented.
    qsizetype begin = 0;
    while (begin < m_bytes.size()
           && std::isspace(static_cast<unsigned char>(m_bytes[begin]))) {
        ++begin;
    }

    // Nothing but whitespace: an empty line inside a comment produces no
    // token at all, and we are still inside whatever we were inside.
    if (begin == m_bytes.size()) {
        m_state = resume;
        return -1;
    }

    qsizetype end = m_bytes.size();
    bool finished = false;

    if (resume.kind == T_COMMENT || resume.kind == T_DOXY_COMMENT) {
        const qsizetype close = m_bytes.indexOf("*/", begin);
        if (close >= 0) {
            end = close + 2;
            finished = true;
        }
    } else if (resume.kind == T_CPP_COMMENT || resume.kind == T_CPP_DOXY_COMMENT) {
        // Only reachable through a line splice, so it ends with this chunk
        // unless the chunk is spliced again.
        finished = !m_bytes.endsWith('\\');
    } else if (isRawString(resume.kind)) {
        // The terminator the caller carried over. Without one -- a caller can
        // resume knowing only that a raw string is open -- the built-in lexer
        // falls back to matching a ')' and, after it, a '"', and so does this.
        const qsizetype close = m_expectedRawStringSuffix.isEmpty()
                                    ? looseRawStringEnd(m_bytes, begin)
                                    : afterMatch(m_bytes, m_expectedRawStringSuffix, begin);
        if (close >= 0) {
            end = close;
            finished = true;
        }
    } else {
        const qsizetype close = m_bytes.indexOf('"', begin);
        if (close >= 0) {
            end = close + 1;
            finished = true;
        }
    }

    // What follows the closing quote of a literal is its ud-suffix.
    bool userDefinedLiteral = false;
    if (finished && !isComment(resume.kind) && end < m_bytes.size()
        && m_features.cxx11Enabled && isIdentifierStart(m_bytes[end])) {
        userDefinedLiteral = true;
        while (end < m_bytes.size()
               && (std::isalnum(static_cast<unsigned char>(m_bytes[end]))
                   || m_bytes[end] == '_'
                   || static_cast<unsigned char>(m_bytes[end]) >= 0x80)) {
            ++end;
        }
    }

    if (isComment(resume.kind) && m_skipComments)
        skipToken(end);
    else
        appendToken(tokens, resume.kind, begin, end, userDefinedLiteral);

    if (!finished) {
        m_state = resume;
        return -1;
    }

    m_state = {};
    m_expectedRawStringSuffix.clear();
    return end;
}

Tokens CxxFrontendLexerRun::run(ResumeState resume, const QByteArray &expectedRawStringSuffix)
{
    Tokens tokens;
    m_expectedRawStringSuffix = expectedRawStringSuffix;
    m_resumedAfterSplice = resume.newlineExpected;

    qsizetype base = resumeUnfinishedToken(resume, tokens);
    if (base < 0) {
        finishSpliceState();
        return tokens;
    }

    auto makeLexer = [this](qsizetype from) {
        auto lexer = std::make_unique<cxx::Lexer>(
            std::string_view(m_bytes.data() + from, size_t(m_bytes.size() - from)));
        lexer->setKeepComments(true);
        lexer->setPreprocessing(m_ppMode);
        return lexer;
    };

    std::unique_ptr<cxx::Lexer> lexer = makeLexer(base);

    // #include and its relatives take a header name, which the scanner does
    // not produce: between the < and the > it would hand out ordinary tokens.
    bool inDirective = false;
    bool expectHeaderName = false;

    while (lexer->next() != cxx::TokenKind::T_EOF_SYMBOL) {
        qsizetype begin = base + lexer->tokenPos();
        const qsizetype end = begin + lexer->tokenLength();

        // The scanner folds a line splice in front of a token into that
        // token's extent. The built-in lexer starts the token after the
        // splice and marks it joined instead, which leadingFlags() below
        // works out from the gap, so trim the splice off the front.
        while (begin < end
               && (m_bytes[begin] == '\\'
                   || std::isspace(static_cast<unsigned char>(m_bytes[begin])))) {
            ++begin;
        }
        if (begin == end)
            begin = base + lexer->tokenPos();    // nothing but the splice

        // A splice inside a token is not in the text the scanner reports, and
        // it is the text that says which token this is.
        const std::string_view clean = lexer->tokenText();
        const QByteArrayView text
            = lexer->tokenIsClean()
                  ? tokenBytes(begin, end)
                  : QByteArrayView(clean.data(), qsizetype(clean.size()));

        if (expectHeaderName && text == "<") {
            // Once the built-in lexer is looking for a header name it takes
            // everything up to the > as one, and everything that is left if
            // there is no >. A newline does not stop it -- which only shows
            // on a malformed directive, and only when a whole file is lexed
            // at once rather than a line at a time.
            const qsizetype close = m_bytes.indexOf('>', begin);
            const qsizetype headerEnd = close < 0 ? m_bytes.size() : close + 1;
            appendToken(tokens, T_ANGLE_STRING_LITERAL, begin, headerEnd);
            base = headerEnd;
            lexer = makeLexer(base);
            expectHeaderName = false;
            continue;
        }
        expectHeaderName = false;

        // The scanner has no Objective-C, so @interface and @"..." arrive as
        // an @ and then whatever follows. Put them back together, the way the
        // built-in lexer reads them: @ and a lower-case word is an
        // at-keyword, @ and a quote is a string.
        if (text == "@" && m_features.objCEnabled && end < m_bytes.size()) {
            const char next = m_bytes[end];
            if (next >= 'a' && next <= 'z') {
                qsizetype word = end;
                while (word < m_bytes.size()
                       && (std::isalnum(static_cast<unsigned char>(m_bytes[word]))
                           || m_bytes[word] == '_' || m_bytes[word] == '$')) {
                    ++word;
                }
                const QByteArrayView keyword = tokenBytes(end, word);
                appendToken(tokens,
                            Lexer::classifyObjCAtKeyword(keyword.data(), int(keyword.size())),
                            begin, word);
                base = word;
                lexer = makeLexer(base);
                continue;
            }
            if (next == '"') {
                const qsizetype close = literalEnd(tokenBytes(end, m_bytes.size()), '"', false);
                const qsizetype stringEnd = close < 0 ? m_bytes.size() : end + close;
                appendToken(tokens, T_STRING_LITERAL, begin, stringEnd);
                if (close < 0) {
                    recordEndState(T_STRING_LITERAL, tokenBytes(begin, stringEnd));
                    break;
                }
                base = stringEnd;
                lexer = makeLexer(base);
                continue;
            }
        }

        // A stray backslash is not a token. The built-in lexer treats every
        // one of them as the start of a line splice and scans on, whether a
        // newline follows or not, and the test data of the quickfixes is full
        // of them. Whether it splices anything is settled by leadingFlags()
        // and finishSpliceState(), which read the source directly.
        if (text == "\\") {
            skipToken(end);
            continue;
        }

        int kind = T_ERROR;
        bool userDefinedLiteral = false;

        switch (lexer->tokenKind()) {
        case cxx::TokenKind::T_COMMENT:
            kind = commentKind(text);
            break;
        case cxx::TokenKind::T_INTEGER_LITERAL:
        case cxx::TokenKind::T_FLOATING_POINT_LITERAL:
            kind = T_NUMERIC_LITERAL;
            userDefinedLiteral = numericLiteralHasUserDefinedSuffix(text, m_features);
            break;
        case cxx::TokenKind::T_CHARACTER_LITERAL:
            kind = charLiteralKind(text);
            userDefinedLiteral = hasUserDefinedSuffix(text, '\'', false);
            break;
        case cxx::TokenKind::T_STRING_LITERAL:
        case cxx::TokenKind::T_WIDE_STRING_LITERAL:
        case cxx::TokenKind::T_UTF8_STRING_LITERAL:
        case cxx::TokenKind::T_UTF16_STRING_LITERAL:
        case cxx::TokenKind::T_UTF32_STRING_LITERAL:
        case cxx::TokenKind::T_USER_DEFINED_STRING_LITERAL: {
            const bool raw = rawStringDelimiter(text).has_value();
            kind = stringLiteralKind(text, raw);
            userDefinedLiteral = hasUserDefinedSuffix(text, '"', raw);
            break;
        }
        case cxx::TokenKind::T_ERROR:
            kind = T_ERROR;
            break;
        default:
            if (!text.isEmpty() && isIdentifierStart(text[0])) {
                // Keywords first, then the spelled-out operators -- and, or,
                // not_eq -- which is the order the built-in lexer uses.
                kind = Lexer::classify(text.data(), int(text.size()), m_features);
                if (kind == T_IDENTIFIER)
                    kind = Lexer::classifyOperator(text.data(), int(text.size()));
            } else if (!text.isEmpty() && text[0] == '@' && m_features.objCEnabled)
                kind = Lexer::classifyObjCAtKeyword(text.data(), int(text.size()));
            else
                kind = punctuationMap().value(QByteArray(text), T_ERROR);
            break;
        }

        // Outside the preprocessor the scanner hands out >> as two > tokens,
        // which is what its parser wants of a template argument list. The
        // built-in front end hands out one token and splits it later, and the
        // editor is written against that, so put the two back together.
        if (kind == T_GREATER && !tokens.isEmpty()) {
            Token &previous = tokens.last();
            if (previous.kind() == T_GREATER && previous.bytesEnd() == begin) {
                previous.f.kind = T_GREATER_GREATER;
                previous.f.bytes = unsigned(end - previous.bytesBegin());
                previous.f.utf16chars = unsigned(m_utf16.at(end)
                                                 - previous.utf16charsBegin());
                m_previousEnd = end;
                continue;
            }
        }

        // SimpleLexer only goes looking for a header name when the # was the
        // very first token of the text -- always so for the highlighter,
        // which feeds it a line at a time, and true of at most one directive
        // in a whole file. That is a quirk rather than a rule, but it is the
        // behaviour every caller has been reading, so reproduce it here and
        // leave changing it to a change that says it is changing it.
        if (leadingFlags(begin).newline && kind == T_POUND) {
            inDirective = true;
        } else if (inDirective && tokens.size() == 1 && kind == T_IDENTIFIER
                   && (text == "include" || text == "include_next"
                       || (m_features.objCEnabled && text == "import"))) {
            expectHeaderName = true;
        }

        if (isComment(kind) && m_skipComments)
            skipToken(end);
        else
            appendToken(tokens, kind, begin, end, userDefinedLiteral);

        if (end >= m_bytes.size())
            recordEndState(kind, text);
    }

    finishSpliceState();

    return tokens;
}

Tokens CxxFrontendLexer::operator()(const QString &text, int state)
{
    const QByteArray bytes = text.toUtf8();

    CxxFrontendLexerRun run(bytes, m_languageFeatures, m_skipComments, m_ppMode);
    Tokens tokens = run.run(ResumeState::fromInt(state), m_expectedRawStringSuffix);

    m_lastState = run.endState().toInt();
    m_expectedRawStringSuffix = run.expectedRawStringSuffix();
    m_endedJoined = run.endedJoined();

    return tokens;
}

void useCxxFrontendLexer(bool enabled)
{
    if (!enabled) {
        SimpleLexer::setScanner({});
        return;
    }

    // A lexer per chunk rather than one kept alive: everything it would carry
    // from one chunk to the next is in the request, which is what lets the
    // scanner be a function and the caller keep holding a SimpleLexer.
    SimpleLexer::setScanner([](const SimpleLexer::ScanRequest &request) {
        CxxFrontendLexer lexer;
        lexer.setLanguageFeatures(request.languageFeatures);
        lexer.setSkipComments(request.skipComments);
        lexer.setPreprocessorMode(request.preprocessorMode);
        lexer.setExpectedRawStringSuffix(request.expectedRawStringSuffix);

        SimpleLexer::ScanResult result;
        result.tokens = lexer(request.text, request.state);
        result.state = lexer.state();
        result.expectedRawStringSuffix = lexer.expectedRawStringSuffix();
        result.endedJoined = lexer.endedJoined();
        return result;
    });
}

} // namespace CPlusPlus
