// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// Measures how the vendored cxx-frontend parser copes with the corpora the
// built-in front end is tested against, so that the gap between the two is a
// number that moves when the snapshot in src/libs/3rdparty/cxx-frontend is
// refreshed, rather than something anyone has to go and re-measure by hand.
//
// The corpora are the data directories of tests/auto/cplusplus/{c99,cxx11}.
// They are shared on purpose: the same input has to keep parsing whichever
// front end Qt Creator ends up using.
//
// Every file that cxx-frontend does not parse cleanly is listed in
// knownFailure() with the reason. The list is a ratchet in both directions --
// a new failure fails the test, and so does a listed file that starts passing,
// because that means the entry is stale and should go.

#include <cxx/control.h>
#include <cxx/diagnostics_client.h>
#include <cxx/memory_layout.h>
#include <cxx/preprocessor.h>
#include <cxx/translation_unit.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>

//TESTED_COMPONENT=src/libs/3rdparty/cxx-frontend

namespace {

class DiagnosticsCollector final : public cxx::DiagnosticsClient
{
public:
    QStringList errors;

    void report(const cxx::Diagnostic &diagnostic) override
    {
        if (diagnostic.severity() == cxx::Severity::Warning)
            return;

        QString location;
        if (cxx::Preprocessor *pp = preprocessor()) {
            const cxx::SourcePosition pos = pp->tokenStartPosition(diagnostic.token());
            location = QString("%1:%2: ").arg(pos.line).arg(pos.column);
        }
        errors.append(location + QString::fromStdString(diagnostic.message()));
    }
};

// Refuses every #include. The corpora are standalone snippets: the built-in
// front end parses them without headers too, and resolving against whatever
// the host happens to have installed would make the result unreproducible.
struct NoIncludes
{
    bool done = false;

    explicit operator bool() const { return !done; }

    void operator()(const cxx::ProcessingComplete &) { done = true; }
    void operator()(const cxx::CanContinuePreprocessing &) {}
    void operator()(const cxx::PendingInclude &state) { state.resolveWith(std::nullopt); }
    void operator()(const cxx::PendingHasIncludes &state)
    {
        for (const auto &request : state.requests)
            request.setExists(false);
    }
    void operator()(const cxx::PendingFileContent &state) { state.setContent(std::nullopt); }
    void operator()(const cxx::EnteringFile &) {}
    void operator()(const cxx::LeavingFile &) {}
};

// The reason a corpus file does not parse, or nullptr if it is expected to.
// Measured against upstream f78ee7e6ff899242255ebbb8f1123113e1f8ffa3; revisit
// whenever the snapshot moves.
const char *knownFailure(const QString &fileName)
{
    // Ill-formed: an unnamed opaque enum, and enumerators redeclared in the
    // same scope. The built-in front end accepts both; cxx-frontend does not,
    // and is right to reject them.
    if (fileName == "enums.1.cpp")
        return "cxx-frontend: rejects the ill-formed declarations this file also contains";

    // cxx-frontend resolves names while it parses, so a snippet that names
    // std:: type traits it never declares does not get far. The built-in front
    // end is purely syntactic here and does not care.
    if (fileName == "binaryExprAsTemplateArg.cpp" || fileName == "braceInitializers.3.cpp"
        || fileName == "concepts.2.cpp") {
        return "cxx-frontend: needs the declarations of the std traits the snippet uses";
    }

    return nullptr;
}

QString corpusRoot()
{
    return QString(SRCDIR "/../cplusplus");
}

} // namespace

class tst_cxxfrontend : public QObject
{
    Q_OBJECT

private slots:
    void parseCorpus_data();
    void parseCorpus();

    void characterLiteralSuffix();
    void attributeOnLabeledStatement();

private:
    // Both return the errors reported for the snippet.
    static QStringList parse(const QByteArray &source,
                             const QString &fileName,
                             cxx::LanguageKind language);
    static QStringList parseFile(const QString &filePath, cxx::LanguageKind language);
};

QStringList tst_cxxfrontend::parseFile(const QString &filePath, cxx::LanguageKind language)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {QString("cannot read %1").arg(filePath)};
    return parse(file.readAll(), filePath, language);
}

QStringList tst_cxxfrontend::parse(const QByteArray &source,
                                   const QString &fileName,
                                   cxx::LanguageKind language)
{
    DiagnosticsCollector diagnostics;
    diagnostics.setErrorLimit(0);
    cxx::TranslationUnit unit(&diagnostics);

    // A fixed 64-bit layout rather than a host toolchain, so that the outcome
    // does not depend on the machine the test runs on.
    cxx::MemoryLayout memoryLayout(64);
    unit.control()->setMemoryLayout(&memoryLayout);

    cxx::Preprocessor *pp = unit.preprocessor();
    pp->setLanguage(language);
    pp->setCanResolveFiles(false);

    unit.beginPreprocessing(source.toStdString(), fileName.toStdString());
    NoIncludes state;
    while (state)
        std::visit(state, unit.continuePreprocessing());
    unit.endPreprocessing();

    unit.parse({.checkTypes = false});

    return diagnostics.errors;
}

void tst_cxxfrontend::parseCorpus_data()
{
    QTest::addColumn<QString>("filePath");
    QTest::addColumn<cxx::LanguageKind>("language");

    const struct {
        const char *subdir;
        const char *pattern;
        cxx::LanguageKind language;
    } corpora[] = {
        {"c99/data", "*.c", cxx::LanguageKind::kC},
        {"cxx11/data", "*.cpp", cxx::LanguageKind::kCXX},
    };

    for (const auto &corpus : corpora) {
        const QDir dir(corpusRoot() + '/' + corpus.subdir);
        const QFileInfoList entries
            = dir.entryInfoList({corpus.pattern}, QDir::Files, QDir::Name);
        QVERIFY2(!entries.isEmpty(),
                 qPrintable(QString("no corpus files in %1").arg(dir.path())));
        for (const QFileInfo &entry : entries)
            QTest::newRow(qPrintable(entry.fileName())) << entry.filePath() << corpus.language;
    }
}

void tst_cxxfrontend::parseCorpus()
{
    QFETCH(QString, filePath);
    QFETCH(cxx::LanguageKind, language);

    const QStringList errors = parseFile(filePath, language);

    if (const char *reason = knownFailure(QFileInfo(filePath).fileName()))
        QEXPECT_FAIL("", reason, Abort);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join("\n")));
}

// The two gaps the first corpus run turned up, fixed upstream since and kept
// here so that a snapshot refresh that loses them again is noticed.

void tst_cxxfrontend::characterLiteralSuffix()
{
    const QStringList errors = parse("int operator\"\"_X(char);\nint a = 'c'_X;\n",
                                     "characterLiteralSuffix.cpp",
                                     cxx::LanguageKind::kCXX);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join("\n")));
}

void tst_cxxfrontend::attributeOnLabeledStatement()
{
    const QStringList errors = parse("void f(int j) { switch (j) { [[likely]] case 1: break; } }\n",
                                     "attributeOnLabeledStatement.cpp",
                                     cxx::LanguageKind::kCXX);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join("\n")));
}

QTEST_GUILESS_MAIN(tst_cxxfrontend)

#include "tst_cxxfrontend.moc"
