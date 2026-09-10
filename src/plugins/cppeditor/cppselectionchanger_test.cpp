// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// What Expand Selection and Shrink Selection do, which nothing covered
// before. The selection changer is a thousand lines of walking the syntax
// tree, and it is next in line to be read off another one, so what it selects
// has to be written down first -- a rewrite of untested code is a rewrite
// nobody can check.

#include "cppselectionchanger_test.h"

#include "cppselectionchanger.h"

#include <cplusplus/CppDocument.h>

#include <utils/filepath.h>

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

using namespace CPlusPlus;

namespace CppEditor::Internal {
namespace {

// Takes the @ marker out of the source and says where it stood.
QByteArray takeMarker(const QByteArray &marked, int *position)
{
    const int at = marked.indexOf('@');
    *position = at;
    QByteArray source = marked;
    source.remove(at, 1);
    return source;
}

// One file, its parse, and a cursor in it -- everything the editor hands the
// selection changer, without the editor.
class Driver
{
public:
    explicit Driver(const QByteArray &marked)
    {
        int position = 0;
        const QByteArray source = takeMarker(marked, &position);

        // With a #line marker in front, as the source processor's output has:
        // without one the built-in translation unit counts lines from zero,
        // and then nothing it says lines up with the text the cursor is in.
        // The marker's own line is consumed by it.
        m_document = Document::create(Utils::FilePath::fromPathPart(u"<test>"));
        m_document->setUtf8Source("#line 1 \"<test>\"\n" + source);
        m_document->check();

        m_text.setPlainText(QString::fromUtf8(source));
        m_cursor = QTextCursor(&m_text);
        m_cursor.setPosition(position);

        // One changer for the whole run, as the editor keeps one: shrinking
        // retraces the steps expanding took, and that is what it remembers.
        m_changer.onCursorPositionChanged(m_cursor);
        m_changer.startChangeSelection();
    }

    ~Driver() { m_changer.stopChangeSelection(); }

    // Every selection the direction leads to, one after another, until it
    // leads nowhere. Bounded, since a walk that never stops would otherwise
    // hang the test rather than fail it.
    QStringList walk(CppSelectionChanger::Direction direction, int maxSteps = 20)
    {
        QStringList selections;
        for (int step = 0; step < maxSteps; ++step) {
            if (!m_changer.changeSelection(direction, m_cursor, m_document))
                break;
            selections << m_cursor.selectedText().replace(QChar::ParagraphSeparator, u'\n');
        }
        return selections;
    }

private:
    Document::Ptr m_document;
    QTextDocument m_text;
    QTextCursor m_cursor;
    CppSelectionChanger m_changer;
};

} // namespace

void SelectionChangerTest::testExpand_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("out of a name in an expression")
        << QByteArray("void f(int a, int b)\n"
                      "{\n"
                      "    g(a@ + b);\n"
                      "}\n")
        << QStringList({"a + b",
                        "(a + b)",
                        "g(a + b)",
                        "g(a + b);",
                        "{\n    g(a + b);\n}",
                        "void f(int a, int b)\n{\n    g(a + b);\n}",
                        "void f(int a, int b)\n{\n    g(a + b);\n}\n"});

    // A literal takes two steps of its own: what it says, and then the
    // quotes around it.
    QTest::newRow("out of a string literal")
        << QByteArray("const char *s = \"he@llo\";\n")
        << QStringList({"hello",
                        "\"hello\"",
                        "*s = \"hello\"",
                        "const char *s = \"hello\";",
                        "const char *s = \"hello\";\n"});

    QTest::newRow("out of a parameter")
        << QByteArray("void f(int par@am, int other);\n")
        << QStringList({"param",
                        "int param",
                        "int param, int other",
                        "(int param, int other)",
                        "f(int param, int other)",
                        "void f(int param, int other);",
                        "void f(int param, int other);\n"});

    QTest::newRow("out of a member of a class")
        << QByteArray("struct S {\n"
                      "    int m_va@lue = 0;\n"
                      "};\n")
        << QStringList({"m_value",
                        "m_value = 0",
                        "int m_value = 0;",
                        "\n    int m_value = 0;\n",
                        "{\n    int m_value = 0;\n}",
                        "struct S {\n    int m_value = 0;\n}",
                        "struct S {\n    int m_value = 0;\n};",
                        "struct S {\n    int m_value = 0;\n};\n"});

    QTest::newRow("out of the condition of a for statement")
        << QByteArray("void f()\n"
                      "{\n"
                      "    for (int i = 0; i <@ 10; ++i)\n"
                      "        g(i);\n"
                      "}\n")
        << QStringList({"i < 10",
                        "int i = 0; i < 10; ++i",
                        "(int i = 0; i < 10; ++i)",
                        "for (int i = 0; i < 10; ++i)\n        g(i);",
                        "{\n    for (int i = 0; i < 10; ++i)\n        g(i);\n}",
                        "void f()\n{\n    for (int i = 0; i < 10; ++i)\n        g(i);\n}",
                        "void f()\n{\n    for (int i = 0; i < 10; ++i)\n        g(i);\n}\n"});

    QTest::newRow("out of a nested statement")
        << QByteArray("void f(bool b)\n"
                      "{\n"
                      "    if (b) {\n"
                      "        g@();\n"
                      "    }\n"
                      "}\n")
        << QStringList({"g()",
                        "g();",
                        "{\n        g();\n    }",
                        "if (b) {\n        g();\n    }",
                        "{\n    if (b) {\n        g();\n    }\n}",
                        "void f(bool b)\n{\n    if (b) {\n        g();\n    }\n}",
                        "void f(bool b)\n{\n    if (b) {\n        g();\n    }\n}\n"});
}

void SelectionChangerTest::testExpand()
{
    QFETCH(QByteArray, marked);
    QFETCH(QStringList, expected);

    Driver driver(marked);
    const QStringList selections = driver.walk(CppSelectionChanger::ExpandSelection);
    QCOMPARE(selections, expected);
}

// Shrinking is not a walk of its own: it is expanding remembered backwards,
// which is why the editor keeps one changer for both.
void SelectionChangerTest::testShrinkRetracesTheWayOut()
{
    const QByteArray source = "void f(int a, int b)\n"
                              "{\n"
                              "    g(a@ + b);\n"
                              "}\n";

    Driver driver(source);
    QStringList out = driver.walk(CppSelectionChanger::ExpandSelection);
    QVERIFY(!out.isEmpty());

    const QStringList back = driver.walk(CppSelectionChanger::ShrinkSelection);

    // The way back, one step behind: the last selection expanding reached is
    // where shrinking starts from, so what comes back is the rest of the list
    // read the other way -- and then one step more, which selects nothing and
    // leaves a bare cursor where the walk began.
    out.removeLast();
    std::reverse(out.begin(), out.end());
    out << QString();
    QCOMPARE(back, out);
}

// And there is nothing to shrink when nothing is selected, which the editor
// relies on to leave the cursor alone.
void SelectionChangerTest::testNothingToShrink()
{
    Driver driver("void f()\n{\n    g@();\n}\n");
    QCOMPARE(driver.walk(CppSelectionChanger::ShrinkSelection), QStringList());
}

} // namespace CppEditor::Internal
