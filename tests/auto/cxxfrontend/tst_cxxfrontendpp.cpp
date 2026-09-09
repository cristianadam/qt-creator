// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// Compares what the two preprocessors hand to the parser.
//
// Neither one's output is text the other would recognise: the built-in engine
// returns a byte array with #line markers and markers around what a macro
// produced, the cxx-frontend one returns tokens. What has to agree is what a
// parser makes of them, so both outputs are reduced to a stream of token
// spellings, and that is what is compared.
//
// The other half of what CppSourceProcessor asks for is the running commentary
// -- which macro was used where, which blocks #if skipped, what the include
// guard was -- which now arrives through the upstream PreprocessorDelegate and
// is checked here too. preprocessorGaps() at the bottom records what is still
// missing rather than pretending it away.
//
// The corpora here are small enough to keep the test quick. The two were also
// run over every .cpp and .h under src/, 11303 files: 439 disagreed, 411 of
// them the quickfix test data under cppeditor/testcases, whose @ markers are
// not C++ and which the two engines discard differently. The remaining 28 are
// macro expansions that genuinely differ and are not yet accounted for. Point
// corpus_data() at src/ with a QDirIterator to repeat it.

#include <cplusplus/CxxFrontendPreprocessor.h>

#include <cplusplus/SimpleLexer.h>
#include <cplusplus/PreprocessorClient.h>
#include <cplusplus/PreprocessorEnvironment.h>
#include <cplusplus/pp.h>

#include <utils/filepath.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>

//TESTED_COMPONENT=src/libs/cplusplus

using namespace CPlusPlus;

namespace {

QString corpusRoot()
{
    return QString(SRCDIR "/../cplusplus");
}

// A Client that answers nothing: the corpora are standalone snippets, so no
// include is resolved and no callback is acted on.
class SilentClient : public Client
{
public:
    void macroAdded(const Macro &) override {}
    void pragmaAdded(const Pragma &) override {}
    void passedMacroDefinitionCheck(int, int, int, const Macro &) override {}
    void failedMacroDefinitionCheck(int, int, const ByteArrayRef &) override {}
    void notifyMacroReference(int, int, int, const Macro &) override {}
    void startExpandingMacro(int, int, int, const Macro &,
                             const QList<MacroArgumentReference> &) override {}
    void stopExpandingMacro(int, const Macro &) override {}
    void markAsIncludeGuard(const QByteArray &) override {}
    void startSkippingBlocks(int) override {}
    void stopSkippingBlocks(int) override {}
    void sourceNeeded(int, const Utils::FilePath &, IncludeType,
                      const Utils::FilePaths &) override {}
};

// The tokens the text lexes to, which is as much of the output as a parser
// can tell apart. Kinds rather than spellings, because the two engines write
// an operator out differently -- one keeps the not the source had, the other
// prints the ! it means -- and the parser cannot tell those apart either.
// Identifiers and literals do carry their spelling, since that is the whole
// of what a macro expansion changes. Comments and line markers are dropped:
// one engine emits #line directives and the other does not, and neither
// reaches the parser.
const QLatin1String stringRunPrefix("<string literal run> ");

// What a string literal says, without its prefix and quotes, so that a run of
// them can be joined without caring how each was spelled.
QString stringContents(const QString &spelling)
{
    const qsizetype first = spelling.indexOf('"');
    const qsizetype last = spelling.lastIndexOf('"');
    if (first < 0 || last <= first)
        return spelling;
    return spelling.mid(first + 1, last - first - 1);
}

QStringList tokenStream(const QString &text)
{
    SimpleLexer lexer;
    lexer.setLanguageFeatures(LanguageFeatures::defaultFeatures());
    lexer.setSkipComments(true);

    QStringList result;
    bool inLineMarker = false;
    for (const Token &token : lexer(text, 0)) {
        if (token.newline())
            inLineMarker = token.is(T_POUND);
        if (inLineMarker)
            continue;
        const QString spelling = text.mid(token.utf16charsBegin(), token.utf16chars());

        // Adjacent string literals are one literal by the time anything looks
        // at them; the two engines simply disagree about who joins them. The
        // cxx-frontend one does it, the built-in one leaves it to the parser.
        // Join them here as well, so that the comparison is about what the
        // parser ends up with rather than about which phase did the joining.
        if (token.isStringLiteral() && !result.isEmpty()
            && result.last().startsWith(stringRunPrefix)) {
            result.last() += stringContents(spelling);
            continue;
        }

        if (token.isStringLiteral()) {
            result.append(stringRunPrefix + stringContents(spelling));
            continue;
        }

        QString entry = QString::fromLatin1(Token::name(token.kind()));
        if (token.is(T_IDENTIFIER) || token.isLiteral())
            entry += ' ' + spelling;
        result.append(entry);
    }
    return result;
}

QString firstDifference(const QStringList &expected, const QStringList &actual)
{
    const int count = qMin(expected.size(), actual.size());
    for (int i = 0; i < count; ++i) {
        if (expected.at(i) != actual.at(i)) {
            return QString("token %1:\n  built-in:      %2\n  cxx-frontend:  %3")
                .arg(i).arg(expected.at(i), actual.at(i));
        }
    }
    if (expected.size() != actual.size()) {
        return QString("token count: built-in %1, cxx-frontend %2\n  first extra: %3")
            .arg(expected.size()).arg(actual.size())
            .arg(expected.size() > actual.size() ? expected.at(count) : actual.at(count));
    }
    return {};
}

} // namespace

