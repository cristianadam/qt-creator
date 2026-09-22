// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodemodelqueries.h"

#include "cpplocatordata.h"
#include "cppmodelmanager.h"
#include "symbolfinder.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"
#endif

#include <cplusplus/AST.h>
#include <cplusplus/ASTVisitor.h>
#include <cplusplus/CppDocument.h>
#include <cplusplus/ExpressionUnderCursor.h>
#include <cplusplus/Icons.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/Overview.h>
#include <cplusplus/SimpleLexer.h>
#include <cplusplus/Symbols.h>
#include <cplusplus/TypeOfExpression.h>

#include <utils/algorithm.h>
#include <utils/textutils.h>

#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor {
namespace {

// Whether \a start stands inside a preprocessor directive.
//
// A directive may be continued over as many lines as it likes, and what says
// it is one stands on the first of them, so the backslashes are followed
// back. What this is for: a macro written inside a #define names nothing --
// what stands there is the definition's own parameter, not a class.
bool insideADirective(const QString &text, int start)
{
    if (start == 0)
        return false;

    // Back to the start of the logical line: a directive may be continued
    // over as many lines as it likes, and what says it is one stands on the
    // first of them. A line written on Windows ends in a backslash, a
    // carriage return and a newline, so the return is stepped over before
    // the backslash is looked for.
    int lineStart = text.lastIndexOf(u'\n', start - 1) + 1;
    while (lineStart > 0) {
        int beforeTheBreak = lineStart - 2;
        if (beforeTheBreak >= 0 && text.at(beforeTheBreak) == u'\r')
            --beforeTheBreak;
        if (beforeTheBreak < 0 || text.at(beforeTheBreak) != u'\\')
            break;
        lineStart = beforeTheBreak == 0 ? 0
                                        : text.lastIndexOf(u'\n', beforeTheBreak - 1) + 1;
    }
    return QStringView(text).mid(lineStart, start - lineStart).trimmed().startsWith(u'#');
}

// The text \a token stands for.
QString spelling(const QString &text, const Token &token)
{
    return text.mid(token.utf16charsBegin(), token.utf16charsEnd() - token.utf16charsBegin());
}

// Where each line of a file begins, so that a place some other reader
// recorded can be found among the file's tokens and a token can be reported
// as a place again.
//
// A place counts lines and columns from one, an index entry counts columns
// from zero, and a token knows only how far into the file it begins -- so the
// line starts are counted out once and each conversion is a lookup.
class LineStarts
{
public:
    explicit LineStarts(const QString &text)
        : m_length(int(text.size()))
    {
        m_starts.append(0);
        for (int at = text.indexOf(u'\n'); at >= 0; at = text.indexOf(u'\n', at + 1))
            m_starts.append(at + 1);
    }

    // Both counted from one, and -1 where the file has no such place.
    int offsetOf(int line, int column) const
    {
        if (line < 1 || line > m_starts.size() || column < 1)
            return -1;
        const int offset = m_starts.at(line - 1) + column - 1;

        // Not past the end of that line. A column is counted here in the
        // units a token's offset is in, and one counted in any other -- bytes
        // where a character takes two of them -- must land on nothing rather
        // than reach into the line below.
        const int end = line < m_starts.size() ? m_starts.at(line) : m_length;
        return offset < end ? offset : -1;
    }

    void placeOf(int offset, int *line, int *column) const
    {
        const auto after = std::upper_bound(m_starts.cbegin(), m_starts.cend(), offset);
        *line = int(after - m_starts.cbegin());
        *column = offset - *(after - 1) + 1;
    }

private:
    QList<int> m_starts;
    int m_length = 0;
};

// A file as the questions answered off its own tokens want it: its text, its
// tokens and where its lines begin.
//
// Lexed once however many of those questions are asked about the file, which
// is what keeping this object alive over a file is for -- a reader asks
// several. Told about Qt's keywords, since what marks an access section as a
// slot section is one of them.
class Lexed
{
public:
    explicit Lexed(const QString &source)
        : text(source)
        , lines(source)
    {
        SimpleLexer lexer;
        lexer.setSkipComments(true);
        lexer.setLanguageFeatures(LanguageFeatures::defaultFeatures());
        tokens = lexer(text);
    }

    QString text;
    LineStarts lines;
    Tokens tokens;

    QString spelled(int at) const { return spelling(text, tokens.at(at)); }

    // The token that begins exactly at \a offset, or -1 where none does --
    // which is the answer where a place some other reader recorded no longer
    // has anything at it.
    int tokenBeginningAt(int offset) const
    {
        if (offset < 0)
            return -1;
        for (int i = 0; i < tokens.size(); ++i) {
            const int begins = tokens.at(i).utf16charsBegin();
            if (begins > offset)
                break;
            if (begins == offset)
                return i;
        }
        return -1;
    }

    // Where the name that *ends* at \a at begins: at itself, or at the first
    // of the names that qualify it -- the "NS" of "NS::Thing".
    int nameBeginsAt(int at) const
    {
        int begin = at;
        while (begin >= 2 && tokens.at(begin - 1).kind() == T_COLON_COLON
               && tokens.at(begin - 2).kind() == T_IDENTIFIER) {
            begin -= 2;
        }
        return begin;
    }

    // The name written at \a at, with whatever stands in front of it:
    // "NS::Thing" where that is how it reads.
    QString qualifiedNameAt(int at) const
    {
        const int begin = nameBeginsAt(at);
        QStringList parts;
        for (int i = begin; i <= at; i += 2)
            parts << spelled(i);
        return parts.join("::");
    }
};

#ifdef QTC_WITH_CXX_FRONTEND

// Whether \a token is one of the words that marks an access section as one of
// Qt's: "slots" and "Q_SLOTS", or "signals" and "Q_SIGNALS".
//
// A lexer told about Qt's keywords hands each over as a token of its own, and
// one told nothing about them as an identifier, so both are taken -- which
// scanner is installed is not this reader's business.
bool marksSlots(const QString &text, const Token &token)
{
    if (token.kind() == T_Q_SLOTS)
        return true;
    if (token.kind() != T_IDENTIFIER)
        return false;
    const QString word = spelling(text, token);
    return word == "slots" || word == "Q_SLOTS";
}

bool marksSignals(const QString &text, const Token &token)
{
    if (token.kind() == T_Q_SIGNALS)
        return true;
    if (token.kind() != T_IDENTIFIER)
        return false;
    const QString word = spelling(text, token);
    return word == "signals" || word == "Q_SIGNALS";
}

// What a test runner needs of a class beyond where it stands: the slots it
// declares privately -- which is how a Qt test writes its test functions --
// and what it derives from.
struct WrittenClassShape
{
    // Where the class's own name stands in its own body, which is not always
    // the place it was looked for at: a class declared before it is written
    // is recorded at the declaration.
    int line = 0; // counted from one, the column too
    int column = 0;

