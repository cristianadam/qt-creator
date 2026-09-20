// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpplocatorfilter_test.h"

#include "cppeditorwidget.h"
#include "cpplocatordata.h"
#include "cpptoolstestcase.h"
#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"
#endif

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/locator/locatorfiltertest.h>

#include <utils/environment.h>

#include <QDebug>
#include <QTest>

using namespace Core;
using namespace Core::Tests;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

const bool debug = qtcEnvironmentVariable("QTC_DEBUG_CPPLOCATORFILTERTESTCASE") == "1";

// Whether what a file declares is being read off the cxx-frontend model,
// which says some of it differently -- see the rows below.
bool onTheCxxFrontendModel()
{
#ifdef QTC_WITH_CXX_FRONTEND
    return cxxFrontendModelRequested();
#else
    return false;
#endif
}

static FilePath dataDir(const QString &subdir)
{
    return FilePath::fromUserInput(SRCDIR "/../../../tests/cpplocators/" + subdir);
}

class CppLocatorFilterTestCase : public CppEditor::Tests::TestCase
{
public:
    CppLocatorFilterTestCase(const LocatorMatcherTasks &matchers,
                             const FilePath &filePath,
                             const QString &searchText,
                             const ResultDataList &expectedResults)
    {
        QVERIFY(succeededSoFar());
        QVERIFY(!filePath.isEmpty());
        QVERIFY(garbageCollectGlobalSnapshot());

        QVERIFY(parseFiles({filePath}));

        // The cxx front end reads a file on a pool, so what it declares
        // reaches the index after the parse rather than during it. Waited
        // for on the count of reads still owed, not on a duration: without
        // this the row races that read and compares the built-in walk's
        // entries against what this model says. Zero at once where the model
        // is off, nothing having been queued.
        QTRY_VERIFY_WITH_TIMEOUT(CppModelManager::locatorData()->cxxFrontendFilesOutstanding() == 0,
                                 30000);

        const LocatorFilterEntries entries = LocatorMatcher::runBlocking(matchers, searchText);
        QVERIFY(garbageCollectGlobalSnapshot());
        const ResultDataList results = ResultData::fromFilterEntryList(entries);
        if (debug) {
            ResultData::printFilterEntries(expectedResults, "Expected:");
            ResultData::printFilterEntries(results, "Results:");
        }
        QVERIFY(!results.isEmpty());
        QCOMPARE(results, expectedResults);
    }
};

class CppCurrentDocumentFilterTestCase : public CppEditor::Tests::TestCase
{
public:
    CppCurrentDocumentFilterTestCase(const FilePath &filePath,
                                     const LocatorMatcherTasks &matchers,
                                     const ResultDataList &expectedResults,
                                     const QString &searchText = QString())
    {
        LocatorFilterEntries entries;
        run(filePath, matchers, searchText, &entries);
        const ResultDataList results = ResultData::fromFilterEntryList(entries);
        if (debug) {
            ResultData::printFilterEntries(expectedResults, "Expected:");
            ResultData::printFilterEntries(results, "Results:");
        }
        QVERIFY(!results.isEmpty());
        QCOMPARE(results, expectedResults);
    }

    // The entries themselves, for a test that asks where one takes the
    // reader rather than what it says.
    CppCurrentDocumentFilterTestCase(const FilePath &filePath,
                                     const LocatorMatcherTasks &matchers,
                                     LocatorFilterEntries *entries)
    {
        run(filePath, matchers, {}, entries);
    }

private:
    void run(const FilePath &filePath, const LocatorMatcherTasks &matchers,
             const QString &searchText, LocatorFilterEntries *entries)
    {
        QVERIFY(succeededSoFar());
        QVERIFY(!filePath.isEmpty());

        QVERIFY(DocumentModel::openedDocuments().isEmpty());
        QVERIFY(garbageCollectGlobalSnapshot());

        const auto editor = EditorManager::openEditor(filePath);
        QVERIFY(editor);

        QVERIFY(waitForFileInGlobalSnapshot(filePath));

        // The filter reads what the editor's own parse of this document
        // settled, so wait for that parse rather than for the file turning
        // up in the snapshot, which the indexer also brings about.
        const auto widget = qobject_cast<CppEditorWidget *>(editor->widget());
        QVERIFY(widget);
        QVERIFY(waitForRehighlightedSemanticDocument(widget));
        *entries = LocatorMatcher::runBlocking(matchers, searchText);
        QVERIFY(closeEditorWithoutGarbageCollectorInvocation(editor));
        QCoreApplication::processEvents();
        QVERIFY(DocumentModel::openedDocuments().isEmpty());
        QVERIFY(garbageCollectGlobalSnapshot());
    }
};

} // anonymous namespace

