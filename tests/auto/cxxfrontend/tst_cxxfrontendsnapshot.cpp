// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// A file with its headers read into it, the way a compiler reads them.
//
// That is what a document is here, and this file asserts what follows from
// it. A header is not a document of its own: it is part of every document
// that includes it, so a file is searched or edited only once process() has
// been called for it, and the macros a header establishes are in force
// afterwards because its text was read and not because anything carried
// them.
//
// It was not always so. Each file used to be a translation unit that stopped
// at its own text, with a header's macros crossing to its includer and
// nothing else -- which made a file that uses its headers, and that is every
// file, unreadable: a type a header declared was not a type here, and a
// declaration whose type is unknown is not read at all.
//
// What the arrangement costs is reuse: the built-in model parses a header
// once for every file that reaches it and shares the result, and this parses
// it again per file. What it buys is a file being readable at all.

#include <cplusplus/CxxFrontendSnapshot.h>

#include <QObject>
#include <QDebug>
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
    void theDeclarationOfADefinition();
    void theDefinitionOfADeclarationInTheSameFile();
    void theNameIsWhereAReaderIsSent();
    void noDefinitionOutsideTheTranslationUnit();
    void whichFileDefinesAFunction();
    void twoOverloadsOfOneCountAreNotToldApart();
    void noCounterpartOffAFunction();
    void noCounterpartWhereNobodyWroteTheOtherSide();
    void aHeaderIsReadIntoItsIncluder();
    void aFilesSymbolsAreItsOwn();
    void theClassOfANameIsFoundInTheHeaderThatWritesIt();
    void aMacroCrossesFromAHeader();
    void aMacroCrossesTwoHeadersDeep();
    void anUndefInAHeaderCrossesToo();
    void aHeaderIsReadOncePerFile();
    void aCycleTerminates();
    void anUnresolvedIncludeIsNotAnError();
    void reportsWhatAFileIncludes();
    void predefinedMacrosReachEveryFile();

    void aHeaderSeesTheMacrosOfItsIncluder();

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
    void anOverloadIsToldFromItsSiblings();
    void aMacroThatDeclaresSomethingReadThroughASnapshot();
    void aUsingDirectiveInAHeaderIsHonoured();
    void aNameIsTakenFromTheNamespaceTheFileOpened();
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
    void aDefinitionApartFromItsDeclarationIsFound();
    void aUsageThroughABaseIsFound();
    void aUsageFromAMacroArgumentIsReportedOnce();
    void aUsageFromAMacroBodyIsNotReported();
    void anUnqualifiedRedeclarationIsReported();
    void aDeclarationInAHeaderNobodyReadIsReported();
    void aPositionThatNamesNothingHasNoUsages();
    void aMemberNamedThroughAnObjectIsFound();

    void completionAtAPosition();
    void unsupportedLookups();
};

void tst_cxxfrontendsnapshot::aHeaderIsReadIntoItsIncluder()
{
    Files files;
    files.add("h.h", "struct FromHeader { int value; };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\n"
                                    "int read(FromHeader h) { return h.value; }\n");
    QVERIFY(document);

    // The header's text is in this translation unit, which is what lets a
    // declaration whose type comes from a header be read at all.
    QVERIFY(document->diagnostics().isEmpty());
    QCOMPARE(symbolNames(document), QStringList("read"));

    // And the header is not a file of its own here. Whoever wants a
    // document for it asks for one, by processing it.
    QCOMPARE(snapshot.files(), QStringList("a.cpp"));
    QVERIFY(!snapshot.document("h.h"));
}

// A reader that has only a class's name -- which is all a test runner
// pointed at a class has -- gets back the file that writes it. The header
// counts, being read into whoever includes it, and that is the whole point:
// a test class is declared in a header and named from a source file.
void tst_cxxfrontendsnapshot::theClassOfANameIsFoundInTheHeaderThatWritesIt()
{
    Files files;
    files.add("tst.h", "namespace NS {\n"
                       "class tst_FromHeader : public QObject\n"
                       "{\n"
                       "};\n"
                       "}\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("main.cpp", "class QObject {};\n"
                                       "#include \"tst.h\"\n"
                                       "int main() { NS::tst_FromHeader t; }\n");
    QVERIFY(document);

    const CxxFrontendDocument::Place place = document->classNamed("NS::tst_FromHeader");
    QCOMPARE(place.filePath, QString("tst.h"));
    QCOMPARE(place.line, 2);
    QCOMPARE(place.column, 7);

    // And the file that named it declares no such class of its own.
    QCOMPARE(document->classNamed("tst_FromHeader").line, 0);
}