    QList<WrittenFunction> privateSlots;
    QStringList baseClasses;
};

// Whether a type is what \a token starts.
//
// What says a name followed by parentheses is being declared rather than
// called -- a parameter list is written with types in it -- and, in front of
// a name, that the type is one of the language's own rather than a class.
bool startsAType(const Token &token)
{
    if (token.isPrimitiveType()) // int, char, void: a keyword of its own kind
        return true;
    switch (token.kind()) {
    case T_CONST: case T_VOLATILE: case T_AUTO: case T_CLASS: case T_STRUCT:
    case T_ENUM: case T_UNION: case T_TYPENAME:
        return true;
    default:
        return false;
    }
}

// A type as it is written, without what decorates it: what a declaration in
// front of a name says the name holds.
struct WrittenTypeOfAName
{
    QString name;          // as written, "NS::Thing"
    bool isPointer = false;
    bool isBuiltin = false; // and then it is no class
};

// What the nearest declaration of \a name before \a before says its type is,
// or nothing where no declaration of it stands there.
//
// Nearest rather than the one in scope: a block is not followed here, and the
// declaration a use means is written above it. What this is for is reading
// "tst_Simple test;" off the main() that hands &test to a runner, which is
// the one shape the answer turns on.
std::optional<WrittenTypeOfAName> declarationBefore(const Lexed &lexed, int before,
                                                    const QString &name)
{
    const Tokens &tokens = lexed.tokens;
    for (int at = before - 1; at > 0; --at) {
        if (tokens.at(at).kind() != T_IDENTIFIER || lexed.spelled(at) != name)
            continue;

        // A declaration's name is followed by the end of it, by what it is
        // initialised with, or by the next name in the same declaration.
        switch (at + 1 < tokens.size() ? tokens.at(at + 1).kind() : T_EOF_SYMBOL) {
        case T_SEMICOLON: case T_EQUAL: case T_COMMA: case T_LPAREN:
        case T_LBRACE: case T_RPAREN: case T_LBRACKET:
            break;
        default:
            continue;
        }

        // And a type in front of it, which is what says this is a
        // declaration rather than a use of the name.
        WrittenTypeOfAName written;
        int from = at;
        for (int back = at - 1; back >= 0; --back) {
            const int kind = tokens.at(back).kind();
            if (kind == T_STAR) {
                written.isPointer = true;
            } else if (kind == T_IDENTIFIER || kind == T_COLON_COLON || kind == T_CONST
                       || kind == T_VOLATILE || kind == T_AMPER || kind == T_LESS
                       || kind == T_GREATER || kind == T_CLASS || kind == T_STRUCT) {
                // A name, what qualifies it, its template arguments, or a
                // word that says how it is held.
            } else if (startsAType(tokens.at(back))) {
                // A type written with a word of the language -- int,
                // unsigned, auto -- which is no class.
                written.isBuiltin = true;
            } else {
                break;
            }
            from = back;
        }
        // The name of the type, which is the last name the run holds: what
        // stands after it says how the thing is held rather than what it is.
        int nameEnd = -1;
        for (int i = from; i < at; ++i) {
            if (tokens.at(i).kind() == T_IDENTIFIER)
                nameEnd = i;
        }

        // A type has to be named for this to be a declaration at all. What
        // stands in front of the name otherwise is an expression it is part
        // of -- "&test" handed to a call reads as "& test" and would
        // otherwise be taken for one.
        if (nameEnd < 0 && !written.isBuiltin)
            continue;

        if (nameEnd >= 0)
            written.name = lexed.qualifiedNameAt(nameEnd);
        return written;
    }
    return std::nullopt;
}

// Whether the name beginning at \a at is being declared rather than called.
//
// A declaration has a type in front of the name -- "void newRow(const char
// *)" -- where a call has the start of a statement, an operator, or a word
// like "return": a file that declares the function it is asked about
// otherwise reads as one that calls it.
bool declaresRatherThanCalls(const Lexed &lexed, int at)
{
    if (at == 0)
        return false;
    const Token &before = lexed.tokens.at(at - 1);

    // What stands in a directive is no type of anything: the token before a
    // call written under an "#endif" is the word "endif", which a lexer hands
    // over as an identifier like any other.
    if (insideADirective(lexed.text, before.utf16charsBegin()))
        return false;

    if (startsAType(before))
        return true;
    switch (before.kind()) {
    case T_IDENTIFIER: // a class as the return type, or a macro in front of it
    case T_STAR: case T_AMPER: case T_AMPER_AMPER: // a pointer or a reference
    case T_GREATER: // the end of a template argument list
    case T_TILDE: // a destructor, which is nobody's call either
        return true;
    default:
        return false;
    }
}

// The name of the function whose body the brace at \a brace opens, as it is
// written -- "C::f" where the function is defined outside its class -- or
// empty where that brace opens no function body.
//
// A head ends in its parameter list, with the words that say how the
// function may be called standing after it, and a constructor's initialiser
// list between that and the body. So the parentheses are found by walking
// back over those.
QString functionOpenedAt(const Lexed &lexed, int brace)
{
    const Tokens &tokens = lexed.tokens;

    // Where a parameter list's ")" stands, given that \a from is the token
    // before something that may follow it.
    const auto closingParenthesisAt = [&](int from) {
        for (int at = from; at >= 0; --at) {
            switch (tokens.at(at).kind()) {
            case T_CONST: case T_VOLATILE: case T_NOEXCEPT: case T_AMPER:
            case T_AMPER_AMPER: case T_THROW: case T_ARROW: case T_COLON_COLON:
            case T_IDENTIFIER: // "override", "final", a macro
                continue;
            case T_RPAREN:
                return at;
            default:
                return -1;
            }
        }
        return -1;
    };

    // And the "(" it belongs to.
    const auto openingParenthesisOf = [&](int close) {
        int nesting = 0;
        for (int at = close; at >= 0; --at) {
            const int kind = tokens.at(at).kind();
            if (kind == T_RPAREN) {
                ++nesting;
            } else if (kind == T_LPAREN && --nesting == 0) {
                return at;
            }
        }
        return -1;
    };

    int close = closingParenthesisAt(brace - 1);
    for (;;) {
        if (close < 0)
            return {};
        const int open = openingParenthesisOf(close);
        if (open <= 0)
            return {};

        // What stands in front of those parentheses: the name of the
        // function, or -- in a constructor's initialiser list -- the name of
        // a member or a base being initialised, and then the head is further
        // back still.
        if (tokens.at(open - 1).kind() != T_IDENTIFIER)
            return {};
        const int name = open - 1;
        const int begins = lexed.nameBeginsAt(name);
        if (begins > 0 && (tokens.at(begins - 1).kind() == T_COMMA
                           || tokens.at(begins - 1).kind() == T_COLON)) {
            close = closingParenthesisAt(begins - 2);
            continue;
        }

        // And that a function is what this head belongs to: it is written
        // like a declaration, with a type in front of the name, where a macro
        // that opens a block -- "TEST_F(Suite, Case) { ... }" -- has nothing
        // in front of it. So is a constructor, whose body this therefore
        // leaves unnamed rather than guessing at.
        if (!declaresRatherThanCalls(lexed, begins))
            return {};
        return lexed.qualifiedNameAt(name);
    }
}

// The scopes open where a walk over a file's tokens has reached, and what
// they are called.
//
// One walk, not one per question: a walk that starts again from the top for
// every call it looks at is quadratic, and a data function writes a thousand
// calls.
class ScopeWalk
{
public:
    // Called for each token in order, and then what is open is what is open
    // *inside* that token -- which for a brace is the scope it just opened.
    void passed(const Lexed &lexed, int at)
    {
        const Tokens &tokens = lexed.tokens;

        // Whether the walk is inside a preprocessor directive, which is a
        // line the compiler never sees braces in: a "#define" whose body
        // opens one -- "namespace X {" is a real macro -- would otherwise
        // put this out by one for the rest of the file.
        //
        // Off the tokens' own line flags rather than by looking at the text
        // for each of them, which would be a scan back to the line's start
        // per token.
        const Token &token = tokens.at(at);
        if (token.newline() && !token.joined())
            m_inADirective = token.kind() == T_POUND;
        if (m_inADirective)
            return;

        switch (token.kind()) {
        case T_LBRACE:
            // A class or a namespace names its scope; otherwise a function's
            // body may, and a statement's block and an initialiser's braces
            // name nothing.
            m_open << (m_pending.isEmpty() ? functionOpenedAt(lexed, at) : m_pending);
            m_pending.clear();
            break;
        case T_RBRACE:
            if (!m_open.isEmpty())
                m_open.removeLast();
            m_pending.clear();
            break;

        // Both of these may be written without a body -- a forward
        // declaration, a namespace alias -- and then the semicolon below
        // takes the name back before any brace uses it.
        case T_NAMESPACE:
            if (at + 1 < tokens.size() && tokens.at(at + 1).kind() == T_IDENTIFIER) {
                // "namespace A::B {" opens both at once, which is one name
                // as far as anything asking about a place is concerned.
                int name = at + 1;
                while (name + 2 < tokens.size()
                       && tokens.at(name + 1).kind() == T_COLON_COLON
                       && tokens.at(name + 2).kind() == T_IDENTIFIER) {
                    name += 2;
                }
                m_pending = lexed.qualifiedNameAt(name);
                m_skipTo = name;
            }
            break;
        case T_CLASS: case T_STRUCT: case T_UNION: {
            // The name: the last of the identifiers written after the word,
            // since an export macro may stand in front of it -- and "final"
            // after it, which is not a name.
            m_pending.clear();
            QStringList written;
            int last = at;
            for (int name = at + 1;
                 name < tokens.size() && tokens.at(name).kind() == T_IDENTIFIER; ++name) {
                written << lexed.spelled(name);
                last = name;
            }
            if (!written.isEmpty() && written.last() == "final")
                written.removeLast();
            if (written.isEmpty())
                break;

            // And only where a class is what this declares. "template <class
            // T>" and "void f(class Foo *p)" name a type they are written in
            // terms of, and neither opens a scope of that name.
            switch (last + 1 < tokens.size() ? tokens.at(last + 1).kind() : T_EOF_SYMBOL) {
            case T_GREATER: case T_GREATER_GREATER: case T_COMMA: case T_STAR:
            case T_AMPER: case T_AMPER_AMPER: case T_RPAREN: case T_EQUAL:
                break;
            default:
                m_pending = written.last();
                m_skipTo = last;
                break;
            }
            break;
        }

        case T_SEMICOLON:
        case T_EQUAL: // "struct S s = { ... }": those braces open no scope
            m_pending.clear();
            break;
        default:
            break;
        }
    }

    // How far a caller may skip ahead, a name having been read past here.
    int skipTo() const { return m_skipTo; }

    // The scopes that have names, outermost first.
    QStringList named() const
    {
        return Utils::filtered(m_open, [](const QString &name) { return !name.isEmpty(); });
    }

    // Whether the innermost of them has no name: a lambda, a block, or a
    // function head this cannot read. A place inside one is a place this
    // cannot say what it is written in.
    bool insideSomethingUnnamed() const { return !m_open.isEmpty() && m_open.last().isEmpty(); }