void LocatorFilterTest::testLocatorFilter()
{
    QFETCH(FilePath, testFile);
    QFETCH(MatcherType, matcherType);
    QFETCH(QString, searchText);
    QFETCH(ResultDataList, expectedResults);

    Tests::VerifyCleanCppModelManager verify;
    CppLocatorFilterTestCase(LocatorMatcher::matchers(matcherType), testFile, searchText,
                             expectedResults);
}

void LocatorFilterTest::testLocatorFilter_data()
{
    QTest::addColumn<FilePath>("testFile");
    QTest::addColumn<MatcherType>("matcherType");
    QTest::addColumn<QString>("searchText");
    QTest::addColumn<ResultDataList>("expectedResults");

    const FilePath testDirectory = dataDir("testdata_basic");
    QVERIFY(testDirectory.exists());
    FilePath testFile = testDirectory / "file1.cpp";
    QString p = testFile.path();
    p[0] = p[0].toLower(); // Ensure Windows path sorts after scope names.
    testFile = testFile.withNewPath(p);
    const FilePath objTestFile = testDirectory / "file1.mm";
    const QString testFileShort = testFile.shortNativePath();
    const QString objTestFileShort = objTestFile.shortNativePath();

    QTest::newRow("CppFunctionsFilter")
        << testFile
        << MatcherType::Functions
        << "function"
        << ResultDataList{
               ResultData("functionDefinedInClass(bool, int)",
                          "<anonymous namespace>::MyClass (file1.cpp)"),
               ResultData("functionDefinedInClass(bool, int)", "MyClass (file1.cpp)"),
               ResultData("functionDefinedInClass(bool, int)",
                          "MyNamespace::MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClass(char)",
                          "<anonymous namespace>::MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClass(char)", "MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClass(char)",
                          "MyNamespace::MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClassAndNamespace(float)",
                          "MyNamespace::MyClass (file1.cpp)"),
               ResultData("myFunction(bool, int)", "<anonymous namespace> (file1.cpp)"),
               ResultData("myFunction(bool, int)", "MyNamespace (file1.cpp)"),
               ResultData("myFunction(bool, int)", testFileShort)
           };

    QTest::newRow("CppFunctionsFilter-Sorting")
        << testFile
        << MatcherType::Functions
        << "pos"
        << ResultDataList{
               ResultData("positiveNumber()", testFileShort),
               ResultData("somePositionWithin()", testFileShort),
               ResultData("pointOfService()", testFileShort),
               ResultData("matchArgument(Pos)", testFileShort)
           };

    QTest::newRow("CppFunctionsFilter-arguments")
        << testFile
        << MatcherType::Functions
        << "function*bool"
        << ResultDataList{
               ResultData("functionDefinedInClass(bool, int)",
                          "<anonymous namespace>::MyClass (file1.cpp)"),
               ResultData("functionDefinedInClass(bool, int)",
                          "MyClass (file1.cpp)"),
               ResultData("functionDefinedInClass(bool, int)",
                          "MyNamespace::MyClass (file1.cpp)"),
               ResultData("myFunction(bool, int)", "<anonymous namespace> (file1.cpp)"),
               ResultData("myFunction(bool, int)", "MyNamespace (file1.cpp)"),
               ResultData("myFunction(bool, int)", testFileShort)
           };

    QTest::newRow("CppFunctionsFilter-WithNamespacePrefix")
        << testFile
        << MatcherType::Functions
        << "mynamespace::"
        << ResultDataList{
               ResultData("MyClass()", "MyNamespace::MyClass (file1.cpp)"),
               ResultData("functionDefinedInClass(bool, int)",
                          "MyNamespace::MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClass(char)",
                          "MyNamespace::MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClassAndNamespace(float)",
                          "MyNamespace::MyClass (file1.cpp)"),
               ResultData("myFunction(bool, int)", "MyNamespace (file1.cpp)"),
           };

    QTest::newRow("CppFunctionsFilter-WithClassPrefix")
        << testFile
        << MatcherType::Functions
        << "MyClass::func"
        << ResultDataList{
               ResultData("functionDefinedInClass(bool, int)",
                          "<anonymous namespace>::MyClass (file1.cpp)"),
               ResultData("functionDefinedInClass(bool, int)",
                          "MyClass (file1.cpp)"),
               ResultData("functionDefinedInClass(bool, int)",
                          "MyNamespace::MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClass(char)",
                          "<anonymous namespace>::MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClass(char)",
                          "MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClass(char)",
                          "MyNamespace::MyClass (file1.cpp)"),
               ResultData("functionDefinedOutSideClassAndNamespace(float)",
                          "MyNamespace::MyClass (file1.cpp)"),
           };

    QTest::newRow("CppClassesFilter")
        << testFile
        << MatcherType::Classes
        << "myclass"
        << ResultDataList{
               ResultData("MyClass", "<anonymous namespace> (file1.cpp)"),
               ResultData("MyClass", "MyNamespace (file1.cpp)"),
               ResultData("MyClass", testFileShort),
               ResultData("Runner<MyNamespace::MyClass>", "<anonymous namespace> (file1.cpp)"),
           };

    QTest::newRow("CppClassesFilter-WithNamespacePrefix")
        << testFile
        << MatcherType::Classes
        << "mynamespace::"
        << ResultDataList{
               ResultData("Runner<MyNamespace::MyClass>", "<anonymous namespace> (file1.cpp)"),
               ResultData("MyClass", "MyNamespace (file1.cpp)"),
           };

    // all symbols in the left column are expected to be fully qualified.
    QTest::newRow("CppLocatorFilter-filtered")
        << testFile
        << MatcherType::AllSymbols
        << "my"
        << ResultDataList{
               ResultData("MyClass", "<anonymous namespace> (file1.cpp)"),
               ResultData("MyClass", "MyNamespace (file1.cpp)"),
               ResultData("MyClass", testFileShort),
               ResultData("MyClass()", "<anonymous namespace>::MyClass (file1.cpp)"),
               ResultData("MyClass()", "MyClass (file1.cpp)"),
               ResultData("MyClass()", "MyNamespace::MyClass (file1.cpp)"),
               ResultData("MyEnum", "<anonymous namespace> (file1.cpp)"),
               ResultData("MyEnum", "MyNamespace (file1.cpp)"),
               ResultData("MyEnum", testFileShort),
               ResultData("myFunction(bool, int)", "<anonymous namespace> (file1.cpp)"),
               ResultData("myFunction(bool, int)", "MyNamespace (file1.cpp)"),
               ResultData("myFunction(bool, int)", testFileShort),
               ResultData("Runner<MyNamespace::MyClass>", "<anonymous namespace> (file1.cpp)"),
           };

    QTest::newRow("CppClassesFilter-ObjC")
        << objTestFile
        << MatcherType::Classes
        << "M"
        << ResultDataList{
               ResultData("MyClass", objTestFileShort),
               ResultData("MyClass", objTestFileShort),
               ResultData("MyClass", objTestFileShort),
               ResultData("MyProtocol", objTestFileShort),
           };

    QTest::newRow("CppFunctionsFilter-ObjC")
        << objTestFile
        << MatcherType::Functions
        << "M"
        << ResultDataList{
               ResultData("anotherMethod", "MyClass (file1.mm)"),
               ResultData("anotherMethod:", "MyClass (file1.mm)"),
               ResultData("someMethod", "MyClass (file1.mm)")
           };
}

