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

// Where the usages are, in one line each, so that a wrong answer says which
// place it got wrong rather than only how many there were.
QStringList placesOf(const QList<CxxFrontendSnapshot::Usage> &usages)
{
    QStringList result;
    for (const CxxFrontendSnapshot::Usage &usage : usages) {
        result.append(QString("%1:%2:%3%4")
                          .arg(usage.filePath)
                          .arg(usage.line)
                          .arg(usage.column)
                          .arg(usage.isDeclaration ? QString(" (declaration)") : QString()));
    }
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

    void aGuardedHeaderIncludedTwiceKeepsWhatItDeclares();
    void aGuardedHeaderReachedTwoWaysKeepsWhatItDeclares();
    void aGuardIsNotAnExcuseToIgnoreOtherMacros();

    void aNameDeclaredInAHeaderResolvesFromTheSource();
    void aCursorAnywhereInANameResolves();
    void aNameDeclaredTwoHeadersAwayResolves();
    void aNameThatIsNowhereResolvesToNothing();
    void aLocalNameStillWinsOverAHeader();
    void aQualifiedNameFromAHeaderResolves();
    void aNestedQualifiedNameResolves();
    void theWrongQualifierResolvesToNothing();
    void anUnqualifiedUseDoesNotReachIntoANamespace();
    void aMemberOfABaseInAHeaderResolves();
    void aMemberOfAnUnrelatedClassDoesNotResolve();
    void aMemberOfAnIndirectBaseInAHeaderResolves();
    void aUsingDeclarationInsideAHeaderIsHonoured();
    void aNameInANestedNamespaceInAHeaderResolves();
    void aBaseChainAcrossThreeFilesResolves();
    void aBaseChainAcrossFourFilesResolves();
    void aCycleInTheBasesTerminates();

    void aUsageInTheSameFileIsFound();
    void usagesCarryTheFunctionTheyAreIn();
    void usagesOfSomethingInAHeaderReachItsIncluder();
    void theDeclarationIsAPlaceToSearchFrom();
    void aNameThatMeansSomethingElseIsNotAUsage();
    void aFileThatDoesNotIncludeTheDeclarationIsNotSearched();
    void aQualifiedUsageIsFound();
    void aDefinitionApartFromItsDeclarationIsAUsage();
    void aUsageThroughABaseIsFound();
    void aUsageFromAMacroArgumentIsReportedOnce();
    void aUsageFromAMacroBodyIsNotReported();
    void anUnqualifiedRedeclarationIsNotReported();
    void aPositionThatNamesNothingHasNoUsages();
    void aMemberNamedThroughAnObjectAcrossFilesIsNotFound();

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

// Every header guards itself, so the reuse rule has to get this right or it
// gets nothing right. A guard reads a macro and then defines it, which looks
// exactly like a dependency on the includer -- and the second time round the
// header does read differently: it reads nothing at all. Reparsing it then
// replaces the document with an empty one and the header's declarations are
// gone.
void tst_cxxfrontendsnapshot::aGuardedHeaderIncludedTwiceKeepsWhatItDeclares()
{
    Files files;
    files.add("h.h", "#ifndef H_H\n#define H_H\nint fromHeader;\n#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\n#include \"h.h\"\nint x;\n");

    QCOMPARE(symbolNames(snapshot.document("h.h")), QStringList("fromHeader"));
}

// And the way it really happens: included once directly and once through
// another header, which is what every file in a project of any size does.
void tst_cxxfrontendsnapshot::aGuardedHeaderReachedTwoWaysKeepsWhatItDeclares()
{
    Files files;
    files.add("inner.h", "#ifndef INNER_H\n#define INNER_H\nint fromInner;\n#endif\n");
    files.add("outer.h", "#ifndef OUTER_H\n#define OUTER_H\n#include \"inner.h\"\n#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp",
                     "#include \"inner.h\"\n#include \"outer.h\"\n"
                     "void f() { fromInner = 1; }\n");

    QCOMPARE(symbolNames(snapshot.document("inner.h")), QStringList("fromInner"));
    QCOMPARE(snapshot.declarationAt("a.cpp", 3, 12).filePath, QString("inner.h"));
}

// The exemption is for the guard and nothing else: a header that branches on
// a macro of its own accord still depends on it.
void tst_cxxfrontendsnapshot::aGuardIsNotAnExcuseToIgnoreOtherMacros()
{
    Files files;
    files.add("h.h",
              "#ifndef H_H\n#define H_H\n"
              "#ifdef FEATURE\nint withFeature;\n#else\nint withoutFeature;\n#endif\n"
              "#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());

    snapshot.process("with.cpp", "#define FEATURE 1\n#include \"h.h\"\n");
    QCOMPARE(symbolNames(snapshot.document("h.h")), QStringList("withFeature"));

    snapshot.process("without.cpp", "#include \"h.h\"\n");
    QCOMPARE(symbolNames(snapshot.document("h.h")), QStringList("withoutFeature"));
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

// Where a cursor actually is. Every test above points at the first character
// of a name, and no editor does: someone following a name has the cursor
// somewhere in the middle of it, and the ones that normalise first put it at
// the end of the word.
void tst_cxxfrontendsnapshot::aCursorAnywhereInANameResolves()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { fromHeader = 1; }\n");

    // fromHeader runs from column 12 to column 21, so 22 is where a cursor
    // moved to the end of the word sits.
    for (const int column : {12, 16, 21, 22}) {
        const CxxFrontendDocument::Declaration found
            = snapshot.declarationAt("a.cpp", 2, column);
        QVERIFY2(found.isValid(), qPrintable(QString("column %1").arg(column)));
        QCOMPARE(found.filePath, QString("h.h"));
    }

    // And a position that is on nothing still means nothing.
    QVERIFY(!snapshot.declarationAt("a.cpp", 2, 23).isValid());
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
    QCOMPARE(found.name, QString("N::v"));
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

// Going through each document's own lookup means the rules that hold inside
// a header hold across the boundary too, without any of them being written
// out a second time here.
void tst_cxxfrontendsnapshot::aMemberOfAnIndirectBaseInAHeaderResolves()
{
    Files files;
    files.add("b.h", "struct A { int m; };\nstruct B : A {};\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nstruct D : B { void f() { m = 1; } };\n");

    // D names B, and B's own lookup reaches A.
    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 26);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("A::m"));
    QCOMPARE(found.line, 1);
}

void tst_cxxfrontendsnapshot::aUsingDeclarationInsideAHeaderIsHonoured()
{
    Files files;
    files.add("b.h", "struct A { int m; };\nstruct B : A { using A::m; };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nstruct D : B { void f() { m = 1; } };\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 26);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("A::m"));
}

void tst_cxxfrontendsnapshot::aNameInANestedNamespaceInAHeaderResolves()
{
    Files files;
    files.add("h.h", "namespace A { namespace B { struct S { int m; }; } }\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { A::B::S s; }\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 18);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("A::B::S"));
}

// A chain of bases running through several files. Each document follows the
// bases it can see; where one stops, the search picks the chain up and
// carries it into the file that declares the next.
void tst_cxxfrontendsnapshot::aBaseChainAcrossThreeFilesResolves()
{
    Files files;
    files.add("a.h", "struct A { int m; };\n");
    files.add("b.h", "#include \"a.h\"\nstruct B : A {};\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nstruct D : B { void f() { m = 1; } };\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 26);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("A::m"));
    QCOMPARE(found.filePath, QString("a.h"));
}

void tst_cxxfrontendsnapshot::aBaseChainAcrossFourFilesResolves()
{
    Files files;
    files.add("a.h", "struct A { int m; };\n");
    files.add("b.h", "#include \"a.h\"\nstruct B : A {};\n");
    files.add("c.h", "#include \"b.h\"\nstruct C : B {};\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"c.h\"\nstruct D : C { void f() { m = 1; } };\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 26);
    QVERIFY(found.isValid());
    QCOMPARE(found.filePath, QString("a.h"));
}

void tst_cxxfrontendsnapshot::aCycleInTheBasesTerminates()
{
    Files files;
    // Ill-formed, and it still must not hang.
    files.add("a.h", "struct B;\nstruct A : B {};\n");
    files.add("b.h", "#include \"a.h\"\nstruct B : A {};\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nstruct D : B { void f() { nowhere = 1; } };\n");

    QVERIFY(!snapshot.declarationAt("a.cpp", 2, 26).isValid());
}

// Find usages is the lookup asked backwards: instead of which declaration one
// name means, which names mean one declaration. So it is the same resolution
// run over every place a file writes the name, and it reaches exactly as far.
void tst_cxxfrontendsnapshot::aUsageInTheSameFileIsFound()
{
    CxxFrontendSnapshot snapshot;
    snapshot.process("a.cpp", "int x;\nvoid f() { x = 1; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("a.cpp", 2, 12)),
             QStringList({"a.cpp:1:5 (declaration)", "a.cpp:2:12"}));
}

void tst_cxxfrontendsnapshot::usagesCarryTheFunctionTheyAreIn()
{
    CxxFrontendSnapshot snapshot;
    snapshot.process("a.cpp", "int x;\nvoid f() { x = 1; }\n");

    const QList<CxxFrontendSnapshot::Usage> usages = snapshot.findUsages("a.cpp", 2, 12);
    QCOMPARE(usages.size(), 2);
    QCOMPARE(usages.first().containingFunction, QString());
    QCOMPARE(usages.last().containingFunction, QString("f"));
    QCOMPARE(usages.last().length, 1);
}

void tst_cxxfrontendsnapshot::usagesOfSomethingInAHeaderReachItsIncluder()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp",
                     "#include \"h.h\"\n"
                     "void f() { fromHeader = 1; }\n"
                     "void g() { fromHeader = 2; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("a.cpp", 2, 12)),
             QStringList({"a.cpp:2:12", "a.cpp:3:12", "h.h:1:5 (declaration)"}));
}

// The declaration is where anyone reading a header stands when they ask, and
// there is nothing there for the parser to resolve -- the name is not a use of
// something, it is where the something comes from.
void tst_cxxfrontendsnapshot::theDeclarationIsAPlaceToSearchFrom()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { fromHeader = 1; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("h.h", 1, 5)),
             QStringList({"a.cpp:2:12", "h.h:1:5 (declaration)"}));
}

// Matching on the spelling would report this, and it would be wrong: the name
// in a.cpp means a.cpp's own variable. Which is why every place is resolved
// and compared against the declaration being searched for.
void tst_cxxfrontendsnapshot::aNameThatMeansSomethingElseIsNotAUsage()
{
    Files files;
    files.add("h.h", "int both;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nint both;\nvoid f() { both = 1; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("h.h", 1, 5)),
             QStringList("h.h:1:5 (declaration)"));
    QCOMPARE(placesOf(snapshot.findUsages("a.cpp", 3, 12)),
             QStringList({"a.cpp:2:5 (declaration)", "a.cpp:3:12"}));
}

void tst_cxxfrontendsnapshot::aFileThatDoesNotIncludeTheDeclarationIsNotSearched()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("uses.cpp", "#include \"h.h\"\nvoid f() { fromHeader = 1; }\n");
    // Declares its own, and never includes the header.
    snapshot.process("other.cpp", "int fromHeader;\nvoid g() { fromHeader = 2; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("h.h", 1, 5)),
             QStringList({"h.h:1:5 (declaration)", "uses.cpp:2:12"}));
}

