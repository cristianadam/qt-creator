// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "boosttestparser.h"

#include "boostcodeparser.h"
#include "boosttesttreeitem.h"

#include <cppeditor/cppcodemodelqueries.h>
#include <cppeditor/cppmodelmanager.h>

#include <QMap>
#include <QPromise>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

using namespace Utils;

namespace Autotest::Internal {

namespace BoostTestUtils {
static const QStringList relevant = {
    QStringLiteral("BOOST_AUTO_TEST_CASE"), QStringLiteral("BOOST_TEST_CASE"),
    QStringLiteral("BOOST_DATA_TEST_CASE"), QStringLiteral("BOOST_FIXTURE_TEST_CASE"),
    QStringLiteral("BOOST_PARAM_TEST_CASE"), QStringLiteral("BOOST_DATA_TEST_CASE_F"),
    QStringLiteral("BOOST_AUTO_TEST_CASE_TEMPLATE"),
    QStringLiteral("BOOST_FIXTURE_TEST_CASE_TEMPLATE"),
};

static QStringList macroNames()
{
    return relevant;
}
} // BoostTestUtils

TestTreeItem *BoostTestParseResult::createTestTreeItem() const
{
    if (itemType == TestTreeItem::Root)
        return nullptr;

    BoostTestTreeItem *item = new BoostTestTreeItem(framework, displayName, fileName, itemType);
    item->setProFile(proFile);
    item->setLine(line);
    item->setColumn(column);
    item->setStates(state);
    item->setFullName(name);

    for (const TestParseResult *funcParseResult : children)
        item->appendChild(funcParseResult->createTestTreeItem());
    return item;
}


static bool includesBoostTest(const FilePath &filePath,
                              const CppEditor::CodeModelQueries &queries)
{
    static const QRegularExpression boostTestHpp("^.*/boost/test/.*\\.hpp$");
    for (const FilePath &include : queries.includeClosureOf(filePath)) {
        if (boostTestHpp.match(include.path()).hasMatch())
            return true;
    }

    return CppParser::precompiledHeaderContains(queries, filePath, boostTestHpp);
}

// Whether the file writes one of Boost's test macros. Read off the file's
// own tokens: which macro a file used is what its text says.
static bool hasBoostTestMacros(const CppEditor::CodeModelQueries &queries,
                               const FilePath &fileName)
{
    return !queries.macroUsesIn(fileName, BoostTestUtils::macroNames()).isEmpty();
}

static BoostTestParseResult *createParseResult(const QString &name, const FilePath &filePath,
                                               const FilePath &projectFile,
                                               ITestFramework *framework,
                                               TestTreeItem::Type type, const BoostTestInfo &info)
{
    BoostTestParseResult *partialSuite = new BoostTestParseResult(framework);
    partialSuite->itemType = type;
    partialSuite->fileName = filePath;
    partialSuite->name = info.fullName;
    partialSuite->displayName = name;
    partialSuite->line = info.line;
    partialSuite->column = 0;
    partialSuite->proFile = projectFile;
    partialSuite->state = info.state;
    return partialSuite;

}

bool BoostTestParser::processDocument(QPromise<TestParseResultPtr> &promise,
                                      const FilePath &fileName)
{
    if (!selectedForBuilding(fileName))
        return false;

    // One reading for every question asked about this file.
    const CppEditor::CodeModelQueries queries(m_cppSnapshot, m_workingCopy);
    if (!includesBoostTest(fileName, queries)
        || !hasBoostTestMacros(queries, fileName)) {
        return false;
    }

    // What a suite is called is read off what the file's macros resolve to,
    // which is the one question here that still wants a translation unit.
    // Asked last, so a file that names no Boost test at all costs nothing.
    const CPlusPlus::Document::Ptr doc = document(fileName);
    if (doc.isNull())
        return false;

    const QList<CppEditor::ProjectPart::ConstPtr> projectParts
            = CppEditor::CppModelManager::projectPart(fileName);
    if (projectParts.isEmpty()) // happens if shutting down while parsing
        return false;
    const CppEditor::ProjectPart::ConstPtr projectPart = projectParts.first();
    const FilePath &projectFile = projectPart->projectFile;
    const QByteArray &fileContent = getFileContent(fileName);

    BoostCodeParser codeParser(fileContent, projectPart->languageFeatures, doc, m_cppSnapshot);
    const BoostTestCodeLocationList foundTests = codeParser.findTests();
    if (foundTests.isEmpty())
        return false;

    for (const BoostTestCodeLocationAndType &locationAndType : foundTests) {
        BoostTestInfoList suitesStates = locationAndType.m_suitesState;
        BoostTestInfo firstSuite = suitesStates.first();
        QStringList suites = firstSuite.fullName.split('/');
        BoostTestParseResult *topLevelSuite = createParseResult(suites.first(), fileName,
                                                                projectFile, framework(),
                                                                TestTreeItem::TestSuite,
                                                                firstSuite);
        BoostTestParseResult *currentSuite = topLevelSuite;
        suitesStates.removeFirst();
        while (!suitesStates.isEmpty()) {
            firstSuite = suitesStates.first();
            suites = firstSuite.fullName.split('/');
            BoostTestParseResult *suiteResult = createParseResult(suites.last(), fileName,
                                                                  projectFile, framework(),
                                                                  TestTreeItem::TestSuite,
                                                                  firstSuite);
            currentSuite->children.append(suiteResult);
            suitesStates.removeFirst();
            currentSuite = suiteResult;
        }

        if (currentSuite) {
            BoostTestInfo tmpInfo{
                locationAndType.m_suitesState.last().fullName + "::" + locationAndType.m_name,
                        locationAndType.m_state, locationAndType.m_line};
            BoostTestParseResult *funcResult = createParseResult(locationAndType.m_name, fileName,
                                                                 projectFile, framework(),
                                                                 locationAndType.m_type,
                                                                 tmpInfo);
            currentSuite->children.append(funcResult);
            promise.addResult(TestParseResultPtr(topLevelSuite));
        }
    }
    return true;
}

} // namespace Autotest::Internal
