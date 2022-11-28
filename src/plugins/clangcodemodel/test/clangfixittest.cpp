// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "clangfixittest.h"

#include "../clangfixitoperation.h"

#include <utils/changeset.h>

#include <QFile>
#include <QtTest>
#include <QVector>

using namespace Utils;

namespace ClangCodeModel::Internal::Tests {

static QString qrcPath(const QString &relativeFilePath)
{
    return QLatin1String(":/unittests/ClangCodeModel/") + relativeFilePath;
}

static QString diagnosticText() { return QString("expected ';' at end of declaration"); }

void ClangFixItTest::testDescription()
{
    ClangFixItOperation operation(diagnosticText(), {semicolonFixIt()});
    QCOMPARE(operation.description(),
             QLatin1String("Apply Fix: expected ';' at end of declaration"));
}

FilePath ClangFixItTest::semicolonFilePath() const
{
    return m_dataDir->absolutePath("diagnostic_semicolon_fixit.cpp");
}

FilePath ClangFixItTest::compareFilePath() const
{
    return m_dataDir->absolutePath("diagnostic_comparison_fixit.cpp");
}

QString ClangFixItTest::fileContent(const QString &relFilePath) const
{
    const FilePath file = m_dataDir->absolutePath(relFilePath);
    const std::optional<QByteArray> result = file.fileContents();
    if (!result) {
        qDebug() << "File with the unsaved content cannot be opened!";
        return {};
    }
    return QString::fromUtf8(*result);
}

ClangFixIt ClangFixItTest::semicolonFixIt() const
{
    return {";", {{semicolonFilePath(), 3u, 12u}, {semicolonFilePath(), 3u, 12u}}};
}

void ClangFixItTest::init()
{
    m_dataDir.reset(new CppEditor::Tests::TemporaryCopiedDir(qrcPath("fixits")));
}

void ClangFixItTest::testAppendSemicolon()
{
    ClangFixItOperation operation(diagnosticText(), {semicolonFixIt()});
    operation.perform();
    QCOMPARE(operation.firstRefactoringFileContent_forTestOnly(),
             fileContent("diagnostic_semicolon_fixit_expected.cpp"));
}

void ClangFixItTest::testComparisonVersusAssignmentChooseComparison()
{
    const ClangFixIt compareFixIt{"==", {{compareFilePath(), 4u, 10u},
            {compareFilePath(), 4u, 11u}}};
    ClangFixItOperation operation(diagnosticText(), {compareFixIt});
    operation.perform();
    QCOMPARE(operation.firstRefactoringFileContent_forTestOnly(),
             fileContent("diagnostic_comparison_fixit_expected1.cpp"));
}

void ClangFixItTest::testComparisonVersusAssignmentChooseParentheses()
{
    const ClangFixIt assignmentFixItParenLeft{"(", {{compareFilePath(), 4u, 8u},
            {compareFilePath(), 4u, 8u}}};
    const ClangFixIt assignmentFixItParenRight{")", {{compareFilePath(), 4u, 13u},
            {compareFilePath(), 4u, 13u}}};
    ClangFixItOperation operation(diagnosticText(), {assignmentFixItParenLeft,
                                                     assignmentFixItParenRight});
    operation.perform();
    QCOMPARE(operation.firstRefactoringFileContent_forTestOnly(),
             fileContent("diagnostic_comparison_fixit_expected2.cpp"));
}

} // namespace ClangCodeModel::Internal::Tests
