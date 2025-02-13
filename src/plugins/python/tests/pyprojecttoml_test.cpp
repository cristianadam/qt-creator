// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifdef WITH_TESTS

#include "pyprojecttoml_test.h"
#include "pyprojecttoml.h"

#include <QtTest/QtTest>

#include <utils/filepath.h>

namespace Python::Internal {

static QString qrcPath(const QString &relativeFilePath)
{
    return ":/unittests/Python/" + relativeFilePath;
}

void printProjectParseResult(const PyProjectTomlParseResult &result)
{
    qDebug() << "Project name: " << result.projectName;
    qDebug() << "Project version: " << result.projectVersion;
    qDebug() << "Project files: " << result.projectFiles;
    for (const auto &error : result.errors) {
        qDebug() << "Error: " << error.text;
    }
}

void PyProjectTomlTest::testCorrectProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("correct/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 0);
    QCOMPARE(result.projectName, QString("CorrectTest"));
    QCOMPARE(result.projectVersion, QString("0.1"));
    QCOMPARE(result.projectFiles.size(), 2);
    QVERIFY(result.projectFiles.contains(QString("main.py")));
    QVERIFY(result.projectFiles.contains(QString("folder/file_in_folder.py")));
}

void PyProjectTomlTest::testEmptyProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("empty/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Missing key error: \"root\" table must contain a \"project\" node"));
}

void PyProjectTomlTest::testFileMissingProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("filemissing/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(result.errors.first().text, QString("File \"non_existing_file.py\" does not exist."));
    // It is expected that even if a file is missing, the other files are still parsed.
    QCOMPARE(result.projectFiles.size(), 1);
    QCOMPARE(result.projectFiles.first(), QString("main.py"));
}

void PyProjectTomlTest::testFilesBlankProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("filesblank/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QVERIFY(result.errors.first().text.startsWith("Parsing error"));
}

void PyProjectTomlTest::testFilesWrongTypeProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("fileswrongtype/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString(
            "Type error: Node \"files\" value type must be a \"TOML array\". Type found: "
            "\"integer\""));
}

void PyProjectTomlTest::testFileWrongTypeProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("filewrongtype/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString(
            "Type error: Node \"files\" value type must be a \"string\". Type found: \"integer\""));
    QCOMPARE(result.projectFiles.size(), 2);
}

void PyProjectTomlTest::testProjectEmptyProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("projectempty/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Missing key error: \"project\" table must contain a \"name\" node"));
}

void PyProjectTomlTest::testProjectMissingProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("projectmissing/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Missing key error: \"root\" table must contain a \"project\" node"));
}

void PyProjectTomlTest::testProjectWrongTypeProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("projectwrongtype/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Type error: \"project\" must be a table, not a \"integer\""));
}

void PyProjectTomlTest::testPySideEmptyProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("pysideempty/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Missing key error: \"pyside6-project\" table must contain a \"files\" node"));
}

void PyProjectTomlTest::testPySideMissingProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("pysidemissing/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Missing key error: \"tool\" table must contain a \"pyside6-project\" node"));
}

void PyProjectTomlTest::testPySideWrongTypeProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("pysidewrongtype/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Type error: \"pyside6-project\" must be a table, not a \"array\""));
}

void PyProjectTomlTest::testToolEmptyProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("toolempty/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Missing key error: \"tool\" table must contain a \"pyside6-project\" node"));
}

void PyProjectTomlTest::testToolMissingProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("toolmissing/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Missing key error: \"root\" table must contain a \"tool\" node"));
}

void PyProjectTomlTest::testToolWrongTypeProjectParsing()
{
    const auto projectFile = Utils::FilePath::fromString(qrcPath("toolwrongtype/pyproject.toml"));
    QVERIFY(projectFile.exists());
    PyProjectTomlParseResult result = parsePyProjectToml(projectFile);
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(
        result.errors.first().text,
        QString("Type error: \"tool\" must be a table, not a \"integer\""));
}

QObject *createPyProjectTomlTest()
{
    return new PyProjectTomlTest;
}

} // namespace Python::Internal

#endif // WITH_TESTS