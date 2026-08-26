// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cmakeparser.h"

#include "3rdparty/cmake/cmListFileCache.h"

#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/documentmodel.h>

#include <utils/algorithm.h>
#include <utils/textfileformat.h>

namespace CMakeProjectManager {

QList<CMakeFunctionCall> CMakeListFile::functionsNamed(const QString &lowerCaseName) const
{
    return Utils::filtered(functions, [&lowerCaseName](const CMakeFunctionCall &function) {
        return function.name == lowerCaseName;
    });
}

static CMakeListFile convert(const cmListFile &listFile, const QString &content)
{
    CMakeListFile result;
    result.content = content;
    result.functions.reserve(qsizetype(listFile.Functions.size()));
    for (const cmListFileFunction &function : listFile.Functions) {
        CMakeFunctionCall call;
        call.name = QString::fromStdString(function.LowerCaseName());
        for (const cmListFileArgument &argument : function.Arguments())
            call.arguments.append(QString::fromStdString(argument.Value));
        call.line = int(function.Line());
        call.lineEnd = int(function.LineEnd());
        result.functions.append(call);
    }
    return result;
}

Utils::Result<CMakeListFile> parseCMakeText(const QString &content, const QString &fileName)
{
    cmListFile listFile;
    std::string error;
    if (!listFile.ParseString(content.toStdString(),
                              fileName.isEmpty() ? "CMakeLists.txt" : fileName.toStdString(),
                              error)) {
        QString message = QString::fromStdString(error).trimmed();
        if (!fileName.isEmpty())
            message = fileName + ": " + message;
        return Utils::ResultError(message);
        }
    return convert(listFile, content);
}

Utils::Result<CMakeListFile> parseCMakeFile(const Utils::FilePath &filePath)
{
    Core::DocumentManager::saveModifiedDocumentSilently(
        Core::DocumentModel::documentForFilePath(filePath));
    QByteArray fileContent;
    const Utils::Result<> result = Utils::TextFileFormat::readFileUtf8(filePath,
                                                                        Utils::TextEncoding::Utf8,
                                                                        &fileContent);
    if (!result)
        return Utils::ResultError(result.error());
    return parseCMakeText(QString::fromUtf8(fileContent), filePath.fileName());
}

} //namespace CMakeProjectManager

#ifdef WITH_TESTS

#include <QTemporaryDir>
#include <QTest>

namespace CMakeProjectManager::Internal {

class CMakeParserTest : public QObject
{
    Q_OBJECT
private slots:
    void testParseText_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("expectedNames");
        QTest::addColumn<QStringList>("firstCallArguments");
        QTest::addColumn<int>("firstLine");
        QTest::addColumn<int>("firstLineEnd");

        QTest::newRow("simple call")
            << "add_executable(app main.cpp)\n"
            << QStringList{"add_executable"} << QStringList{"app", "main.cpp"} << 1 << 1;
        QTest::newRow("name is lower-cased")
            << "ADD_Executable(app)\n"
            << QStringList{"add_executable"} << QStringList{"app"} << 1 << 1;
        QTest::newRow("multi-line call")
            << "qt_add_android_permission(app\n    NAME android.permission.CAMERA\n)\n"
            << QStringList{"qt_add_android_permission"}
            << QStringList{"app", "NAME", "android.permission.CAMERA"} << 1 << 3;
        QTest::newRow("commented-out call is not a call")
            << "# add_library(x)\nproject(p)\n"
            << QStringList{"project"} << QStringList{"p"} <<2 << 2;
        QTest::newRow("quoted argument keeps spaces, loses quotes")
            << "message(\"hello world\")\n"
            << QStringList{"message"} << QStringList{"hello world"} << 1 << 1;
        QTest::newRow("bracket argument kept raw")
            << "message([[raw ${x} text]])\n"
            << QStringList{"message"} << QStringList{"raw ${x} text"} << 1 << 1;
        QTest::newRow("variable reference kept literal")
            << "target_link_libraries(${TGT} Qt6::Core)\n"
            << QStringList{"target_link_libraries"}
            << QStringList{"${TGT}", "Qt6::Core"} << 1 << 1;
        QTest::newRow("crlf line endings")
            << "project(p)\r\nadd_library(l STATIC)\r\n"
            << QStringList{"project", "add_library"} << QStringList{"p"} << 1 << 1;
   }

    void testParseText()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, expectedNames);
        QFETCH(QStringList, firstCallArguments);
        QFETCH(int, firstLine);
        QFETCH(int, firstLineEnd);

        const auto result = parseCMakeText(content);
        QVERIFY(result);
        QCOMPARE(result->content, content);
        QCOMPARE(Utils::transform(result->functions, &CMakeFunctionCall::name), expectedNames);
        QCOMPARE(result->functions.first().arguments, firstCallArguments);
        QCOMPARE(result->functions.first().line, firstLine);
        QCOMPARE(result->functions.first().lineEnd, firstLineEnd);
    }

    void testParseEmpty()
    {
        const auto result = parseCMakeText(QString());
        QVERIFY(result);
        QVERIFY(result->functions.isEmpty());
    }

    void testParseError()
    {
        const auto result = parseCMakeText("add_executable(app\n", "broken.cmake");
        QVERIFY(!result);
        QVERIFY(!result.error().isEmpty());
        QVERIFY(result.error().contains("broken.cmake"));
    }

    void testFunctionsNamed()
    {
        const auto result = parseCMakeText("foo(1)\nbar(2)\nFOO(3)\n");
        QVERIFY(result);
        const auto foos = result->functionsNamed("foo");
        QCOMPARE(foos.size(), 2);
        QCOMPARE(foos.at(0).arguments, QStringList{"1"});
        QCOMPARE(foos.at(1).arguments, QStringList{"3"});
    }

    void testParseFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Utils::FilePath path = Utils::FilePath::fromString(dir.path()) / "CMakeLists.txt";
        const QString content = "project(p)\nadd_executable(app main.cpp)\n";
        QVERIFY(path.writeFileContents(content.toUtf8()));

        const auto result = parseCMakeFile(path);
        QVERIFY(result);
        QCOMPARE(result->content, content);
        QCOMPARE(result->functions.size(), 2);

        QVERIFY(!parseCMakeFile(path.parentDir() / "missing.txt"));
    }
};

QObject *createCMakeParserTest()
{
    return new CMakeParserTest;
}

} // namespace CMakeProjectManager::Internal

#endif // WITH_TESTS

#include "cmakeparser.moc"