void LocatorFilterTest::testCurrentDocumentFilter()
{
    QFETCH(FilePath, testFile);
    QFETCH(ResultDataList, expectedResults);
    QFETCH(ResultDataList, onTheModel);

    QVERIFY(testFile.exists());

    Tests::VerifyCleanCppModelManager verify;
    CppCurrentDocumentFilterTestCase(
        testFile, LocatorMatcher::matchers(MatcherType::CurrentDocumentSymbols),
        onTheCxxFrontendModel() ? onTheModel : expectedResults);
}

void LocatorFilterTest::testCurrentDocumentFilter_data()
{
    QTest::addColumn<FilePath>("testFile");
    QTest::addColumn<ResultDataList>("expectedResults");
    // What the same file reads as on the cxx-frontend model. The two lists
    // are almost the same list; the comment above each row says where they
    // part company and why, and everything else has to agree exactly.
    QTest::addColumn<ResultDataList>("onTheModel");

    const FilePath testDirectory = dataDir("testdata_basic");

    // The two models disagree about an enumerator's type: the built-in one
    // says int, this one says the enumeration, and this one is right.
    QTest::newRow("namespaces-classes-and-functions")
        << testDirectory / "file1.cpp"
        << ResultDataList{
            ResultData("int myVariable", ""),
            ResultData("myFunction(bool, int)", ""),
            ResultData("Pos", ""),
            ResultData("somePositionWithin()", ""),
            ResultData("pointOfService()", ""),
            ResultData("matchArgument(Pos)", ""),
            ResultData("positiveNumber()", ""),
            ResultData("MyEnum", ""),
            ResultData("int V1", "MyEnum"),
            ResultData("int V2", "MyEnum"),
            ResultData("MyClass", ""),
            ResultData("MyClass()", "MyClass"),
            ResultData("functionDeclaredOnly()", "MyClass"),
            ResultData("functionDefinedInClass(bool, int)", "MyClass"),
            ResultData("functionDefinedOutSideClass(char)", "MyClass"),
            ResultData("int myVariable", "MyNamespace"),
            ResultData("myFunction(bool, int)", "MyNamespace"),
            ResultData("MyEnum", "MyNamespace"),
            ResultData("int V1", "MyNamespace::MyEnum"),
            ResultData("int V2", "MyNamespace::MyEnum"),
            ResultData("MyClass", "MyNamespace"),
            ResultData("MyClass()", "MyNamespace::MyClass"),
            ResultData("functionDeclaredOnly()", "MyNamespace::MyClass"),
            ResultData("functionDefinedInClass(bool, int)", "MyNamespace::MyClass"),
            ResultData("functionDefinedOutSideClass(char)", "MyNamespace::MyClass"),
            ResultData("functionDefinedOutSideClassAndNamespace(float)",
                       "MyNamespace::MyClass"),
            ResultData("int myVariable", "<anonymous namespace>"),
            ResultData("myFunction(bool, int)", "<anonymous namespace>"),
            ResultData("MyEnum", "<anonymous namespace>"),
            ResultData("int V1", "<anonymous namespace>::MyEnum"),
            ResultData("int V2", "<anonymous namespace>::MyEnum"),
            ResultData("MyClass", "<anonymous namespace>"),
            ResultData("MyClass()", "<anonymous namespace>::MyClass"),
            ResultData("functionDeclaredOnly()", "<anonymous namespace>::MyClass"),
            ResultData("functionDefinedInClass(bool, int)", "<anonymous namespace>::MyClass"),
            ResultData("functionDefinedOutSideClass(char)", "<anonymous namespace>::MyClass"),
            ResultData("Runner", "<anonymous namespace>"),
            ResultData("run()", "<anonymous namespace>::Runner"),
            ResultData("Runner<MyNamespace::MyClass>", "<anonymous namespace>"),
            ResultData("run()", "<anonymous namespace>::Runner<MyNamespace::MyClass>"),
            ResultData("main()", ""),
        }
        << ResultDataList{
            ResultData("int myVariable", ""),
            ResultData("myFunction(bool, int)", ""),
            ResultData("Pos", ""),
            ResultData("somePositionWithin()", ""),
            ResultData("pointOfService()", ""),
            ResultData("matchArgument(Pos)", ""),
            ResultData("positiveNumber()", ""),
            ResultData("MyEnum", ""),
            ResultData("MyEnum V1", "MyEnum"),
            ResultData("MyEnum V2", "MyEnum"),
            ResultData("MyClass", ""),
            ResultData("MyClass()", "MyClass"),
            ResultData("functionDeclaredOnly()", "MyClass"),
            ResultData("functionDefinedInClass(bool, int)", "MyClass"),
            ResultData("functionDefinedOutSideClass(char)", "MyClass"),
            ResultData("int myVariable", "MyNamespace"),
            ResultData("myFunction(bool, int)", "MyNamespace"),
            ResultData("MyEnum", "MyNamespace"),
            ResultData("MyEnum V1", "MyNamespace::MyEnum"),
            ResultData("MyEnum V2", "MyNamespace::MyEnum"),
            ResultData("MyClass", "MyNamespace"),
            ResultData("MyClass()", "MyNamespace::MyClass"),
            ResultData("functionDeclaredOnly()", "MyNamespace::MyClass"),
            ResultData("functionDefinedInClass(bool, int)", "MyNamespace::MyClass"),
            ResultData("functionDefinedOutSideClass(char)", "MyNamespace::MyClass"),
            ResultData("functionDefinedOutSideClassAndNamespace(float)",
                       "MyNamespace::MyClass"),
            ResultData("int myVariable", "<anonymous namespace>"),
            ResultData("myFunction(bool, int)", "<anonymous namespace>"),
            ResultData("MyEnum", "<anonymous namespace>"),
            ResultData("MyEnum V1", "<anonymous namespace>::MyEnum"),
            ResultData("MyEnum V2", "<anonymous namespace>::MyEnum"),
            ResultData("MyClass", "<anonymous namespace>"),
            ResultData("MyClass()", "<anonymous namespace>::MyClass"),
            ResultData("functionDeclaredOnly()", "<anonymous namespace>::MyClass"),
            ResultData("functionDefinedInClass(bool, int)", "<anonymous namespace>::MyClass"),
            ResultData("functionDefinedOutSideClass(char)", "<anonymous namespace>::MyClass"),
            ResultData("Runner", "<anonymous namespace>"),
            ResultData("run()", "<anonymous namespace>::Runner"),
            ResultData("Runner<MyNamespace::MyClass>", "<anonymous namespace>"),
            ResultData("run()", "<anonymous namespace>::Runner<MyNamespace::MyClass>"),
            ResultData("main()", ""),
        };

    // A function written apart from its declaration is listed once, at its
    // definition -- the filter drops the declaration of a function it has
    // exactly one definition of, which is why staticFunction() and ~Outer()
    // stand where the file defines them rather than where the class declares
    // them. A variable is not deduplicated that way, so Outer::staticVariable
    // is listed both times.
    //
    // Three things the cxx-frontend model says differently here, each on
    // purpose:
    //   - it holds a thing once, where it is declared, so the definition of
    //     Outer::staticVariable is no second entry and the members stand in
    //     the order the class declares them;
    //   - an enumerator has the type of its enumeration, not int, and this
    //     model is the one that is right;
    //   - an alias is printed as what it resolves to (int for MyAlias), and
    //     a type is printed without the scopes around it, which is what an
    //     outline wants of the same answer.
    QTest::newRow("aliases-nested-types-and-members")
        << testDirectory / "file2.cpp"
        << ResultDataList{
            ResultData("int MyTypedef", ""),
            ResultData("MyTypedef MyAlias", ""),
            ResultData("int declaredVariable", ""),
            ResultData("Outer", ""),
            ResultData("Inner", "Outer"),
            ResultData("int innerField", "Outer::Inner"),
            ResultData("ScopedEnum", "Outer"),
            ResultData("int First", "Outer::ScopedEnum"),
            ResultData("int Second", "Outer::ScopedEnum"),
            ResultData("int staticVariable", "Outer"),
            ResultData("int field", "Outer"),
            ResultData("operator+(int) const", "Outer"),
            ResultData("int Outer::staticVariable", ""),
            ResultData("staticFunction(int)", "Outer"),
            ResultData("~Outer()", "Outer"),
            ResultData("MyUnion", ""),
            ResultData("int asInt", "MyUnion"),
            ResultData("float asFloat", "MyUnion"),
            ResultData("templateFunction(T)", ""),
            ResultData("Outer::Inner AliasInNamespace", "MyOtherNamespace"),
            ResultData("Outer::ScopedEnum TypedefInNamespace", "MyOtherNamespace"),
        }
        << ResultDataList{
            ResultData("int MyTypedef", ""),
            ResultData("int MyAlias", ""),
            ResultData("int declaredVariable", ""),
            ResultData("Outer", ""),
            ResultData("Inner", "Outer"),
            ResultData("int innerField", "Outer::Inner"),
            ResultData("ScopedEnum", "Outer"),
            ResultData("ScopedEnum First", "Outer::ScopedEnum"),
            ResultData("ScopedEnum Second", "Outer::ScopedEnum"),
            ResultData("int staticVariable", "Outer"),
            ResultData("int field", "Outer"),
            ResultData("staticFunction(int)", "Outer"),
            ResultData("operator+(int) const", "Outer"),
            ResultData("~Outer()", "Outer"),
            ResultData("MyUnion", ""),
            ResultData("int asInt", "MyUnion"),
            ResultData("float asFloat", "MyUnion"),
            ResultData("templateFunction(T)", ""),
            ResultData("Inner AliasInNamespace", "MyOtherNamespace"),
            ResultData("ScopedEnum TypedefInNamespace", "MyOtherNamespace"),
        };
}

