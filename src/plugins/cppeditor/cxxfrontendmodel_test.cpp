// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// The cxx-frontend model over real files: the ones on disk, the one being
// typed into, and the include closure as the built-in model resolved it.
//
// tests/auto/cxxfrontend says what the model answers when it is handed source
// text. What it cannot say is that anything hands it the right text -- that
// the includes of a file in a project are found, that a header being edited is
// read from the editor rather than from disk, and that the project's defines
// arrive. That is what this covers, and it is the part that has to work before
// a consumer can be moved onto it.

#include "cxxfrontendmodel_test.h"

#include "cppmodelmanager.h"
#include "cpptoolstestcase.h"
#include "cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendSnapshot.h>

#include <QTest>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {

namespace {

using CppEditor::Tests::TemporaryDir;

// A directory of files, parsed by the built-in model and then by the other
// one, the way the editor's parser does it.
class Parsed
{
public:
    // \a files is name to contents; \a mainFile is the one the model is run
    // for. Whatever \a workingCopy holds stands in for an open editor.
    Parsed(const QList<QPair<QString, QByteArray>> &files, const QString &mainFile,
           const QByteArray &configFile = {}, const WorkingCopy &workingCopy = {})
    {
        if (!m_dir.isValid() || !m_testCase.succeededSoFar())
            return;

        QSet<FilePath> paths;
        for (const auto &[name, contents] : files)
            paths.insert(m_dir.createFile(name.toLocal8Bit(), contents));
        if (!CppEditor::Tests::TestCase::parseFiles(paths))
            return;

        m_mainFilePath = m_dir.filePath() / mainFile;
        updateCxxFrontendModel(CppEditor::Tests::TestCase::globalSnapshot(), m_mainFilePath, configFile,
                               workingCopy);
        m_model = cxxFrontendModel(m_mainFilePath);
    }

    bool isValid() const { return m_dir.isValid() && m_model != nullptr; }
    const CxxFrontendSnapshot *model() const { return m_model.get(); }
    FilePath path(const QString &name) const { return m_dir.filePath() / name; }
    const FilePath &mainFilePath() const { return m_mainFilePath; }

    // What the model made of the main file, or nullptr.
    const CxxFrontendDocument *mainDocument() const
    {
        return m_model ? m_model->document(m_mainFilePath.toFSPathString()) : nullptr;
    }

    QStringList symbolNames() const
    {
        QStringList names;
        if (const CxxFrontendDocument *document = mainDocument()) {
            for (const CxxFrontendDocument::Symbol &symbol : document->symbols())
                names.append(symbol.name);
        }
        return names;
    }

private:
    // First member, so that the model manager is collected before these files
    // are parsed into it and again once they are gone. A test that leaves
    // documents behind is a test that breaks the next one.
    CppEditor::Tests::TestCase m_testCase;
    TemporaryDir m_dir;
    FilePath m_mainFilePath;
    std::shared_ptr<const CxxFrontendSnapshot> m_model;
};

} // namespace