    // Whether the walk is on a preprocessor directive, where what is written
    // is not what the file says: a macro whose body holds the name a reader
    // is looking for makes the tokens the wrong thing to read it off.
    bool onADirective() const { return m_inADirective; }

private:
    QStringList m_open; // with an empty entry for a scope that has no name
    QString m_pending;  // the name the next brace would open
    int m_skipTo = -1;
    bool m_inADirective = false;
};

// The named scopes the token at \a at is written inside, outermost first:
// "NS", "NS::Outer" and so on, as they are written.
//
// What a lexer can say about a place in a file nobody parsed, and what the
// index has to be asked with: an entry is keyed by the name written out in
// full.
QStringList scopesAround(const Lexed &lexed, int at)
{
    ScopeWalk walk;
    for (int i = 0; i < at && i < lexed.tokens.size(); ++i) {
        walk.passed(lexed, i);
        i = std::max(i, walk.skipTo());
    }
    return walk.named();
}

// Whether a class is what the name at \a at is the name of, and the word that
// says so -- "class" or "struct", which is also what the members written
// before any access label have for access. An export macro may stand between
// the word and the name; a class a macro's body wrote has neither.
//
// -1 where the name is nobody's class.
int classKeywordBefore(const Lexed &lexed, int at)
{
    int keyword = at - 1;
    while (keyword >= 0 && lexed.tokens.at(keyword).kind() == T_IDENTIFIER)
        --keyword;
    if (keyword < 0 || (lexed.tokens.at(keyword).kind() != T_CLASS
                        && lexed.tokens.at(keyword).kind() != T_STRUCT)) {
        return -1;
    }
    return keyword;
}

// That shape, read off the tokens of the file that writes the class, whose
// own name stands at \a at.
//
// A lexer's job, and moc's precedent: an access section, the word that marks
// one as Qt's and a base clause are all there in the text, and none of them
// needs a name resolved or a header read. Where the class is written is a
// question for whoever has read the project -- the index answers it -- and
// this is what the class says there.
//
// Nothing where no class with a body is written at that place, which is what
// says the name is a declaration's rather than the class's own, that the
// place is out of date, or that a macro's body wrote the class.
//
// As the text has it, which differs from a reading of the translation unit in
// the ways a reader asking what a file says can live with. A slot declared in
// a branch this configuration does not build is among them, the way a macro
// use in one is. A base is named as the class names it rather than written
// out in full, which is what a reader looking that base up in turn asks with
// anyway. And the signature carries the parameter list as written, names and
// all, where a reading writes the types alone -- a test function takes none,
// and nothing asks this of a private slot.
std::optional<WrittenClassShape> shapeOfTheClassAt(const Lexed &lexed, const FilePath &filePath,
                                                   int at)
{
    const QString &text = lexed.text;
    const Tokens &tokens = lexed.tokens;

    const int keyword = classKeywordBefore(lexed, at);
    if (keyword < 0)
        return std::nullopt;

    WrittenClassShape shape;
    lexed.lines.placeOf(tokens.at(at).utf16charsBegin(), &shape.line, &shape.column);
    int i = at + 1;

    // "final" stands between the name and what follows it, and is written as
    // an identifier rather than a keyword.
    if (i < tokens.size() && tokens.at(i).kind() == T_IDENTIFIER
        && spelling(text, tokens.at(i)) == "final") {
        ++i;
    }

    if (i < tokens.size() && tokens.at(i).kind() == T_COLON) {
        // The base clause: what the class names, as written, up to its body.
        // A comma inside brackets of any kind separates nothing -- Base<int,
        // char> is one base, however it reads -- so what nests is counted.
        int nesting = 0;
        int from = -1; // the first token of the base being read, and the last
        int to = -1;
        const auto flush = [&] {
            if (from >= 0 && to >= from) {
                shape.baseClasses << text.mid(tokens.at(from).utf16charsBegin(),
                                              tokens.at(to).utf16charsEnd()
                                                  - tokens.at(from).utf16charsBegin());
            }
            from = to = -1;
        };
        for (++i; i < tokens.size(); ++i) {
            const int kind = tokens.at(i).kind();
            if (nesting == 0 && (kind == T_LBRACE || kind == T_SEMICOLON))
                break;
            switch (kind) {
            case T_LESS: case T_LPAREN: case T_LBRACKET:
                ++nesting;
                break;
            // Never below nothing: a base clause may write a ">" that closes
            // no bracket -- "Base<(N > 1 ? X : Y)>" -- and a count gone
            // negative would never see the body begin.
            case T_GREATER: case T_RPAREN: case T_RBRACKET:
                nesting = std::max(0, nesting - 1);
                break;
            case T_GREATER_GREATER: // two template arguments closing at once
                nesting = std::max(0, nesting - 2);
                break;
            case T_COMMA:
                if (nesting == 0) {
                    flush();
                    continue;
                }
                break;
            case T_PUBLIC: case T_PRIVATE: case T_PROTECTED: case T_VIRTUAL:
                if (nesting == 0)
                    continue; // how it is inherited, not what from
                break;
            }
            if (from < 0)
                from = i;
            to = i;
        }
        flush();
    }

    // A class named without a body declares nothing there, so there is
    // nothing to read off it.
    if (i >= tokens.size() || tokens.at(i).kind() != T_LBRACE)
        return std::nullopt;

    // What a member written here has for access, and whether the section it
    // stands in is one of Qt's slot sections. A class starts in no such
    // section whatever it is written as -- a section is one only where the
    // word that marks it stands -- and what is written before any label at
    // all is private in a class and public in a struct.
    bool isPrivate = tokens.at(keyword).kind() == T_CLASS;
    bool inSlotsSection = false;

    // Qt's other spelling: a member marked a slot one at a time rather than a
    // section of them. The mark stands in front of the declaration, so what
    // ended the one before it says how far back to look.
    const auto markedASlot = [&](int name) {
        for (int back = name - 1; back >= 0; --back) {
            const Token &before = tokens.at(back);
            if (before.kind() == T_Q_SLOT
                || (before.kind() == T_IDENTIFIER && spelling(text, before) == "Q_SLOT")) {
                return true;
            }
            switch (before.kind()) {
            case T_SEMICOLON: case T_LBRACE: case T_RBRACE: case T_COLON:
                return false;
            default:
                break;
            }
        }
        return false;
    };

    int depth = 1;
    for (++i; i < tokens.size() && depth > 0; ++i) {
        const Token &token = tokens.at(i);
        const int kind = token.kind();
        if (kind == T_LBRACE) {
            ++depth;
            continue;
        }
        if (kind == T_RBRACE) {
            --depth;
            continue;
        }
        // What a nested class declares, and what a function defined here
        // writes inside itself, is its own business.
        if (depth != 1)
            continue;

        if (kind == T_PUBLIC || kind == T_PRIVATE || kind == T_PROTECTED) {
            // "slots" is one of Qt's own macros where this is compiled, so
            // it is no name for a variable here.
            int after = i + 1;
            const bool marked = after < tokens.size() && marksSlots(text, tokens.at(after));
            if (marked || (after < tokens.size() && marksSignals(text, tokens.at(after))))
                ++after;
            if (after < tokens.size() && tokens.at(after).kind() == T_COLON) {
                isPrivate = kind == T_PRIVATE;
                inSlotsSection = marked;
                i = after;
            }
            continue;
        }

        // A section written with no access in front of it, which Qt's
        // signals are: whatever section stood before it has ended.
        if ((marksSlots(text, token) || marksSignals(text, token)) && i + 1 < tokens.size()
            && tokens.at(i + 1).kind() == T_COLON) {
            isPrivate = false;
            inSlotsSection = false;
            ++i;
            continue;
        }

        if (kind != T_IDENTIFIER || i + 1 >= tokens.size()
            || tokens.at(i + 1).kind() != T_LPAREN) {
            continue;
        }

        // A private slot, written either way: in a section of them, or marked
        // on its own in a private section.
        if (!isPrivate || !(inSlotsSection || markedASlot(i)))
            continue;

        // A name inside a preprocessor directive declares nothing: what
        // stands in "#if defined(SOMETHING)" is a condition and what stands
        // in "#define WRAPPER(x)" is a definition's own parameter.
        if (insideADirective(text, token.utf16charsBegin()))
            continue;

        // A slot has a return type written in front of its name. What has
        // nothing in front of it is not one, however much it reads like a
        // call: a macro a class body uses -- Q_CLASSINFO("a", "b"),
        // Q_DECLARE_FLAGS(Flags, Flag) -- stands at the start of what it
        // writes, and so do a constructor and, after its tilde, a
        // destructor. Neither of those is a slot either.
        //
        // The token before the name is always there: the body's brace stands
        // before everything in it.
        switch (tokens.at(i - 1).kind()) {
        case T_SEMICOLON: case T_LBRACE: case T_RBRACE: case T_COLON:
        case T_COMMA: case T_TILDE: case T_POUND:
            continue;
        default:
            break;
        }

        // Where the parentheses close.
        int nesting = 0;
        int close = -1;
        for (int j = i + 1; j < tokens.size(); ++j) {
            const int inside = tokens.at(j).kind();
            if (inside == T_LPAREN) {
                ++nesting;
            } else if (inside == T_RPAREN && --nesting == 0) {
                close = j;
                break;
            }
        }
        if (close < 0)
            continue;

        // And that a declaration is what this is: what may follow a
        // parameter list is the end of it, a body written here, or one of the
        // words a declaration carries -- const, noexcept, a reference
        // qualifier, a trailing return type, "= 0", "override".
        switch (close + 1 < tokens.size() ? tokens.at(close + 1).kind() : T_EOF_SYMBOL) {
        case T_SEMICOLON: case T_LBRACE: case T_CONST: case T_VOLATILE:
        case T_NOEXCEPT: case T_THROW: case T_ARROW: case T_EQUAL:
        case T_AMPER: case T_AMPER_AMPER: case T_IDENTIFIER:
            break;
        default:
            continue;
        }

        int nameLine = 0;
        int nameColumn = 0;
        lexed.lines.placeOf(token.utf16charsBegin(), &nameLine, &nameColumn);
        const QString name = spelling(text, token);
        const QString parameters = text.mid(tokens.at(i + 1).utf16charsBegin(),
                                            tokens.at(close).utf16charsEnd()
                                                - tokens.at(i + 1).utf16charsBegin());
        shape.privateSlots << WrittenFunction{name, name + parameters.simplified(), filePath,
                                              nameLine, nameColumn};
        // Past the parameter list. A body written here is stepped over by the
        // depth above, a declaration's semicolon says nothing.
        i = close;
    }

    return shape;
}

// The same, off the place a reader of the project recorded the class at: \a
// line and \a column, both counted from one, of the file \a lexed holds.
//
// Every place the name stands as a class's is tried, that one first. A class
// declared before it is written -- "class tst_Foo;" above, which is how a
// header keeps its includes down -- is *one* symbol to the front end that
// recorded the place, and the place is the declaration, where nothing is
// declared to read. The definition is further down the same file as a rule,
// and where it is in another the caller reads after all.
std::optional<WrittenClassShape> classShapeIn(const Lexed &lexed, const FilePath &filePath,
                                              const QString &ownName, int line, int column)
{
    const int offset = lexed.lines.offsetOf(line, column);
    if (offset < 0)
        return std::nullopt;

    int at = lexed.tokenBeginningAt(offset);
    if (at < 0 || lexed.tokens.at(at).kind() != T_IDENTIFIER || lexed.spelled(at) != ownName)
        return std::nullopt;

    for (; at < lexed.tokens.size(); ++at) {
        if (lexed.tokens.at(at).kind() != T_IDENTIFIER || lexed.spelled(at) != ownName
            || classKeywordBefore(lexed, at) < 0) {
            continue;
        }
        if (const std::optional<WrittenClassShape> shape
            = shapeOfTheClassAt(lexed, filePath, at)) {
            return shape;
        }
    }
    return std::nullopt;
}

// Whether the name written at \a at is the one \a asked about, that being a
// function's name split on "::".
//
// Written with as much in front of it as the file bothers with, which has to
// be the tail of what was asked for: QTest::qExec is asked for and "qExec"
// under a using directive is it, where "Other::qExec" is somebody else's
// function of the same name.
bool namesTheFunction(const Lexed &lexed, int at, const QStringList &asked)
{
    const QStringList written = lexed.qualifiedNameAt(at).split("::");
    return written.size() <= asked.size()
           && written == asked.mid(asked.size() - written.size());
}

// The classes a file hands to calls of a function called \a functionName --
// written out in full -- read off the file's own tokens, or nothing where a
// call is written in a way its text does not settle.
//
// Which class a Qt test runs is what its main() says, and a runner is handed
// a pointer to it: "QTest::qExec(&test, argc, argv)" with "tst_Simple test;"
// above, or a class made right there. Both are in the text.
//
// A call whose first argument is neither -- what some function hands back, a
// cast, a member of something else, or a name this file does not declare --
// is one only a reading settles, and then nothing comes back here at all: a
// list with that call left out of it would be read as an answer, and a
// missing class is a test nobody can run.
//
// An empty list, on the other hand, is an answer: most files that include
// QtTest call no runner at all, which is the case this exists for.
//
// As the text has it. A call reachable through a using directive is among
// them, being written with as much in front of it as the name asked for ends
// in; a class is named as the declaration names it rather than written out in
// full, which is what a reader looking it up in turn asks with anyway. A
// class handed over twice comes back twice, since the caller dedupes.
std::optional<QStringList> classesPassedToIn(const Lexed &lexed, const QString &functionName)
{
    const Tokens &tokens = lexed.tokens;
    const QStringList asked = functionName.split("::", Qt::SkipEmptyParts);
    if (asked.isEmpty())
        return std::nullopt;

    QStringList classes;
    for (int i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens.at(i).kind() != T_IDENTIFIER || tokens.at(i + 1).kind() != T_LPAREN
            || lexed.spelled(i) != asked.last()) {
            continue;
        }

        // A macro written around the call: "#define RUN(c) QTest::qExec(c)"
        // and then RUN wherever a test is run. Where the call is is not
        // something the tokens of this file say any more, so they are the
        // wrong thing to read it off at all.
        if (insideADirective(lexed.text, tokens.at(i).utf16charsBegin()))
            return std::nullopt;

        if (!namesTheFunction(lexed, i, asked)
            || declaresRatherThanCalls(lexed, lexed.nameBeginsAt(i))) {
            continue;
        }

        // What the first argument says. A class made right there names
        // itself.
        const int first = i + 2;
        if (tokens.at(first).kind() == T_NEW) {
            if (first + 1 >= tokens.size() || tokens.at(first + 1).kind() != T_IDENTIFIER)
                return std::nullopt;
            int nameEnd = first + 1;
            while (nameEnd + 2 < tokens.size() && tokens.at(nameEnd + 1).kind() == T_COLON_COLON
                   && tokens.at(nameEnd + 2).kind() == T_IDENTIFIER) {
                nameEnd += 2;
            }
            classes << lexed.qualifiedNameAt(nameEnd);
            continue;
        }

        // A declaration of a function of that name rather than a call of one,
        // which a file that declares the runner itself writes. It hands
        // nothing over.
        if (startsAType(tokens.at(first)))
            continue;

        const bool addressOf = tokens.at(first).kind() == T_AMPER;
        const int named = addressOf ? first + 1 : first;
        if (named + 1 >= tokens.size() || tokens.at(named).kind() != T_IDENTIFIER)
            return std::nullopt;

        // And that the name is the whole of the argument. Something that
        // merely begins with one -- "test.get()", "d->tc" -- hands over what
        // the rest of it says, which is a reading's question: reading the
        // name's own declaration would answer about the wrong thing.
        if (tokens.at(named + 1).kind() != T_COMMA && tokens.at(named + 1).kind() != T_RPAREN)
            return std::nullopt;

        // Otherwise what the name holds, as the declaration above it writes
        // it -- and nothing at all where no declaration of it stands in this
        // file, that being a question for a reading rather than for a lexer.
        const std::optional<WrittenTypeOfAName> holds
            = declarationBefore(lexed, named, lexed.spelled(named));
        if (!holds)
            return std::nullopt;
        if (holds->isBuiltin || holds->name.isEmpty()) {
            // Handed something the text names no class in. A value of such a
            // type is an answer -- a runner takes an object, so it hands over
            // no class -- where an address or a pointer is a question for a
            // reading: what "auto *test = new tst_Simple" holds is written in
            // the initialiser rather than in the type.
            if (addressOf || holds->isPointer)
                return std::nullopt;
            continue;
        }

        // A runner takes an object rather than a value: the address of one,
        // or a pointer that already holds one.
        if (addressOf ? !holds->isPointer : holds->isPointer)
            classes << holds->name;
    }
    return classes;
}

