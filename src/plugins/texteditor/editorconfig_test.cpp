// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "editorconfig_test.h"

#include "editorconfig.h"
#include "icodestylepreferences.h"
#include "marginsettings.h"
#include "storagesettings.h"
#include "tabsettings.h"
#include "textdocument.h"
#include "texteditor.h"

#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>

#include <utils/filepath.h>
#include <utils/temporarydirectory.h>

#include <QScopeGuard>
#include <QTest>

using namespace Utils;

namespace TextEditor::Internal {

class EditorConfigTest final : public QObject
{
    Q_OBJECT

private slots:
    void testGlob_data();
    void testGlob();
    void testPropertiesForFile();
    void testDerivedIndentation();
    void testDisabled();
    void testOpenAndSave();
};

void EditorConfigTest::testGlob_data()
{
    QTest::addColumn<QString>("glob");
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("matches");

    QTest::newRow("suffix") << "*.cpp" << "main.cpp" << true;
    QTest::newRow("suffix in subdirectory") << "*.cpp" << "src/main.cpp" << true;
    QTest::newRow("other suffix") << "*.cpp" << "main.h" << false;
    QTest::newRow("suffix is anchored") << "*.cpp" << "main.cpp.orig" << false;
    QTest::newRow("dot is literal") << "a.b" << "aXb" << false;
    QTest::newRow("name in subdirectory") << "Makefile" << "sub/Makefile" << true;
    QTest::newRow("star") << "*" << "a/b.txt" << true;
    QTest::newRow("path") << "src/*.cpp" << "src/main.cpp" << true;
    QTest::newRow("path is anchored") << "src/*.cpp" << "lib/src/main.cpp" << false;
    QTest::newRow("leading slash") << "/src/*.cpp" << "src/main.cpp" << true;
    QTest::newRow("star stops at slash") << "src/*.cpp" << "src/sub/main.cpp" << false;
    QTest::newRow("double star") << "src/**.cpp" << "src/sub/main.cpp" << true;
    QTest::newRow("double star directory empty") << "a/**/z" << "a/z" << true;
    QTest::newRow("double star directories") << "a/**/z" << "a/b/c/z" << true;
    QTest::newRow("question mark") << "?.txt" << "a.txt" << true;
    QTest::newRow("question marks") << "??.txt" << "a.txt" << false;
    QTest::newRow("brackets") << "[ab].txt" << "b.txt" << true;
    QTest::newRow("negated brackets") << "[!ab].txt" << "b.txt" << false;
    QTest::newRow("negated brackets other") << "[!ab].txt" << "c.txt" << true;
    QTest::newRow("bracket range") << "[a-c].txt" << "b.txt" << true;
    QTest::newRow("bracket range outside") << "[a-c].txt" << "d.txt" << false;
    QTest::newRow("braces") << "*.{cpp,h}" << "x.h" << true;
    QTest::newRow("braces other") << "*.{cpp,h}" << "x.c" << false;
    QTest::newRow("nested braces") << "{a,{b,c}}.txt" << "c.txt" << true;
    QTest::newRow("single brace") << "{single}.txt" << "{single}.txt" << true;
    QTest::newRow("single brace no alternative") << "{single}.txt" << "single.txt" << false;
    QTest::newRow("numeric range") << "file{1..3}.txt" << "file2.txt" << true;
    QTest::newRow("numeric range outside") << "file{1..3}.txt" << "file4.txt" << false;
    QTest::newRow("negative numeric range") << "file{-1..3}.txt" << "file-1.txt" << true;
    QTest::newRow("escaped star") << "\\*.txt" << "*.txt" << true;
    QTest::newRow("escaped star literal") << "\\*.txt" << "a.txt" << false;
}

void EditorConfigTest::testGlob()
{
    QFETCH(QString, glob);
    QFETCH(QString, path);
    QFETCH(bool, matches);

    QCOMPARE(editorConfigGlobMatches(glob, path), matches);
}

void EditorConfigTest::testPropertiesForFile()
{
    TemporaryDirectory tempDir("qtc-editorconfig-test");
    QVERIFY(tempDir.isValid());
    const FilePath outer = tempDir.path();
    const FilePath project = outer.pathAppended("project");
    const FilePath sub = project.pathAppended("sub");
    QVERIFY(sub.createDir());

    QVERIFY(outer.pathAppended(".editorconfig").writeFileContents("[*]\ntab_width = 7\n"));
    QVERIFY(project.pathAppended(".editorconfig")
                .writeFileContents("root = true\n"
                                   "\n"
                                   "[*]\n"
                                   "indent_style = space\n"
                                   "indent_size = 4\n"
                                   "charset = utf-8\n"
                                   "\n"
                                   "[*.mk]\n"
                                   "indent_style = tab\n"));
    QVERIFY(sub.pathAppended(".editorconfig")
                .writeFileContents("# comment\n"
                                   "; comment\n"
                                   "[*.cpp]\n"
                                   "INDENT_SIZE = 2\n"
                                   "charset = unset\n"));

    const QMap<QString, QString> cpp = editorConfigPropertiesForFile(sub.pathAppended("a.cpp"));
    const QMap<QString, QString> expectedCpp{{"indent_style", "space"}, {"indent_size", "2"}};
    QCOMPARE(cpp, expectedCpp);

    const QMap<QString, QString> mk = editorConfigPropertiesForFile(project.pathAppended("b.mk"));
    const QMap<QString, QString> expectedMk{
        {"indent_style", "tab"}, {"indent_size", "4"}, {"charset", "utf-8"}};
    QCOMPARE(mk, expectedMk);

    const QMap<QString, QString> outside
        = editorConfigPropertiesForFile(outer.pathAppended("c.txt"));
    const QMap<QString, QString> expectedOutside{{"tab_width", "7"}};
    QCOMPARE(outside, expectedOutside);
}

void EditorConfigTest::testDerivedIndentation()
{
    TabSettingsData tabSettings(TabSettingsData::SpacesOnlyTabPolicy,
                                8,
                                4,
                                TabSettingsData::ContinuationAlignWithSpaces);

    const EditorConfigProperties tabs = EditorConfigProperties::fromProperties(
        {{"indent_style", "Tab"}});
    QCOMPARE(tabs.indentWithTabs, std::optional<bool>(true));
    QVERIFY(tabs.indentSizeIsTabWidth);
    TabSettingsData tabsApplied = tabSettings;
    tabs.applyTo(tabsApplied);
    QCOMPARE(tabsApplied.m_tabPolicy, TabSettingsData::TabsOnlyTabPolicy);
    QCOMPARE(tabsApplied.m_indentSize, 8);
    QVERIFY(!tabsApplied.m_autoDetect);

    const EditorConfigProperties indentOnly = EditorConfigProperties::fromProperties(
        {{"indent_size", "2"}});
    QCOMPARE(indentOnly.indentSize, std::optional<int>(2));
    QCOMPARE(indentOnly.tabWidth, std::optional<int>(2));

    const EditorConfigProperties indentTab = EditorConfigProperties::fromProperties(
        {{"indent_size", "tab"}, {"tab_width", "5"}});
    QCOMPARE(indentTab.indentSize, std::optional<int>(5));

    QVERIFY(EditorConfigProperties::fromProperties({}).isEmpty());
    QVERIFY(EditorConfigProperties::fromProperties({{"end_of_line", "cr"}}).isEmpty());
    QVERIFY(EditorConfigProperties::fromProperties({{"unknown", "true"}}).isEmpty());
}

void EditorConfigTest::testDisabled()
{
    TemporaryDirectory tempDir("qtc-editorconfig-test");
    QVERIFY(tempDir.isValid());
    QVERIFY(tempDir.path()
                .pathAppended(".editorconfig")
                .writeFileContents("root = true\n[*]\nindent_size = 3\n"));
    const FilePath file = tempDir.path().pathAppended("a.txt");

    QVERIFY(!EditorConfigProperties::forFile(file).isEmpty());

    const bool wasEnabled = globalEditorConfigSettings().enabled();
    const auto restore = qScopeGuard(
        [wasEnabled] { globalEditorConfigSettings().enabled.setValue(wasEnabled); });
    globalEditorConfigSettings().enabled.setValue(false);
    QVERIFY(EditorConfigProperties::forFile(file).isEmpty());
}

void EditorConfigTest::testOpenAndSave()
{
    TemporaryDirectory tempDir("qtc-editorconfig-test");
    QVERIFY(tempDir.isValid());
    QVERIFY(tempDir.path()
                .pathAppended(".editorconfig")
                .writeFileContents("root = true\n"
                                   "[*.txt]\n"
                                   "indent_style = tab\n"
                                   "tab_width = 3\n"
                                   "end_of_line = crlf\n"
                                   "charset = latin1\n"
                                   "trim_trailing_whitespace = true\n"
                                   "insert_final_newline = true\n"
                                   "max_line_length = 66\n"));
    const FilePath file = tempDir.path().pathAppended("a.txt");
    QVERIFY(file.writeFileContents("a  \n\xe4"));

    Core::IEditor *editor = Core::EditorManager::openEditor(file);
    QVERIFY(editor);
    const auto closeEditor = qScopeGuard([editor] {
        Core::EditorManager::closeEditors({editor}, false);
    });
    auto textEditor = qobject_cast<BaseTextEditor *>(editor);
    QVERIFY(textEditor);
    TextDocument *document = textEditor->textDocument();

    QCOMPARE(document->plainText(), QString("a  \n") + QChar(0xe4));
    QCOMPARE(document->encoding().name(), TextEncoding(TextEncoding::Latin1).name());
    QCOMPARE(document->lineTerminationMode(), TextFileFormat::CRLFLineTerminator);
    QCOMPARE(textEditor->editorWidget()->marginSettings().m_marginColumn, 66);

    const TabSettingsData tabSettings = document->tabSettings();
    QCOMPARE(tabSettings.m_tabPolicy, TabSettingsData::TabsOnlyTabPolicy);
    QCOMPARE(tabSettings.m_tabSize, 3);
    QCOMPARE(tabSettings.m_indentSize, 3);

    // A code style set later, for example from a project, does not override it.
    ICodeStylePreferences *originalCodeStyle = document->codeStyle();
    ICodeStylePreferences codeStyle;
    codeStyle.setTabSettings(TabSettingsData(TabSettingsData::SpacesOnlyTabPolicy,
                                             8,
                                             4,
                                             TabSettingsData::ContinuationAlignWithSpaces));
    document->setCodeStyle(&codeStyle);
    QCOMPARE(document->tabSettings().m_tabPolicy, TabSettingsData::TabsOnlyTabPolicy);
    QCOMPARE(document->tabSettings().m_indentSize, 3);
    document->setCodeStyle(originalCodeStyle);

    StorageSettingsData storageSettings;
    storageSettings.m_cleanWhitespace = false;
    storageSettings.m_addFinalNewLine = false;
    document->setStorageSettings(storageSettings);

    QVERIFY(Core::DocumentManager::saveDocument(document));
    const Result<QByteArray> contents = file.fileContents();
    QVERIFY(contents);
    QCOMPARE(*contents, QByteArray("a\r\n\xe4\r\n"));
}

QObject *createEditorConfigTest()
{
    return new EditorConfigTest;
}

} // namespace TextEditor::Internal

#include "editorconfig_test.moc"