// Where an entry takes the reader, which the lists above do not say. The
// two models point at different ends of a function written apart from its
// declaration: the built-in reading has both places and the filter keeps
// the definition, while this model holds a function once, where the class
// declares it.
void LocatorFilterTest::testCurrentDocumentFilterLinks()
{
    const FilePath testFile = dataDir("testdata_basic") / "file2.cpp";
    QVERIFY(testFile.exists());

    LocatorFilterEntries entries;
    Tests::VerifyCleanCppModelManager verify;
    CppCurrentDocumentFilterTestCase(
        testFile, LocatorMatcher::matchers(MatcherType::CurrentDocumentSymbols), &entries);
    QVERIFY(!entries.isEmpty());

    const auto placeOf = [&entries](const QString &displayName) {
        for (const LocatorFilterEntry &entry : std::as_const(entries)) {
            if (entry.displayName != displayName || !entry.linkForEditor)
                continue;
            const Link &link = *entry.linkForEditor;
            return QString("%1:%2").arg(link.target.line).arg(link.target.column);
        }
        return QString("nothing named " + displayName);
    };

    // A class, which either model has once and in one place.
    QCOMPARE(placeOf("Outer"), QString("13:7"));

    // A destructor stands under this model where its tilde is written and
    // under the built-in one where the name after it begins, so the two
    // differ by a character as well as by which end they point at.
    if (onTheCxxFrontendModel()) {
        QCOMPARE(placeOf("staticFunction(int)"), QString("25:15"));
        QCOMPARE(placeOf("~Outer()"), QString("27:12"));
    } else {
        QCOMPARE(placeOf("staticFunction(int)"), QString("32:11"));
        QCOMPARE(placeOf("~Outer()"), QString("34:8"));
    }
}