void tst_cxxfrontendsnapshot::aQualifiedUsageIsFound()
{
    Files files;
    files.add("h.h", "namespace N { int v; }\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { N::v = 1; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("h.h", 1, 19)),
             QStringList({"a.cpp:2:15", "h.h:1:19 (declaration)"}));
}

// Declared in a header, defined in a source file: the two are not in one
// translation unit here, and the definition names the class in front of it,
// which is what the search follows back to the declaration.
void tst_cxxfrontendsnapshot::aDefinitionApartFromItsDeclarationIsAUsage()
{
    Files files;
    files.add("b.h", "struct B { void f(); };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nvoid B::f() {}\n");

    QCOMPARE(placesOf(snapshot.findUsages("b.h", 1, 17)),
             QStringList({"a.cpp:2:9", "b.h:1:17 (declaration)"}));
}

void tst_cxxfrontendsnapshot::aUsageThroughABaseIsFound()
{
    Files files;
    files.add("b.h", "struct B { int m; };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nstruct D : B { void f() { m = 1; } };\n");

    QCOMPARE(placesOf(snapshot.findUsages("b.h", 1, 16)),
             QStringList({"a.cpp:2:27", "b.h:1:16 (declaration)"}));
}

// A name handed to a macro is written once and comes out of the expansion as
// many times as the macro repeats it. It is one place in the file, so it is
// one usage, at the place it was written rather than at the expansion.
void tst_cxxfrontendsnapshot::aUsageFromAMacroArgumentIsReportedOnce()
{
    CxxFrontendSnapshot snapshot;
    snapshot.process("a.cpp", "int x;\n#define TWICE(v) v + v\nint y = TWICE(x);\n");

    QCOMPARE(placesOf(snapshot.findUsages("a.cpp", 1, 5)),
             QStringList({"a.cpp:1:5 (declaration)", "a.cpp:3:15"}));
}

