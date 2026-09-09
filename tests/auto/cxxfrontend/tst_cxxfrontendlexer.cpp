// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// Runs SimpleLexer and CxxFrontendLexer over the same input and compares what
// they produce, token for token. This is what makes swapping the scanner
// underneath the editor a decision that can be checked rather than hoped for:
// the highlighter, the indenter and the completion machinery all read these
// tokens, and none of them care which scanner produced them as long as the
// answer is the same.
//
// Input comes from the corpora of tests/auto/cplusplus, and each file is fed
// through twice: whole, and a line at a time carrying the state from one line
// to the next, the way BaseTextDocumentLayout drives the highlighter. The
// second is the one that exercises resuming inside a block comment or a raw
// string, which the cxx-frontend scanner has no notion of on its own.
//
// The corpora are small enough to keep this test quick. They are not what the
// two were actually reconciled against: that was every .cpp and .h under src/,
// 11303 files, of which 9 disagreed. Eight of those were 'c'_X, fixed upstream
// since; the one left is 0x0p+0, listed in knownDivergence() below. Point
// addCorpusRows() at src/ with a QDirIterator to repeat it.

#include <cplusplus/CxxFrontendLexer.h>
#include <cplusplus/SimpleLexer.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>

//TESTED_COMPONENT=src/libs/cplusplus

using namespace CPlusPlus;

Q_DECLARE_METATYPE(LanguageFeatures)

namespace {

QString corpusRoot()
{
    return QString(SRCDIR "/../cplusplus");
}

QString describe(const Token &token)
{
    return QString("%1 @%2+%3%4%5%6%7")
        .arg(QString::fromLatin1(Token::name(token.kind())))
        .arg(token.utf16charsBegin())
        .arg(token.utf16chars())
        .arg(token.newline() ? " nl" : "")
        .arg(token.whitespace() ? " ws" : "")
        .arg(token.joined() ? " joined" : "")
        .arg(token.userDefinedLiteral() ? " udl" : "");
}

QStringList describe(const Tokens &tokens)
{
    QStringList result;
    result.reserve(tokens.size());
    for (const Token &token : tokens)
        result.append(describe(token));
    return result;
}

// Drives a lexer the way the highlighter does: a lexer per line, with the
// state and the pending raw string terminator carried across by the caller,
// which is what CppHighlighter keeps in the block's data. Not one lexer kept
// alive, because then a lexer could carry something across that it never
// reported, and the caller reading the same code from its own state would get
// something else -- which is exactly what happened.
template<typename LexerType>
QStringList lexByLine(const QString &text, LanguageFeatures features)
{
    QStringList result;
    int state = 0;
    QByteArray rawStringSuffix;

    for (const QString &line : text.split('\n')) {
        LexerType lexer;
        lexer.setLanguageFeatures(features);
        lexer.setExpectedRawStringSuffix(rawStringSuffix);

        const Tokens tokens = lexer(line, state);
        state = lexer.state();
        rawStringSuffix = lexer.expectedRawStringSuffix();

        result.append(describe(tokens));
        // The terminator is part of the answer, not scratch space: the
        // highlighter reads it to know that the next line continues a raw
        // string, and formats the closing delimiter from it.
        result.append(QString("| state %1%2 suffix %3")
                          .arg(state)
                          .arg(lexer.endedJoined() ? " joined" : "")
                          .arg(QString::fromUtf8(rawStringSuffix)));
    }
    return result;
}

template<typename LexerType>
QStringList lexWhole(const QString &text, LanguageFeatures features)
{
    LexerType lexer;
    lexer.setLanguageFeatures(features);
    return describe(lexer(text, 0));
}

// The first place the two disagree, rendered so that the failure says what
// differs rather than dumping two long lists.
QString firstDifference(const QStringList &expected, const QStringList &actual)
{
    const int count = qMin(expected.size(), actual.size());
    for (int i = 0; i < count; ++i) {
        if (expected.at(i) != actual.at(i)) {
            return QString("token %1:\n  SimpleLexer:       %2\n  CxxFrontendLexer:  %3")
                .arg(i).arg(expected.at(i), actual.at(i));
        }
    }
    if (expected.size() != actual.size()) {
        return QString("token count: SimpleLexer %1, CxxFrontendLexer %2\n"
                       "  first extra: %3")
            .arg(expected.size()).arg(actual.size())
            .arg(expected.size() > actual.size() ? expected.at(count) : actual.at(count));
    }
    return {};
}

// Why the two are allowed to disagree on a given input, or nullptr if they
// are not. The one entry left is a case where CxxFrontendLexer is in the
// right; it is here so that the disagreement is written down and so that the
// day it goes away is noticed. As in tst_cxxfrontend, the list is a ratchet
// both ways -- an unlisted disagreement fails, and so does a listed one that
// has stopped happening.
const char *knownDivergence(const QString &row)
{
    // The built-in lexer has no hexadecimal floating point literals, so it
    // reads the p of 0x1p3 as the start of a ud-suffix.
    if (row == "hex floating literal")
        return "SimpleLexer reads 0x1p3 as a user-defined literal";

    return nullptr;
}

} // namespace