void LocatorFilterTest::testCurrentDocumentHighlighting()
{
    const FilePath testDirectory = dataDir("testdata_basic");
    const FilePath testFile = testDirectory / "file1.cpp";
    QVERIFY(testFile.exists());

    const QString searchText = "pos";
    const ResultDataList expectedResults{
        ResultData("Pos", "",
                   "~~~"),
        ResultData("pointOfService()", "",
                   "~    ~ ~        "),
        ResultData("positiveNumber()", "",
                   "~~~             "),
        ResultData("somePositionWithin()", "",
                   "    ~~~             "),
        ResultData("matchArgument(Pos)", "",
                   "              ~~~ ")
       };

    Tests::VerifyCleanCppModelManager verify;
    CppCurrentDocumentFilterTestCase(testFile,
        LocatorMatcher::matchers(MatcherType::CurrentDocumentSymbols), expectedResults, searchText);
}

void LocatorFilterTest::testFunctionsFilterHighlighting()
{
    const FilePath testDirectory = dataDir("testdata_basic");
    const FilePath testFile = testDirectory / "file1.cpp";
    const QString testFileShort = testFile.shortNativePath();

    const QString searchText = "pos";
    const ResultDataList expectedResults{
        ResultData("positiveNumber()", testFileShort,
                   "~~~             "),
        ResultData("somePositionWithin()", testFileShort,
                   "    ~~~             "),
        ResultData("pointOfService()", testFileShort,
                   "~    ~ ~        "),
        ResultData("matchArgument(Pos)", testFileShort,
                   "              ~~~ ")
       };

    Tests::VerifyCleanCppModelManager verify;
    CppLocatorFilterTestCase(LocatorMatcher::matchers(MatcherType::Functions), testFile,
                             searchText, expectedResults);
}

} // namespace CppEditor::Internal
