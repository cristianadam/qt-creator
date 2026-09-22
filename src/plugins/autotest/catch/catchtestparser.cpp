// Copyright (C) 2019 Jochen Seemann
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "catchtestparser.h"

#include "catchcodeparser.h"
#include "catchtreeitem.h"

#include <cppeditor/cppcodemodelqueries.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/projectpart.h>

#include <QPromise>
#include <QRegularExpression>

using namespace Utils;

namespace Autotest::Internal {

static const QStringList validTestCaseMacros = {
    QStringLiteral("TEST_CASE"), QStringLiteral("SCENARIO"),
    QStringLiteral("TEMPLATE_TEST_CASE"), QStringLiteral("TEMPLATE_PRODUCT_TEST_CASE"),
    QStringLiteral("TEMPLATE_LIST_TEST_CASE"),
    QStringLiteral("TEMPLATE_TEST_CASE_SIG"), QStringLiteral("TEMPLATE_PRODUCT_TEST_CASE_SIG"),
    QStringLiteral("TEST_CASE_METHOD"), QStringLiteral("TEMPLATE_TEST_CASE_METHOD"),
    QStringLiteral("TEMPLATE_PRODUCT_TEST_CASE_METHOD"),
    QStringLiteral("TEST_CASE_METHOD"),
    QStringLiteral("SCENARIO_METHOD"),
    QStringLiteral("TEMPLATE_TEST_CASE_METHOD_SIG"),
    QStringLiteral("TEMPLATE_PRODUCT_TEST_CASE_METHOD_SIG"),
    QStringLiteral("TEMPLATE_TEST_CASE_METHOD"),
    QStringLiteral("TEMPLATE_LIST_TEST_CASE_METHOD"),
    QStringLiteral("METHOD_AS_TEST_CASE"), QStringLiteral("REGISTER_TEST_CASE")
};

static const QStringList validSectionMacros = {
    QStringLiteral("SECTION"), QStringLiteral("WHEN")
};

// Every name one of these may be written under: Catch defines each of them
// twice, once bare and once behind a CATCH_ of its own, and a file may use
// either.
static QStringList catchMacroNames()
{
    QStringList names;
    for (const QString &macro : validTestCaseMacros + validSectionMacros)
        names << macro << "CATCH_" + macro;
    names.removeDuplicates();
    return names;
}

static bool includesCatchHeader(const FilePath &filePath,
                                const CppEditor::CodeModelQueries &queries)
{
    static const QStringList catchHeaders{"catch.hpp", // v2
                                          "catch_all.hpp", // v3 - new approach
                                          "catch_amalgamated.hpp",
                                          "catch_test_macros.hpp",
                                          "catch_template_test_macros.hpp"
                                         };
    for (const FilePath &include : queries.includeClosureOf(filePath)) {
        for (const QString &catchHeader : catchHeaders) {
            if (include.endsWith(catchHeader))
                return true;
        }
    }

    for (const QString &catchHeader : catchHeaders) {
        if (CppParser::precompiledHeaderContains(queries, filePath, catchHeader))
            return true;
    }
    return false;
}

// Whether the file writes one of Catch's macros with something in its
// parentheses. Read off the file's own tokens, a use with no arguments not
// being among what those report.
static bool hasCatchNames(const CppEditor::CodeModelQueries &queries, const FilePath &fileName)
{
    return !queries.macroUsesIn(fileName, catchMacroNames()).isEmpty();
}

bool CatchTestParser::processDocument(QPromise<TestParseResultPtr> &promise,
                                      const FilePath &fileName)
{
    if (!selectedForBuilding(fileName))
        return false;

    // One reading for every question asked about this file.
    const CppEditor::CodeModelQueries queries(m_cppSnapshot, m_workingCopy);
    if (!includesCatchHeader(fileName, queries))
        return false;

    const QString &filePath = fileName.toUserOutput();
    const QByteArray &fileContent = getFileContent(fileName);

    if (!hasCatchNames(queries, fileName)) {
        static const QRegularExpression regex("\\b(CATCH_)?"
                                              "(SCENARIO(_METHOD)?|(TEMPLATE_(PRODUCT_)?)?TEST_CASE(_METHOD)?|"
                                              "TEMPLATE_TEST_CASE(_METHOD)?_SIG|"
                                              "TEMPLATE_PRODUCT_TEST_CASE(_METHOD)?_SIG|"
                                              "TEMPLATE_LIST_TEST_CASE_METHOD|METHOD_AS_TEST_CASE|"
                                              "REGISTER_TEST_CASE)");
        if (!regex.match(QString::fromUtf8(fileContent)).hasMatch())
            return false;
    }


    const QList<CppEditor::ProjectPart::ConstPtr> projectParts
        = CppEditor::CppModelManager::projectPart(fileName);
    if (projectParts.isEmpty()) // happens if shutting down while parsing
        return false;
    FilePath proFile;
    const CppEditor::ProjectPart::ConstPtr projectPart = projectParts.first();
    proFile = projectPart->projectFile;

    CatchCodeParser codeParser(fileContent, projectPart->languageFeatures);
    const CatchTestCodeLocationList foundTests = codeParser.findTests();

    CatchParseResult *parseResult = new CatchParseResult(framework());
    parseResult->itemType = TestTreeItem::TestSuite;
    parseResult->fileName = fileName;
    parseResult->name = filePath;
    parseResult->displayName = filePath;
    parseResult->proFile = proFile;

    for (const CatchTestCodeLocationAndType & testLocation : foundTests) {
        CatchParseResult *testCase = new CatchParseResult(framework());
        testCase->fileName = fileName;
        testCase->name = testLocation.m_name;
        testCase->proFile = proFile;
        testCase->itemType = testLocation.m_type;
        testCase->line = testLocation.m_line;
        testCase->column = testLocation.m_column;
        testCase->states = testLocation.states;

        parseResult->children.append(testCase);
    }

    promise.addResult(TestParseResultPtr(parseResult));

    return !foundTests.isEmpty();
}

TestTreeItem *CatchParseResult::createTestTreeItem() const
{
    if (itemType == TestTreeItem::Root)
        return nullptr;

    CatchTreeItem *item = new CatchTreeItem(framework, name, fileName, itemType);
    item->setProFile(proFile);
    item->setLine(line);
    item->setColumn(column);
    item->setStates(states);

    for (const TestParseResult *testSet : children)
        item->appendChild(testSet->createTestTreeItem());

    return item;
}

} // namespace Autotest::Internal
