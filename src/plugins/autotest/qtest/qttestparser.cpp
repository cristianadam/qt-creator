// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qttestparser.h"


#include <cppeditor/cppcodemodelqueries.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/projectpart.h>

#include <cplusplus/SimpleLexer.h>

#include <utils/algorithm.h>

#include <QPromise>
#include <QRegularExpressionMatchIterator>

using namespace Utils;

namespace Autotest::Internal {

TestTreeItem *QtTestParseResult::createTestTreeItem() const
{
    if (itemType == TestTreeItem::Root)
        return nullptr;

    QtTestTreeItem *item = new QtTestTreeItem(framework, displayName, fileName, itemType);
    item->setProFile(proFile);
    item->setLine(line);
    item->setColumn(column);
    item->setInherited(m_inherited);
    item->setRunsMultipleTestcases(m_multiTest);

    for (const TestParseResult *funcParseResult : children)
        item->appendChild(funcParseResult->createTestTreeItem());
    return item;
}

static bool includesQtTest(const CPlusPlus::Document::Ptr &doc, const CPlusPlus::Snapshot &snapshot)
{
    static QStringList expectedHeaderPrefixes = HostOsInfo::isMacHost()
            ? QStringList({"QtTest.framework/Headers", "QtTest"}) : QStringList({"QtTest"});

    const QList<CPlusPlus::Document::Include> includes = doc->resolvedIncludes();

    for (const CPlusPlus::Document::Include &inc : includes) {
        // TODO this short cut works only for #include <QtTest>
        // bad, as there could be much more different approaches
        if (inc.unresolvedFileName() == QString("QtTest")) {
            for (const QString &prefix : expectedHeaderPrefixes) {
                if (inc.resolvedFileName().endsWith(QString("%1/QtTest").arg(prefix)))
                    return true;
            }
        }
    }

    const QSet<FilePath> allIncludes = snapshot.allIncludesForDocument(doc->filePath());
    for (const FilePath &include : allIncludes) {
        for (const QString &prefix : expectedHeaderPrefixes) {
        if (include.pathView().endsWith(QString("%1/qtest.h").arg(prefix)))
            return true;
        }
    }

    for (const QString &prefix : expectedHeaderPrefixes) {
        if (CppParser::precompiledHeaderContains(snapshot,
                                                 doc->filePath(),
                                                 QString("%1/qtest.h").arg(prefix))) {
            return true;
        }
    }
    return false;
}

static bool qtTestLibDefined(const FilePath &fileName)
{
    const QList<CppEditor::ProjectPart::ConstPtr> parts =
            CppEditor::CppModelManager::projectPart(fileName);
    if (!parts.isEmpty()) {
        return Utils::anyOf(parts.at(0)->projectMacros, [](const ProjectExplorer::Macro &macro) {
            return macro.key == "QT_TESTLIB_LIB";
        });
    }
    return false;
}

// Whether the match stands inside a #define. A macro written to stand in for
// one of these ("#define APP_TEST_MAIN(C) QTEST_MAIN(C)") names no class of
// its own -- its parameter is not one -- so what is written there is not an
// answer. Where such a macro is *used* is, and wrapperMacrosIn() below is how
// that is followed.
//
// A definition may be continued over as many lines as it likes, and what says
// it is a definition stands on the first of them, so the backslashes are
// followed back.
static bool insideADefine(const QString &text, int start)
{
    if (start == 0)
        return false;

    int lineStart = text.lastIndexOf(u'\n', start - 1) + 1;
    while (lineStart >= 2 && text.at(lineStart - 2) == u'\\') {
        if (lineStart < 3) {
            lineStart = 0;
            break;
        }
        lineStart = text.lastIndexOf(u'\n', lineStart - 3) + 1;
    }

    const QStringView inFront = QStringView(text).mid(lineStart, start - lineStart).trimmed();
    return inFront.startsWith(u'#') && inFront.sliced(1).trimmed().startsWith(u"define");
}

// The macros a file defines in terms of one of these. Reading the text as
// written rather than preprocessed lost these: a file that writes
// "#define TST_MAIN(Class) QTEST_MAIN(Class)" and then uses it had the use
// expanded in the preprocessed source, so the class was found there. The
// definition is read here and the use is read off the code model, which knows
// the uses of a macro properly.
QStringList wrapperMacrosIn(const QString &text)
{
    static const QRegularExpression define(
        "^[ \\t]*#[ \\t]*define[ \\t]+(\\w+)[ \\t]*\\("
        "(?:[^\\n]|\\\\\\n)*?"
        "\\bQTEST_(?:APPLESS_|GUILESS_)?MAIN\\b",
        QRegularExpression::MultilineOption);

    QStringList names;
    QRegularExpressionMatchIterator it = define.globalMatch(text);
    while (it.hasNext())
        names << it.next().captured(1);
    return names;
}

// The last word on which class a file's test runs, for a file whose
// QTEST_MAIN was never defined -- the macro is Qt's, and a file read without
// it has nothing but the text left to say so.
//
// What is searched is the file's own text, so a macro written in a branch
// this configuration does not build is found too: it still says which class
// the file names, which is the question being asked. (The preprocessed
// source was searched before, and left such a file out of the tree.)
TestCases mainsWrittenIn(const QString &text)
{
    // \w rather than [[:alnum:]], which leaves out the underscore: the name a
    // Qt test is conventionally written under has one (tst_Simple), so this
    // never matched the usual spelling at all.
    static const QRegularExpression regex("\\b(QTEST_(APPLESS_|GUILESS_)?MAIN)"
                                          "\\s*\\(\\s*(\\w+)\\s*\\)");

    // Where the comments stand, since a macro written inside one runs
    // nothing. Whichever scanner is installed answers.
    CPlusPlus::SimpleLexer lexer;
    lexer.setSkipComments(false);
    QList<std::pair<int, int>> comments;
    for (const CPlusPlus::Token &token : lexer(text)) {
        if (token.isComment())
            comments.append({token.utf16charsBegin(), token.utf16charsEnd()});
    }

    TestCases result;
    QRegularExpressionMatchIterator it = regex.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int start = match.capturedStart(1);
        const int end = match.capturedEnd(1);

        const bool commentedOut = anyOf(comments, [start, end](const std::pair<int, int> &comment) {
            return comment.first <= start && comment.second > end;
        });
        if (commentedOut) // don't treat commented out macros as active
            continue;

        if (insideADefine(text, start))
            continue;

        const QString className = match.captured(3);
        if (!Utils::anyOf(result, [&className](const TestCase &already) {
                return already.name == className;
            })) {
            result.append({className, false});
        }
    }