// The calls a file makes to any of the functions called \a functionNames --
// each written out in full -- read off the file's own tokens: what each
// argument says where it is a string literal, and the function the call
// stands in.
//
// Both of those are in the text. A tag is a literal handed to a call, and
// which function writes it is which braces the call stands between -- so a
// data function's tags need neither a name resolved nor a header read.
//
// Nothing where a call stands in a scope this cannot name: a lambda, or a
// head it cannot read. A call whose function is unknown is one a caller
// looking for a "_data" function drops, and a dropped call is a tag nobody
// can be sent to -- so the file is read rather than half-answered for.
//
// As the text has it, and a superset on purpose the same way the classes
// above are: a call reachable through a using directive is among them, and
// so is one in a branch this configuration does not build.
std::optional<QList<CodeModelQueries::WrittenCall>> callsToIn(
    const Lexed &lexed, const QStringList &functionNames)
{
    const Tokens &tokens = lexed.tokens;
    QList<QStringList> asked;
    for (const QString &name : functionNames) {
        const QStringList parts = name.split("::", Qt::SkipEmptyParts);
        if (!parts.isEmpty())
            asked << parts;
    }

    QList<CodeModelQueries::WrittenCall> calls;
    if (asked.isEmpty())
        return calls;

    ScopeWalk walk;
    for (int i = 0; i < tokens.size(); ++i) {
        walk.passed(lexed, i);
        i = std::max(i, walk.skipTo());
        if (i + 1 >= tokens.size() || tokens.at(i).kind() != T_IDENTIFIER
            || tokens.at(i + 1).kind() != T_LPAREN) {
            continue;
        }
        if (!Utils::anyOf(asked, [&](const QStringList &name) {
                return lexed.spelled(i) == name.last() && namesTheFunction(lexed, i, name);
            })) {
            continue;
        }

        // A macro written around the call: "#define ROW(t) QTest::newRow(t)"
        // and then ROW wherever a row is wanted. Where the calls are is not
        // something the tokens of this file say any more, so they are the
        // wrong thing to read them off at all.
        if (walk.onADirective())
            return std::nullopt;

        // A file that declares the function itself is not calling it there.
        const int begins = lexed.nameBeginsAt(i);
        if (declaresRatherThanCalls(lexed, begins))
            continue;

        // Which function it stands in, which is what a caller keeping the
        // calls a "_data" function makes goes by.
        if (walk.insideSomethingUnnamed())
            return std::nullopt;

        // Where the called name stands, which is where what qualifies it
        // begins: "QTest::newRow" stands where the "QTest" does.
        CodeModelQueries::WrittenCall call;
        call.insideFunction = walk.named().join("::");
        lexed.lines.placeOf(tokens.at(begins).utf16charsBegin(), &call.line, &call.column);

        // One entry per argument: what it says where it is a string literal
        // -- the text between the quotes -- and nothing where it is anything
        // else, so that a caller wanting the third can count to it. A
        // literal written with a prefix or as a raw string says nothing
        // here: what it stands for is not what stands between its quotes.
        int nesting = 0;
        int from = i + 2; // the first token of the argument being read
        const auto flush = [&](int end) {
            QString says;
            if (end == from + 1) {
                const QString spelled = lexed.spelled(from);
                if (tokens.at(from).kind() == T_STRING_LITERAL && spelled.size() >= 2
                    && spelled.startsWith(u'"') && spelled.endsWith(u'"')) {
                    says = spelled.mid(1, spelled.size() - 2);
                }
            }
            call.arguments << says;
        };
        for (int j = i + 1; j < tokens.size(); ++j) {
            const int kind = tokens.at(j).kind();
            if (kind == T_LPAREN || kind == T_LBRACKET || kind == T_LBRACE) {
                ++nesting;
            } else if (kind == T_RPAREN || kind == T_RBRACKET || kind == T_RBRACE) {
                if (--nesting > 0)
                    continue;
                if (j > i + 2) // a call handed nothing has no arguments
                    flush(j);
                break;
            } else if (kind == T_COMMA && nesting == 1) {
                flush(j);
                from = j + 1;
            }
        }

        calls << call;
    }
    return calls;
}

#endif // QTC_WITH_CXX_FRONTEND

// The name \a name stands for where it is written, written out in full. A
// name nothing declares -- which is what a header that was never generated
// leaves behind -- is taken as it was written.
QString fullyQualifiedName(const LookupContext &context, const Name *name, Scope *scope)
{
    if (!name || !scope)
        return QString();

    const QList<LookupItem> items = context.lookup(name, scope);
    if (items.isEmpty())
        return Overview().prettyName(name);
    return Overview().prettyName(LookupContext::fullyQualifiedName(items.first().declaration()));
}

bool inherits(const Class *klass, const QString &baseClass)
{
    const Overview overview;
    for (int b = 0, count = klass->baseClassCount(); b < count; ++b) {
        if (overview.prettyName(klass->baseClassAt(b)->name()) == baseClass)
            return true;
    }
    return false;
}

// The first class in \a scope, or in a namespace inside it, that declares a
// member of type \a className or derives from it.
const Class *classUsing(const Scope *scope, const LookupContext &context,
                        const QString &className)
{
    for (int i = 0, count = scope->memberCount(); i < count; ++i) {
        Symbol * const member = scope->memberAt(i);
        const Class * const klass = member->asClass();
        if (!klass) {
            if (const Namespace * const ns = member->asNamespace()) {
                if (const Class * const found = classUsing(ns, context, className))
                    return found;
            }
            continue;
        }

        for (int j = 0, members = klass->memberCount(); j < members; ++j) {
            Declaration * const decl = klass->memberAt(j)->asDeclaration();
            if (!decl)
                continue;
            const NamedType *named = decl->type()->asNamedType();
            if (!named) {
                if (PointerType * const pointer = decl->type()->asPointerType())
                    named = pointer->elementType()->asNamedType();
            }
            if (!named)
                continue;
            if (fullyQualifiedName(context, named->name(), decl->enclosingScope()) == className)
                return klass;
        }

        if (inherits(klass, className))
            return klass;
    }
    return nullptr;
}

// The class whose own name stands at \a line and \a column.
Class *classWrittenAt(const Document::Ptr &doc, int line, int column)
{
    if (!doc)
        return nullptr;
    QList<const Scope *> scopes{doc->globalNamespace()};
    while (!scopes.isEmpty()) {
        const Scope * const scope = scopes.takeFirst();
        for (int i = 0, count = scope->memberCount(); i < count; ++i) {
            Symbol * const symbol = scope->memberAt(i);
            if (const Scope * const inner = symbol->asScope())
                scopes << inner;
            if (Class * const klass = symbol->asClass();
                klass && klass->line() == line && klass->column() == column) {
                return klass;
            }
        }
    }
    return nullptr;
}

// The function declared at \a line and \a column -- where its own name
// stands, which is where a front end records it.
Function *functionWrittenAt(const Document::Ptr &doc, int line, int column)
{
    if (!doc)
        return nullptr;
    QList<const Scope *> scopes{doc->globalNamespace()};
    while (!scopes.isEmpty()) {
        const Scope * const scope = scopes.takeFirst();
        for (int i = 0, count = scope->memberCount(); i < count; ++i) {
            Symbol * const symbol = scope->memberAt(i);
            if (const Scope * const inner = symbol->asScope())
                scopes << inner;
            if (symbol->line() != line || symbol->column() != column)
                continue;
            if (Function * const function = symbol->type()->asFunctionType())
                return function;
        }
    }
    return nullptr;
}

// What the built-in front end writes a function's name and parameter types
// as, which is how a member is told from another of the same name.
QString signatureOf(const Function *function)
{
    const Overview overview;
    QString signature = overview.prettyName(function->name()) + '(';
    for (int i = 0, count = function->argumentCount(); i < count; ++i) {
        if (i > 0)
            signature += ", ";
        signature += overview.prettyType(function->argumentAt(i)->asArgument()->type());
    }
    return signature + ')';
}

// What the built-in front end says, which is the answer wherever the
// cxx-frontend model has not read the file.

WrittenClass builtinClassUsingClass(const Snapshot &snapshot, const FilePath &filePath,
                                    const QString &className, int maxIncludeDepth)
{
    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc)
        return {};

    const LookupContext context(doc, snapshot);
    if (const Class * const klass = classUsing(doc->globalNamespace(), context, className))
        return {Overview().prettyName(klass->name()),
                Overview().prettyName(
                    LookupContext::fullyQualifiedName(const_cast<Class *>(klass))),
                filePath, klass->line(), klass->column()};
    if (maxIncludeDepth <= 0)
        return {};

    for (const FilePath &include : doc->includedFiles()) {
        const WrittenClass found = builtinClassUsingClass(snapshot, include, className,
                                                          maxIncludeDepth - 1);
        if (found.isValid())
            return found;
    }
    return {};
}