class tst_cxxfrontendlexer : public QObject
{
    Q_OBJECT

private slots:
    void snippets_data();
    void snippets();

    void resumedSnippets_data();
    void resumedSnippets();

    void corpusWhole_data();
    void corpusWhole();

    void corpusByLine_data();
    void corpusByLine();

    void installingAScannerChangesWhatSimpleLexersCallersGet();
    void anInstalledScannerCarriesTheStateAcrossLines();
};

void tst_cxxfrontendlexer::snippets_data()
{
    QTest::addColumn<QString>("source");

    QTest::newRow("empty") << "";
    QTest::newRow("identifiers and keywords") << "int main() { return 0; }";
    QTest::newRow("operators") << "a <=> b; c >>= d; e ->* f; g ... h";
    QTest::newRow("alternative operators") << "if (a and b or not c) {}";
    QTest::newRow("line comment") << "// a comment\nint x;";
    QTest::newRow("doxygen line comment") << "/// doc\n//! doc\nint x;";
    QTest::newRow("block comment") << "/* a comment */ int x;";
    QTest::newRow("doxygen block comment") << "/** doc */ /*! doc */ int x;";
    QTest::newRow("empty block comment") << "/**/ int x;";
    QTest::newRow("string literals")
        << R"(auto a = "s"; auto b = L"s"; auto c = u8"s"; auto d = u"s"; auto e = U"s";)";
    QTest::newRow("raw string") << R"(auto a = R"delim(body)delim";)";
    QTest::newRow("char literals") << "auto a = 'c'; auto b = L'c'; auto c = U'c';";
    QTest::newRow("numeric literals") << "auto a = 1'000; auto b = 0xffu; auto c = .5f;";
    QTest::newRow("hex floating literal") << "auto a = 0x1p3;";
    QTest::newRow("user defined literals") << R"(auto a = 12_km + 0.5_Pa + "abd"_L;)";
    QTest::newRow("include angle") << "#include <vector>\nint x;";
    QTest::newRow("include quoted") << "#include \"header.h\"\nint x;";
    QTest::newRow("less than is not an include") << "if (a < b && c > d) {}";
    QTest::newRow("line splice") << "int a\\\n b;";
    QTest::newRow("non ascii") << QString::fromUtf8("auto \xc3\xa4 = \"\xc3\xb6\xc3\xbc\"; int x;");
    QTest::newRow("qt keywords") << "class A { signals: void s(); public slots: void t(); };";
}