    // One name is one test however many times it is written: a file that
    // names the same class in two branches of an #ifdef -- which reading the
    // text as written rather than one configuration of it makes possible --
    // runs one test, and saying otherwise takes the checkbox off it and makes
    // it unrunnable. Where the names really do differ, none of them is the
    // only test the executable runs, which is what the qExec() reading above
    // says of what it finds.
    if (result.size() > 1) {
        for (TestCase &testCase : result)
            testCase.multipleTestCases = true;
    }
    return result;
}

TestCases QtTestParser::testCases(const FilePath &filePath) const
{
    if (CppEditor::CppModelManager::document(filePath).isNull())
        return {};

    const CppEditor::CodeModelQueries queries(m_cppSnapshot, m_workingCopy);

    // A QTEST_MAIN-family macro says which class the test runs, and what it
    // says is the text it was handed: the macro's own definition is Qt's,
    // and expanding it says nothing about the class.
    for (const CppEditor::CodeModelQueries::WrittenMacroUse &use
         : queries.macroUsesIn(filePath)) {
        if (QTestUtils::isQTestMacro(use.name.toUtf8()) && !use.arguments.isEmpty())
            return { {use.arguments.first(), false} };
    }

    // check if one has used a self-defined macro or QTest::qExec() directly
    const QStringList handedOver = queries.classesPassedTo(filePath, "QTest::qExec");
    if (!handedOver.isEmpty()) {
        const bool several = handedOver.size() > 1;
        return Utils::transform(handedOver, [several](const QString &className) {
            return TestCase{className, several};
        });
    }

    // Read only here: where a macro use or a qExec() call said which class
    // runs, the text never has to be looked at.
    const QString text = QString::fromUtf8(getFileContent(filePath));

    // A macro the file wrote to stand for one of Qt's: the definition says
    // which macro that is, and the code model says where it was used and what
    // it was handed. Preprocessing used to do both at once.
    const QStringList wrappers = wrapperMacrosIn(text);
    if (!wrappers.isEmpty()) {
        for (const CppEditor::CodeModelQueries::WrittenMacroUse &use
             : queries.macroUsesIn(filePath)) {
            if (wrappers.contains(use.name) && !use.arguments.isEmpty())
                return { {use.arguments.first(), false} };
        }
    }

    return mainsWrittenIn(text);
}