// Every class \a scope declares, nested ones included, in the order they
// are written.
void collectClasses(const Scope *scope, const FilePath &filePath, QList<WrittenClass> *into)
{
    const Overview overview;
    for (int i = 0, count = scope->memberCount(); i < count; ++i) {
        Symbol * const member = scope->memberAt(i);
        if (const Class * const klass = member->asClass()) {
            into->append({overview.prettyName(klass->name()),
                          overview.prettyName(LookupContext::fullyQualifiedName(member)),
                          filePath, klass->line(), klass->column()});
        }
        if (const Scope * const inner = member->asScope())
            collectClasses(inner, filePath, into);
    }
}

// Everything \a scope declares, each entry saying what it is written
// inside. The rules are what "declares" means: a name the file only
// mentions is not one, and what a function writes inside itself is not
// either.
void collectDeclarations(const Scope *scope, const FilePath &filePath, int parent,
                         QList<WrittenDeclaration> *into)
{
    const Overview overview;
    for (int i = 0, count = scope->memberCount(); i < count; ++i) {
        Symbol * const member = scope->memberAt(i);
        if (!member)
            continue;

        if (member->asForwardClassDeclaration() || member->isExtern() || member->isFriend()
            || member->isGenerated() || member->asUsingNamespaceDirective()
            || member->asUsingDeclaration()) {
            continue;
        }

        // Written under a qualified name, which is a definition of something
        // declared where that name was given.
        if (member->name() && member->name()->asQualifiedNameId())
            continue;

        const int index = into->size();
        into->append({overview.prettyName(member->name()).trimmed(),
                      overview.prettyType(member->type()).trimmed(),
                      CPlusPlus::Icons::iconTypeForSymbol(member),
                      // The symbol's own file rather than the document's: a
                      // declaration stands where it was written.
                      member->filePath(),
                      member->line(),
                      member->column(),
                      parent,
                      member->asNamespace() != nullptr});

        if (const Scope * const inner = member->asScope(); inner && !member->asFunction())
            collectDeclarations(inner, filePath, index, into);
    }
}

#ifdef QTC_WITH_CXX_FRONTEND
// What would be written after the name, the way the built-in front end's
// prettyType writes it: a function's parameter list, the type of anything
// else, and for a scope its own name over again.
QString writtenTypeOf(const CxxFrontendDocument::Symbol &symbol)
{
    if (!symbol.signature.isEmpty())
        return symbol.signature;
    if (!symbol.valueType.isEmpty())
        return symbol.valueType;
    return symbol.name;
}
#endif


// The calls a file makes to one of several functions, with what each
// argument says where it is a literal, and the function each is written
// inside. A call written
// without its scopes counts where a using directive made it reachable, which
// is what the depth bookkeeping here is for: a directive is in force from
// where it is written to the end of the scope that holds it.
class CallsTo : protected ASTVisitor
{
public:
    CallsTo(const Document::Ptr &document, const QStringList &functionNames)
        : ASTVisitor(document->translationUnit())
        , m_document(document)
    {
        for (const QString &name : functionNames) {
            m_qualified.append(name);
            // The last part of it, and the whole of a name that has only
            // one part: lastIndexOf answers -1 where there is no "::", and
            // taking two off that cuts the first character away.
            const int afterTheScopes = name.lastIndexOf("::");
            if (afterTheScopes < 0) {
                // Asked for without scopes, so a call written without them
                // is the call: there is nothing for a directive to lend.
                m_plain.append(name);
            } else {
                m_lentByADirective.append(name.mid(afterTheScopes + 2));
            }
        }
        accept(document->translationUnit()->ast());
    }

    QList<CodeModelQueries::WrittenCall> calls() const { return m_calls; }

protected:
    bool preVisit(AST *ast) override
    {
        ++m_depth;

        // Where a using directive's reach ends: at the end of the block or
        // the namespace it stands in, which is the scope to measure against
        // rather than whatever node happens to hold the directive -- inside
        // a function that is one declaration statement, and a directive
        // written there would stop applying before the next line.
        if (ast->asCompoundStatement() || ast->asNamespace() || ast->asTranslationUnit()
            || ast->asLinkageBody()) {
            m_scopeDepths.append(m_depth);
        }
        return true;
    }

    void postVisit(AST *ast) override
    {
        if (ast->asCompoundStatement() || ast->asNamespace() || ast->asTranslationUnit()
            || ast->asLinkageBody()) {
            m_scopeDepths.removeLast();
        }
        --m_depth;
        m_reachableUnqualified &= m_depth >= m_usingDirectiveDepth;
        if (ast->asFunctionDefinition())
            m_insideFunction.clear();
    }

    bool visit(UsingDirectiveAST *ast) override
    {
        // Which namespace it names does not matter: the functions asked
        // about are named by their scopes, and a directive for another
        // namespace cannot make one of them reachable unqualified.
        if (ast->name && !m_scopeDepths.isEmpty()) {
            m_reachableUnqualified = true;
            m_usingDirectiveDepth = m_scopeDepths.last();
        }
        return true;
    }

    bool visit(FunctionDefinitionAST *ast) override
    {
        m_insideFunction = ast->symbol
                               ? Overview().prettyName(
                                     LookupContext::fullyQualifiedName(ast->symbol))
                               : QString();
        return true;
    }

    bool visit(CallAST *ast) override
    {
        if (!ast->base_expression)
            return true;
        IdExpressionAST * const id = ast->base_expression->asIdExpression();
        NameAST * const called = id ? id->name : nullptr;
        if (!called || !called->name)
            return true;

        const QString name = Overview().prettyName(called->name);
        const bool isOne = called->asQualifiedName()
                               ? m_qualified.contains(name)
                               : m_plain.contains(name)
                                     || (m_reachableUnqualified
                                         && m_lentByADirective.contains(name));
        if (!isOne)
            return true;

        // Each argument as it stands: what a literal says, and nothing for
        // anything else. The whole run of a literal, since adjacent ones
        // are one string.
        QStringList arguments;
        for (const ExpressionListAST *at = ast->expression_list; at; at = at->next) {
            const StringLiteralAST * const text = at->value ? at->value->asStringLiteral()
                                                            : nullptr;
            QString literal;
            for (const StringLiteralAST *piece = text; piece; piece = piece->next) {
                const Token token = m_document->translationUnit()->tokenAt(piece->literal_token);
                if (!token.isStringLiteral()) {
                    literal.clear();
                    break;
                }
                literal += QString::fromUtf8(token.spell());
            }
            arguments.append(literal);
        }

        int line = 0;
        int column = 0;
        m_document->translationUnit()->getTokenPosition(called->firstToken(), &line, &column);
        m_calls.append({m_insideFunction, arguments, line, column});
        return true;
    }

private:
    Document::Ptr m_document;
    QStringList m_qualified;        // asked for with scopes in front
    QStringList m_plain;            // asked for without any
    QStringList m_lentByADirective; // the last part of a qualified one
    QString m_insideFunction;
    QList<CodeModelQueries::WrittenCall> m_calls;
    QList<int> m_scopeDepths;
    int m_depth = 0;
    int m_usingDirectiveDepth = 0;
    bool m_reachableUnqualified = false;
};

// The classes a file hands to calls of one function, by the name of what
// each call's first argument points at. The type is looked up where the call
// stands, which is what says which class a name written there means.
class ClassesPassedTo : protected ASTVisitor
{
public:
    ClassesPassedTo(const Document::Ptr &document, const Snapshot &snapshot,
                    const QString &functionName)
        : ASTVisitor(document->translationUnit())
        , m_document(document)
        , m_snapshot(snapshot)
        , m_functionName(functionName)
    {
        accept(document->translationUnit()->ast());
    }

    QStringList classes() const { return m_classes; }

protected:
    bool visit(CompoundStatementAST *ast) override
    {
        // A call is looked up from the block it stands in, which is what
        // gives a name written there its meaning.
        m_scope = ast && ast->symbol ? ast->symbol->asScope() : nullptr;
        return m_scope != nullptr;
    }

    bool visit(CallAST *ast) override
    {
        if (!m_scope || !ast->base_expression || !ast->expression_list
            || !ast->expression_list->value) {
            return true;
        }
        const IdExpressionAST * const id = ast->base_expression->asIdExpression();
        const NameAST * const name = id ? id->name : nullptr;
        if (!name || Overview().prettyName(name->name) != m_functionName)
            return true;

        TypeOfExpression typeOfExpression;
        typeOfExpression.init(m_document, m_snapshot);
        const QList<LookupItem> items = typeOfExpression(ast->expression_list->value,
                                                         m_document, m_scope);
        // A lookup item with no type at all is no answer: its type operator
        // hands back what it holds without looking.
        if (items.isEmpty() || !items.first().type().type())
            return true;
        if (const PointerType * const pointer = items.first().type()->asPointerType())
            m_classes.append(Overview().prettyType(pointer->elementType()));
        return true;
    }

private:
    Document::Ptr m_document;
    const Snapshot &m_snapshot;
    QString m_functionName;
    Scope *m_scope = nullptr;
    QStringList m_classes;
};

// The class \a snapshot has under \a className as the reading of \a filePath
// sees it: the name is looked up as a type from the file's own scope, so a
// class a header declares is found where a file that includes it names it.
const Class *builtinClassNamed(const Snapshot &snapshot, const FilePath &filePath,
                               const QString &className)
{
    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc || className.isEmpty())
        return nullptr;

    TypeOfExpression typeOfExpression;
    typeOfExpression.init(doc, snapshot);
    const QList<LookupItem> items = typeOfExpression(className.toUtf8(),
                                                     doc->globalNamespace());
    for (const LookupItem &item : items) {
        if (Symbol * const symbol = item.declaration()) {
            if (Class * const klass = symbol->asClass())
                return klass;
        }
    }
    return nullptr;
}

} // namespace

// What the name at \a cursor resolves to, and whether that is a function --
// the two questions functionNamedAt() and nameResolvedAt() are each half of.
namespace {
struct ResolvedName
{
    QString qualifiedName;
    bool isFunction = false;
};
}  // namespace

static ResolvedName resolveNameAt(const Snapshot &snapshot, const FilePath &filePath,
                                  const QTextCursor &cursor)
{
    // At the end of the name, which is where an expression read backwards
    // from a cursor has to start.
    QTextCursor atTheEnd = cursor;
    const QTextDocument * const text = atTheEnd.document();
    for (QChar ch = text->characterAt(atTheEnd.position());
         ch.isLetterOrNumber() || ch == '_';
         ch = text->characterAt(atTheEnd.position())) {
        atTheEnd.movePosition(QTextCursor::NextCharacter);
    }

    int line = 0;
    int column = 0;
    Utils::Text::convertPosition(text, atTheEnd.position(), &line, &column);

#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<CxxFrontendDocument::Declaration> declaration
        = Internal::cxxFrontendDeclarationAt(filePath, line, column)) {
        if (!declaration->isValid())
            return {};
        return {declaration->name,
                declaration->kind == CxxFrontendDocument::Kind::Function};
    }
