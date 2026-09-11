// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpplocatorfilter_test.h"

#include "cpptoolstestcase.h"

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
        QVERIFY(succeededSoFar());
        QVERIFY(!filePath.isEmpty());

        QVERIFY(DocumentModel::openedDocuments().isEmpty());
        QVERIFY(garbageCollectGlobalSnapshot());

        const auto editor = EditorManager::openEditor(filePath);
        QVERIFY(editor);

        QVERIFY(waitForFileInGlobalSnapshot(filePath));
        const LocatorFilterEntries entries = LocatorMatcher::runBlocking(matchers, searchText);
        QVERIFY(closeEditorWithoutGarbageCollectorInvocation(editor));
        QCoreApplication::processEvents();
        QVERIFY(DocumentModel::openedDocuments().isEmpty());
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

    QVERIFY(testFile.exists());

    Tests::VerifyCleanCppModelManager verify;
    CppCurrentDocumentFilterTestCase(
        testFile, LocatorMatcher::matchers(MatcherType::CurrentDocumentSymbols), expectedResults);
}

void LocatorFilterTest::testCurrentDocumentFilter_data()
{
    QTest::addColumn<FilePath>("testFile");
    QTest::addColumn<ResultDataList>("expectedResults");

    const FilePath testDirectory = dataDir("testdata_basic");

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
        };

    // A function written apart from its declaration is listed once, at its
    // definition -- the filter drops the declaration of a function it has
    // exactly one definition of, which is why staticFunction() and ~Outer()
    // stand where the file defines them rather than where the class declares
    // them. A variable is not deduplicated that way, so Outer::staticVariable
    // is listed both times.
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
        };
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
