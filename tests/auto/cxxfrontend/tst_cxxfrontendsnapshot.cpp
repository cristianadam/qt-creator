// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// One translation unit per file, the way Qt Creator holds a code model.
//
// A compiler reads a file and everything it includes as one translation unit.
// Qt Creator cannot: a header is included by hundreds of files, and parsing it
// once per includer would be too slow to type against. So each file gets a
// document of its own, and what crosses from a header to its includer is the
// macros it established, not its text. Snapshot is that collection.
//
// What is asserted here is that arrangement, since that is what the built-in
// model guarantees and what everything above it relies on.

#include <cplusplus/CxxFrontendSnapshot.h>

#include <QObject>
#include <QTest>

//TESTED_COMPONENT=src/libs/cplusplus

using namespace CPlusPlus;

namespace {

// A set of files in memory, standing in for the file system.
class Files
{
public:
    void add(const QString &path, const QString &source) { m_files.insert(path, source); }

    CxxFrontendSnapshot::HeaderResolver resolver() const
    {
        return [this](const QString &name, bool, const QString &)
                   -> std::optional<CxxFrontendSnapshot::Header> {
            const auto it = m_files.constFind(name);
            if (it == m_files.cend())
                return std::nullopt;
            return CxxFrontendSnapshot::Header{name, *it};
        };
    }

private:
    QHash<QString, QString> m_files;
};

QStringList symbolNames(const CxxFrontendDocument *document)
{
    QStringList result;
    for (const CxxFrontendDocument::Symbol &symbol : document->symbols())
        result.append(symbol.name);
    return result;
}

} // namespace

class tst_cxxfrontendsnapshot : public QObject
{
    Q_OBJECT

private slots:
    void aHeaderGetsItsOwnDocument();
    void aHeaderIsNotTakenIntoItsIncluder();
    void aMacroCrossesFromAHeader();
    void aMacroCrossesTwoHeadersDeep();
    void anUndefInAHeaderCrossesToo();
    void aHeaderIsProcessedOnce();
    void aCycleTerminates();
    void anUnresolvedIncludeIsNotAnError();
    void reportsWhatAFileIncludes();
    void predefinedMacrosReachEveryFile();

    void aHeaderSeesTheMacrosOfItsIncluder();
    void aHeaderIsNotReusedUnderADifferentEnvironment();
    void aHeaderIsReusedWhenTheEnvironmentAgrees();
    void anUnrelatedMacroDoesNotForceAReparse();
    void aHeaderThatAsksAboutNothingIsAlwaysReused();

    void aNameDeclaredInAHeaderResolvesFromTheSource();
    void aNameDeclaredTwoHeadersAwayResolves();
    void aNameThatIsNowhereResolvesToNothing();
    void aLocalNameStillWinsOverAHeader();
    void aQualifiedNameFromAHeaderResolves();
    void aNestedQualifiedNameResolves();
    void theWrongQualifierResolvesToNothing();
    void anUnqualifiedUseDoesNotReachIntoANamespace();
    void aMemberOfABaseInAHeaderResolves();
    void aMemberOfAnUnrelatedClassDoesNotResolve();
    void unsupportedLookups();
};

void tst_cxxfrontendsnapshot::aHeaderGetsItsOwnDocument()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nint fromSource;\n");

    QCOMPARE(snapshot.files(), QStringList({"a.cpp", "h.h"}));
    QVERIFY(snapshot.document("h.h"));
    QCOMPARE(symbolNames(snapshot.document("h.h")), QStringList("fromHeader"));
}

void tst_cxxfrontendsnapshot::aHeaderIsNotTakenIntoItsIncluder()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\nint fromSource;\n");

    // The whole point of a document per file: what the header declares is in
    // the header's document and nowhere else.
    QCOMPARE(symbolNames(document), QStringList("fromSource"));
}