#endif

    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc)
        return {};

    ExpressionUnderCursor expressionUnderCursor(doc->languageFeatures());
    const QString expression = expressionUnderCursor(atTheEnd);

    TypeOfExpression typeOfExpression;
    typeOfExpression.init(doc, snapshot);
    const QList<LookupItem> items = typeOfExpression(expression.toUtf8(),
                                                     doc->scopeAt(line, column));
    if (items.isEmpty())
        return {};

    // The first candidate, as this has always taken: which overload the name
    // means is not settled by the name alone.
    Symbol * const symbol = items.first().declaration();
    if (!symbol)
        return {};
    return {Overview().prettyName(LookupContext::fullyQualifiedName(symbol)),
            symbol->asFunction() != nullptr || symbol->type()->asFunctionType() != nullptr};
}

QString functionNamedAt(const Snapshot &snapshot, const FilePath &filePath,
                        const QTextCursor &cursor)
{
    const ResolvedName resolved = resolveNameAt(snapshot, filePath, cursor);
    return resolved.isFunction ? resolved.qualifiedName : QString();
}

QString nameResolvedAt(const Snapshot &snapshot, const FilePath &filePath,
                       const QTextCursor &cursor)
{
    return resolveNameAt(snapshot, filePath, cursor).qualifiedName;
}

QString classAround(const Snapshot &snapshot, const FilePath &filePath, int line, int column)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QString> klass
        = Internal::cxxFrontendClassAround(filePath, line, column)) {
        return *klass;
    }
#endif

    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc)
        return {};

    Scope * const scope = doc->scopeAt(line, column);
    if (!scope || !scope->asClass())
        return {};
    return Overview().prettyName(LookupContext::fullyQualifiedName(scope));
}

EnclosingFunction functionAround(const FilePath &filePath, int line, int column)
{
    return functionAround(CppModelManager::snapshot(), filePath, line, column);
}

QString functionNamedAt(const FilePath &filePath, const QTextCursor &cursor)
{
    return functionNamedAt(CppModelManager::snapshot(), filePath, cursor);
}

QString nameResolvedAt(const FilePath &filePath, const QTextCursor &cursor)
{
    return nameResolvedAt(CppModelManager::snapshot(), filePath, cursor);
}

QString classAround(const FilePath &filePath, int line, int column)
{
    return classAround(CppModelManager::snapshot(), filePath, line, column);
}

FilePaths filesIncludingFileNamed(const Snapshot &snapshot, const QString &fileName)
{
    FilePaths files;
    for (const Document::Ptr &doc : snapshot) {
        const QList<Document::Include> includes = doc->resolvedIncludes()
                                                  + doc->unresolvedIncludes();
        for (const Document::Include &include : includes) {
            // What the file wrote, which is all there is to go on where the
            // include resolved to nothing.
            if (FilePath::fromUserInput(include.unresolvedFileName()).fileName() != fileName)
                continue;
            files.append(doc->filePath());
            break; // named once is named
        }
    }
    return files;
}

FilePaths includesOf(const FilePath &filePath)
{
    const Document::Ptr doc = CppModelManager::snapshot().document(filePath);
    if (!doc)
        return {};

    const QList<Document::Include> resolved = doc->resolvedIncludes();
    FilePaths includes;
    includes.reserve(resolved.size());
    for (const Document::Include &include : resolved)
        includes.append(include.resolvedFileName());
    return includes;
}

EnclosingFunction functionAround(const Snapshot &snapshot, const FilePath &filePath,
                                 int line, int column)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<Internal::CxxFrontendEnclosingFunction> function
        = Internal::cxxFrontendFunctionAround(filePath, line, column)) {
        return {function->qualifiedName, function->fromLine, function->toLine};
    }
#endif

    const Document::Ptr doc = snapshot.document(filePath);
    if (!doc)
        return {};

    EnclosingFunction function;
    function.qualifiedName = doc->functionAt(line, column, &function.fromLine,
                                             &function.toLine);
    return function;
}

class CodeModelQueries::Private
{
public:
    Snapshot snapshot;
    WorkingCopy workingCopy;
#ifdef QTC_WITH_CXX_FRONTEND
    // In an optional because a reading needs the snapshot and the working
    // copy to be built, and those are members here rather than arguments.
    std::optional<Internal::CxxFrontendReading> model;
#endif

    // Parsed again, tree and all: what the model manager leaves in the
    // snapshot has had its source and syntax tree released, and a walk over
    // the tree is what the built-in front end answers some of this with.
    //
    // Kept for as long as this object is, so that asking several questions
    // about one file costs one parse. A file does not change underneath an
    // object that lives for one question or two.
    Document::Ptr reparse(const FilePath &filePath) const
    {
        const auto known = reparsed.constFind(filePath);
        if (known != reparsed.constEnd())
            return *known;

        QByteArray contents;
        if (const auto source = workingCopy.source(filePath))
            contents = *source;
        else if (const Result<QByteArray> read = filePath.fileContents())
            contents = *read;
        else
            return {};

        const Document::Ptr doc = snapshot.preprocessedDocument(contents, filePath);
        if (doc)
            doc->check();
        reparsed.insert(filePath, doc);
        return doc;
    }

    // \a filePath's own tokens, lexed once however many questions are asked
    // off them -- and nothing where the file cannot be read, which is
    // remembered too so that it is not tried again.
    //
    // What is being typed rather than what is on disk where somebody has the
    // file open, the same way a reparse takes it.
    std::shared_ptr<const Lexed> lexed(const FilePath &filePath) const
    {
        const auto known = lexedFiles.constFind(filePath);
        if (known != lexedFiles.constEnd())
            return *known;

        QByteArray contents;
        if (const auto source = workingCopy.source(filePath))
            contents = *source;
        else if (const Result<QByteArray> read = filePath.fileContents())
            contents = *read;
        else
            return *lexedFiles.insert(filePath, {});

        return *lexedFiles.insert(filePath,
                                  std::make_shared<const Lexed>(QString::fromUtf8(contents)));
    }