class tst_cxxfrontendpp : public QObject
{
    Q_OBJECT

private slots:
    void snippets_data();
    void snippets();

    void corpus_data();
    void corpus();

    void reportsMacroDefinitions();
    void reportsMacroUses();
    void reportsSkippedRegions();
    void reportsUndefinedMacroUses();

    void preprocessorGaps();

private:
    static QStringList builtIn(const QString &source);
    static QStringList cxxFrontend(const QString &source);
};

QStringList tst_cxxfrontendpp::builtIn(const QString &source)
{
    Environment environment;
    SilentClient client;
    Preprocessor preprocess(&client, &environment);
    const QByteArray output = preprocess.run(Utils::FilePath::fromString("<stdin>"),
                                             source.toUtf8(),
                                             /*noLines=*/true,
                                             /*markGeneratedTokens=*/false);
    return tokenStream(QString::fromUtf8(output));
}

QStringList tst_cxxfrontendpp::cxxFrontend(const QString &source)
{
    return tokenStream(CxxFrontendPreprocessor().run(source, "<stdin>"));
}

void tst_cxxfrontendpp::snippets_data()
{
    QTest::addColumn<QString>("source");

    QTest::newRow("object macro") << "#define A 1\nint x = A;\n";
    QTest::newRow("function macro") << "#define F(a, b) a + b\nint x = F(1, 2);\n";
    QTest::newRow("stringize") << "#define S(x) #x\nconst char *s = S(hello);\n";
    QTest::newRow("concat") << "#define C(a, b) a ## b\nint ab = 1; int x = C(a, b);\n";
    QTest::newRow("varargs") << "#define V(...) f(__VA_ARGS__)\nV(1, 2, 3);\n";
    QTest::newRow("nested") << "#define A B\n#define B 42\nint x = A;\n";
    QTest::newRow("undef") << "#define A 1\n#undef A\nint A;\n";
    QTest::newRow("if taken") << "#define X 1\n#if X\nint a;\n#else\nint b;\n#endif\n";
    QTest::newRow("if not taken") << "#if 0\nint a;\n#else\nint b;\n#endif\n";
    QTest::newRow("ifdef") << "#define A\n#ifdef A\nint a;\n#endif\n";
    QTest::newRow("defined") << "#define A\n#if defined(A)\nint a;\n#endif\n";
    QTest::newRow("elif") << "#if 0\nint a;\n#elif 1\nint b;\n#endif\n";
    QTest::newRow("nested if") << "#if 1\n#if 0\nint a;\n#endif\nint b;\n#endif\n";
    QTest::newRow("no macros at all") << "int main() { return 0; }\n";
    QTest::newRow("empty macro") << "#define E\nint E x;\n";
    QTest::newRow("recursive macro") << "#define A A\nint A;\n";
    QTest::newRow("macro in string") << "#define A 1\nconst char *s = \"A\";\n";
    QTest::newRow("pragma once") << "#pragma once\nint x;\n";
    QTest::newRow("unknown directive") << "#foobar\nint x;\n";
    QTest::newRow("adjacent string literals") << "const char *s = \"a\" \"b\";\n";
    QTest::newRow("adjacent literals over lines") << "const char *s =\n \"a\"\n \"b\";\n";
    QTest::newRow("adjacent literals from a macro")
        << "#define A \"a\"\nconst char *s = A \"b\";\n";
}

void tst_cxxfrontendpp::snippets()
{
    QFETCH(QString, source);

    const QString difference = firstDifference(builtIn(source), cxxFrontend(source));
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
}