void tst_cxxfrontendsnapshot::aFilesSymbolsAreItsOwn()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\nint fromSource;\n");

    // The header is read here, but what this file declares is its own: an
    // outline of a.cpp is a list of what a.cpp says.
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

void tst_cxxfrontendsnapshot::aHeaderIsReadOncePerFile()
{
    Files files;
    files.add("guarded.h", "#pragma once\nstruct FromHeader {};\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());

    // A header that guards itself is read as often as it is written and
    // contributes once, which is what nearly every header does.
    const CxxFrontendDocument *guarded
        = snapshot.process("a.cpp",
                            "#include \"guarded.h\"\n#include \"guarded.h\"\nFromHeader x;\n");
    QVERIFY(guarded);
    QVERIFY(guarded->diagnostics().isEmpty());

    // And each file reads for itself, since each has a translation unit of
    // its own -- what one file made of a header is not handed to the next.
    const CxxFrontendDocument *other
        = snapshot.process("b.cpp", "#include \"guarded.h\"\nFromHeader y;\n");
    QVERIFY(other);
    QVERIFY(other->diagnostics().isEmpty());
    QCOMPARE(snapshot.files(), QStringList({"a.cpp", "b.cpp"}));
    QCOMPARE(snapshot.allIncludesFor("a.cpp"), QStringList("guarded.h"));
}

void tst_cxxfrontendsnapshot::aCycleTerminates()
{
    Files files;
    files.add("a.h", "#include \"b.h\"\nint fromA;\n");
    files.add("b.h", "#include \"a.h\"\nint fromB;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("main.cpp", "#include \"a.h\"\nint x;\n");

    // Neither header guards itself, so reading them as written has no end.
    // A file being read again while it is still open is where that stops.
    QVERIFY(document);
    QCOMPARE(snapshot.files(), QStringList("main.cpp"));
    QCOMPARE(snapshot.allIncludesFor("main.cpp"), QStringList({"a.h", "b.h"}));
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

    // Everything the file reached, however deep, which is what an include
    // hierarchy is drawn from.
    QCOMPARE(snapshot.allIncludesFor("a.cpp"), QStringList({"inner.h", "outer.h"}));

    // And nothing for a file nobody processed.
    QCOMPARE(snapshot.allIncludesFor("outer.h"), QStringList());
}

void tst_cxxfrontendsnapshot::predefinedMacrosReachEveryFile()
{
    Files files;
    files.add("h.h", "#ifndef FROM_PROJECT\n#error no\n#endif\nint fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.setPredefinedMacros({"FROM_PROJECT 1"});
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\nint x;\n");

    // What the project defines is in force before the first line, and a
    // header is read after that line.
    QVERIFY(document);
    QVERIFY2(document->diagnostics().isEmpty(),
             qPrintable(document->diagnostics().isEmpty()
                            ? QString()
                            : document->diagnostics().first().text));
}

// A header is read where it is included, so what the includer has defined
// by that point decides which half of an #ifdef it contributes. Getting
// this wrong does not fail loudly: it gives the wrong half, silently.
void tst_cxxfrontendsnapshot::aHeaderSeesTheMacrosOfItsIncluder()
{
    Files files;
    files.add("h.h", "#ifdef FEATURE\nint withFeature;\n#else\nint withoutFeature;\n#endif\n"
                     "void use(int);\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#define FEATURE 1\n"
                                    "#include \"h.h\"\n"
                                    "void f() { use(withFeature); }\n");

    // Naming what that half declared is how the file says which half it
    // got.
    QVERIFY(document);
    QVERIFY2(document->diagnostics().isEmpty(),
             qPrintable(document->diagnostics().isEmpty()
                            ? QString()
                            : document->diagnostics().first().text));
}

void tst_cxxfrontendsnapshot::aGuardedHeaderIncludedTwiceKeepsWhatItDeclares()
{
    Files files;
    files.add("h.h", "#ifndef H_H\n#define H_H\nint fromHeader;\n#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\n#include \"h.h\"\n"
                                    "void f() { fromHeader = 1; }\n");

    // The guard keeps the second reading out, and what the first brought
    // in is still there to be named.
    QVERIFY(document);
    QVERIFY(document->diagnostics().isEmpty());
    QCOMPARE(snapshot.declarationAt("a.cpp", 3, 12).filePath, QString("h.h"));
}

void tst_cxxfrontendsnapshot::aGuardedHeaderReachedTwoWaysKeepsWhatItDeclares()
{
    Files files;
    files.add("inner.h", "#ifndef INNER_H\n#define INNER_H\nint fromInner;\n#endif\n");
    files.add("outer.h", "#ifndef OUTER_H\n#define OUTER_H\n#include \"inner.h\"\n#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp",
                           "#include \"inner.h\"\n#include \"outer.h\"\n"
                           "void f() { fromInner = 1; }\n");

    QVERIFY(document);
    QVERIFY(document->diagnostics().isEmpty());
    QCOMPARE(snapshot.declarationAt("a.cpp", 3, 12).filePath, QString("inner.h"));
}

void tst_cxxfrontendsnapshot::aGuardIsNotAnExcuseToIgnoreOtherMacros()
{
    Files files;
    files.add("h.h",
              "#ifndef H_H\n#define H_H\n"
              "#ifdef FEATURE\nint withFeature;\n#else\nint withoutFeature;\n#endif\n"
              "#endif\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());

    // Each file reads the header for itself, so each gets the half its own
    // macros ask for. Naming the other half is what says so.
    const CxxFrontendDocument *with
        = snapshot.process("with.cpp", "#define FEATURE 1\n#include \"h.h\"\n"
                                       "void f() { withFeature = 1; }\n");
    QVERIFY(with);
    QVERIFY(with->diagnostics().isEmpty());

    const CxxFrontendDocument *without
        = snapshot.process("without.cpp", "#include \"h.h\"\n"
                                          "void f() { withoutFeature = 1; }\n");
    QVERIFY(without);
    QVERIFY(without->diagnostics().isEmpty());
}

void tst_cxxfrontendsnapshot::aNameDeclaredInAHeaderResolvesFromTheSource()
{
    Files files;
    files.add("h.h", "int fromHeader;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { fromHeader = 1; }\n");

    // The header is read into this file, so the file's own document
    // resolves the name -- and says which file it was declared in.
    const CxxFrontendDocument::Declaration here = document->declarationAt(2, 12);
    QVERIFY(here.isValid());
    QCOMPARE(here.filePath, QString("h.h"));

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

// Which of several declarations a call means, where they are in a header.
// A header is read into the file that includes it, so the checker weighs
// the arguments against them exactly as it does for a call in one file.
// The same as the document test of the name: a macro whose body declares
// something has no text of its own to point at.
void tst_cxxfrontendsnapshot::aMacroThatDeclaresSomethingReadThroughASnapshot()
{
    CxxFrontendSnapshot snapshot;
    const CxxFrontendDocument * const document = snapshot.process(
        "a.cpp",
        "// Copyright header\n"
        "\n"
        "#define GENERATE_FUNC void myFunctionGenerated() {}\n"
        "\n"
        "//\n"
        "// Symbols in a global namespace\n"
        "//\n"
        "\n"
        "GENERATE_FUNC\n"
        "\n"
        "int myVariable;\n");
    QVERIFY(document);

    QStringList described;
    for (const CxxFrontendDocument::Symbol &symbol : document->symbols()) {
        described << QString("%1 @%2:%3%4").arg(symbol.name).arg(symbol.line).arg(symbol.column)
                         .arg(symbol.isGenerated ? " generated" : "");
    }
    QCOMPARE(described,
             QStringList({"myFunctionGenerated @9:1 generated", "myVariable @11:5"}));
}

void tst_cxxfrontendsnapshot::anOverloadIsToldFromItsSiblings()
{
    Files files;
    files.add("h.h", "void g(int);\nvoid g(double);\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nvoid f() { g(2.5); }\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 12);
    QVERIFY(found.isValid());
    QCOMPARE(found.filePath, QString("h.h"));
    QCOMPARE(found.line, 2); // the one taking a double, called with one
}

// A using directive written in a header reaches the file that includes it:
// the header is read into that file, so the directive is in force where the
// name is written and the parser applies it.
void tst_cxxfrontendsnapshot::aUsingDirectiveInAHeaderIsHonoured()
{
    Files files;
    files.add("h.h", "namespace N { int fromN; }\nusing namespace N;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nint f() { return fromN; }\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 2, 18);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("N::fromN"));
    QCOMPARE(found.filePath, QString("h.h"));
}

// And which of two headers wins is the language's rule rather than which
// was included last: only the namespace the file opened is looked in.
void tst_cxxfrontendsnapshot::aNameIsTakenFromTheNamespaceTheFileOpened()
{
    Files files;
    files.add("first.h", "namespace A { int both; }\n");
    files.add("second.h", "namespace B { int both; }\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"first.h\"\n#include \"second.h\"\n"
                              "using namespace A;\nint f() { return both; }\n");

    const CxxFrontendDocument::Declaration found = snapshot.declarationAt("a.cpp", 4, 18);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("A::both"));
    QCOMPARE(found.filePath, QString("first.h"));
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
    // Searching from the header means having read it as a file of its own,
    // which is what the editor does with the file somebody is in.
    snapshot.process("h.h", "int fromHeader;\n");

    QCOMPARE(placesOf(snapshot.findUsages("h.h", 1, 5)),
             QStringList({"a.cpp:2:12", "h.h:1:5 (declaration)"}));
}

// Matching on the spelling would report these, and it would be wrong: what
// a.cpp calls "both" is a variable of its own, declared in a scope of its
// own, and the front end links it to nothing. Which is why every place is
// resolved and compared against the declaration being searched for.
void tst_cxxfrontendsnapshot::aNameThatMeansSomethingElseIsNotAUsage()
{
    Files files;
    files.add("h.h", "namespace N { int both; }\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nint both;\nvoid f() { both = 1; }\n");
    snapshot.process("h.h", "namespace N { int both; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("h.h", 1, 19)),
             QStringList("h.h:1:19 (declaration)"));
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
    snapshot.process("h.h", "int fromHeader;\n");

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
    snapshot.process("h.h", "namespace N { int v; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("h.h", 1, 19)),
             QStringList({"a.cpp:2:15", "h.h:1:19 (declaration)"}));
}

// Declared in a header, defined in a source file: the two are not in one
// translation unit here, and the definition names the class in front of it,
// which is what the search follows back to the declaration.
void tst_cxxfrontendsnapshot::aDefinitionApartFromItsDeclarationIsFound()
{
    Files files;
    files.add("b.h", "struct B { void f(); };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nvoid B::f() {}\n");
    snapshot.process("b.h", "struct B { void f(); };\n");

    // The name in void B::f() {} declares nothing new, so nothing resolves
    // there -- and what stands there is that function all the same, which
    // is what a name written on a declaration means. Both places come back,
    // each said to be a declaration of the thing rather than a use of it.
    QCOMPARE(placesOf(snapshot.findUsages("b.h", 1, 17)),
             QStringList({"a.cpp:2:9 (declaration)", "b.h:1:17 (declaration)"}));
    QVERIFY(!CxxFrontendSnapshot::unsupportedLookups()
                 .contains("a definition written apart from its declaration"));
}

void tst_cxxfrontendsnapshot::aUsageThroughABaseIsFound()
{
    Files files;
    files.add("b.h", "struct B { int m; };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nstruct D : B { void f() { m = 1; } };\n");
    snapshot.process("b.h", "struct B { int m; };\n");

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

// The other side of that rule. "extern int x;" in a file that includes the
// header declaring x names the header's x -- the front end reads the header
// into the file and links the two -- so both places are the same thing and a
// search started at either of them finds all of them, b.cpp included. It has
// to: renaming the one in the header and leaving the others behind would
// break the program.
//
// Which is why the files to read are the ones that reach where the thing was
// *first* declared. b.cpp does not include a.cpp, and a search begun at
// a.cpp's line would never look at it otherwise.
void tst_cxxfrontendsnapshot::anUnqualifiedRedeclarationIsReported()
{
    Files files;
    files.add("h.h", "int shared;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nextern int shared;\nvoid f() { shared = 1; }\n");
    snapshot.process("b.cpp", "#include \"h.h\"\nvoid g() { shared = 2; }\n");
    snapshot.process("h.h", "int shared;\n");

    const QStringList everyPlace{"a.cpp:2:12 (declaration)", "a.cpp:3:12", "b.cpp:2:12",
                                 "h.h:1:5 (declaration)"};
    QCOMPARE(placesOf(snapshot.findUsages("h.h", 1, 5)), everyPlace);
    QCOMPARE(placesOf(snapshot.findUsages("a.cpp", 2, 12)), everyPlace);
    QCOMPARE(placesOf(snapshot.findUsages("b.cpp", 2, 12)), everyPlace);
}

// A header is read into whoever includes it rather than being a document of
// its own, so a search may never look at the file a declaration is in. Where
// it stands is known from the declaration itself, and is reported as a place
// all the same -- otherwise a rename would leave the header behind.
void tst_cxxfrontendsnapshot::aDeclarationInAHeaderNobodyReadIsReported()
{
    Files files;
    files.add("h.h", "int shared;\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"h.h\"\nextern int shared;\nvoid f() { shared = 1; }\n");

    QCOMPARE(placesOf(snapshot.findUsages("a.cpp", 2, 12)),
             QStringList({"a.cpp:2:12 (declaration)", "a.cpp:3:12", "h.h:1:5 (declaration)"}));
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
void tst_cxxfrontendsnapshot::aMemberNamedThroughAnObjectIsFound()
{
    Files files;
    files.add("b.h", "struct B { int m; };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    snapshot.process("a.cpp", "#include \"b.h\"\nvoid f(B b) { b.m = 1; }\n");
    snapshot.process("b.h", "struct B { int m; };\n");

    // b's type is written in a header, and the header is read into the file
    // that uses it, so the parser knows what b.m means and the search finds
    // it. This is what one translation unit per file could not do.
    QCOMPARE(placesOf(snapshot.findUsages("b.h", 1, 16)),
             QStringList({"a.cpp:2:17", "b.h:1:16 (declaration)"}));
}

void tst_cxxfrontendsnapshot::completionAtAPosition()
{
    Files files;
    files.add("h.h", "struct FromHeader { int fromHeader; };\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());

    const auto namesIn = [&](const QString &source, int line, int column) {
        QStringList names;
        const CxxFrontendDocument *document
            = snapshot.processForCompletion("main.cpp", source, line, column);
        if (!document)
            return names;
        for (const CxxFrontendDocument::Completion::Candidate &candidate :
             document->completion().candidates) {
            names.append(candidate.name);
        }
        return names;
    };

    // A type this file declares, just after the dot.
    const QString own = "#include \"h.h\"\n"
                        "struct Own { int fromHere; };\n"
                        "void f(Own own)\n"
                        "{\n"
                        "    own.\n"
                        "}\n";
    const QStringList offered = namesIn(own, 5, 9);
    QVERIFY2(offered.contains("fromHere"), qPrintable(offered.join(", ")));

    // And a type a header declares, which is what reading the header into
    // this file makes possible: its members are members here.
    const QString fromHeader = "#include \"h.h\"\n"
                               "void f(FromHeader other)\n"
                               "{\n"
                               "    other.\n"
                               "}\n";
    const QStringList across = namesIn(fromHeader, 4, 11);
    QVERIFY2(across.contains("fromHeader"), qPrintable(across.join(", ")));
}

void tst_cxxfrontendsnapshot::unsupportedLookups()
{
    const QStringList unsupported = CxxFrontendSnapshot::unsupportedLookups();

    // Weighing a call's arguments against the declarations a header holds
    // is the checker's, the header being read into the file that includes
    // it: it was on this list until the case that pinned it was written so
    // that it compiles.
    QVERIFY(!unsupported.contains("overload resolution across files"));

    // What reading a header into its includer settled, so that the list
    // does not keep saying it. A file that declares over again what a header
    // declares is linked to the header by the front end, so the two are one
    // thing and a search reaches both.
    QVERIFY(!unsupported.contains("members named through an object across files"));
    QVERIFY(!unsupported.contains("unqualified redeclarations across files"));

    // And what reading it in settled about the rules: a using directive
    // written in a header is in force where the includer writes a name, and
    // which of two headers a name comes from is the language's rule rather
    // than which was included last. Both have cases of their own above.
    QVERIFY(!unsupported.contains("using directives across files"));
    QVERIFY(!unsupported.contains("shadowing between headers"));
}

// The two places a function is written, and the one direction that is
// reachable: from the definition in the file being edited to the declaration
// in the header it was read from. What "Switch Between Function
// Declaration/Definition" follows.
//
// Not through the name: "void C::f(int)" declares nothing new there and
// resolves to nothing, which is why this is asked of the definition itself.
void tst_cxxfrontendsnapshot::theDeclarationOfADefinition()
{
    Files files;
    files.add("h.h", "struct C {\n    void f(int a);\n};\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\n\nvoid C::f(int a) {}\n");
    QVERIFY(document);

    const CxxFrontendDocument::Counterpart declaration = document->counterpartAt(3, 9);
    QVERIFY(declaration.isValid());
    QCOMPARE(declaration.filePath, QString("h.h"));
    QCOMPARE(declaration.line, 2);
    QCOMPARE(declaration.column, 10);
    QVERIFY(!declaration.isDefinition);
}

// And the other way round where one file holds both, which is the case a
// document can answer: the definition below is what the declaration above
// points at.
void tst_cxxfrontendsnapshot::theDefinitionOfADeclarationInTheSameFile()
{
    const CxxFrontendDocument document("void f(int a);\n\nvoid f(int a) {}\n", "a.cpp");

    const CxxFrontendDocument::Counterpart definition = document.counterpartAt(1, 6);
    QVERIFY(definition.isValid());
    QCOMPARE(definition.filePath, QString("a.cpp"));
    QCOMPARE(definition.line, 3);
    QCOMPARE(definition.column, 6);
    QVERIFY(definition.isDefinition);
}

// A destructor is recorded where its tilde is written and a definition where
// its declaration starts, and neither is where a reader is sent: the name is.
void tst_cxxfrontendsnapshot::theNameIsWhereAReaderIsSent()
{
    const CxxFrontendDocument document("struct C {\n    ~C();\n};\n\nC::~C() {}\n", "a.cpp");

    // From the declaration to the definition: past "C::" and past the tilde.
    const CxxFrontendDocument::Counterpart definition = document.counterpartAt(2, 6);
    QVERIFY(definition.isValid());
    QCOMPARE(definition.line, 5);
    QCOMPARE(definition.column, 5);

    // And back, to the name in the class rather than to the tilde.
    const CxxFrontendDocument::Counterpart declaration = document.counterpartAt(5, 5);
    QVERIFY(declaration.isValid());
    QCOMPARE(declaration.line, 2);
    QCOMPARE(declaration.column, 6);
}

// What is out of reach, and what comes back instead: a declaration in a
// header whose definition is in some source file this document never read.
// A document holds one file and what it includes, not the project -- so
// there is no place to give, but the function is named, and whoever knows
// the project's files can ask each of them.
void tst_cxxfrontendsnapshot::noDefinitionOutsideTheTranslationUnit()
{
    const CxxFrontendDocument header("struct C {\n    void f(int a);\n};\n", "h.h");

    const CxxFrontendDocument::Counterpart declaration = header.counterpartAt(2, 10);
    QVERIFY(!declaration.isValid());
    QVERIFY(declaration.namesAFunction());
    QCOMPARE(declaration.name, QString("C::f"));
    QCOMPARE(declaration.parameterCount, 1);

    QVERIFY(CxxFrontendSnapshot::unsupportedLookups().contains(
        "the definition of a declaration outside the translation unit"));
}

// And that is the question each file is asked: does this one define it. The
// answer is where the file itself writes it, so a source file that reads the
// declaration out of a header does not report the header's line as its own.
void tst_cxxfrontendsnapshot::whichFileDefinesAFunction()
{
    Files files;
    files.add("h.h", "struct C {\n    void f(int a);\n    void g();\n};\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document
        = snapshot.process("a.cpp", "#include \"h.h\"\n\nvoid C::f(int a) {}\n");
    QVERIFY(document);

    // The place is the name, not the line it starts on: that is where a
    // reader sent to a function is put.
    const CxxFrontendDocument::Counterpart definition = document->definitionOf("C::f", 1);
    QVERIFY(definition.isValid());
    QCOMPARE(definition.filePath, QString("a.cpp"));
    QCOMPARE(definition.line, 3);
    QCOMPARE(definition.column, 9);
    QVERIFY(definition.isDefinition);

    // The one it does not define, and a name it never heard of.
    QVERIFY(!document->definitionOf("C::g", 0).isValid());
    QVERIFY(!document->definitionOf("C::nothing", 0).isValid());
    // The parameters have to match, since that is as far as this tells two
    // functions of one name apart.
    QVERIFY(!document->definitionOf("C::f", 2).isValid());
}

// And where that is not far enough -- two definitions of one name taking the
// same number of parameters -- nothing is said at all. Either answer would
// send a reader to a function they did not ask about.
void tst_cxxfrontendsnapshot::twoOverloadsOfOneCountAreNotToldApart()
{
    Files files;
    files.add("h.h", "struct C {\n    void f(int a);\n    void f(double a);\n};\n");

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(files.resolver());
    const CxxFrontendDocument *document = snapshot.process(
        "a.cpp", "#include \"h.h\"\n\nvoid C::f(int a) {}\nvoid C::f(double a) {}\n");
    QVERIFY(document);

    QVERIFY(!document->definitionOf("C::f", 1).isValid());
    QVERIFY(CxxFrontendSnapshot::unsupportedLookups().contains(
        "which overload a definition in another file belongs to"));
}

// Neither side of a function is a function at all here.
void tst_cxxfrontendsnapshot::noCounterpartOffAFunction()
{
    const CxxFrontendDocument document("int global;\nvoid f() {}\n", "a.cpp");

    QVERIFY(!document.counterpartAt(1, 5).isValid());
    QVERIFY(!document.counterpartAt(2, 6).isValid());
}

// Every class declares a constructor and a destructor whether or not
// anybody writes one, and a definition written for one of those has no other
// *written* place: what the front end declared for the class stands where
// the class is named, and there is nothing there to point at.
void tst_cxxfrontendsnapshot::noCounterpartWhereNobodyWroteTheOtherSide()
{
    const CxxFrontendDocument document("struct C {};\nC::C() {}\nC::~C() {}\n", "a.cpp");

    const CxxFrontendDocument::Counterpart constructor = document.counterpartAt(2, 4);
    QVERIFY(constructor.namesAFunction());
    QVERIFY(!constructor.isValid());

    const CxxFrontendDocument::Counterpart destructor = document.counterpartAt(3, 5);
    QVERIFY(destructor.namesAFunction());
    QVERIFY(!destructor.isValid());

    // One somebody did write still answers.
    const CxxFrontendDocument written("struct C { C(); };\nC::C() {}\n", "a.cpp");
    const CxxFrontendDocument::Counterpart declaration = written.counterpartAt(2, 4);
    QVERIFY(declaration.isValid());
    QCOMPARE(declaration.line, 1);
}

QTEST_GUILESS_MAIN(tst_cxxfrontendsnapshot)

#include "tst_cxxfrontendsnapshot.moc"