void tst_cxxfrontendlexer::snippets()
{
    QFETCH(QString, source);

    const LanguageFeatures features = LanguageFeatures::defaultFeatures();
    const QStringList expected = lexWhole<SimpleLexer>(source, features);
    const QStringList actual = lexWhole<CxxFrontendLexer>(source, features);

    const QString difference = firstDifference(expected, actual);
    if (const char *reason = knownDivergence(QTest::currentDataTag()))
        QEXPECT_FAIL("", reason, Abort);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
}

// The states that only exist because an editor lexes a line at a time. The
// corpora hardly touch these, and they are the part CxxFrontendLexer has to
// supply itself, the cxx-frontend scanner having no notion of resuming.
void tst_cxxfrontendlexer::resumedSnippets_data()
{
    QTest::addColumn<QString>("source");

    QTest::newRow("block comment over lines") << "/* one\ntwo\nthree */ int x;";
    QTest::newRow("doxygen comment over lines") << "/** one\n * two\n */ int x;";
    QTest::newRow("unterminated block comment") << "int x;\n/* one\ntwo";
    QTest::newRow("empty line in comment") << "/*\n\n*/ int x;";
    QTest::newRow("comment closed at once") << "/*\n*/int x;";
    QTest::newRow("raw string over lines") << "auto a = R\"x(\none\ntwo\n)x\"; int b;";
    QTest::newRow("unterminated raw string") << "auto a = R\"x(\none";
    QTest::newRow("raw string with suffix") << "auto a = R\"x(\none\n)x\"_L; int b;";
    QTest::newRow("spliced line comment") << "// one \\\ntwo\nint x;";
    QTest::newRow("spliced string") << "auto a = \"one \\\ntwo\"; int b;";
    QTest::newRow("trailing backslash") << "int a \\\n= 1;";
    QTest::newRow("comment then code") << "/* c */ int x;\nint y;";
    // What the highlighter's own test case is made of, and what the corpora
    // have none of: a blank line inside a raw string, a line of one that
    // looks like it opens another, and a quote in the middle of one.
    QTest::newRow("blank line in a raw string") << "auto a = R\"(one\n\ntwo)\";";
    QTest::newRow("raw string looking like a prefix")
        << "auto a = R\"(foo\n\n        R\"notaprefix!(\n    barfoobar)\";";
    QTest::newRow("quote inside a raw string") << "auto a = uR\"(\"o\n     ne\")\"_w;";
}

void tst_cxxfrontendlexer::resumedSnippets()
{
    QFETCH(QString, source);

    const LanguageFeatures features = LanguageFeatures::defaultFeatures();
    const QString difference = firstDifference(lexByLine<SimpleLexer>(source, features),
                                               lexByLine<CxxFrontendLexer>(source, features));
    if (const char *reason = knownDivergence(QTest::currentDataTag()))
        QEXPECT_FAIL("", reason, Abort);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
}

static void addCorpusRows()
{
    QTest::addColumn<QString>("filePath");
    QTest::addColumn<LanguageFeatures>("features");

    const struct {
        const char *subdir;
        const char *pattern;
        bool isC;
    } corpora[] = {
        {"c99/data", "*.c", true},
        {"cxx11/data", "*.cpp", false},
    };

    for (const auto &corpus : corpora) {
        const QDir dir(corpusRoot() + '/' + corpus.subdir);
        const QFileInfoList entries
            = dir.entryInfoList({corpus.pattern}, QDir::Files, QDir::Name);
        QVERIFY2(!entries.isEmpty(),
                 qPrintable(QString("no corpus files in %1").arg(dir.path())));
        const LanguageFeatures features = corpus.isC ? LanguageFeatures::cFeatures()
                                                     : LanguageFeatures::defaultFeatures();
        for (const QFileInfo &entry : entries)
            QTest::newRow(qPrintable(entry.fileName())) << entry.filePath() << features;
    }
}

static QString readCorpusFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

void tst_cxxfrontendlexer::corpusWhole_data()
{
    addCorpusRows();
}