void tst_cxxfrontendsnapshot::aMacroCrossesFromAHeader()
{
    Files files;
    files.add("h.h", "#define ANSWER 42\n#define ADD(a, b) a + b\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp",
                           "#include \"h.h\"\n"
                           "#ifndef ANSWER\n#error no\n#endif\n"
                           "int x = ANSWER;\nint y = ADD(1, 2);\n");

    // Both macros were in force, so neither the #error nor a parse failure.
    QVERIFY2(document->diagnostics().isEmpty(),
             qPrintable(document->diagnostics().isEmpty()
                            ? QString()
                            : document->diagnostics().first().text));
    QCOMPARE(symbolNames(document), QStringList({"x", "y"}));
}

void tst_cxxfrontendsnapshot::aMacroCrossesTwoHeadersDeep()
{
    Files files;
    files.add("inner.h", "#define DEEP 1\n");
    files.add("outer.h", "#include \"inner.h\"\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"outer.h\"\n#ifndef DEEP\n#error no\n#endif\nint x;\n");

    QVERIFY(document->diagnostics().isEmpty());
}

void tst_cxxfrontendsnapshot::anUndefInAHeaderCrossesToo()
{
    Files files;
    files.add("h.h", "#define GONE 1\n#undef GONE\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\n#ifdef GONE\n#error no\n#endif\nint x;\n");

    QVERIFY(document->diagnostics().isEmpty());
}

void tst_cxxfrontendsnapshot::aHeaderIsProcessedOnce()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\n#include \"h.h\"\nint x;\n");

    // Including it twice, and from two files, still leaves one document.
    snapshot.process("b.cpp", "#include \"h.h\"\nint y;\n");
    QCOMPARE(snapshot.files(), QStringList({"a.cpp", "b.cpp", "h.h"}));
}