// The closure, not just the file: a header reached through another header is
// in the model too, which is what says the built-in model's resolution came
// across rather than only its first level.
void CxxFrontendModelTest::testRunsOverAFileAndItsIncludes()
{
    const Parsed parsed({{"inner.h", "int fromInner;\n"},
                         {"outer.h", "#include \"inner.h\"\nint fromOuter;\n"},
                         {"main.cpp", "#include \"outer.h\"\nint fromSource;\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    QCOMPARE(parsed.symbolNames(), QStringList("fromSource"));
    QVERIFY(parsed.model()->contains(parsed.path("outer.h").toFSPathString()));
    QVERIFY(parsed.model()->contains(parsed.path("inner.h").toFSPathString()));
    QCOMPARE(parsed.model()->allIncludesFor(parsed.mainFilePath().toFSPathString()).size(), 2);
}

// An open editor holds text that is nowhere on disk, and that is the text the
// model has to read -- of the file itself and of any header being edited
// beside it.
void CxxFrontendModelTest::testReadsWhatIsBeingTyped()
{
    CppEditor::Tests::TestCase testCase;
    QVERIFY(testCase.succeededSoFar());

    TemporaryDir dir;
    QVERIFY(dir.isValid());
    const FilePath header = dir.createFile("h.h", "int onDisk;\n");
    const FilePath source = dir.createFile("main.cpp", "#include \"h.h\"\nint alsoOnDisk;\n");
    QVERIFY(CppEditor::Tests::TestCase::parseFiles({header, source}));

    WorkingCopy workingCopy;
    workingCopy.insert(header, "int beingTyped;\n");
    workingCopy.insert(source, "#include \"h.h\"\nint alsoBeingTyped;\n");

    updateCxxFrontendModel(CppEditor::Tests::TestCase::globalSnapshot(), source, {}, workingCopy);
    const std::shared_ptr<const CxxFrontendSnapshot> model = cxxFrontendModel(source);
    QVERIFY(model);

    const CxxFrontendDocument *sourceDocument = model->document(source.toFSPathString());
    QVERIFY(sourceDocument);
    QCOMPARE(sourceDocument->symbols().size(), 1);
    QCOMPARE(sourceDocument->symbols().first().name, QString("alsoBeingTyped"));

    const CxxFrontendDocument *headerDocument = model->document(header.toFSPathString());
    QVERIFY(headerDocument);
    QCOMPARE(headerDocument->symbols().size(), 1);
    QCOMPARE(headerDocument->symbols().first().name, QString("beingTyped"));
}

// The question a consumer will ask first, over files rather than over strings:
// the name is used here and declared in a header, and the answer has to be
// that header's path on disk.
void CxxFrontendModelTest::testResolvesANameDeclaredInAnInclude()
{
    const Parsed parsed({{"h.h", "int fromHeader;\n"},
                         {"main.cpp", "#include \"h.h\"\nvoid f() { fromHeader = 1; }\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const CxxFrontendDocument::Declaration found
        = parsed.model()->declarationAt(parsed.mainFilePath().toFSPathString(), 2, 12);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("fromHeader"));
    QCOMPARE(FilePath::fromUserInput(found.filePath), parsed.path("h.h"));
    QCOMPARE(found.line, 1);
}

// A project part's defines decide which half of an #ifdef is code, so the
// model reads nothing right without them.
void CxxFrontendModelTest::testTakesTheProjectsDefines()
{
    const QByteArray configFile = "#define FEATURE 1\n";
    const Parsed parsed({{"main.cpp",
                          "#ifdef FEATURE\nint withFeature;\n#else\nint withoutFeature;\n#endif\n"}},
                        "main.cpp", configFile);
    QVERIFY(parsed.isValid());

    QCOMPARE(parsed.symbolNames(), QStringList("withFeature"));
}

// And without them the other half, which is what makes the test above about
// the defines rather than about the default.
void CxxFrontendModelTest::testWithoutTheProjectsDefines()
{
    const Parsed parsed({{"main.cpp",
                          "#ifdef FEATURE\nint withFeature;\n#else\nint withoutFeature;\n#endif\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    QCOMPARE(parsed.symbolNames(), QStringList("withoutFeature"));
}

// A link in the units the editor speaks: the cursor is somewhere in a name,
// one-based line and zero-based column, and the answer has a zero-based column
// of its own. An off-by-one here sends someone to the wrong character of the
// right line, which is the kind of wrong that looks right.
void CxxFrontendModelTest::testFollowsANameToItsDeclaration()
{
    const Parsed parsed({{"main.cpp", "int here;\nvoid f() { here = 1; }\n"}}, "main.cpp");
    QVERIFY(parsed.isValid());

    // "here" is at column 12 counting from one, so 11 from zero, and the
    // cursor may be anywhere in it or just after it.
    for (const int column : {11, 13, 15}) {
        const Link link = cxxFrontendFollowSymbol(parsed.mainFilePath(), 2, column, 100, 110);
        QVERIFY2(link.hasValidTarget(), qPrintable(QString("column %1").arg(column)));
        QCOMPARE(link.targetFilePath, parsed.mainFilePath());
        QCOMPARE(link.target.line, 1);
        // "int here;" declares it at the fifth character, the fourth from zero.
        QCOMPARE(link.target.column, 4);
        // Whatever the caller measured, handed back untouched.
        QCOMPARE(link.linkTextStart, 100);
        QCOMPARE(link.linkTextEnd, 110);
    }
}

// And nothing where it has nothing, which the caller then answers as it always
// did. The last of these is the interesting one: the snapshot would offer a
// place for a name declared in a header, by searching the headers for the name
// -- a search that guesses where the language has rules. A guess returned as
// an answer is worse than no answer, so a link only comes from what the parser
// itself resolved while reading this file.
void CxxFrontendModelTest::testFollowsNothingItCannotAnswerFor()
{
    const Parsed parsed({{"h.h", "int fromHeader;\n"},
                         {"main.cpp", "#include \"h.h\"\nvoid f() { fromHeader = 1; }\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    // A file the model never ran over.
    QVERIFY(!cxxFrontendFollowSymbol(parsed.path("elsewhere.cpp"), 1, 0, 0, 0).hasValidTarget());
    // A position on no name at all.
    QVERIFY(!cxxFrontendFollowSymbol(parsed.mainFilePath(), 2, 8, 0, 0).hasValidTarget());
    // A name from a header: the model has the header, and this still declines.
    QVERIFY(parsed.model()->contains(parsed.path("h.h").toFSPathString()));
    QVERIFY(!cxxFrontendFollowSymbol(parsed.mainFilePath(), 2, 11, 0, 0).hasValidTarget());
}

// A name a using declaration brought in: the built-in model answers with the
// using declaration, and that is the answer to keep, so this declines.
void CxxFrontendModelTest::testDeclinesANameFromAUsingDeclaration()
{
    const Parsed parsed({{"main.cpp",
                          "namespace NS { class Foo {}; }\n"
                          "using NS::Foo;\n"
                          "void f() { Foo brought; }\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    QVERIFY(!cxxFrontendFollowSymbol(parsed.mainFilePath(), 3, 11, 0, 0).hasValidTarget());
}

// The case that stopped follow symbol from using this: a class forward
// declared in the file being edited and defined in another, where the answer
// has to be the definition and this model does not have it. Offering the
// forward declaration would send someone to a line that declares nothing, so
// it offers nothing and the built-in lookup finds the definition as before.
void CxxFrontendModelTest::testDeclinesAForwardDeclaration()
{
    const Parsed parsed({{"main.cpp", "class Foo;\nFoo *p;\nclass Bar { int m; };\nBar b;\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    // Foo, declared on line 1 and defined nowhere here.
    QVERIFY(!cxxFrontendFollowSymbol(parsed.mainFilePath(), 2, 0, 0, 0).hasValidTarget());

    // Bar, defined on line 3, still answers -- so this is about the
    // declaration and not about classes.
    const Link link = cxxFrontendFollowSymbol(parsed.mainFilePath(), 4, 0, 0, 0);
    QVERIFY(link.hasValidTarget());
    QCOMPARE(link.target.line, 3);
}

} // namespace CppEditor::Internal