// The other half of the same rule, and the built-in model's: a name the macro
// body wrote is not in the text where the macro was used, so there is nothing
// there to report or to click on.
void tst_cxxfrontendsnapshot::aUsageFromAMacroBodyIsNotReported()
{
    CxxFrontendSnapshot snapshot;
    snapshot.process("a.cpp", "int x;\n#define USE x + 1\nint y = USE;\n");

    QCOMPARE(placesOf(snapshot.findUsages("a.cpp", 1, 5)),
             QStringList("a.cpp:1:5 (declaration)"));
}

// The price of that rule, and the reason it is written down. "extern int x;"
// looks exactly like a file's own variable, and saying that it is the header's
// x is a question about linkage, not about scopes. So the two are two things
// here: a search from either side finds that side's places, and neither
// reaches the other.
void tst_cxxfrontendsnapshot::anUnqualifiedRedeclarationIsNotReported()
{
    Files files;
    files.add("h.h", "int shared;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nextern int shared;\nvoid f() { shared = 1; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("h.h", 1, 5)),
             QStringList("h.h:1:5 (declaration)"));
    QCOMPARE(placesOf(snapshot.findUsages("a.cpp", 2, 12)),
             QStringList({"a.cpp:2:12 (declaration)", "a.cpp:3:12"}));
    QVERIFY(CxxFrontendSnapshot::unsupportedLookups()
                .contains("unqualified redeclarations across files"));
}