    // What \a filePath includes, the headers of its headers among them, out
    // of what has already been read -- and nothing where nothing has read it,
    // which is not the same answer as a file that includes nothing.
    //
    // The reading passed in first, which is the other way round from every
    // other question here, and for two reasons.
    //
    // A closure is not something the two front ends answer differently: it is
    // which files the preprocessor read, and a pass that has read the file
    // knows them. So where that reading has the file its answer is as good,
    // and it costs a walk over documents it holds already -- where reading
    // the file to find out costs a parse of it and every header it reaches,
    // seconds for a file that includes a Qt module.
    //
    // And it is the answer to trust where the two differ. The cxx-frontend
    // model resolves includes against the header paths of one project part,
    // where a pass resolved them as the file was really built, so a closure
    // read there can come back short of one already known.
    //
    // Then the index's store, which kept this beside the entries because a
    // stored reading has to be checked against every file that went into it.
    // It is clangd's IncludeGraph, and answering out of it is what clangd
    // does with every cross-file question: one parse per translation unit
    // ever, and queries served from what that parse was distilled into.
    //
    // Not for a file being edited, whose text is not what was indexed. The
    // same order clangd merges in: what is open wins over what is stored.
    std::optional<FilePaths> closureAlreadyKnown(const FilePath &filePath) const
    {
        if (snapshot.contains(filePath))
            return Utils::toList(snapshot.allIncludesForDocument(filePath));

        if (!workingCopy.get(filePath)) {
            if (CppLocatorData * const index = CppModelManager::locatorData()) {
                if (const std::optional<FilePaths> stored = index->storedIncludesFor(filePath))
                    return stored;
            }
        }
        return std::nullopt;
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The class called \a className -- written out in full -- as the index
    // and that class's own tokens have it between them, or nothing where the
    // two cannot answer.
    //
    // What clangd does with a cross-file question: one parse per translation
    // unit ever, distilled into per-file entries, and every question after
    // that served from those -- and where it needs a text-level fact about a
    // file it never parsed, it lexes that file. Asking a front end instead is
    // a parse of the file and every header it reaches, which for anything
    // including a Qt module is a second; a test framework's scan asks this of
    // every test class and of every class those derive from, so it is the
    // difference between a scan that finishes and one that does not.
    //
    // \a reachable is what \a filePath includes: a class of that name written
    // in a file this one never reads is a different class. Where two of the
    // reachable files write one, or one of them writes two under different
    // scopes, only a reading settles which is meant and this declines.
    std::optional<ClassWithPrivateSlots> classShapeFromTheIndex(
        const QString &className, const FilePath &filePath, const FilePaths &reachable) const
    {
        CppLocatorData * const index = CppModelManager::locatorData();
        if (!index)
            return std::nullopt;

        // The file asked about before the files it reads: where it writes a
        // class of that name itself, that is the one it means.
        const QSet<FilePath> among(reachable.cbegin(), reachable.cend());
        QList<IndexItem::Ptr> candidates;
        for (const IndexItem::Ptr &candidate : index->findSymbols(IndexItem::Class, className)) {
            if (candidate->filePath() == filePath)
                candidates.prepend(candidate);
            else if (among.contains(candidate->filePath()))
                candidates.append(candidate);
        }

        std::optional<ClassWithPrivateSlots> answer;
        for (const IndexItem::Ptr &candidate : std::as_const(candidates)) {
            // Not for a file being edited: what the index has of it is a
            // reading of what was on disk, and the text somebody is typing is
            // not that. The order clangd merges its two indexes in -- what is
            // open wins over what was stored.
            if (workingCopy.get(candidate->filePath()))
                return std::nullopt;

            const std::shared_ptr<const Lexed> tokens = lexed(candidate->filePath());
            if (!tokens)
                return std::nullopt;

            // An entry counts columns from zero, where a place counts them
            // from one.
            const std::optional<WrittenClassShape> shape
                = classShapeIn(*tokens, candidate->filePath(), candidate->symbolName(),
                               candidate->line(), candidate->column() + 1);

            // Nothing written there. Which is what the index says of a
            // forward declaration as much as of a class -- "class tst_Foo;"
            // is an entry of its own -- so the next candidate is tried
            // rather than the whole question given up on.
            if (!shape)
                continue;

            if (answer) {
                // A second class really written under that name. Two files
                // writing one, or two scopes of one file: the index is asked
                // with a name as the code writes it, which for a base class
                // is as little as the class deriving from it bothered with,
                // so both happen. Only a reading knows which is in scope.
                if (answer->klass.filePath != candidate->filePath()
                    || answer->klass.qualifiedName != candidate->scopedSymbolName()) {
                    return std::nullopt;
                }
                continue; // one file writing it twice, in two branches of an #if
            }

            // Where the tokens write it rather than where the index
            // recorded it: those differ for a class declared before it is
            // written, and it is the body a reader is sent to.
            answer.emplace();
            answer->klass = {candidate->symbolName(), candidate->scopedSymbolName(),
                             candidate->filePath(), shape->line, shape->column};
            answer->privateSlots = shape->privateSlots;
            answer->baseClasses = shape->baseClasses;

            // Where the file asked about writes it, nothing else can be
            // meant and the rest are not looked at.
            if (candidate->filePath() == filePath)
                return answer;
        }
        return answer;
    }

    // Where the project defines the function declared at \a line and
    // \a column of \a filePath, out of the index and without reading
    // anything -- or nothing where the index cannot settle it.
    //
    // clangd's rule for a cross-file question: served from the index, never
    // by parsing a closed file. The reading this stands in front of does the
    // opposite -- it parses the file, and then as many of the files whose
    // names look like counterparts of it as its bound allows -- so it is both
    // slower and less complete: a definition in a file that bound leaves out
    // is one the index has and it does not.
    //
    // An entry is keyed by the name written out in full, which is what the
    // tokens around the declaration say: the scopes it stands inside, and the
    // name itself.
    std::optional<Link> definitionFromTheIndex(const FilePath &filePath,
                                               int line, int column) const
    {
        CppLocatorData * const index = CppModelManager::locatorData();
        if (!index)
            return std::nullopt;

        const std::shared_ptr<const Lexed> tokens = lexed(filePath);
        if (!tokens)
            return std::nullopt;

        const int at = tokens->tokenBeginningAt(tokens->lines.offsetOf(line, column));
        if (at < 0 || tokens->tokens.at(at).kind() != T_IDENTIFIER)
            return std::nullopt;

        QStringList scopes = scopesAround(*tokens, at);
        scopes << tokens->spelled(at);
        const QString qualified = scopes.join("::");

        // Exactly one definition, or this says nothing. Two of one name is a
        // project that builds one file or the other -- a platform's own
        // implementation -- and which of them a reader means is not a
        // question the index answers.
        const QString ownName = scopes.last();
        Link found;
        for (const IndexItem::Ptr &candidate : index->findSymbols(IndexItem::Function, qualified)) {
            if (!candidate->isFunctionDefinition() || candidate->scopedSymbolName() != qualified)
                continue;
            if (found.hasValidTarget())
                return std::nullopt;

            // And the place has to be where that function still stands. An
            // entry says where it was when the file was last read for the
            // index, which is behind a file that has moved on since -- one
            // somebody is typing in, or one a checkout rewrote -- and a link
            // into the wrong line is worse than the reading this declines
            // to, which reads the file itself.
            const std::shared_ptr<const Lexed> defining = lexed(candidate->filePath());
            if (!defining)
                return std::nullopt;
            const int where = defining->lines.offsetOf(candidate->line(),
                                                       candidate->column() + 1);
            const int at = defining->tokenBeginningAt(where);
            if (at < 0 || defining->tokens.at(at).kind() != T_IDENTIFIER
                || defining->spelled(at) != ownName) {
                return std::nullopt;
            }

            // An entry counts columns from zero, and so does a link.
            found = Link(candidate->filePath(), candidate->line(), candidate->column());
        }
        if (!found.hasValidTarget())
            return std::nullopt;
        return found;
    }
#endif

    mutable QHash<FilePath, Document::Ptr> reparsed;
    mutable QHash<FilePath, std::shared_ptr<const Lexed>> lexedFiles;
};

CodeModelQueries::CodeModelQueries(const Snapshot &snapshot, const WorkingCopy &workingCopy)
    : d(new Private)
{
    d->snapshot = snapshot;
    d->workingCopy = workingCopy;
#ifdef QTC_WITH_CXX_FRONTEND
    d->model.emplace(workingCopy);
#endif
}

CodeModelQueries::~CodeModelQueries() = default;

WrittenClass CodeModelQueries::classUsingClass(const FilePath &filePath, const QString &className,
                                               int maxIncludeDepth) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // A header is read into whoever includes it, so one document holds every
    // file this walks -- which is why the depth is applied to the answers
    // rather than to the reading.
    if (const std::optional<QList<CxxFrontendDocument::ClassUsingAClass>> classes
        = d->model->classesUsing(filePath, className)) {
        FilePaths reachable{filePath};
        if (maxIncludeDepth > 0) {
            if (const Document::Ptr doc = d->snapshot.document(filePath))
                reachable += doc->includedFiles();
        }
        for (const FilePath &candidate : std::as_const(reachable)) {
            for (const CxxFrontendDocument::ClassUsingAClass &klass : *classes) {
                if (FilePath::fromUserInput(klass.place.filePath) != candidate)
                    continue;
                return {klass.name, klass.qualifiedName, candidate, klass.place.line,
                        klass.place.column};
            }
        }
        // Nothing found is not the same as nothing there: a type nothing
        // declares names no class on this model, and a ui header that has
        // not been generated yet is exactly that. The built-in front end
        // takes such a type for a class of the name that was written, and
        // where this model has no answer that one's is the answer.
    }
#endif
    return builtinClassUsingClass(d->snapshot, filePath, className, maxIncludeDepth);
}

QList<WrittenFunction> CodeModelQueries::memberFunctionsOf(const WrittenClass &klass) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QList<CxxFrontendDocument::MemberFunction>> members
        = d->model->memberFunctionsIn(klass.filePath, klass.filePath, klass.line, klass.column);
        members && !members->isEmpty()) {
        QList<WrittenFunction> functions;
        for (const CxxFrontendDocument::MemberFunction &member : *members) {
            functions << WrittenFunction{member.unqualifiedName, member.signature,
                                         FilePath::fromUserInput(member.filePath),
                                         member.line, member.column};
        }
        return functions;
    }
#endif

    const Class * const found = classWrittenAt(d->snapshot.document(klass.filePath),
                                               klass.line, klass.column);
    if (!found)
        return {};

    const Overview overview;
    QList<WrittenFunction> functions;
    for (int i = 0, count = found->memberCount(); i < count; ++i) {
        Symbol * const member = found->memberAt(i);
        const Declaration * const decl = member->asDeclaration();
        Function * const function = decl ? decl->type()->asFunctionType() : member->asFunction();
        if (!function)
            continue;
        functions << WrittenFunction{overview.prettyName(function->name()),
                                     signatureOf(function),
                                     klass.filePath,
                                     member->line(),
                                     member->column()};
    }
    return functions;
}

QList<WrittenClass> CodeModelQueries::classesDeclaredIn(const FilePath &filePath) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // An empty list is an answer: a file that declares nothing of its own
    // declares nothing. Handing the question back would answer it off the
    // built-in front end, whose global namespace holds what the headers
    // declare as well -- so a file with nothing in it would be shown
    // everything its headers have.
    if (const std::optional<QList<CxxFrontendDocument::Symbol>> symbols
        = d->model->symbolsIn(filePath)) {
        QList<WrittenClass> classes;
        for (const CxxFrontendDocument::Symbol &symbol : *symbols) {
            // A class named without its body declares nothing to say
            // anything about, and one a macro wrote stands nowhere.
            if (symbol.kind != CxxFrontendDocument::Kind::Class || symbol.isForwardDeclaration
                || symbol.isGenerated || symbol.name.isEmpty()) {
                continue;
            }
            QStringList path = symbol.qualified;
            path << symbol.name;
            classes.append({symbol.name, path.join("::"), filePath, symbol.line, symbol.column});
        }
        return classes;
    }
#endif

    const Document::Ptr doc = d->snapshot.document(filePath);
    if (!doc)
        return {};
    QList<WrittenClass> classes;
    collectClasses(doc->globalNamespace(), filePath, &classes);
    return classes;
}

QList<WrittenDeclaration> CodeModelQueries::declarationsIn(const FilePath &filePath) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // An empty list is an answer: a file that declares nothing of its own
    // declares nothing. Handing the question back would answer it off the
    // built-in front end, whose global namespace holds what the headers
    // declare as well -- so a file with nothing in it would be shown
    // everything its headers have.
    if (const std::optional<QList<CxxFrontendDocument::Symbol>> symbols
        = d->model->symbolsIn(filePath)) {
        QList<WrittenDeclaration> declarations;

        // Which entry each symbol became, since what is left out takes what
        // is written inside it along. The list has a scope before its
        // members, so the answer is always already here.
        QList<int> entryFor(symbols->size(), -1);

        for (int i = 0; i < symbols->size(); ++i) {
            const CxxFrontendDocument::Symbol &symbol = symbols->at(i);

            // A name a macro's body wrote stands nowhere a reader could be
            // taken to, a class named without its body declares nothing
            // here to say anything about, something extern is a promise
            // about a declaration elsewhere, and a using declaration makes
            // a name reachable rather than declaring it.
            if (symbol.isGenerated || symbol.isForwardDeclaration || symbol.isExtern
                || symbol.kind == CxxFrontendDocument::Kind::UsingDeclaration) {
                continue;
            }

            int parent = -1;
            if (symbol.parent >= 0) {
                parent = entryFor.at(symbol.parent);
                if (parent < 0)
                    continue;
            }

            entryFor[i] = int(declarations.size());
            declarations.append({symbol.name,
                                 writtenTypeOf(symbol),
                                 symbol.icon,
                                 filePath,
                                 symbol.line,
                                 symbol.column,
                                 parent,
                                 symbol.kind == CxxFrontendDocument::Kind::Namespace});
        }
        return declarations;
    }
#endif

    const Document::Ptr doc = d->snapshot.document(filePath);
    if (!doc)
        return {};

    QList<WrittenDeclaration> declarations;
    collectDeclarations(doc->globalNamespace(), filePath, -1, &declarations);
    return declarations;
}

FilePaths CodeModelQueries::includeClosureOf(const FilePath &filePath) const
{
    if (const std::optional<FilePaths> known = d->closureAlreadyKnown(filePath))
        return *known;

    // And only then the file itself, which is a parse of it and every header
    // it reaches -- seconds, where the two the helper asks are a lookup.
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<FilePaths> reached = d->model->allIncludesFor(filePath))
        return *reached;
#endif

    return {};
}