static QSet<FilePath> filesWithDataFunctionDefinitions(
            const QMap<QString, QtTestCodeLocationAndType> &testFunctions)
{
    QSet<FilePath> result;
    QMap<QString, QtTestCodeLocationAndType>::ConstIterator it = testFunctions.begin();
    const QMap<QString, QtTestCodeLocationAndType>::ConstIterator end = testFunctions.end();

    for ( ; it != end; ++it) {
        const QString &key = it.key();
        if (key.endsWith("_data") && testFunctions.contains(key.left(key.size() - 5)))
            result.insert(it.value().m_filePath);
    }
    return result;
}

QHash<QString, QtTestCodeLocationList> QtTestParser::checkForDataTags(
        const FilePath &fileName) const
{
    static const QString dataSuffix("_data");
    const CppEditor::CodeModelQueries queries(m_cppSnapshot, m_workingCopy);

    QHash<QString, QtTestCodeLocationList> dataTags;
    for (const CppEditor::CodeModelQueries::WrittenCall &call
         : queries.callsTo(fileName, {"QTest::newRow", "QTest::addRow"})) {
        // Only what a data function writes, and the tags belong to the test
        // function it goes with rather than to itself.
        if (!call.insideFunction.endsWith(dataSuffix))
            continue;

        // The tag is what the first argument says; a call handed anything
        // else says nothing about it.
        const QString tag = call.arguments.value(0);
        if (tag.isEmpty())
            continue;

        // A tag put together out of a format string is not a tag anybody
        // can be sent to: what it will say is not written down anywhere.
        if (tag.contains('%') && call.arguments.size() > 1)
            continue;

        QtTestCodeLocationAndType locationAndType;
        locationAndType.m_name = tag;
        locationAndType.m_line = call.line;
        locationAndType.m_column = call.column - 1; // the tree counts from zero
        locationAndType.m_type = TestTreeItem::TestDataTag;
        dataTags[call.insideFunction.left(call.insideFunction.size() - dataSuffix.size())]
            .append(locationAndType);
    }
    return dataTags;
}

/*!
 * \brief Checks whether \a testFunctions (keys are full qualified names) contains already the
 * given \a function (unqualified name).
 *
 * \return true if this function is already contained, false otherwise
 */
static bool containsFunction(const QMap<QString, QtTestCodeLocationAndType> &testFunctions,
                             const QString &function)
{
    const QString search = "::" + function;
    return Utils::anyOf(testFunctions.keys(), [&search](const QString &key) {
        return key.endsWith(search);
    });
}

static void mergeTestFunctions(QMap<QString, QtTestCodeLocationAndType> &testFunctions,
                               const QMap<QString, QtTestCodeLocationAndType> &inheritedFunctions)
{
    static const QString dataSuffix("_data");
    // take over only inherited test functions that have not been re-implemented
    QMap<QString, QtTestCodeLocationAndType>::ConstIterator it = inheritedFunctions.begin();
    QMap<QString, QtTestCodeLocationAndType>::ConstIterator end = inheritedFunctions.end();
    for ( ; it != end; ++it) {
        const QString functionName = it.key();
        const QString &shortName = functionName.mid(functionName.lastIndexOf(':') + 1);
        if (shortName.endsWith(dataSuffix)) {
            const QString &correspondingFunc = functionName.left(functionName.size()
                                                                 - dataSuffix.size());
            // inherited test data functions only if we're inheriting the corresponding test
            // function as well (and the inherited test function is not omitted)
            if (inheritedFunctions.contains(correspondingFunc)) {
                if (!testFunctions.contains(correspondingFunc))
                    continue;
                testFunctions.insert(functionName, it.value());
            }
        } else if (!containsFunction(testFunctions, shortName)) {
            // normal test functions only if not re-implemented
            testFunctions.insert(functionName, it.value());
        }
    }
}

// What the test tree keeps of each private slot the class declares: where
// it is and which of the three kinds of test function it is, which is what
// its name says.
static QMap<QString, QtTestCodeLocationAndType> testFunctionsOf(
        const CppEditor::CodeModelQueries &queries,
        const QString &className,
        const QList<CppEditor::WrittenFunction> &privateSlots,
        bool inherited)
{
    static const QStringList specialFunctions{"initTestCase", "cleanupTestCase",
                                              "init", "cleanup"};
    QMap<QString, QtTestCodeLocationAndType> functions;
    for (const CppEditor::WrittenFunction &slot : privateSlots) {
        QtTestCodeLocationAndType locationAndType;
        locationAndType.m_filePath = slot.filePath;
        locationAndType.m_line = slot.line;
        locationAndType.m_column = slot.column - 1; // the tree counts them from zero

        if (slot.name.endsWith("_data")) {
            // Costly, but the data tags are written where the function is
            // defined rather than where it is declared, and that is the
            // entry a reader of the tags needs.
            const Link definition = queries.definitionOfFunctionAt(slot.filePath, slot.line,
                                                                   slot.column);
            if (definition.hasValidTarget()) {
                locationAndType.m_filePath = definition.targetFilePath;
                locationAndType.m_line = definition.target.line;
                locationAndType.m_column = definition.target.column;
            }
            locationAndType.m_type = TestTreeItem::TestDataFunction;
        } else if (specialFunctions.contains(slot.name)) {
            locationAndType.m_type = TestTreeItem::TestSpecialFunction;
        } else {
            locationAndType.m_type = TestTreeItem::TestFunction;
        }

        locationAndType.m_inherited = inherited;
        locationAndType.m_name = className + "::" + slot.name;
        functions.insert(locationAndType.m_name, locationAndType);
    }
    return functions;
}