void tst_cxxfrontendsnapshot::aPositionThatNamesNothingHasNoUsages()
{
    CxxFrontendSnapshot snapshot;
    snapshot.process("a.cpp", "int x;\nvoid f() { x = 1; }\n");

    // On the type, which declares nothing and names nothing declared here.
    QVERIFY(snapshot.findUsages("a.cpp", 1, 1).isEmpty());
    QVERIFY(snapshot.findUsages("nowhere.cpp", 1, 1).isEmpty());
}

// The limit this shares with the lookup it is built on: reaching a member
// through an object needs the type of that object, and the type is declared in
// a file this one does not contain. Reporting only the declaration is the
// honest answer; reporting the member because it is spelled the same would be
// a wrong one.
void tst_cxxfrontendsnapshot::aMemberNamedThroughAnObjectAcrossFilesIsNotFound()
{
    Files files;
    files.add("b.h", "struct B { int m; };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nvoid f(B b) { b.m = 1; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("b.h", 1, 16)),
             QStringList("b.h:1:16 (declaration)"));
    QVERIFY(CxxFrontendSnapshot::unsupportedLookups()
                .contains("members named through an object across files"));
}

// What this lookup does not do. Each is a rule about which declaration a name
// means, and answering one of them wrongly is worse than saying nothing, so
// they are written down rather than approximated.
void tst_cxxfrontendsnapshot::unsupportedLookups()
{
    const QStringList unsupported = CxxFrontendSnapshot::unsupportedLookups();
    QVERIFY(unsupported.contains("overload resolution across files"));
    QVERIFY(unsupported.contains("using directives across files"));

    // Every one of them is about crossing a file. Inside a file the parser
    // has already applied the rule, and tst_cxxfrontenddocument says so.
    for (const QString &entry : unsupported) {
        QVERIFY2(entry.contains("across") || entry.contains("between"),
                 qPrintable("not a cross-file limit: " + entry));
    }
}

QTEST_GUILESS_MAIN(tst_cxxfrontendsnapshot)

#include "tst_cxxfrontendsnapshot.moc"
