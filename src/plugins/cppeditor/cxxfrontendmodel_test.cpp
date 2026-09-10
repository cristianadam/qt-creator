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
#include "cxxfrontendmodel.h"

#include <cplusplus/ASTVisitor.h>
#include <cplusplus/AST.h>
#include <cplusplus/CxxFrontendDocument.h>
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
    // A name from a header is not one of them: the header is read into this
    // file, so the model does answer, and what with is in
    // testResolvesANameDeclaredInAnInclude.
    QVERIFY(cxxFrontendFollowSymbol(parsed.mainFilePath(), 2, 11, 0, 0).hasValidTarget());
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
// a ternary, which are punctuation; a label; and the Qt keywords, which are
// macros before the parser sees them.
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
        lines.append(QString("%1%2 @%3 ->%4:%5%6")
                         .arg(QString(depth * 2, ' '),
                              index.data(Qt::DisplayRole).toString())
                         .arg(model.positionFromIndex(index).line)
                         .arg(link.targetFilePath.fileName())
                         .arg(link.target.line)
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

} // namespace CppEditor::Internal