static void fetchAndMergeBaseTestFunctions(const CppEditor::CodeModelQueries &queries,
                                           const QStringList &baseClasses,
                                           QMap<QString, QtTestCodeLocationAndType> &testFunctions,
                                           const FilePath &filePath)
{
    QStringList bases = baseClasses;
    QSet<QString> seen;
    while (!bases.empty()) {
        const QString base = bases.takeFirst();
        if (base == "QObject" || !Utils::insert(seen, base))
            continue;
        const CppEditor::CodeModelQueries::ClassWithPrivateSlots found
                = queries.classWithPrivateSlots(filePath, base);
        if (!found.klass.isValid())
            continue;
        bases.append(found.baseClasses);
        mergeTestFunctions(testFunctions,
                           testFunctionsOf(queries, base, found.privateSlots, true));
    }
}

static QtTestCodeLocationList tagLocationsFor(const QtTestParseResult *func,
                                              const QHash<QString, QtTestCodeLocationList> &dataTags)
{
    if (!func->inherited())
        return dataTags.value(func->name);

    QHash<QString, QtTestCodeLocationList>::ConstIterator it = dataTags.begin();
    QHash<QString, QtTestCodeLocationList>::ConstIterator end = dataTags.end();
    const int lastColon = func->name.lastIndexOf(':');
    QString funcName = lastColon == -1 ? func->name : func->name.mid(lastColon - 1);
    for ( ; it != end; ++it) {
        if (it.key().endsWith(funcName))
            return it.value();
    }
    return QtTestCodeLocationList();
}

static bool isQObject(const FilePath &file)
{
    return (HostOsInfo::isMacHost() && file.endsWith("QtCore.framework/Headers/qobject.h"))
            || file.endsWith("QtCore/qobject.h")  || file.endsWith("kernel/qobject.h");
}

bool QtTestParser::processDocument(QPromise<TestParseResultPtr> &promise,
                                   const FilePath &fileName)
{
    if (!m_prefilteredFiles.contains(fileName))
        return false;

    CPlusPlus::Document::Ptr doc = document(fileName);
    if (doc.isNull())
        return false;
    const TestCases &oldTestCases = m_testCases.value(fileName);
    if ((!includesQtTest(doc, m_cppSnapshot) || !qtTestLibDefined(fileName))
        && oldTestCases.isEmpty()) {
        return false;
    }

    TestCases testCaseList(testCases(fileName));
    bool reported = false;
    // we might be in a reparse without the original entry point with the QTest::qExec()
    if (testCaseList.isEmpty() && !oldTestCases.empty())
        testCaseList.append(oldTestCases);
    for (const TestCase &testCase : std::as_const(testCaseList)) {
        if (!testCase.name.isEmpty()) {
            TestCaseData data;
            std::optional<bool> earlyReturn = fillTestCaseData(testCase.name, doc, data);
            if (earlyReturn.has_value() || !data.valid)
                continue;

            QList<CppEditor::ProjectPart::ConstPtr> projectParts
                    = CppEditor::CppModelManager::projectPart(fileName);
            if (projectParts.isEmpty()) // happens if shutting down while parsing
                return false;

            data.multipleTestCases = testCase.multipleTestCases;
            QtTestParseResult *parseResult
                    = createParseResult(testCase.name, data, projectParts.first()->projectFile);
            promise.addResult(TestParseResultPtr(parseResult));
            reported = true;
        }
    }
    return reported;
}

