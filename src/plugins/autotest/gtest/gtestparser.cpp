// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gtestparser.h"

#include "gtesttreeitem.h"
#include "gtestvisitors.h"
#include "gtest_utils.h"

#include <cppeditor/cppcodemodelqueries.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/projectpart.h>

#include <QPromise>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

using namespace Utils;

namespace Autotest::Internal {

TestTreeItem *GTestParseResult::createTestTreeItem() const
{
    if (itemType != TestTreeItem::TestSuite && itemType != TestTreeItem::TestCase)
        return nullptr;
    GTestTreeItem *item = new GTestTreeItem(framework, name, fileName, itemType);
    item->setProFile(proFile);
    item->setLine(line);
    item->setColumn(column);

    if (parameterized)
        item->setState(GTestTreeItem::Parameterized);
    if (typed)
        item->setState(GTestTreeItem::Typed);
    if (disabled)
        item->setState(GTestTreeItem::Disabled);
    for (const TestParseResult *testSet : children)
        item->appendChild(testSet->createTestTreeItem());
    return item;
}

static bool includesGTest(const CPlusPlus::Document::Ptr &doc,
                          const CPlusPlus::Snapshot &snapshot,
                          const CppEditor::CodeModelQueries &queries)
{
    static const QString gtestH("gtest/gtest.h");
    for (const CPlusPlus::Document::Include &inc : doc->resolvedIncludes()) {
        if (inc.resolvedFileName().endsWith(gtestH))
            return true;
    }

    // Asked only where what the file writes itself did not say so: off the
    // cxx front end this reads the file and the headers it reaches.
    for (const FilePath &include : queries.includeClosureOf(doc->filePath())) {
        if (include.path().endsWith(gtestH))
            return true;
    }

    return CppParser::precompiledHeaderContains(snapshot, doc->filePath(), gtestH);
}

// Whether the file writes one of GTest's macros with the two arguments a
// test is declared by. Read off the file's own tokens: which macro a file
// used is what its text says, and a reading of it says no more.
static bool hasGTestNames(const CppEditor::CodeModelQueries &queries, const FilePath &fileName)
{
    for (const CppEditor::CodeModelQueries::WrittenMacroUse &use
         : queries.macroUsesIn(fileName, GTestUtils::macroNames())) {
        if (use.arguments.size() == 2)
            return true;
    }
    return false;
}

bool GTestParser::processDocument(QPromise<TestParseResultPtr> &promise,
                                  const FilePath &fileName)
{
    CPlusPlus::Document::Ptr doc = document(fileName);
    if (doc.isNull())
        return false;

    // One reading for every question asked about this file.
    const CppEditor::CodeModelQueries queries(m_cppSnapshot, m_workingCopy);
    if (!includesGTest(doc, m_cppSnapshot, queries))
        return false;

    const QByteArray &fileContent = getFileContent(fileName);
    if (!hasGTestNames(queries, fileName)) {
        static const QRegularExpression regex("\\b(TEST(_[FP])?|TYPED_TEST(_P)?|(GTEST_TEST))");
        if (!regex.match(QString::fromUtf8(fileContent)).hasMatch())
            return false;
    }

    const FilePath filePath = doc->filePath();
    CPlusPlus::Document::Ptr document = m_cppSnapshot.preprocessedDocument(fileContent, fileName, false);
    document->check();
    CPlusPlus::AST *ast = document->translationUnit()->ast();
    GTestVisitor visitor(document);
    visitor.accept(ast);

    const QMap<GTestCaseSpec, GTestCodeLocationList> result = visitor.gtestFunctions();
    FilePath proFile;
    const QList<CppEditor::ProjectPart::ConstPtr> &ppList =
        CppEditor::CppModelManager::projectPart(filePath);
    if (!ppList.isEmpty())
        proFile = ppList.first()->projectFile;
    else
        return false; // happens if shutting down while parsing

    for (auto it = result.cbegin(); it != result.cend(); ++it) {
        const GTestCaseSpec &testSpec = it.key();
        GTestParseResult *parseResult = new GTestParseResult(framework());
        parseResult->itemType = TestTreeItem::TestSuite;
        parseResult->fileName = fileName;
        parseResult->name = testSpec.testCaseName;
        parseResult->parameterized = testSpec.parameterized;
        parseResult->typed = testSpec.typed;
        parseResult->disabled = testSpec.disabled;
        parseResult->proFile = proFile;

        for (const GTestCodeLocationAndType &location : it.value()) {
            GTestParseResult *testSet = new GTestParseResult(framework());
            testSet->name = location.m_name;
            testSet->fileName = fileName;
            testSet->line = location.m_line;
            testSet->column = location.m_column;
            testSet->disabled = location.m_state & GTestTreeItem::Disabled;
            testSet->itemType = location.m_type;
            testSet->proFile = proFile;

            parseResult->children.append(testSet);
        }

        promise.addResult(TestParseResultPtr(parseResult));
    }
    return !result.isEmpty();
}

} // namespace Autotest::Internal
