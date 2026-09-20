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

#include "cppchecksymbols.h"
#include "cppeditorwidget.h"
#include "cpplocalsymbols.h"
#include "cppmodelmanager.h"
#include "cppoutlinemodel.h"
#include "cpptoolstestcase.h"
#include "cppcodemodelqueries.h"
#include "cxxfrontendindexcache.h"
#include "cxxfrontendmodel.h"

#include <cplusplus/ASTVisitor.h>
#include <cplusplus/AST.h>
#include <cplusplus/CxxFrontendDocument.h>
#include <cplusplus/Icons.h>
#include <cplusplus/CxxFrontendSnapshot.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/TranslationUnit.h>

#include <QSignalSpy>
#include <QTest>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {

namespace {

using CppEditor::Tests::TemporaryDir;

// The first function definition of a file, which is the one the built-in
// LocalSymbols is driven with.
class FindFirstFunctionDefinition : protected ASTVisitor
{
public:
    explicit FindFirstFunctionDefinition(TranslationUnit *unit)
        : ASTVisitor(unit)
    {}

    FunctionDefinitionAST *operator()()
    {
        accept(translationUnit()->ast());
        return m_definition;
    }

protected:
    bool preVisit(AST *ast) override
    {
        if (FunctionDefinitionAST *definition = ast->asFunctionDefinition()) {
            m_definition = definition;
            return false;
        }
        return true;
    }

private:
    FunctionDefinitionAST *m_definition = nullptr;
};

// Every use in one line each, "name@line:column+length" with line and column
// counted from zero, so that the two models can be compared as text and a
// difference says where it is.
QStringList placesOf(const SemanticInfo::LocalUseMap &uses)
{
    QStringList result;
    for (auto it = uses.cbegin(), end = uses.cend(); it != end; ++it) {
        for (const SemanticInfo::Use &use : it.value()) {
            result.append(QString("%1@%2:%3+%4")
                              .arg(QString::fromUtf8(Overview().prettyName(it.key()->name())
                                                         .toUtf8()))
                              .arg(use.line).arg(use.column).arg(use.length));
        }
    }
    result.sort();
    return result;
}

QStringList placesOf(const QList<CxxFrontendDocument::Local> &locals)
{
    QStringList result;
    for (const CxxFrontendDocument::Local &local : locals) {
        for (const CxxFrontendDocument::Occurrence &place : local.places) {
            // A HighlightingResult counts lines from zero and columns from
            // one; this model counts both from one.
            result.append(QString("%1@%2:%3+%4")
                              .arg(local.name)
                              .arg(place.line - 1).arg(place.column).arg(place.length));
        }
    }
    result.sort();
    return result;
}

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

// The closure, not just the file: a header reached through another header was
// read too, which is what says the built-in model's resolution came across
// rather than only its first level.
//
// What it was read into is the one document there is. A header is read where
// it is written, the way a compiler reads it, so it has no document of its
// own -- and what it contributed is in the includer's.
void CxxFrontendModelTest::testRunsOverAFileAndItsIncludes()
{
    const Parsed parsed({{"inner.h", "int fromInner;\n"},
                         {"outer.h", "#include \"inner.h\"\nint fromOuter;\n"},
                         {"main.cpp", "#include \"outer.h\"\nint fromSource;\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    QCOMPARE(parsed.symbolNames(), QStringList("fromSource"));
    QCOMPARE(parsed.model()->files(), QStringList(parsed.mainFilePath().toFSPathString()));

    const QStringList includes
        = parsed.model()->allIncludesFor(parsed.mainFilePath().toFSPathString());
    QCOMPARE(includes.size(), 2);
    QVERIFY(includes.contains(parsed.path("outer.h").toFSPathString()));
    QVERIFY(includes.contains(parsed.path("inner.h").toFSPathString()));
}

// An open editor holds text that is nowhere on disk, and that is the text the
// model has to read -- of the file itself and of any header being edited
// beside it. The header has no document to look into, so what says its edited
// text is the text that was read is that a name only that text declares
// resolves at all.
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
    workingCopy.insert(source, "#include \"h.h\"\nint alsoBeingTyped = beingTyped;\n");

    updateCxxFrontendModel(CppEditor::Tests::TestCase::globalSnapshot(), source, {}, workingCopy);
    const std::shared_ptr<const CxxFrontendSnapshot> model = cxxFrontendModel(source);
    QVERIFY(model);

    const CxxFrontendDocument *sourceDocument = model->document(source.toFSPathString());
    QVERIFY(sourceDocument);
    QCOMPARE(sourceDocument->symbols().size(), 1);
    QCOMPARE(sourceDocument->symbols().first().name, QString("alsoBeingTyped"));

    // "beingTyped" is in no version of the header on disk, so resolving it
    // says which text the header was read from.
    const CxxFrontendDocument::Declaration found
        = model->declarationAt(source.toFSPathString(), 2, 22);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, QString("beingTyped"));
    QCOMPARE(FilePath::fromUserInput(found.filePath), header);
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
        const Link link = cxxFrontendFollowSymbol({}, parsed.mainFilePath(), 2, column, 100, 110);
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
    QVERIFY(!cxxFrontendFollowSymbol({}, parsed.path("elsewhere.cpp"), 1, 0, 0, 0).hasValidTarget());
    // A position on no name at all.
    QVERIFY(!cxxFrontendFollowSymbol({}, parsed.mainFilePath(), 2, 8, 0, 0).hasValidTarget());
    // A name from a header is not one of them: the header is read into this
    // file, so the model does answer, and what with is in
    // testResolvesANameDeclaredInAnInclude.
    QVERIFY(cxxFrontendFollowSymbol({}, parsed.mainFilePath(), 2, 11, 0, 0).hasValidTarget());
}

// The other side of a function, and the case the model cannot answer alone:
// the declaration is in a header and the definition in a source file that
// the file being edited never read. The files to look in come from the
// built-in snapshot, in SymbolFinder's order, and each is read by this model
// until one of them defines it.
void CxxFrontendModelTest::testFindsTheDefinitionInAnotherFile()
{
    const Parsed parsed({{"h.h", "struct C {\n    void f(int a);\n};\n"},
                         {"other.cpp", "#include \"h.h\"\n\nvoid C::f(int a) {}\n"},
                         {"main.cpp", "#include \"h.h\"\n\nvoid g() {}\n"}},
                        "h.h");
    QVERIFY(parsed.isValid());

    // Asked on the declaration in the header, which the model ran over: its
    // own translation unit has no definition, so the search finds the one in
    // other.cpp.
    const std::optional<Link> definition = cxxFrontendCounterpart(
        CppEditor::Tests::TestCase::globalSnapshot(), parsed.mainFilePath(), 2, 10);
    QVERIFY(definition.has_value());
    QCOMPARE(definition->targetFilePath, parsed.path("other.cpp"));
    QCOMPARE(definition->target.line, 3);
}

// Following a name this file has only a declaration of. The definition is
// what somebody following it wants, and it is in a source file this one
// never read, so the project's files are searched for it -- the same search
// switching between the two sides makes.
void CxxFrontendModelTest::testFollowsADeclarationToItsDefinitionElsewhere()
{
    const Parsed parsed({{"h.h", "struct C {\n    void f(int a);\n};\n"},
                         {"other.cpp", "#include \"h.h\"\n\nvoid C::f(int a) {}\n"},
                         {"main.cpp", "#include \"h.h\"\n\nvoid g(C *c) { c->f(1); }\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    // On "f" of "c->f(1)", which resolves to the declaration in the header.
    const Link link = cxxFrontendFollowSymbol(CppEditor::Tests::TestCase::globalSnapshot(),
                                              parsed.mainFilePath(), 3, 18, 0, 0);
    QVERIFY(link.hasValidTarget());
    QCOMPARE(link.targetFilePath, parsed.path("other.cpp"));
    QCOMPARE(link.target.line, 3);
}

// The same, for a name with nothing in front of it. Which files are worth
// reading is decided by the last part of the name, and a name that has only
// one part is that part -- it had been cut short by two characters, so no
// file was ever worth reading and every such search came up empty.
void CxxFrontendModelTest::testFollowsAFreeFunctionToItsDefinitionElsewhere()
{
    const Parsed parsed({{"h.h", "void loose(int a);\n"},
                         {"other.cpp", "#include \"h.h\"\n\nvoid loose(int a) {}\n"},
                         {"main.cpp", "#include \"h.h\"\n\nvoid g() { loose(1); }\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    // On "loose" of "loose(1)", which resolves to the declaration in the
    // header.
    const Link link = cxxFrontendFollowSymbol(CppEditor::Tests::TestCase::globalSnapshot(),
                                              parsed.mainFilePath(), 3, 13, 0, 0);
    QVERIFY(link.hasValidTarget());
    QCOMPARE(link.targetFilePath, parsed.path("other.cpp"));
    QCOMPARE(link.target.line, 3);
}

// Every class a file declares, by the name written out in full. What a
// model diagram asks when a file is dragged into it, and the one question
// that plugin puts to the code model.
void CxxFrontendModelTest::testTheClassesAFileDeclares()
{
    const Parsed parsed({{"main.cpp",
                          "class Outer {\n"
                          "    class Inner {};\n"
                          "};\n"
                          "namespace N { struct InNamespace {}; }\n"
                          "class NamedOnly;\n"
                          "void notAClass();\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const CodeModelQueries code(CppEditor::Tests::TestCase::globalSnapshot(),
                                CppModelManager::workingCopy());
    QStringList said;
    for (const WrittenClass &klass : code.classesDeclaredIn(parsed.mainFilePath()))
        said << klass.qualifiedName;
    said.sort();

    // The forward declaration and the function are not classes this file
    // declares; the nested one and the one in a namespace are.
    QCOMPARE(said, QStringList({"N::InNamespace", "Outer", "Outer::Inner"}));
}

// What Designer asks of a header uic generated, to tell that it is the one
// the form claims: how many functions of a name the file declares. uic writes
// setupUi as a member of the Ui_ class and writes it once, and Designer
// refuses to go to a slot unless it finds exactly one.
void CxxFrontendModelTest::testTheFunctionsAGeneratedHeaderDeclares()
{
    // Shaped as uic writes one: the members in a Ui_ class, and a class in
    // the Ui namespace deriving from it that declares nothing of its own.
    const Parsed parsed({{"ui_form.h",
                          "class QWidget;\n"
                          "class Ui_Form {\n"
                          "public:\n"
                          "    void setupUi(QWidget *Form) {}\n"
                          "    void retranslateUi(QWidget *Form) {}\n"
                          "};\n"
                          "namespace Ui { class Form : public Ui_Form {}; }\n"}},
                        "ui_form.h");
    QVERIFY(parsed.isValid());

    const CodeModelQueries code(CppEditor::Tests::TestCase::globalSnapshot(),
                                CppModelManager::workingCopy());

    auto functionsNamed = [&](const QString &name) {
        int found = 0;
        for (const WrittenClass &klass : code.classesDeclaredIn(parsed.mainFilePath())) {
            for (const WrittenFunction &member : code.memberFunctionsOf(klass)) {
                if (member.name == name)
                    ++found;
            }
        }
        return found;
    };

    QCOMPARE(functionsNamed("setupUi"), 1);
    QCOMPARE(functionsNamed("retranslateUi"), 1);
    // The derived class declares neither, inheriting is not declaring.
    QCOMPARE(functionsNamed("notThere"), 0);
}

// Where what a declaration stands for is defined, which is where a reader
// picking a row of the Class View out of a header wants to be taken. Asked
// with a place anywhere in the declaration rather than with the name's own,
// the way a cursor lands.
void CxxFrontendModelTest::testWhereWhatADeclarationStandsForIsDefined()
{
    const Parsed parsed({{"h.h",
                          "struct C {\n"
                          "    void f(int a);\n"
                          "    static int count;\n"
                          "};\n"
                          "void loose();\n"},
                         {"main.cpp",
                          "#include \"h.h\"\n"
                          "\n"
                          "void C::f(int a) {}\n"
                          "int C::count = 0;\n"
                          "void loose() {}\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const CodeModelQueries code(CppEditor::Tests::TestCase::globalSnapshot(),
                                CppModelManager::workingCopy());
    const auto definitionOf = [&](int line, int column) {
        const Link link = code.definitionOfWhatIsDeclaredAt(parsed.path("h.h"), line, column);
        return link.hasValidTarget()
                   ? QString("%1:%2").arg(link.targetFilePath.fileName()).arg(link.target.line)
                   : QString("nothing");
    };

    // A member function, a free function, a static member -- and a class,
    // which is declared and defined in one place, so there is nowhere else
    // to go. Asked all at once, so that what a sabotage reddens says which
    // of them the other model answers.
    //
    // The static member is a question about the project that model does not
    // take, so the built-in front end answers that one either way.
    QCOMPARE(QStringList({definitionOf(2, 10),
                          definitionOf(5, 6),
                          definitionOf(3, 16),
                          definitionOf(1, 8)})
                 .join(", "),
             QString("main.cpp:3, main.cpp:5, main.cpp:4, nothing"));
}

// Where the project defines the function declared at a place -- asked with
// the column, since a place is what the answer is used as: a reader is sent
// there.
void CxxFrontendModelTest::testWhereAFunctionIsDefined()
{
    const Parsed parsed({{"h.h", "struct C {\n    void slotOfSorts();\n};\n"},
                         {"main.cpp", "#include \"h.h\"\n\nvoid C::slotOfSorts() {}\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const CodeModelQueries code(CppEditor::Tests::TestCase::globalSnapshot(),
                                CppModelManager::workingCopy());

    // On the declaration's own name, in the header.
    const Link definition = code.definitionOfFunctionAt(parsed.path("h.h"), 2, 10);
    QVERIFY(definition.hasValidTarget());
    QCOMPARE(definition.targetFilePath, parsed.path("main.cpp"));

    // A link counts columns from zero and lines from one, which is what
    // whoever opens an editor at it expects. "void C::slotOfSorts" puts the
    // name's own first character at column 8.
    QCOMPARE(QString("%1:%2").arg(definition.target.line).arg(definition.target.column),
             QString("3:8"));
}

// The function a place is inside of and the lines it spans, which is what a
// debugger tooltip is pinned by: the name it was taken in, and whether the
// line the program stopped at is still inside that function.
void CxxFrontendModelTest::testTheFunctionAPlaceIsInside()
{
    const Parsed parsed({{"main.cpp",
                          "void free(int a)\n"       // 1
                          "{\n"                      // 2
                          "    int local = a;\n"     // 3
                          "}\n"                      // 4
                          "struct C {\n"             // 5
                          "    void member()\n"      // 6
                          "    {\n"                  // 7
                          "        int here = 1;\n"  // 8
                          "    }\n"                  // 9
                          "};\n"}},                  // 10
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const auto around = [&](int line, int column) {
        const EnclosingFunction function
            = functionAround(CppEditor::Tests::TestCase::globalSnapshot(),
                             parsed.mainFilePath(), line, column);
        if (!function.isValid())
            return QString("nothing");
        QString name = function.qualifiedName;
        if (name.startsWith("::"))
            name = name.mid(2);
        return QString("%1 %2-%3").arg(name).arg(function.fromLine).arg(function.toLine);
    };

    // Inside each body, and a line that is inside neither. Asked in one
    // comparison, so that a sabotage says which of them the other model
    // answers.
    QCOMPARE(QStringList({around(3, 9), around(8, 13), around(5, 8)}).join(", "),
             QString("free 1-4, C::member 6-9, nothing"));
}

// The function a name stands for, written out in full, which is what a
// profiler asked to collect the costs of the function under the cursor puts
// in its command line.
void CxxFrontendModelTest::testTheFunctionANameStandsFor()
{
    const QByteArray source = "namespace N {\n"                        // 1
                              "struct C {\n"                           // 2
                              "    void f(int a);\n"                    // 3
                              "    int m_count = 0;\n"                  // 4
                              "};\n"                                    // 5
                              "void use(C *c) { c->f(c->m_count); }\n"  // 6
                              "}\n";                                    // 7
    const Parsed parsed({{"main.cpp", source}}, "main.cpp");
    QVERIFY(parsed.isValid());

    QTextDocument text(QString::fromUtf8(source));
    const auto namedAt = [&](int line, int column) {
        QTextCursor cursor(&text);
        cursor.setPosition(Utils::Text::positionInText(&text, line, column));
        return functionNamedAt(CppEditor::Tests::TestCase::globalSnapshot(),
                               parsed.mainFilePath(), cursor);
    };

    // On the "f" of the call, on its declaration's own name, on the name the
    // enclosing function is declared under -- and on a field and on a type,
    // neither of which names a function. Asked in one comparison, so that a
    // sabotage says which of them the other model answers.
    QCOMPARE(QStringList({namedAt(6, 21), namedAt(3, 10), namedAt(6, 6),
                          namedAt(6, 26), namedAt(6, 9)})
                 .join(", "),
             QString("N::C::f, N::C::f, N::use, , "));
}

// Which thing a name means, written out in full, whatever kind of thing it
// is -- and however the file reached the name. What tells a Boost test
// decorator from a function of the same name somewhere else, and the shapes
// here are the ones Boost's own test sources write.
void CxxFrontendModelTest::testWhichThingANameMeans()
{
    const QByteArray source = "namespace lib {\n"                              // 1
                              "namespace inner {\n"                            // 2
                              "void disabled();\n"                             // 3
                              "class label {};\n"                              // 4
                              "int counter;\n"                                 // 5
                              "}\n"                                            // 6
                              "}\n"                                            // 7
                              "using lib::inner::label;\n"                     // 8
                              "namespace alias = lib::inner;\n"                // 9
                              "#define DECORATE(x) x\n"                        // 10
                              "void use()\n"                                   // 11
                              "{\n"                                            // 12
                              "    lib::inner::disabled();\n"                  // 13
                              "    label marker;\n"                            // 14
                              "    alias::counter = 1;\n"                      // 15
                              "    DECORATE(lib::inner::disabled)();\n"        // 16
                              "}\n";                                           // 17
    const Parsed parsed({{"main.cpp", source}}, "main.cpp");
    QVERIFY(parsed.isValid());

    QTextDocument text(QString::fromUtf8(source));
    const auto meansAt = [&](int line, int column) {
        QTextCursor cursor(&text);
        cursor.setPosition(Utils::Text::positionInText(&text, line, column));
        return nameResolvedAt(CppEditor::Tests::TestCase::globalSnapshot(),
                              parsed.mainFilePath(), cursor);
    };

    // Written out; a class reached through a using declaration, which
    // functionNamedAt() would refuse because it is no function; a variable
    // reached through a namespace alias; and a name handed to a macro, which
    // is how a Boost decorator is written. Asked in one comparison so that a
    // sabotage says which of them moved.
    QCOMPARE(QStringList({meansAt(13, 18), meansAt(14, 5), meansAt(15, 12),
                          meansAt(16, 26)})
                 .join(", "),
             QString("lib::inner::disabled, lib::inner::label, "
                     "lib::inner::counter, lib::inner::disabled"));
}

// What a file includes, which is the model manager's own bookkeeping rather
// than either front end's reading -- an include is resolved while
// preprocessing, and the cxx-frontend model is handed those resolutions. It
// is asked here so that a plugin wanting nothing but the include closure --
// the model editor draws its component dependencies from it -- need not know
// a front end at all.
void CxxFrontendModelTest::testWhatAFileIncludes()
{
    const Parsed parsed({{"leaf.h", "class Leaf {};\n"},
                         {"middle.h", "#include \"leaf.h\"\n"},
                         {"main.cpp", "#include \"middle.h\"\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const FilePath dir = parsed.mainFilePath().parentDir();
    QCOMPARE(includesOf(parsed.mainFilePath()), FilePaths{dir / "middle.h"});
    QCOMPARE(includesOf(dir / "middle.h"), FilePaths{dir / "leaf.h"});
    QCOMPARE(includesOf(dir / "leaf.h"), FilePaths());

    // A file nothing has read includes nothing anybody knows about.
    QCOMPARE(includesOf(dir / "absent.h"), FilePaths());
}

// Which files include a header of a given name -- the question Designer asks
// to find the class behind a form. The header it looks for is one uic writes,
// so the name is all there is: the include may resolve to nothing at all,
// and a file that names it twice is still one file.
void CxxFrontendModelTest::testWhichFilesIncludeAHeaderNamed()
{
    const Parsed parsed({{"leaf.h", "class Leaf {};\n"},
                         {"form.cpp",
                          "#include \"ui_form.h\"\n"   // nothing has generated this
                          "#include \"leaf.h\"\n"
                          "#include \"ui_form.h\"\n"}, // named twice, still one file
                         {"main.cpp", "#include \"leaf.h\"\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const FilePath dir = parsed.mainFilePath().parentDir();
    const auto includers = [&](const QString &name) {
        FilePaths files = filesIncludingFileNamed(
            CppEditor::Tests::TestCase::globalSnapshot(), name);
        Utils::sort(files);
        return files;
    };

    QCOMPARE(includers("ui_form.h"), FilePaths{dir / "form.cpp"});

    FilePaths bothOfThem{dir / "form.cpp", dir / "main.cpp"};
    Utils::sort(bothOfThem);
    QCOMPARE(includers("leaf.h"), bothOfThem);

    QCOMPARE(includers("nobody_includes_this.h"), FilePaths());
}

// What a Qt test class says about itself: the slots it declares privately,
// which is how a test writes its test functions, and what it derives from.
// Asked with the class's *name*, which is all the text of QTest::qExec()
// gives, and about a source file that only includes the header writing it.
void CxxFrontendModelTest::testAClassPrivateSlotsAndBases()
{
    const Parsed parsed({{"base.h",
                          "class QObject {};\n"
                          "class tst_Base : public QObject\n"
                          "{\n"
                          "private slots:\n"
                          "    void inherited();\n"
                          "};\n"},
                         {"tst.h",
                          "#include \"base.h\"\n"
                          "namespace NS {\n"
                          "class tst_Simple : public tst_Base\n"
                          "{\n"
                          "public:\n"
                          "    void notASlot();\n"
                          "public slots:\n"
                          "    void notPrivate();\n"
                          "private slots:\n"
                          "    void testOne();\n"
                          "    void testTwo_data();\n"
                          "private:\n"
                          "    void notASlotEither();\n"
                          "};\n"
                          "}\n"},
                         {"main.cpp",
                          "#include \"tst.h\"\n"
                          "int main() { NS::tst_Simple t; }\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const CodeModelQueries code(CppEditor::Tests::TestCase::globalSnapshot(),
                                CppModelManager::workingCopy());
    const auto said = [&](const QString &className) {
        const CodeModelQueries::ClassWithPrivateSlots found
            = code.classWithPrivateSlots(parsed.mainFilePath(), className);
        if (!found.klass.isValid())
            return QString("nothing");
        QStringList slotNames;
        for (const WrittenFunction &slot : found.privateSlots) {
            slotNames << QString("%1 at %2:%3").arg(slot.name, slot.filePath.fileName())
                             .arg(slot.line);
        }
        QStringList bases;
        for (QString base : found.baseClasses)
            bases << (base.startsWith("::") ? base.mid(2) : base);
        return QString("%1 at %2:%3 | %4 | bases: %5")
            .arg(found.klass.qualifiedName, found.klass.filePath.fileName())
            .arg(found.klass.line)
            .arg(slotNames.join(", "), bases.join(", "));
    };

    // Only the private slots, in the order they are declared, each in the
    // file that writes it -- and the class itself found through a header the
    // source file includes.
    QCOMPARE(said("NS::tst_Simple"),
             QString("NS::tst_Simple at tst.h:3 | testOne at tst.h:10, "
                     "testTwo_data at tst.h:11 | bases: tst_Base"));

    // And the base, asked for by the name the class above named it with.
    QCOMPARE(said("tst_Base"),
             QString("tst_Base at base.h:2 | inherited at base.h:5 | bases: QObject"));

    QCOMPARE(said("NS::tst_Missing"), QString("nothing"));
}

// The classes a file hands to a runner, which is how a Qt test's main()
// says which class it runs.
void CxxFrontendModelTest::testTheClassesAFileHandsToARunner()
{
    const Parsed parsed({{"main.cpp",
                          "namespace QTest { int qExec(void *, int, char **); }\n"
                          "namespace NS { class tst_One {}; }\n"
                          "class tst_Two {};\n"
                          "int byValue(int);\n"
                          "int main(int argc, char **argv)\n"
                          "{\n"
                          "    NS::tst_One one;\n"
                          "    tst_Two two;\n"
                          "    QTest::qExec(&one, argc, argv);\n"
                          "    QTest::qExec(&two, argc, argv);\n"
                          "    byValue(argc);\n"
                          "    return 0;\n"
                          "}\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const CodeModelQueries code(CppEditor::Tests::TestCase::globalSnapshot(),
                                CppModelManager::workingCopy());

    // Both of them, in the order they are handed over -- and nothing off a
    // call handed a value, or off a function nobody calls.
    QCOMPARE(code.classesPassedTo(parsed.mainFilePath(), "QTest::qExec").join(", "),
             QString("NS::tst_One, tst_Two"));
    QCOMPARE(code.classesPassedTo(parsed.mainFilePath(), "byValue").join(", "), QString());
    QCOMPARE(code.classesPassedTo(parsed.mainFilePath(), "QTest::qExecNot").join(", "),
             QString());
}

// The calls a file makes with a literal in front of them, which is what the
// tags of a Qt test's data function are made of.
void CxxFrontendModelTest::testTheCallsWithALiteral()
{
    const Parsed parsed({{"main.cpp",
                          "namespace QTest {\n"
                          "void newRow(const char *);\n"
                          "void addRow(const char *, int);\n"
                          "}\n"
                          "using namespace QTest;\n"
                          "void elsewhere(const char *);\n"
                          "void tst_Thing_data()\n"
                          "{\n"
                          "    QTest::newRow(\"first\");\n"
                          "    newRow(\"unqualified\");\n"
                          "    addRow(\"format %1\", 2);\n"
                          "    elsewhere(\"not a tag\");\n"
                          "}\n"
                          "void tst_Other_data()\n"
                          "{\n"
                          "    using namespace QTest;\n"
                          "    newRow(\"inside a function\");\n"
                          "}\n"
                          "int quick_test_main(int, char **, const char *);\n"
                          "int main(int argc, char **argv)\n"
                          "{\n"
                          "    return quick_test_main(argc, argv, \"TheQmlTests\");\n"
                          "}\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const CodeModelQueries code(CppEditor::Tests::TestCase::globalSnapshot(),
                                CppModelManager::workingCopy());
    QStringList said;
    for (const CodeModelQueries::WrittenCall &call
         : code.callsTo(parsed.mainFilePath(), {"QTest::newRow", "QTest::addRow"})) {
        // Put together rather than formatted: a tag can hold a "%1" of its
        // own, and QString::arg() would fill that in.
        said << call.arguments.value(0) + " in " + call.insideFunction + " at "
                    + QString::number(call.line) + ":" + QString::number(call.column)
                    + (call.arguments.size() > 1 ? " (more follows)" : "");
    }

    // The last one is a directive written inside the function, which is in
    // force from there to the end of the block it stands in.
    QCOMPARE(said.join("\n"),
             QString("first in tst_Thing_data at 9:5\n"
                     "unqualified in tst_Thing_data at 10:5\n"
                     "format %1 in tst_Thing_data at 11:5 (more follows)\n"
                     "inside a function in tst_Other_data at 17:5"));

    // An argument that is not the first: a runner is handed argc and argv
    // and then the name, and what is wanted is the third thing. The two
    // arguments in front of it say nothing, being no literals.
    const QList<CodeModelQueries::WrittenCall> runners
        = code.callsTo(parsed.mainFilePath(), {"quick_test_main"});
    QCOMPARE(runners.size(), 1);
    QCOMPARE(runners.first().arguments, QStringList({"", "", "TheQmlTests"}));
    QCOMPARE(runners.first().insideFunction, QString("main"));
}

// The function-like macro uses a file makes and what each was handed, which
// is how a test says which class it runs when the macro that says so is
// Qt's own.
void CxxFrontendModelTest::testTheMacroUsesOfAFile()
{
    const Parsed parsed({{"main.cpp",
                          "#define RUN(klass) int main() { return 0; }\n"
                          "#define PLAIN 1\n"
                          "#define TWO(a, b) a + b\n"
                          "int value = PLAIN;\n"
                          "int sum = TWO( 1 , 2 );\n"
                          "RUN(tst_Thing)\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const CodeModelQueries code(CppEditor::Tests::TestCase::globalSnapshot(),
                                CppModelManager::workingCopy());
    QStringList said;
    for (const CodeModelQueries::WrittenMacroUse &use
         : code.macroUsesIn(parsed.mainFilePath())) {
        said << use.name + "(" + use.arguments.join(", ") + ")";
    }

    // What was written, each argument trimmed -- and nothing for the one
    // used without arguments, which has nothing to read.
    QCOMPARE(said.join(", "), QString("TWO(1, 2), RUN(tst_Thing)"));
}

// And the other direction needs no search at all: a file being edited beside
// its header holds both sides.
void CxxFrontendModelTest::testFindsTheDeclarationOfADefinition()
{
    const Parsed parsed({{"h.h", "struct C {\n    void f(int a);\n};\n"},
                         {"main.cpp", "#include \"h.h\"\n\nvoid C::f(int a) {}\n"}},
                        "main.cpp");
    QVERIFY(parsed.isValid());

    const std::optional<Link> declaration = cxxFrontendCounterpart(
        CppEditor::Tests::TestCase::globalSnapshot(), parsed.mainFilePath(), 3, 9);
    QVERIFY(declaration.has_value());
    QCOMPARE(declaration->targetFilePath, parsed.path("h.h"));
    QCOMPARE(declaration->target.line, 2);
}

// Nothing where no file defines it, and nothing off a function.
void CxxFrontendModelTest::testNoCounterpartWhereThereIsNone()
{
    const Parsed parsed({{"h.h", "struct C {\n    void f(int a);\n};\n"},
                         {"main.cpp", "#include \"h.h\"\n\nvoid g() {}\n"}},
                        "h.h");
    QVERIFY(parsed.isValid());

    const Snapshot snapshot = CppEditor::Tests::TestCase::globalSnapshot();
    QVERIFY(!cxxFrontendCounterpart(snapshot, parsed.mainFilePath(), 2, 10).has_value());
    QVERIFY(!cxxFrontendCounterpart(snapshot, parsed.mainFilePath(), 1, 8).has_value());
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

    QVERIFY(!cxxFrontendFollowSymbol({}, parsed.mainFilePath(), 3, 11, 0, 0).hasValidTarget());
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
    QVERIFY(!cxxFrontendFollowSymbol({}, parsed.mainFilePath(), 2, 0, 0, 0).hasValidTarget());

    // Bar, defined on line 3, still answers -- so this is about the
    // declaration and not about classes.
    const Link link = cxxFrontendFollowSymbol({}, parsed.mainFilePath(), 4, 0, 0, 0);
    QVERIFY(link.hasValidTarget());
    QCOMPARE(link.target.line, 3);
}

// The locals of a function, from both models, over the same source. This is
// the one question a single file answers completely -- a parameter or a block
// variable cannot be named anywhere else -- so the two have no excuse to
// differ, and what the editor highlights around the cursor comes from here.
void CxxFrontendModelTest::testLocalUses_data()
{
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<int>("line");
    QTest::addColumn<int>("column");

    // Each source starts a line down, so that nothing is declared on the
    // first line: see knownDivergence() and the row that goes there on
    // purpose. Line and column say where to ask, counted from one.
    QTest::newRow("basic") << QByteArray("\n"
                                         "int f(int arg)\n"
                                         "{\n"
                                         "    int local;\n"
                                         "    g(&local);\n"
                                         "    return local + arg;\n"
                                         "}\n")
                           << 4 << 9;
    QTest::newRow("lambda") << QByteArray("\n"
                                          "void f()\n"
                                          "{\n"
                                          "    auto func = [](int arg) { return arg; };\n"
                                          "    func(1);\n"
                                          "}\n")
                            << 5 << 5;
    QTest::newRow("nested blocks") << QByteArray("\n"
                                                 "void f(int a)\n"
                                                 "{\n"
                                                 "    int b = a;\n"
                                                 "    {\n"
                                                 "        int b = 2;\n"
                                                 "        b = b + a;\n"
                                                 "    }\n"
                                                 "    b = 3;\n"
                                                 "}\n")
                                   << 4 << 9;
    QTest::newRow("loop and reference") << QByteArray("\n"
                                                      "void f(int *p)\n"
                                                      "{\n"
                                                      "    for (int i = 0; i < 10; ++i)\n"
                                                      "        p[i] = i;\n"
                                                      "    int &r = *p;\n"
                                                      "    r = 1;\n"
                                                      "}\n")
                                        << 4 << 14;
    QTest::newRow("shadowing a parameter") << QByteArray("\n"
                                                         "void f(int a)\n"
                                                         "{\n"
                                                         "    {\n"
                                                         "        int a = 1;\n"
                                                         "        a = a + 1;\n"
                                                         "    }\n"
                                                         "    a = 2;\n"
                                                         "}\n")
                                           << 5 << 13;
    QTest::newRow("member function") << QByteArray("\n"
                                                   "struct S {\n"
                                                   "    int m;\n"
                                                   "    void f(int a) { m = a; }\n"
                                                   "};\n")
                                     << 4 << 21;

    // And one that does declare on the first line, to hold the difference in
    // place rather than leave it to be found again.
    QTest::newRow("declared on the first line")
        << QByteArray("int f(int arg) { return arg; }\n") << 1 << 20;
}

// Why the two are allowed to differ on a row, or nullptr if they are not. The
// one entry is a case where this model is in the right.
static const char *knownDivergence(const QString &row)
{
    // TranslationUnit::findColumnNumber() subtracts the offset of the newline
    // that begins the line, so its columns count from one -- except on the
    // first line, which has no newline before it and comes out one short.
    // Everything that reads a HighlightingResult takes column - 1, so the
    // built-in model marks a declaration on line one a character to its left.
    if (row == "declared on the first line")
        return "the built-in model is one column short on the first line";

    return nullptr;
}

namespace {

// The kinds the other model claims to answer for. What is left out is not a
// name -- a macro, which the preprocessor reports and the processor merges
// in; the angle brackets of a template argument list and the two halves of
// a ternary, which are punctuation; and the Qt keywords, which are macros
// before the parser sees them.
QString nameOf(SemanticHighlighter::Kind kind)
{
    switch (kind) {
    case SemanticHighlighter::TypeUse: return "Type";
    case SemanticHighlighter::NamespaceUse: return "Namespace";
    case SemanticHighlighter::LocalUse: return "Local";
    case SemanticHighlighter::FieldUse: return "Field";
    case SemanticHighlighter::StaticFieldUse: return "StaticField";
    case SemanticHighlighter::EnumerationUse: return "Enumeration";
    case SemanticHighlighter::FunctionUse: return "Function";
    case SemanticHighlighter::VirtualMethodUse: return "VirtualMethod";
    case SemanticHighlighter::StaticMethodUse: return "StaticMethod";
    case SemanticHighlighter::FunctionDeclarationUse: return "FunctionDeclaration";
    case SemanticHighlighter::VirtualFunctionDeclarationUse:
        return "VirtualFunctionDeclaration";
    case SemanticHighlighter::StaticMethodDeclarationUse: return "StaticMethodDeclaration";
    case SemanticHighlighter::LabelUse: return "Label";
    case SemanticHighlighter::PseudoKeywordUse: return "PseudoKeyword";
    default: return {};
    }
}

QString nameOf(CxxFrontendDocument::NameKind kind)
{
    using NameKind = CxxFrontendDocument::NameKind;
    switch (kind) {
    case NameKind::Type: return "Type";
    case NameKind::Namespace: return "Namespace";
    case NameKind::Local: return "Local";
    case NameKind::Field: return "Field";
    case NameKind::StaticField: return "StaticField";
    case NameKind::Enumeration: return "Enumeration";
    case NameKind::Function: return "Function";
    case NameKind::VirtualMethod: return "VirtualMethod";
    case NameKind::StaticMethod: return "StaticMethod";
    case NameKind::FunctionDeclaration: return "FunctionDeclaration";
    case NameKind::VirtualFunctionDeclaration: return "VirtualFunctionDeclaration";
    case NameKind::StaticMethodDeclaration: return "StaticMethodDeclaration";
    case NameKind::Label: return "Label";
    case NameKind::PseudoKeyword: return "PseudoKeyword";
    }
    return {};
}

// One line per name, "line:column+length Kind", so that the two models can
// be compared as text and a difference says where it is.
QStringList colouredBy(const QList<CheckSymbols::Result> &results)
{
    QStringList lines;
    for (const CheckSymbols::Result &result : results) {
        const QString kind = nameOf(SemanticHighlighter::Kind(result.kind));
        if (kind.isEmpty())
            continue;
        lines.append(QString("%1:%2+%3 %4")
                         .arg(result.line).arg(result.column).arg(result.length).arg(kind));
    }
    lines.sort();
    return lines;
}

QStringList colouredBy(const QList<CxxFrontendDocument::Name> &names)
{
    QStringList lines;
    for (const CxxFrontendDocument::Name &name : names) {
        // A document that was never preprocessed carries no #line
        // markers, and TranslationUnit::getPosition() counts its lines
        // from zero for want of one -- see the expectations in
        // cpplocalsymbols_test.cpp, which are the same shape. The editor's
        // own documents are preprocessed and count from one, as this model
        // does.
        lines.append(QString("%1:%2+%3 %4")
                         .arg(name.line - 1).arg(name.column).arg(name.length)
                         .arg(nameOf(name.kind)));
    }
    lines.sort();
    return lines;
}

// The tree an outline draws, one line per entry: how deep it sits, what it
// says, and which line it takes the reader to. Everything the pane shows
// except the icon, which is a picture and is compared where it is a number,
// in tests/auto/cxxfrontend.
QStringList drawnBy(OutlineModel &model, const QModelIndex &parent = {}, int depth = 0)
{
    QStringList lines;
    for (int row = 0, rows = model.rowCount(parent); row < rows; ++row) {
        const QModelIndex index = model.index(row, 0, parent);
        // The first row of the tree is the "<Select Symbol>" placeholder,
        // which stands for no symbol and has no position.
        if (depth == 0 && row == 0)
            continue;
        const Utils::Link link = model.linkFromIndex(index);
        lines.append(QString("%1%2 @%3 ->%4:%5:%6%7")
                         .arg(QString(depth * 2, ' '),
                              index.data(Qt::DisplayRole).toString())
                         .arg(model.positionFromIndex(index).line)
                         .arg(link.targetFilePath.fileName())
                         .arg(link.target.line)
                         .arg(link.target.column)
                         .arg(model.isGenerated(index) ? " generated" : ""));
        lines.append(drawnBy(model, index, depth + 1));
    }
    return lines;
}

// The same tree, waited for rather than slept on: update() rebuilds after a
// pause, and the reset it ends with is what says it is done.
QStringList outlineOf(const Document::Ptr &document)
{
    OutlineModel model;
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.update(document);
    if (!reset.wait(5000))
        return QStringList("the outline was never rebuilt");
    return drawnBy(model);
}

} // namespace

// The outline of one file from both models. A file's own structure is the
// question a single document settles, so the two have no excuse to differ,
// and what is compared is what the pane shows: the tree, the text of every
// row, and the line each row jumps to.
void CxxFrontendModelTest::testOutline_data()
{
    QTest::addColumn<QByteArray>("source");

    QTest::newRow("a class and its members")
        << QByteArray("class C {\n"
                      "public:\n"
                      "    C();\n"
                      "    int value() const;\n"
                      "private:\n"
                      "    static int s_count;\n"
                      "    int m_value;\n"
                      "};\n");
    QTest::newRow("namespaces") << QByteArray("namespace A {\n"
                                              "namespace B {\n"
                                              "int x;\n"
                                              "void f();\n"
                                              "}\n"
                                              "}\n");
    QTest::newRow("a namespace written without a name")
        << QByteArray("namespace {\n"
                      "int hidden;\n"
                      "struct S { void f(); };\n"
                      "}\n");
    QTest::newRow("a class template and a specialization of it")
        << QByteArray("template<typename T> struct R { void run(); };\n"
                      "template<> struct R<int> { void run(); };\n");
    QTest::newRow("an enum and a typedef") << QByteArray("enum E { First, Second };\n"
                                                         "typedef int Integer;\n"
                                                         "struct S { E kind; };\n");
    QTest::newRow("a forward declaration") << QByteArray("class Later;\n"
                                                          "Later *p;\n"
                                                          "class Later { int m; };\n");
    QTest::newRow("members a macro declared")
        << QByteArray("#define DECLARE_THINGS int fromMacro;\n"
                      "class C { DECLARE_THINGS int written; };\n");
    QTest::newRow("a function with a body") << QByteArray("int f(int arg)\n"
                                                           "{\n"
                                                           "    int local = arg;\n"
                                                           "    return local;\n"
                                                           "}\n");
}

// Why the two trees are allowed to differ on a row, or nullptr if they are
// not. Both entries are about what the models are, not about what an outline
// should draw.
static const char *knownOutlineDivergence(const QString &row)
{
    // An unscoped enumerator has the type of its enum. The built-in front end
    // gives it int, the other gives it the enum, and the other is right --
    // the same disagreement tst_cxxfrontendoverview holds open.
    if (row == "an enum and a typedef")
        return "the two disagree about the type of an enumerator";

    // A template is a symbol of its own to the built-in model, with the
    // class under it and its parameters beside it, so a class template
    // draws three rows where this model draws one. This model has no such
    // distinction -- a class template is a class that has parameters -- and
    // it writes a specialization under the arguments it is for, which is
    // what somebody wrote, where the built-in one writes "R<>".
    if (row == "a class template and a specialization of it")
        return "the built-in model makes a template a symbol of its own";

    // One symbol stands for every declaration of a class, recorded where the
    // class was first named. So a class declared above and defined below is
    // one row rather than two, and the row is at the declaration while its
    // members are under it.
    if (row == "a forward declaration")
        return "a class declared twice is one symbol to this model";

    // Both models mark what a macro declared, and disagree about where it
    // stands: this one says where the macro was written, the built-in one
    // says a line past the end of the file. Neither is ever seen, since the
    // outline filters those rows out -- see OutlineProxyModel.
    if (row == "members a macro declared")
        return "the two put a macro's declaration in different places";

    // Where a row with no name of its own takes a reader: this model sends
    // them to the brace the namespace opens with, the built-in one to the
    // start of the line, having no name token to have recorded a column of.
    // The same line either way, and nothing else in these rows differs by a
    // column -- which is why the column is compared.
    if (row == "a namespace written without a name")
        return "the two put an unnamed namespace at different columns";

    return nullptr;
}

void CxxFrontendModelTest::testOutline()
{
    QFETCH(QByteArray, source);

    const Parsed parsed({{"main.cpp", source}}, "main.cpp");
    QVERIFY(parsed.isValid());

    const Document::Ptr document
        = CppEditor::Tests::TestCase::globalSnapshot().document(parsed.mainFilePath());
    QVERIFY(document);

    // With the model, and then without it: forgetting what was kept for the
    // file is what leaves the built-in walk to draw the tree.
    const QStringList fromModel = outlineOf(document);
    forgetCxxFrontendModel(parsed.mainFilePath());
    const QStringList fromBuiltin = outlineOf(document);

    if (const char *reason = knownOutlineDivergence(QString::fromUtf8(QTest::currentDataTag())))
        QEXPECT_FAIL("", reason, Abort);
    QCOMPARE(fromModel.join('\n'), fromBuiltin.join('\n'));
}

namespace {

// What the built-in front end shows beside each thing a file declares, in
// the order it declares them: the same walk an outline makes, and the icon
// is the number Icons::iconTypeForSymbol() answers with.
void iconsOf(const CPlusPlus::Scope *scope, QStringList *shown)
{
    Overview overview;
    for (int i = 0, members = scope->memberCount(); i < members; ++i) {
        CPlusPlus::Symbol * const member = scope->memberAt(i);
        if (!member->name() || member->isGenerated())
            continue;
        shown->append(QString("%1 %2").arg(overview.prettyName(member->name()))
                          .arg(int(::CPlusPlus::Icons::iconTypeForSymbol(member))));
        if (const CPlusPlus::Scope * const nested = member->asScope())
            iconsOf(nested, shown);
    }
}

} // namespace

// The icon beside each thing a file declares, from both models.
//
// The outline above compares the tree and leaves the icon out of it -- it is
// a picture there -- so nothing compared what the two front ends make of a
// member. A slot was shown as a plain function for as long as that was so.
//
// Over sources the two list the same things for, which is what keeps this
// about the icon: which things are listed at all is the outline's question,
// and where they differ -- a template, whose parameters and whose class the
// built-in front end makes symbols of their own, or a function, whose
// arguments it holds -- that test says so.
void CxxFrontendModelTest::testIcons_data()
{
    QTest::addColumn<QByteArray>("source");

    QTest::newRow("what a class holds")
        << QByteArray("class C {\n"
                      "public:\n"
                      "    void open();\n"
                      "    static int count();\n"
                      "    int m_size;\n"
                      "    static int s_total;\n"
                      "protected:\n"
                      "    void tick();\n"
                      "private:\n"
                      "    void hide();\n"
                      "    int m_hidden;\n"
                      "};\n");

    QTest::newRow("what Qt makes of a member")
        << QByteArray("class C {\n"
                      "signals:\n"
                      "    void changed();\n"
                      "public slots:\n"
                      "    void open();\n"
                      "protected slots:\n"
                      "    void tick();\n"
                      "private slots:\n"
                      "    void hide();\n"
                      "};\n");

    QTest::newRow("a class inside a class")
        << QByteArray("class Outer {\n"
                      "public:\n"
                      "    struct Inner { int m; };\n"
                      "    enum Kind { First, Second };\n"
                      "    union Both { int i; float f; };\n"
                      "};\n");

    QTest::newRow("what a file declares around a class")
        << QByteArray("namespace N { }\n"
                      "struct S { int m; };\n"
                      "enum E { First, Second };\n"
                      "typedef int Number;\n"
                      "int counter;\n"
                      "void f();\n");
}

void CxxFrontendModelTest::testIcons()
{
    QFETCH(QByteArray, source);

    const Parsed parsed({{"main.cpp", source}}, "main.cpp");
    QVERIFY(parsed.isValid());

    const Document::Ptr document
        = CppEditor::Tests::TestCase::globalSnapshot().document(parsed.mainFilePath());
    QVERIFY(document);

    QStringList fromBuiltin;
    iconsOf(document->globalNamespace(), &fromBuiltin);

    QStringList fromModel;
    const CxxFrontendDocument * const read = parsed.mainDocument();
    QVERIFY(read);
    for (const CxxFrontendDocument::Symbol &symbol : read->symbols()) {
        if (symbol.isGenerated || symbol.name.isEmpty())
            continue;
        fromModel.append(QString("%1 %2").arg(symbol.name).arg(int(symbol.icon)));
    }

    QCOMPARE(fromModel.join('\n'), fromBuiltin.join('\n'));
}

// What every name in a file stands for, from both models. This is what the
// editor colours, and the largest thing the built-in model is still asked:
// CheckSymbols resolves every name in the file to decide it.
void CxxFrontendModelTest::testNames_data()
{
    QTest::addColumn<QByteArray>("source");

    QTest::newRow("locals and parameters") << QByteArray("\n"
                                                          "int f(int arg)\n"
                                                          "{\n"
                                                          "    int local = arg;\n"
                                                          "    return local + arg;\n"
                                                          "}\n");
    QTest::newRow("fields") << QByteArray("\n"
                                          "struct S {\n"
                                          "    int m_value;\n"
                                          "    static int s_count;\n"
                                          "    int value() const { return m_value; }\n"
                                          "};\n");
    QTest::newRow("types") << QByteArray("\n"
                                          "class C {};\n"
                                          "enum E { First };\n"
                                          "typedef int Integer;\n"
                                          "C *makeOne(Integer size, E kind);\n");
    QTest::newRow("namespaces") << QByteArray("\n"
                                               "namespace N { int x; void f(); }\n"
                                               "void g() { N::f(); }\n");
    QTest::newRow("virtual and static methods")
        << QByteArray("\n"
                      "struct B {\n"
                      "    virtual void run();\n"
                      "    static void help();\n"
                      "};\n"
                      "void use(B *b) { b->run(); B::help(); }\n");
    QTest::newRow("a method defined outside its class")
        << QByteArray("\n"
                      "struct S { int f(int a); static int s; };\n"
                      "int S::s = 0;\n"
                      "int S::f(int a) { return a + s; }\n");
    QTest::newRow("inheritance") << QByteArray("\n"
                                                "struct Base { virtual void run(); };\n"
                                                "struct Derived : Base { void run() override; };\n"
                                                "void call(Derived *d) { d->run(); }\n");
    QTest::newRow("a constructor and a destructor")
        << QByteArray("\n"
                      "class C {\n"
                      "public:\n"
                      "    C();\n"
                      "    ~C();\n"
                      "};\n"
                      "C::C() {}\n");
    QTest::newRow("a template and its use")
        << QByteArray("\n"
                      "template <class T> struct Holder { T value; };\n"
                      "int read(Holder<int> *h) { return h->value; }\n");
    QTest::newRow("a lambda") << QByteArray("\n"
                                             "void f(int outer)\n"
                                             "{\n"
                                             "    auto g = [outer](int inner) { return outer + inner; };\n"
                                             "    g(1);\n"
                                             "}\n");
    QTest::newRow("a static at file scope") << QByteArray("\n"
                                                           "static int s_counter;\n"
                                                           "int plain;\n"
                                                           "int f() { return s_counter + plain; }\n");
    QTest::newRow("labels") << QByteArray("\n"
                                          "void f(int a)\n"
                                          "{\n"
                                          "    if (a) goto out;\n"
                                          "    a = 1;\n"
                                          "out:\n"
                                          "    return;\n"
                                          "}\n");
    QTest::newRow("override and final")
        << QByteArray("\n"
                      "struct B { virtual void run(); virtual void stop(); };\n"
                      "struct D final : B {\n"
                      "    void run() override;\n"
                      "    void stop() final;\n"
                      "};\n");
    QTest::newRow("enumerators") << QByteArray("\n"
                                                "enum E { First, Second };\n"
                                                "E pick() { return Second; }\n");
}

// Why the two colour a name differently, or nullptr if they may not. Both
// entries are places where the built-in model contradicts itself.
static const char *knownNameDivergence(const QString &row)
{
    // A static member is a static field where it is declared and a plain
    // field where it is defined, because CheckSymbols reaches the two
    // through different paths -- maybeAddField sees that it is static and
    // maybeAddTypeOrStatic does not care. This model says the same thing in
    // both places.
    if (row == "a method defined outside its class")
        return "the built-in model calls a static member a field where it is defined";

    // A destructor is written down twice, once for the tilde and once for
    // the name, both at the name. The editor paints the same place twice.
    if (row == "a constructor and a destructor")
        return "the built-in model writes a destructor down twice";

    return nullptr;
}

void CxxFrontendModelTest::testNames()
{
    QFETCH(QByteArray, source);

    // Parsed here rather than through the model manager: CheckSymbols reads
    // the tree, and a document in the global snapshot has let go of it.
    const Document::Ptr builtinDocument = Document::create(FilePath::fromPathPart(u"test.cpp"));
    builtinDocument->setUtf8Source(source);
    builtinDocument->check();
    QVERIFY(builtinDocument->translationUnit() && builtinDocument->translationUnit()->ast());

    Snapshot snapshot;
    snapshot.insert(builtinDocument);
    const LookupContext context(builtinDocument, snapshot);
    QFuture<CheckSymbols::Result> future
        = CheckSymbols::go(builtinDocument, QString::fromUtf8(source), context, {});
    future.waitForFinished();

    QList<CheckSymbols::Result> results;
    for (int i = 0; i < future.resultCount(); ++i)
        results.append(future.resultAt(i));

    const CxxFrontendDocument document(QString::fromUtf8(source), "test.cpp");

    if (const char *reason = knownNameDivergence(QString::fromUtf8(QTest::currentDataTag())))
        QEXPECT_FAIL("", reason, Abort);
    QCOMPARE(colouredBy(document.namesIn()).join('\n'), colouredBy(results).join('\n'));
}

// And that the answer reaches the text. testNames says the two models agree
// on what every name is; this says the editor is coloured by it -- the
// runner hands the highlighter a future, the highlighter applies what it
// reports, and a local ends up with a format of its own.
//
// Which model answered depends on QTC_CXX_FRONTEND_MODEL, as everywhere
// else: the file has a model only where the parser was asked to keep one.
// A file that uses what a header declares, which is every real file. The
// model keeps one translation unit per file, so a name a header declared is
// not in this one -- and what that costs the colours is what this measures.
// What a file that uses its headers gets, which is what every real file is.
//
// A header is read into whoever includes it, so a type it declares is a type
// here and everything written with it is read. This measured what one
// translation unit per file used to lose -- a signature took the whole
// function with it -- and now says there is nothing lost.
void CxxFrontendModelTest::testNamesAcrossFiles()
{
    const auto found = [](const QByteArray &body) {
        const Parsed parsed({{"h.h", "struct FromHeader { int value; };\n"},
                             {"main.cpp", QByteArray("#include \"h.h\"\n") + body}},
                            "main.cpp");
        if (!parsed.isValid() || !parsed.mainDocument())
            return QStringList();
        QStringList names;
        for (const CxxFrontendDocument::Name &name : parsed.mainDocument()->namesIn())
            names.append(QString("%1:%2 %3").arg(name.line).arg(name.column).arg(nameOf(name.kind)));
        return names;
    };

    // Nothing from the header: everything is found, as in a file of its own.
    QCOMPARE(found("int f(int a)\n{\n    return a;\n}\n"),
             QStringList({"2:5 FunctionDeclaration", "2:11 Local", "4:12 Local"}));

    // A header's type in the signature, and everything is still read: the
    // header is part of this translation unit, so FromHeader is a type
    // here and h.value is a field of it.
    QCOMPARE(found("int f(FromHeader h)\n{\n    return h.value;\n}\n"),
             QStringList({"2:5 FunctionDeclaration", "2:7 Type", "2:18 Local",
                          "4:12 Local", "4:14 Field"}));

    // And in the body.
    QCOMPARE(found("int f()\n{\n    FromHeader h;\n    return h.value;\n}\n"),
             QStringList({"2:5 FunctionDeclaration", "4:5 Type", "4:16 Local",
                          "5:12 Local", "5:14 Field"}));

    // A base is read where it is written rather than resolved, so it keeps
    // its colour.
    QCOMPARE(found("struct Derived : FromHeader { int own; };\n"),
             QStringList({"2:8 Type", "2:18 Type", "2:35 Field"}));
}

void CxxFrontendModelTest::testHighlightingReachesTheEditor()
{
    CppEditor::Tests::TestCase testCase;
    QVERIFY(testCase.succeededSoFar());

    TemporaryDir dir;
    QVERIFY(dir.isValid());
    Tests::CppTestDocument testFile(
        "file.cpp",
        "int f(int arg)\n"
        "{\n"
        "    int local = arg;\n"
        "    return local;\n"
        "}\n");
    testFile.setBaseDirectory(dir.path());
    QVERIFY(testFile.writeToDisk());

    TextEditor::BaseTextEditor *editor = nullptr;
    CppEditorWidget *widget = nullptr;
    QVERIFY(CppEditor::Tests::TestCase::openCppEditor(testFile.filePath(), &editor, &widget));
    testCase.closeEditorAtEndOfTestCase(editor);
    QVERIFY(CppEditor::Tests::TestCase::waitForRehighlightedSemanticDocument(widget));

    // "local" where it is declared: line 3, nine characters in, five long.
    // The syntactic highlighter colours the keyword before it and leaves
    // the name alone, so a format there is the semantic one.
    const auto localIsColoured = [widget] {
        const QTextBlock block = widget->document()->findBlockByNumber(2);
        for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
            if (range.start == 8 && range.length == 5)
                return true;
        }
        return false;
    };
    QTRY_VERIFY(localIsColoured());
}

void CxxFrontendModelTest::testLocalUses()
{
    QFETCH(QByteArray, source);
    QFETCH(int, line);
    QFETCH(int, column);

    const Document::Ptr document = Document::create(FilePath::fromPathPart(u"test.cpp"));
    document->setUtf8Source(source);
    document->check();
    QVERIFY(document->diagnosticMessages().isEmpty());
    QVERIFY(document->translationUnit() && document->translationUnit()->ast());
    FindFirstFunctionDefinition findDefinition(document->translationUnit());
    DeclarationAST * const definition = findDefinition();
    QVERIFY(definition);

    const LocalSymbols builtIn(document, QString::fromUtf8(source), definition);
    const CxxFrontendDocument other(QString::fromUtf8(source), "test.cpp");

    if (const char *reason = knownDivergence(QString::fromUtf8(QTest::currentDataTag())))
        QEXPECT_FAIL("", reason, Abort);
    QCOMPARE(placesOf(other.localsAt(line, column)), placesOf(builtIn.uses));
}

// The index's store.
//
// What these are about is not that a reading survives being written down --
// that much a round trip shows -- but that one is *not* given back when
// anything it was read through has changed since. A store that answers with
// yesterday's reading describes code that is not there, and nothing about it
// looks wrong from the outside.

namespace {

// Two entries, nested, so that a round trip has something to lose: the
// second hangs under the first, and the fields are all different from each
// other's so that a field swapped for its neighbour shows up.
CxxFrontendIndexRead aReading()
{
    CxxFrontendIndexRead read;
    CxxFrontendIndexEntry cls;
    cls.name = "Thing";
    cls.extra = "class Thing";
    cls.scope = "ns";
    cls.itemType = int(IndexItem::Class);
    cls.line = 3;
    cls.column = 7;
    cls.icon = int(Utils::CodeModelIcon::Class);
    cls.isFunctionDefinition = false;
    cls.parent = -1;

    CxxFrontendIndexEntry fn;
    fn.name = "doIt";
    fn.extra = "(int, bool)";
    fn.scope = "ns::Thing";
    fn.itemType = int(IndexItem::Function);
    fn.line = 5;
    fn.column = 11;
    fn.icon = int(Utils::CodeModelIcon::FuncPublic);
    fn.isFunctionDefinition = true;
    fn.parent = 0;
    read.files.append({FilePath::fromUserInput("thing.cpp"), {cls, fn}});
    return read;
}

bool sameAs(const CxxFrontendIndexRead &left, const CxxFrontendIndexRead &right)
{
    if (left.files.size() != right.files.size())
        return false;
    for (int f = 0; f < left.files.size(); ++f) {
        if (left.files.at(f).filePath != right.files.at(f).filePath)
            return false;
        if (left.files.at(f).entries.size() != right.files.at(f).entries.size())
            return false;
        for (int i = 0; i < left.files.at(f).entries.size(); ++i) {
            const CxxFrontendIndexEntry &a = left.files.at(f).entries.at(i);
            const CxxFrontendIndexEntry &b = right.files.at(f).entries.at(i);
        if (a.name != b.name || a.extra != b.extra || a.scope != b.scope
            || a.itemType != b.itemType || a.line != b.line || a.column != b.column
            || a.icon != b.icon || a.isFunctionDefinition != b.isFunctionDefinition
                || a.parent != b.parent) {
                return false;
            }
        }
    }
    return left.includedFiles == right.includedFiles;
}

// A store of its own in \a dir, so that a row neither reads nor writes the
// one the running Qt Creator keeps for real projects.
class StoreFixture
{
public:
    StoreFixture()
        : source(dir.createFile("thing.cpp", "#include \"thing.h\"\nint x;\n"))
        , header(dir.createFile("thing.h", "struct Thing {};\n"))
        , cache(std::make_unique<CxxFrontendIndexCache>(QStringList{"FOO 1"},
                                                        dir.filePath() / "store"))
    {
        read = aReading();
        read.includedFiles = QStringList{header.toFSPathString()};
    }

    // Puts the reading in and hands back a store that has not looked at any
    // file yet, which is what a later session is.
    void storeAndReopen(const QStringList &macros = {"FOO 1"})
    {
        cache->store(source, "projectkey", read);
        cache = std::make_unique<CxxFrontendIndexCache>(macros, dir.filePath() / "store");
    }

    TemporaryDir dir;
    Utils::FilePath source;
    Utils::FilePath header;
    CxxFrontendIndexRead read;
    std::unique_ptr<CxxFrontendIndexCache> cache;
};

} // namespace

void CxxFrontendModelTest::testTheStoreGivesBackWhatWasPutIn()
{
    StoreFixture f;
    QVERIFY(f.dir.isValid());
    f.storeAndReopen();

    const std::optional<CxxFrontendIndexRead> back = f.cache->take(f.source, "projectkey");
    QVERIFY(back);
    QVERIFY(sameAs(*back, f.read));
    QCOMPARE(f.cache->hits(), 1);

    // And a tree built from it is walked the way the locator walks one, the
    // nested entry reached by recursing rather than sitting beside its
    // class. Asked through visitAllChildren because that is the only way a
    // consumer sees an entry at all.
    const IndexItem::Ptr root = cxxFrontendIndexTreeFrom(back->files.first());
    QVERIFY(root);
    QStringList walked;
    root->visitAllChildren([&walked](const IndexItem::Ptr &item) {
        walked << item->scopedSymbolName();
        return IndexItem::Recurse;
    });
    QCOMPARE(walked, QStringList({"ns::Thing", "ns::Thing::doIt"}));

    // Stopping at the class reaches neither what is in it nor anything
    // beside it, which is what says the second really does hang under the
    // first rather than being a second child of the file.
    QStringList stopped;
    root->visitAllChildren([&stopped](const IndexItem::Ptr &item) {
        stopped << item->scopedSymbolName();
        return IndexItem::Continue;
    });
    QCOMPARE(stopped, QStringList({"ns::Thing"}));
}

void CxxFrontendModelTest::testTheStoreForgetsWhenTheFileChanges()
{
    StoreFixture f;
    QVERIFY(f.dir.isValid());
    f.storeAndReopen();

    QVERIFY(f.source.writeFileContents("#include \"thing.h\"\nint x;\nint y;\n"));
    QVERIFY(!f.cache->take(f.source, "projectkey"));
    QCOMPARE(f.cache->misses(), 1);
}

void CxxFrontendModelTest::testTheStoreForgetsWhenAnIncludedFileChanges()
{
    StoreFixture f;
    QVERIFY(f.dir.isValid());
    f.storeAndReopen();

    // The file itself is untouched. What changed is what it was read
    // through, and the entries are as much a reading of that.
    QVERIFY(f.header.writeFileContents("struct Thing { int extra; };\n"));
    QVERIFY(!f.cache->take(f.source, "projectkey"));
}

void CxxFrontendModelTest::testTheStoreForgetsWhenTheProjectChanges()
{
    StoreFixture f;
    QVERIFY(f.dir.isValid());
    f.storeAndReopen();

    // Nothing on disk changed. The header paths an include is looked up
    // along did, which can make the same line read a different file.
    QVERIFY(!f.cache->take(f.source, "anotherprojectkey"));
}

void CxxFrontendModelTest::testTheStoreForgetsWhenTheDefinesChange()
{
    StoreFixture f;
    QVERIFY(f.dir.isValid());
    f.storeAndReopen({"FOO 2"});

    QVERIFY(!f.cache->take(f.source, "projectkey"));
}

void CxxFrontendModelTest::testTheStoreDeclinesAFileItNeverHad()
{
    StoreFixture f;
    QVERIFY(f.dir.isValid());
    f.storeAndReopen();

    QVERIFY(!f.cache->take(f.dir.filePath() / "never-seen.cpp", "projectkey"));
}

} // namespace CppEditor::Internal