QList<CodeModelQueries::WrittenMacroUse> CodeModelQueries::macroUsesIn(
    const FilePath &filePath, const QStringList &names) const
{
    if (names.isEmpty())
        return {};

    QByteArray contents;
    if (const auto source = d->workingCopy.source(filePath))
        contents = *source;
    else if (const Result<QByteArray> read = filePath.fileContents())
        contents = *read;
    else
        return {};

    const QString text = QString::fromUtf8(contents);
    const QSet<QString> wanted(names.cbegin(), names.cend());

    // Whichever scanner is installed, which is the same one the front ends
    // read with.
    SimpleLexer lexer;
    lexer.setSkipComments(true);
    const Tokens tokens = lexer(text);

    QList<WrittenMacroUse> uses;
    for (int i = 0; i + 1 < tokens.size(); ++i) {
        const Token &name = tokens.at(i);
        if (name.kind() != T_IDENTIFIER || tokens.at(i + 1).kind() != T_LPAREN)
            continue;

        const QString spelled = spelling(text, name);
        if (!wanted.contains(spelled) || insideADirective(text, name.utf16charsBegin()))
            continue;

        // What stands between the parentheses, split where the preprocessor
        // splits it: on a comma inside no brackets of any kind. A comma
        // between angle brackets is not one of those -- Thing<int, int> is
        // two arguments to a macro, however it reads.
        QStringList arguments;
        int depth = 0;
        int argumentBegin = tokens.at(i + 1).utf16charsEnd();
        int end = i + 1;
        for (; end < tokens.size(); ++end) {
            const Token &token = tokens.at(end);
            const int kind = token.kind();
            if (kind == T_LPAREN || kind == T_LBRACKET || kind == T_LBRACE) {
                ++depth;
            } else if (kind == T_RPAREN || kind == T_RBRACKET || kind == T_RBRACE) {
                if (--depth > 0)
                    continue;
                arguments.append(text.mid(argumentBegin,
                                          token.utf16charsBegin() - argumentBegin).trimmed());
                break;
            } else if (kind == T_COMMA && depth == 1) {
                arguments.append(text.mid(argumentBegin,
                                          token.utf16charsBegin() - argumentBegin).trimmed());
                argumentBegin = token.utf16charsEnd();
            }
        }

        // Nothing closed it, so the file ends inside the call and there is
        // nothing to read off it.
        if (end == tokens.size())
            continue;

        // A use with no arguments says nothing.
        if (arguments.size() != 1 || !arguments.first().isEmpty())
            uses.append({spelled, arguments});
        i = end;
    }
    return uses;
}

QList<CodeModelQueries::WrittenCall> CodeModelQueries::callsTo(
    const FilePath &filePath, const QStringList &functionNames) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // The file's own tokens first, which cost no parse at all: a tag is a
    // literal handed to a call, and which function writes it is which braces
    // the call stands between.
    //
    // Only where this model is the one in use: the built-in path below is
    // what this series is replacing, not what it is improving.
    if (Internal::cxxFrontendModelRequested()) {
        if (const std::shared_ptr<const Lexed> tokens = d->lexed(filePath)) {
            if (const std::optional<QList<WrittenCall>> calls
                = callsToIn(*tokens, functionNames)) {
                return *calls;
            }
        }
    }

    if (const std::optional<QList<CxxFrontendDocument::WrittenCall>> calls
        = d->model->callsIn(filePath, functionNames)) {
        QList<WrittenCall> written;
        for (const CxxFrontendDocument::WrittenCall &call : *calls)
            written.append({call.insideFunction, call.arguments, call.line, call.column});
        return written;
    }
#endif

    const Document::Ptr doc = d->reparse(filePath);
    if (!doc || !doc->translationUnit() || !doc->translationUnit()->ast())
        return {};
    return CallsTo(doc, functionNames).calls();
}

QStringList CodeModelQueries::classesPassedTo(const FilePath &filePath,
                                              const QString &functionName) const
{
    // Once each, whichever front end answers and however many times a class
    // is handed over: a caller that finds several classes takes the file for
    // one that runs several tests, which takes the checkbox off every one of
    // them. The tokens below hold both branches of an #ifdef where a build
    // takes one, so that is where a repeat comes from -- and it is the same
    // reason the text-based reading of a main() in QtTestParser dedupes.
    const auto onceEach = [](const QStringList &classes) {
        return Utils::filteredUnique(classes);
    };

#ifdef QTC_WITH_CXX_FRONTEND
    // The file's own tokens first, which cost no parse at all. Which class a
    // test runs is what its main() says in so many words, and most of the
    // files this is asked about call no runner at all -- so the common answer
    // is an empty list off a lexer rather than a translation unit.
    //
    // Only where this model is the one in use: the built-in path below is
    // what this series is replacing, not what it is improving.
    if (Internal::cxxFrontendModelRequested()) {
        if (const std::shared_ptr<const Lexed> tokens = d->lexed(filePath)) {
            if (const std::optional<QStringList> classes
                = classesPassedToIn(*tokens, functionName)) {
                return onceEach(*classes);
            }
        }
    }

    if (const std::optional<QStringList> classes
        = d->model->classesPassedToIn(filePath, functionName)) {
        return onceEach(*classes);
    }
#endif

    const Document::Ptr doc = d->reparse(filePath);
    if (!doc || !doc->translationUnit() || !doc->translationUnit()->ast())
        return {};
    return onceEach(ClassesPassedTo(doc, d->snapshot, functionName).classes());
}

CodeModelQueries::ClassWithPrivateSlots CodeModelQueries::classWithPrivateSlots(
    const FilePath &filePath, const QString &className) const
{
    const int afterTheScopes = className.lastIndexOf("::");
    const QString ownName = afterTheScopes < 0 ? className : className.mid(afterTheScopes + 2);

#ifdef QTC_WITH_CXX_FRONTEND
    // The index and the class's own tokens first, which is the whole answer
    // for no parse at all where they have it.
    //
    // Only where this model is the one in use: with the built-in one running
    // there is a pass that has read every file, and its answer costs nothing
    // either and is a reading of the translation unit rather than of the
    // text. It is the one below, and this would be a step backwards from it.
    if (Internal::cxxFrontendModelRequested()) {
        if (const std::optional<FilePaths> reachable = d->closureAlreadyKnown(filePath)) {
            if (const std::optional<ClassWithPrivateSlots> found
                = d->classShapeFromTheIndex(className, filePath, *reachable)) {
                return *found;
            }
        }
    }

    if (const std::optional<CxxFrontendDocument::Place> place
        = d->model->classNamedIn(filePath, className);
        place && place->line > 0) {
        const FilePath classFile = FilePath::fromUserInput(place->filePath);

        ClassWithPrivateSlots answer;
        answer.klass = {ownName, className, classFile, place->line, place->column};

        // Read where the class is written rather than where it was named:
        // what it declares is the same either way, and the file that writes
        // it is the one a reader is sent to.
        if (const std::optional<QList<CxxFrontendDocument::MemberFunction>> members
            = d->model->memberFunctionsIn(classFile, classFile, place->line, place->column)) {
            for (const CxxFrontendDocument::MemberFunction &member : *members) {
                if (member.access != CxxFrontendDocument::Access::Private
                    || member.qtMethod != CxxFrontendDocument::QtMethod::Slot) {
                    continue;
                }
                answer.privateSlots << WrittenFunction{member.unqualifiedName, member.signature,
                                                       FilePath::fromUserInput(member.filePath),
                                                       member.line, member.column};
            }
        }
        if (const std::optional<QStringList> bases
            = d->model->basesOfTheClassIn(classFile, place->line, place->column)) {
            answer.baseClasses = *bases;
        }
        return answer;
    }
#endif

    const Class * const klass = builtinClassNamed(d->snapshot, filePath, className);
    if (!klass)
        return {};

    const Overview overview;
    ClassWithPrivateSlots answer;
    answer.klass = {overview.prettyName(klass->name()),
                    overview.prettyName(
                        LookupContext::fullyQualifiedName(const_cast<Class *>(klass))),
                    FilePath::fromUtf8(klass->fileName()),
                    klass->line(),
                    klass->column()};

    for (int i = 0, count = klass->memberCount(); i < count; ++i) {
        Symbol * const member = klass->memberAt(i);
        Function * const function = member->type().type()
                                        ? member->type().type()->asFunctionType()
                                        : nullptr;
        if (!function || !function->isSlot() || !member->isPrivate())
            continue;
        answer.privateSlots << WrittenFunction{overview.prettyName(function->name()),
                                               signatureOf(function),
                                               FilePath::fromUtf8(member->fileName()),
                                               member->line(),
                                               member->column()};
    }

    for (int i = 0, count = klass->baseClassCount(); i < count; ++i) {
        if (BaseClass * const base = klass->baseClassAt(i))
            answer.baseClasses << overview.prettyName(LookupContext::fullyQualifiedName(base));
    }
    return answer;
}

DeclarationToDefine CodeModelQueries::declarationToDefineAt(const CppRefactoringChanges &changes,
                                                            const FilePath &filePath,
                                                            int line, int column) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<DeclarationToDefine> declaration
        = d->model->declarationToDefineIn(filePath, line, column);
        declaration && declaration->isValid()) {
        return *declaration;
    }
#endif

    const CppRefactoringFilePtr file = changes.cppFile(filePath);
    Function * const function = functionWrittenAt(file ? file->cppDocument() : Document::Ptr(),
                                                  line, column);
    return function ? declarationToDefine(function, changes) : DeclarationToDefine();
}

Link CodeModelQueries::definitionOfFunctionAt(const FilePath &filePath, int line, int column) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // The index first, which costs no parse at all: where a function is
    // defined is what an index is for, and this one holds every definition
    // the project has.
    //
    // Only where this model is the one in use: the built-in path below reads
    // the question off a pass that has already parsed the project, which is
    // as free and is what this series is replacing rather than improving.
    if (Internal::cxxFrontendModelRequested()) {
        if (const std::optional<Link> definition = d->definitionFromTheIndex(filePath, line,
                                                                             column)) {
            return *definition;
        }
    }

    if (const std::optional<Link> definition
        = d->model->definitionOfFunctionIn(filePath, line, column);
        definition && definition->hasValidTarget()) {
        return *definition;
    }
#endif

    Function * const function = functionWrittenAt(d->snapshot.document(filePath), line, column);
    if (!function)
        return {};

    SymbolFinder symbolFinder;
    const Function * const definition = symbolFinder.findMatchingDefinition(function, d->snapshot,
                                                                            true);
    if (!definition)
        return {};
    // Through toLink(), which is what says how a link counts columns: from
    // zero, where a symbol counts them from one.
    return definition->toLink();
}

Link CodeModelQueries::definitionOfWhatIsDeclaredAt(const FilePath &filePath,
                                                    int line, int column) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // The other model answers for a function. A variable declared in one
    // file and defined in another is a question about the project that it
    // does not take, so that one is left to the front end that does.
    if (const std::optional<Link> definition
        = d->model->definitionOfFunctionIn(filePath, line, column);
        definition && definition->hasValidTarget()) {
        return *definition;
    }
#endif

    const Document::Ptr doc = d->snapshot.document(filePath);
    if (!doc)
        return {};

    Symbol * const symbol = doc->lastVisibleSymbolAt(line, column);
    if (!symbol || !symbol->type().type())
        return {};

    SymbolFinder symbolFinder;
    const Symbol * const definition
        = symbol->type().type()->asFunctionType()
              ? symbolFinder.findMatchingDefinition(symbol, d->snapshot, false)
              : symbolFinder.findMatchingVarDefinition(symbol, d->snapshot);
    return definition ? definition->toLink() : Link();
}

} // namespace CppEditor