std::optional<bool> QtTestParser::fillTestCaseData(
        const QString &testCaseName, const CPlusPlus::Document::Ptr &doc,
        TestCaseData &data) const
{
    // One reading for the class and every base of it: reading a file is what
    // this costs, and a hierarchy means asking about the same files again.
    const CppEditor::CodeModelQueries queries(m_cppSnapshot, m_workingCopy);

    // The file that names the class, or one of the files it was found named
    // in before -- a test class is declared in a header and named from a
    // source file, and either may be the one being parsed.
    FilePath namedIn = doc->filePath();
    CppEditor::CodeModelQueries::ClassWithPrivateSlots found
            = queries.classWithPrivateSlots(namedIn, testCaseName);
    if (!found.klass.isValid()) {
        const FilePaths &alternativeFiles = m_alternativeFiles.values(doc->filePath());
        for (const FilePath &alternativeFile : alternativeFiles) {
            found = queries.classWithPrivateSlots(alternativeFile, testCaseName);
            if (found.klass.isValid()) {
                namedIn = alternativeFile;
                break;
            }
        }
    }
    if (!found.klass.isValid())
        return false;

    data.line = found.klass.line;
    data.column = found.klass.column - 1; // the tree counts them from zero

    data.testFunctions = testFunctionsOf(queries, testCaseName, found.privateSlots, false);
    // gather appropriate information of base classes as well and merge into already found
    // functions - but only as far as QtTest can handle this appropriate
    fetchAndMergeBaseTestFunctions(queries, found.baseClasses, data.testFunctions, namedIn);

    // handle tests that are not runnable without more information (plugin unit test of QC)
    if (data.testFunctions.isEmpty() && testCaseName == "QObject"
        && isQObject(found.klass.filePath)) {
        return true; // we did not handle it, but we do not expect any test defined there either
    }

    const QSet<FilePath> &files = filesWithDataFunctionDefinitions(data.testFunctions);
    for (const FilePath &file : files)
        Utils::addToHash(&(data.dataTags), checkForDataTags(file));

    data.fileName = found.klass.filePath;
    data.valid = true;
    return std::optional<bool>();
}

QtTestParseResult *QtTestParser::createParseResult(
    const QString &testCaseName, const TestCaseData &data, const FilePath &projectFile) const
{
    QtTestParseResult *parseResult = new QtTestParseResult(framework());
    parseResult->itemType = TestTreeItem::TestCase;
    parseResult->fileName = data.fileName;
    parseResult->name = testCaseName;
    parseResult->displayName = testCaseName;
    parseResult->line = data.line;
    parseResult->column = data.column;
    parseResult->proFile = projectFile;
    parseResult->setRunsMultipleTestcases(data.multipleTestCases);
    QMap<QString, QtTestCodeLocationAndType>::ConstIterator it = data.testFunctions.begin();
    const QMap<QString, QtTestCodeLocationAndType>::ConstIterator end = data.testFunctions.end();

    for ( ; it != end; ++it) {
        const QtTestCodeLocationAndType &location = it.value();
        QtTestParseResult *func = new QtTestParseResult(framework());
        func->itemType = location.m_type;
        func->name = location.m_name;
        func->displayName = location.m_name.mid(location.m_name.lastIndexOf(':') + 1);
        func->fileName = location.m_filePath;
        func->line = location.m_line;
        func->column = location.m_column;
        func->setInherited(location.m_inherited);
        func->setRunsMultipleTestcases(data.multipleTestCases);

        const QtTestCodeLocationList &tagLocations = tagLocationsFor(func, data.dataTags);
        for (const QtTestCodeLocationAndType &tag : tagLocations) {
            QtTestParseResult *dataTag = new QtTestParseResult(framework());
            dataTag->itemType = tag.m_type;
            dataTag->name = tag.m_name;
            dataTag->displayName = tag.m_name;
            dataTag->fileName = data.testFunctions.value(it.key() + "_data").m_filePath;
            dataTag->line = tag.m_line;
            dataTag->column = tag.m_column;
            dataTag->setInherited(tag.m_inherited);
            dataTag->setRunsMultipleTestcases(data.multipleTestCases);

            func->children.append(dataTag);
        }
        parseResult->children.append(func);
    }
    return parseResult;
}

void QtTestParser::init(const QSet<FilePath> &filesToParse, bool fullParse)
{
    if (!fullParse) { // in a full parse cached information might lead to wrong results
        m_testCases = QTestUtils::testCaseNamesForFiles(framework(), filesToParse);
        m_alternativeFiles = QTestUtils::alternativeFiles(framework(), filesToParse);
    }

    if (std::optional<QSet<Utils::FilePath>> prefiltered = filesContainingMacro("QT_TESTLIB_LIB"))
        m_prefilteredFiles = prefiltered->intersect(filesToParse);
    else
        m_prefilteredFiles = filesToParse;

    CppParser::init(filesToParse, fullParse);
}

void QtTestParser::release()
{
    m_testCases.clear();
    m_alternativeFiles.clear();
    m_prefilteredFiles.clear();
    CppParser::release();
}

} // namespace Autotest::Internal