void tst_cxxfrontendsnapshot::aCycleTerminates()
{
    Files files;
    files.add("a.h", "#include \"b.h\"\nint fromA;\n");
    files.add("b.h", "#include \"a.h\"\nint fromB;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("main.cpp", "#include \"a.h\"\nint x;\n");

    QCOMPARE(snapshot.files(), QStringList({"a.h", "b.h", "main.cpp"}));
}

void tst_cxxfrontendsnapshot::anUnresolvedIncludeIsNotAnError()
{
    CxxFrontendSnapshot snapshot;
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include <missing>\nint x;\n");

    // Qt Creator parses files whose includes are not all there, all the time.
    QCOMPARE(symbolNames(document), QStringList("x"));
    QCOMPARE(snapshot.files(), QStringList("a.cpp"));
}

void tst_cxxfrontendsnapshot::reportsWhatAFileIncludes()
{
    Files files;
    files.add("inner.h", "int i;\n");
    files.add("outer.h", "#include \"inner.h\"\nint o;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"outer.h\"\nint x;\n");

    QCOMPARE(snapshot.allIncludesFor("a.cpp"), QStringList({"inner.h", "outer.h"}));
    QCOMPARE(snapshot.allIncludesFor("outer.h"), QStringList("inner.h"));
    QCOMPARE(snapshot.allIncludesFor("inner.h"), QStringList());
}

void tst_cxxfrontendsnapshot::predefinedMacrosReachEveryFile()
{
    Files files;
    files.add("h.h", "#ifndef FROM_PROJECT\n#error no\n#endif\nint fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.setPredefinedMacros({"FROM_PROJECT 1"});
    snapshot.process("a.cpp", "#include \"h.h\"\nint x;\n");

    QVERIFY(snapshot.document("h.h"));
    QVERIFY(snapshot.document("h.h")->diagnostics().isEmpty());
}

// A header is preprocessed where it is included, so what the includer has
// defined by that point is in force inside it. Getting this wrong does not
// fail loudly: it gives the wrong half of an #ifdef, silently.
void tst_cxxfrontendsnapshot::aHeaderSeesTheMacrosOfItsIncluder()
{
    Files files;
    files.add("h.h", "#ifdef FEATURE\nint withFeature;\n#else\nint withoutFeature;\n#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#define FEATURE 1\n#include \"h.h\"\n");

    QCOMPARE(symbolNames(snapshot.document("h.h")), QStringList("withFeature"));
}

// And two includers can disagree about it, so a document parsed for one of
// them cannot simply be handed to the other.
void tst_cxxfrontendsnapshot::aHeaderIsNotReusedUnderADifferentEnvironment()
{
    Files files;
    files.add("h.h", "#ifdef FEATURE\nint withFeature;\n#else\nint withoutFeature;\n#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());

    snapshot.process("with.cpp", "#define FEATURE 1\n#include \"h.h\"\n");
    QCOMPARE(symbolNames(snapshot.document("h.h")), QStringList("withFeature"));

    snapshot.process("without.cpp", "#include \"h.h\"\n");
    QCOMPARE(symbolNames(snapshot.document("h.h")), QStringList("withoutFeature"));
}

// Reparsing whenever anything differs would be correct and useless: a header
// is included by hundreds of files and reparsing it for each is what the
// document per file exists to avoid. So the other half of the rule matters as
// much as the first -- a document survives an environment it does not care
// about. A document that survived is the same document, so the pointer says
// whether it did.

void tst_cxxfrontendsnapshot::aHeaderIsReusedWhenTheEnvironmentAgrees()
{
    Files files;
    files.add("h.h", "#ifdef FEATURE\nint a;\n#else\nint b;\n#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());

    snapshot.process("one.cpp", "#define FEATURE 1\n#include \"h.h\"\n");
    const CxxFrontendDocument *first = snapshot.document("h.h");

    snapshot.process("two.cpp", "#define FEATURE 1\n#include \"h.h\"\n");
    QCOMPARE(snapshot.document("h.h"), first);
}

void tst_cxxfrontendsnapshot::anUnrelatedMacroDoesNotForceAReparse()
{
    Files files;
    files.add("h.h", "#ifdef FEATURE\nint a;\n#else\nint b;\n#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());

    snapshot.process("one.cpp", "#include \"h.h\"\n");
    const CxxFrontendDocument *first = snapshot.document("h.h");

    // The header never asks about SOMETHING_ELSE, so it cannot read
    // differently because of it.
    snapshot.process("two.cpp", "#define SOMETHING_ELSE 1\n#include \"h.h\"\n");
    QCOMPARE(snapshot.document("h.h"), first);
}

void tst_cxxfrontendsnapshot::aHeaderThatAsksAboutNothingIsAlwaysReused()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());

    snapshot.process("one.cpp", "#define A 1\n#include \"h.h\"\n");
    const CxxFrontendDocument *first = snapshot.document("h.h");

    snapshot.process("two.cpp", "#define B 2\n#include \"h.h\"\n");
    QCOMPARE(snapshot.document("h.h"), first);
}

// The point of the whole arrangement, and the thing the per-file model makes
// hard: code uses what its headers declare, and a header's declarations are
// not in the includer's translation unit at all. The document cannot answer
// this and does not pretend to; the snapshot has to.
void tst_cxxfrontendsnapshot::aNameDeclaredInAHeaderResolvesFromTheSource()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { fromHeader = 1; }\n");

    QVERIFY2(!document->declarationAt(2, 12).isValid(),
             "the document answered for a name it cannot see");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 12);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("fromHeader"));
    QCOMPARE(found.filePath, QString("h.h"));
    QCOMPARE(found.line, 1);
}

