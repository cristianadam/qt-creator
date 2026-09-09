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

QTEST_GUILESS_MAIN(tst_cxxfrontendsnapshot)

#include "tst_cxxfrontendsnapshot.moc"
