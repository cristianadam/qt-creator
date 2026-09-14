// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpptodoitemsscanner.h"

#include "keyword.h"
#include "todoicons.h"

#include <utils/filepath.h>
#include <utils/temporarydirectory.h>
#include <utils/textcodec.h>

#include <QTest>

using namespace Utils;

namespace Todo::Internal {
namespace {

// The keywords the pane ships with, cut down to the two the rows use.
KeywordList testKeywords()
{
    Keyword todo;
    todo.name = "TODO";
    todo.iconType = IconType::Todo;

    Keyword fixme;
    fixme.name = "FIXME";
    fixme.iconType = IconType::Error;

    return {todo, fixme};
}

// One string per item found, so that a row says what the pane would show and
// on which line -- the two things a reader of the list uses.
QStringList found(const QList<TodoItem> &items)
{
    QStringList rows;
    for (const TodoItem &item : items)
        rows << QString("%1: %2").arg(item.line).arg(item.text);
    return rows;
}

} // namespace

class CppTodoScannerTest final : public QObject
{
    Q_OBJECT

private slots:
    void testItemsInText_data();
    void testItemsInText();
    void testWhereAFilesTextComesFrom();
};

// Which text a batch reads per file, which is the half of the scanner that
// touches the disk. Kept out of the rows above, which are about one text.
void CppTodoScannerTest::testWhereAFilesTextComesFrom()
{
    TemporaryDirectory dir("todo-scanner-XXXXXX");
    QVERIFY(dir.isValid());

    // Written in Latin-1, which is what the default encoding setting is for:
    // decoded as UTF-8 the o-umlaut would be a replacement character.
    const FilePath onDisk = dir.filePath("latin1.cpp");
    const TextEncoding latin1(TextEncoding::Latin1);
    QVERIFY(onDisk.writeFileContents(latin1.encode(u"// TODO: prüfen\n")));

    // A file being edited, whose text is the editor's rather than the disk's.
    const FilePath edited = dir.filePath("edited.cpp");
    QVERIFY(edited.writeFileContents("// TODO: what disk says\n"));
    CppEditor::WorkingCopy workingCopy;
    workingCopy.insert(edited, "// TODO: what the editor says\n");

    // And one that is not there at all.
    const FilePath missing = dir.filePath("gone.cpp");

    const QList<ScannedFile> scanned = scanFiles(
        testKeywords(), {onDisk, edited, missing}, workingCopy, latin1);

    QCOMPARE(scanned.size(), 3);
    for (const ScannedFile &file : scanned) {
        if (file.filePath == onDisk)
            QCOMPARE(found(file.items), QStringList{"1: TODO: prüfen"});
        else if (file.filePath == edited)
            QCOMPARE(found(file.items), QStringList{"1: TODO: what the editor says"});
        else if (file.filePath == missing)
            // An answer, not silence: nothing else takes what a file used to
            // hold out of the pane.
            QCOMPARE(found(file.items), QStringList{});
        else
            QFAIL("a file nobody asked about");
    }
}

void CppTodoScannerTest::testItemsInText_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("a line comment")
        << "int a; // TODO: fix this\n"
        << QStringList{"1: TODO: fix this"};

    QTest::newRow("a block comment on one line")
        << "/* TODO: one */\n"
        << QStringList{"1: TODO: one"};

    // Each line of a comment is read on its own, and the line a keyword
    // stands on is the one reported -- not the line the comment opened on.
    // What is shown starts at the keyword: the line parser takes off what
    // stands in front of it, the stars of a block comment included.
    QTest::newRow("a block comment over several lines")
        << "/*\n"
           " * TODO: the second line\n"
           " * FIXME: the third\n"
           " */\n"
        << QStringList{"2: TODO: the second line", "3: FIXME: the third"};

    // A doxygen comment is a comment, in both of its shapes.
    QTest::newRow("a doxygen comment")
        << "/*! TODO: documented */\n"
        << QStringList{"1: TODO: documented"};

    QTest::newRow("a doxygen line comment")
        << "//! TODO: documented\n"
        << QStringList{"1: TODO: documented"};

    // Nothing outside a comment is read, however much it looks like one.
    QTest::newRow("a keyword in a string is no item")
        << "const char *s = \"TODO: not a comment\";\n"
        << QStringList{};

    QTest::newRow("a keyword in code is no item")
        << "int TODO = 1;\n"
        << QStringList{};

    // What changed with the move to the file's own text: the preprocessed
    // source has been through the conditionals, so this used to be invisible.
    QTest::newRow("a comment in a branch that is not built")
        << "#if 0\n"
           "// TODO: in the dead branch\n"
           "#endif\n"
           "// TODO: in the live one\n"
        << QStringList{"2: TODO: in the dead branch", "4: TODO: in the live one"};

    QTest::newRow("a comment in an ifdef nobody defined")
        << "#ifdef NEVER_DEFINED\n"
           "// FIXME: still written down\n"
           "#endif\n"
        << QStringList{"2: FIXME: still written down"};

    // A macro is not expanded, so a comment after one is still where it was
    // written. The old reading counted lines through #line markers to get
    // this right; now there is nothing to count through.
    QTest::newRow("lines are those of the file, not of an expansion")
        << "#define WIDE(x) x x x x x\n"
           "WIDE(int a;)\n"
           "// TODO: on line three\n"
        << QStringList{"3: TODO: on line three"};

    // A comment the lexer cannot finish still holds what was written in it,
    // all of it: there is no "*/" to take off the end of an unclosed one, and
    // taking two characters off anyway lost the last two of the text.
    QTest::newRow("a block comment nobody closed")
        << "// TODO: first\n"
           "/* TODO: never closed\n"
        << QStringList{"1: TODO: first", "2: TODO: never closed"};

    // The other half of that rule: only a block comment's "*/" comes off. A
    // line comment can end in one and keeps it.
    QTest::newRow("a line comment ending in a block comment's close")
        << "// TODO: superseded, see /* the old one */\n"
        << QStringList{"1: TODO: superseded, see /* the old one */"};

    QTest::newRow("no comments at all")
        << "int main() { return 0; }\n"
        << QStringList{};

    // Whitespace around a line is taken off either end, stars and all being
    // left alone -- the pane shows the line as the file has it.
    QTest::newRow("a line is trimmed at both ends")
        << "/*\n"
           "        TODO: indented deeply        \n"
           "*/\n"
        << QStringList{"2: TODO: indented deeply"};
}

void CppTodoScannerTest::testItemsInText()
{
    QFETCH(QString, source);
    QFETCH(QStringList, expected);

    // The free function, not a scanner: constructing one connects to the code
    // model and asks it to update every file of every project.
    const FilePath filePath = FilePath::fromPathPart(u"<test>");

    QCOMPARE(found(todoItemsIn(testKeywords(), filePath, source)), expected);
}

QObject *createCppTodoScannerTest()
{
    return new CppTodoScannerTest;
}

} // Todo::Internal

#include "cpptodoitemsscanner_test.moc"