void tst_cxxfrontendsnapshot::aNameDeclaredTwoHeadersAwayResolves()
{
    Files files;
    files.add("inner.h", "int deep;\n");
    files.add("outer.h", "#include \"inner.h\"\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"outer.h\"\nvoid f() { deep = 1; }\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 12);
    QVERIFY(found.isValid());
    QCOMPARE(found.filePath, QString("inner.h"));
}

void tst_cxxfrontendsnapshot::aNameThatIsNowhereResolvesToNothing()
{
    Files files;
    files.add("h.h", "int something;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { nowhere = 1; }\n");

    QVERIFY(!snapshot.declarationAt("a.cpp", 2, 12).isValid());
}

void tst_cxxfrontendsnapshot::aLocalNameStillWinsOverAHeader()
{
    Files files;
    files.add("h.h", "int both;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nint both;\nvoid f() { both = 1; }\n");

    // The file's own declaration is the one the parser resolved, and it is
    // the one that is right.
    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 3, 12);
    QVERIFY(found.isValid());
    QCOMPARE(found.filePath, QString("a.cpp"));
    QCOMPARE(found.line, 2);
}

// N::x, where N is in a header. The name written says which scope it means,
// which is the one thing about the surrounding scopes this search can be sure
// of without applying the language's rules.
void tst_cxxfrontendsnapshot::aQualifiedNameFromAHeaderResolves()
{
    Files files;
    files.add("h.h", "namespace N { int v; }\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { N::v = 1; }\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 15);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("v"));
    QCOMPARE(found.filePath, QString("h.h"));
}

void tst_cxxfrontendsnapshot::aNestedQualifiedNameResolves()
{
    Files files;
    files.add("h.h", "namespace A { namespace B { int v; } }\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { A::B::v = 1; }\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 18);
    QVERIFY(found.isValid());
    QCOMPARE(found.filePath, QString("h.h"));
}

void tst_cxxfrontendsnapshot::theWrongQualifierResolvesToNothing()
{
    Files files;
    files.add("h.h", "namespace N { int v; }\nnamespace M { }\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { M::v = 1; }\n");

    // There is a v in the header, and it is not this one.
    QVERIFY(!snapshot.declarationAt("a.cpp", 2, 15).isValid());
}

void tst_cxxfrontendsnapshot::anUnqualifiedUseDoesNotReachIntoANamespace()
{
    Files files;
    files.add("h.h", "namespace N { int v; }\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { v = 1; }\n");

    // Reaching inside N without saying so takes a using directive, which is
    // a rule this search does not have. Saying nothing is the right answer.
    QVERIFY(!snapshot.declarationAt("a.cpp", 2, 12).isValid());
}

// A class in one file, its base in another: the member is declared nowhere
// this file can see, and the class names the base even so.
void tst_cxxfrontendsnapshot::aMemberOfABaseInAHeaderResolves()
{
    Files files;
    files.add("b.h", "struct B { int m; };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nstruct D : B { void f() { m = 1; } };\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 26);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("B::m"));
    QCOMPARE(found.filePath, QString("b.h"));
}

void tst_cxxfrontendsnapshot::aMemberOfAnUnrelatedClassDoesNotResolve()
{
    Files files;
    files.add("b.h", "struct B { int m; };\nstruct Other { int n; };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nstruct D : B { void f() { n = 1; } };\n");

    // n belongs to a class D does not derive from.
    QVERIFY(!snapshot.declarationAt("a.cpp", 2, 26).isValid());
}

// What this lookup does not do. Each is a rule about which declaration a name
// means, and answering one of them wrongly is worse than saying nothing, so
// they are written down rather than approximated.
void tst_cxxfrontendsnapshot::unsupportedLookups()
{
    const QStringList unsupported = CxxFrontendSnapshot::unsupportedLookups();
    QVERIFY(unsupported.contains("indirect bases across files"));
    QVERIFY(unsupported.contains("overload resolution across files"));
    QVERIFY(unsupported.contains("using across files"));

    // Every one of them is about crossing a file. Inside a file the parser
    // has already applied the rule, and tst_cxxfrontenddocument says so.
    for (const QString &entry : unsupported) {
        QVERIFY2(entry.contains("across") || entry.contains("between"),
                 qPrintable("not a cross-file limit: " + entry));
    }
}

QTEST_GUILESS_MAIN(tst_cxxfrontendsnapshot)

#include "tst_cxxfrontendsnapshot.moc"