void tst_cxxfrontendpp::corpus_data()
{
    QTest::addColumn<QString>("filePath");

    const char *subdirs[] = {"c99/data", "cxx11/data"};
    for (const char *subdir : subdirs) {
        const QDir dir(corpusRoot() + '/' + subdir);
        const QFileInfoList entries
            = dir.entryInfoList({"*.c", "*.cpp"}, QDir::Files, QDir::Name);
        QVERIFY2(!entries.isEmpty(), qPrintable(dir.path()));
        for (const QFileInfo &entry : entries)
            QTest::newRow(qPrintable(entry.fileName())) << entry.filePath();
    }
}

void tst_cxxfrontendpp::corpus()
{
    QFETCH(QString, filePath);

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString source = QString::fromUtf8(file.readAll());

    const QString difference = firstDifference(builtIn(source), cxxFrontend(source));
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
}

// The commentary. Each of these is a Client callback that had nothing to feed
// it before the delegate.

void tst_cxxfrontendpp::reportsMacroDefinitions()
{
    CxxFrontendPreprocessor preprocessor;
    preprocessor.run("#define ANSWER 42\n#define ADD(a, b) a + b\n", "<stdin>");

    const auto &defined = preprocessor.report().definedMacros;
    QCOMPARE(defined.size(), 2);
    QCOMPARE(defined.at(0).name, QString("ANSWER"));
    QCOMPARE(defined.at(0).body, QString("42"));
    QVERIFY(!defined.at(0).isFunctionLike);
    QCOMPARE(defined.at(1).name, QString("ADD"));
    QVERIFY(defined.at(1).isFunctionLike);
    QCOMPARE(defined.at(1).parameters, QStringList({"a", "b"}));
}

void tst_cxxfrontendpp::reportsMacroUses()
{
    const QString source = "#define ADD(a, b) a + b\nint x = ADD(1, y);\n";

    CxxFrontendPreprocessor preprocessor;
    preprocessor.run(source, "<stdin>");

    const auto &uses = preprocessor.report().macroUses;
    QCOMPARE(uses.size(), 1);
    QCOMPARE(uses.at(0).name, QString("ADD"));
    QVERIFY(uses.at(0).expanded);

    // The offsets have to point back at the source that was handed in, since
    // that is what an editor will highlight.
    const CxxFrontendPreprocessor::Range &range = uses.at(0).range;
    QCOMPARE(source.mid(range.offset, range.length), QString("ADD"));

    QCOMPARE(uses.at(0).arguments.size(), 2);
    QCOMPARE(source.mid(uses.at(0).arguments.at(0).offset,
                        uses.at(0).arguments.at(0).length),
             QString("1"));
    QCOMPARE(source.mid(uses.at(0).arguments.at(1).offset,
                        uses.at(0).arguments.at(1).length),
             QString("y"));
}

void tst_cxxfrontendpp::reportsSkippedRegions()
{
    const QString source = "#if 0\nint skipped;\n#endif\nint kept;\n";

    CxxFrontendPreprocessor preprocessor;
    preprocessor.run(source, "<stdin>");

    const auto &skipped = preprocessor.report().skippedRegions;
    QCOMPARE(skipped.size(), 1);
    QCOMPARE(source.mid(skipped.at(0).offset, skipped.at(0).length),
             QString("\nint skipped;\n"));
}

void tst_cxxfrontendpp::reportsUndefinedMacroUses()
{
    CxxFrontendPreprocessor preprocessor;
    preprocessor.run("#ifdef NOT_A_MACRO\n#endif\n", "<stdin>");

    QCOMPARE(preprocessor.report().undefinedMacroUses, QStringList("NOT_A_MACRO"));
    QVERIFY(preprocessor.report().macroUses.isEmpty());
}

// What is still missing, asserted so that the list cannot quietly go stale.
// The three that arrived are asserted too, so that a snapshot that loses them
// again is noticed.
void tst_cxxfrontendpp::preprocessorGaps()
{
    const CxxFrontendPreprocessor::Gaps gaps = CxxFrontendPreprocessor::gaps();

    QVERIFY(gaps.reportsMacroUses);
    QVERIFY(gaps.reportsSkippedBlocks);
    QVERIFY(gaps.reportsIncludeGuards);

    QVERIFY2(!gaps.marksExpandedTokens,
             "cxx::Token says which tokens a macro produced now: give Document the "
             "expanded and generated flags it wants, and drop this");
}

QTEST_GUILESS_MAIN(tst_cxxfrontendpp)

#include "tst_cxxfrontendpp.moc"