void tst_cxxfrontendlexer::corpusWhole()
{
    QFETCH(QString, filePath);
    QFETCH(LanguageFeatures, features);

    const QString source = readCorpusFile(filePath);
    QVERIFY2(!source.isEmpty(), qPrintable(filePath));

    const QString difference = firstDifference(lexWhole<SimpleLexer>(source, features),
                                               lexWhole<CxxFrontendLexer>(source, features));
    if (const char *reason = knownDivergence(QTest::currentDataTag()))
        QEXPECT_FAIL("", reason, Abort);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
}

void tst_cxxfrontendlexer::corpusByLine_data()
{
    addCorpusRows();
}

void tst_cxxfrontendlexer::corpusByLine()
{
    QFETCH(QString, filePath);
    QFETCH(LanguageFeatures, features);

    const QString source = readCorpusFile(filePath);
    QVERIFY2(!source.isEmpty(), qPrintable(filePath));

    const QString difference = firstDifference(lexByLine<SimpleLexer>(source, features),
                                               lexByLine<CxxFrontendLexer>(source, features));
    if (const char *reason = knownDivergence(QTest::currentDataTag()))
        QEXPECT_FAIL("", reason, Abort);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
}

// Comparing the two lexers says they agree; it does not say that anything
// reads the new one. Callers hold a SimpleLexer, so what has to be shown is
// that installing the scanner changes what a SimpleLexer produces -- and that
// taking it away brings the built-in answer back, since a switch that only
// goes one way is not a switch.
//
// The input is the one case where the two are known to disagree, which is
// exactly what makes it able to tell them apart. If SimpleLexer ever grows
// hexadecimal floating point literals this test loses its grip, and
// knownDivergence() above is what will say so.
void tst_cxxfrontendlexer::installingAScannerChangesWhatSimpleLexersCallersGet()
{
    const QString source = "auto a = 0x1p3;";
    const LanguageFeatures features = LanguageFeatures::defaultFeatures();

    const QStringList builtIn = lexWhole<SimpleLexer>(source, features);
    const QStringList replacement = lexWhole<CxxFrontendLexer>(source, features);
    QVERIFY2(builtIn != replacement, "the input no longer tells the two scanners apart");
    QVERIFY(!SimpleLexer::hasScanner());

    useCxxFrontendLexer(true);
    QVERIFY(SimpleLexer::hasScanner());
    QCOMPARE(lexWhole<SimpleLexer>(source, features), replacement);

    useCxxFrontendLexer(false);
    QVERIFY(!SimpleLexer::hasScanner());
    QCOMPARE(lexWhole<SimpleLexer>(source, features), builtIn);
}

// The scanner is a function, so everything a lexer would have remembered
// between two lines travels through the request and back: the state, the raw
// string delimiter still being looked for, and whether the line ended spliced.
// Getting that plumbing wrong shows up here and nowhere else, because a single
// chunk needs none of it.
void tst_cxxfrontendlexer::anInstalledScannerCarriesTheStateAcrossLines()
{
    const LanguageFeatures features = LanguageFeatures::defaultFeatures();
    const QStringList sources{"/* one\ntwo\nthree */ int x;",
                              "auto a = R\"x(\none\n)x\"_L; int b;",
                              "// one \\\ntwo\nint x;"};

    for (const QString &source : sources) {
        const QStringList direct = lexByLine<CxxFrontendLexer>(source, features);

        useCxxFrontendLexer(true);
        const QStringList throughScanner = lexByLine<SimpleLexer>(source, features);
        useCxxFrontendLexer(false);

        // Same argument order as everywhere else here, so that the labels in
        // the failure say which side is which.
        const QString difference = firstDifference(throughScanner, direct);
        QVERIFY2(difference.isEmpty(), qPrintable(source + '\n' + difference));
    }
}

QTEST_GUILESS_MAIN(tst_cxxfrontendlexer)

#include "tst_cxxfrontendlexer.moc"
