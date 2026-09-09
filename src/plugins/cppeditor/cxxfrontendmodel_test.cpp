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

} // namespace CppEditor::Internal
