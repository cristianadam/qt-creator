// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// What Expand Selection and Shrink Selection do, which nothing covered
// before. The selection changer is a thousand lines of walking the syntax
// tree, and it is next in line to be read off another one, so what it selects
// has to be written down first -- a rewrite of untested code is a rewrite
// nobody can check.
//
// Which is also what says the other tree answers the same: with
// QTC_CXX_FRONTEND_MODEL set every row here is walked on the cxx-frontend
// model instead, and the selections are the same ones.

#include "cppselectionchanger_test.h"

#include "cppselectionchanger.h"

#include <cplusplus/CppDocument.h>

#include <utils/filepath.h>

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#ifdef QTC_WITH_CXX_FRONTEND
#include "cppworkingcopy.h"
#include "cxxfrontendmodel.h"
#endif

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
        const Utils::FilePath filePath = Utils::FilePath::fromPathPart(u"<test>");
        m_document = Document::create(filePath);
        m_document->setUtf8Source("#line 1 \"<test>\"\n" + source);
        m_document->check();

#ifdef QTC_WITH_CXX_FRONTEND
        // What the editor's parser does when the other model is asked for:
        // run it over the file being edited, so that the changer finds it and
        // reads that tree instead. The rows are the same either way -- which
        // tree answers is decided by the environment, exactly as in the
        // editor -- and that is the whole of what the comparison says.
        //
        // The source itself, with no marker in front of it: that model counts
        // lines from the text it is given.
        if (cxxFrontendModelRequested()) {
            WorkingCopy workingCopy;
            workingCopy.insert(filePath, source);
            updateCxxFrontendModel({}, filePath, {}, workingCopy);
        }
#endif

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

    // A class is one construct with three ways into it -- the keyword, the
    // name, the body -- and which one the cursor is in decides what the first
    // steps select.
    QTest::newRow("out of the keyword of a class")
        << QByteArray("str@uct S {\n"
                      "    int m_value;\n"
                      "};\n")
        << QStringList({"struct",
                        "struct S",
                        "struct S {\n    int m_value;\n}",
                        "struct S {\n    int m_value;\n};",
                        "struct S {\n    int m_value;\n};\n"});

    QTest::newRow("out of the name of a class")
        << QByteArray("struct Sh@ape {\n"
                      "    int m_value;\n"
                      "};\n")
        << QStringList({"Shape",
                        "struct Shape",
                        "struct Shape {\n    int m_value;\n}",
                        "struct Shape {\n    int m_value;\n};",
                        "struct Shape {\n    int m_value;\n};\n"});

    QTest::newRow("out of the keyword of a namespace")
        << QByteArray("namesp@ace N {\n"
                      "int value;\n"
                      "}\n")
        << QStringList({"namespace",
                        "namespace N",
                        "namespace N {\nint value;\n}",
                        "namespace N {\nint value;\n}\n"});

    QTest::newRow("out of the name of a namespace")
        << QByteArray("namespace Na@med {\n"
                      "int value;\n"
                      "}\n")
        << QStringList({"Named",
                        "namespace Named",
                        "namespace Named {\nint value;\n}",
                        "namespace Named {\nint value;\n}\n"});

    QTest::newRow("out of a char literal")
        << QByteArray("const char c = '@a';\n")
        << QStringList({"a",
                        "'a'",
                        "c = 'a'",
                        "const char c = 'a';",
                        "const char c = 'a';\n"});

    // A raw literal has a parenthesis inside each quote, and what it says is
    // between those.
    QTest::newRow("out of a raw string literal")
        << QByteArray("const char *s = R\"(he@llo)\";\n")
        << QStringList({"hello",
                        "R\"(hello)\"",
                        "*s = R\"(hello)\"",
                        "const char *s = R\"(hello)\";",
                        "const char *s = R\"(hello)\";\n"});

    // The lambda's own steps are its capture group with its arguments, and
    // then -- where one is written -- its prototype, the arrow and the return
    // type included.
    QTest::newRow("out of the parameter of a lambda")
        << QByteArray("void f()\n"
                      "{\n"
                      "    auto g = [](int a@) { return a; };\n"
                      "}\n")
        << QStringList({"(int a)",
                        "[](int a)",
                        "[](int a) { return a; }",
                        "g = [](int a) { return a; }",
                        "auto g = [](int a) { return a; };",
                        "{\n    auto g = [](int a) { return a; };\n}",
                        "void f()\n{\n    auto g = [](int a) { return a; };\n}",
                        "void f()\n{\n    auto g = [](int a) { return a; };\n}\n"});

    QTest::newRow("out of the return type of a lambda")
        << QByteArray("void f()\n"
                      "{\n"
                      "    auto g = [](int a@) -> int { return a; };\n"
                      "}\n")
        << QStringList({"(int a) -> int",
                        "[](int a) -> int",
                        "[](int a) -> int { return a; }",
                        "g = [](int a) -> int { return a; }",
                        "auto g = [](int a) -> int { return a; };",
                        "{\n    auto g = [](int a) -> int { return a; };\n}",
                        "void f()\n{\n    auto g = [](int a) -> int { return a; };\n}",
                        "void f()\n{\n    auto g = [](int a) -> int { return a; };\n}\n"});

    QTest::newRow("out of the keyword of a template")
        << QByteArray("temp@late<typename T>\n"
                      "void f(T t);\n")
        << QStringList({"template",
                        "template<typename T>",
                        "template<typename T>\nvoid f(T t);",
                        "template<typename T>\nvoid f(T t);\n"});

    QTest::newRow("out of the name of a template instance")
        << QByteArray("List<int> val@ue;\n")
        << QStringList({"value",
                        "List<int> value;",
                        "List<int> value;\n"});

    // The name a template is instantiated by, before the instantiation.
    QTest::newRow("out of the name of a template being instantiated")
        << QByteArray("Li@st<int> value;\n")
        << QStringList({"List",
                        "List<int>",
                        "List<int> value;",
                        "List<int> value;\n"});

    // The declarator is offered twice: without the qualifiers written after
    // the parameters, and then with them.
    QTest::newRow("out of a const member function")
        << QByteArray("struct S {\n"
                      "    void f@() const;\n"
                      "};\n")
        << QStringList({"()",
                        "f()",
                        "f() const",
                        "void f() const;",
                        "\n    void f() const;\n",
                        "{\n    void f() const;\n}",
                        "struct S {\n    void f() const;\n}",
                        "struct S {\n    void f() const;\n};",
                        "struct S {\n    void f() const;\n};\n"});

    QTest::newRow("out of the range of a for statement")
        << QByteArray("void f(int *values)\n"
                      "{\n"
                      "    for (int i : val@ues)\n"
                      "        g(i);\n"
                      "}\n")
        << QStringList({"values",
                        "int i : values",
                        "(int i : values)",
                        "for (int i : values)\n        g(i);",
                        "{\n    for (int i : values)\n        g(i);\n}",
                        "void f(int *values)\n{\n    for (int i : values)\n        g(i);\n}",
                        "void f(int *values)\n{\n    for (int i : values)\n        g(i);\n}\n"});

    // A scope with nothing in it still has a first step: the blank space
    // between the braces.
    QTest::newRow("out of the inside of an empty body")
        << QByteArray("void f()\n"
                      "{\n"
                      "    @\n"
                      "}\n")
        << QStringList({"\n    \n",
                        "{\n    \n}",
                        "void f()\n{\n    \n}",
                        "void f()\n{\n    \n}\n"});

    QTest::newRow("out of an initializer written with parentheses")
        << QByteArray("int value(4@2);\n")
        << QStringList({"42",
                        "(42)",
                        "value(42)",
                        "int value(42);",
                        "int value(42);\n"});

    // Everything to the left of the body, before the definition as a whole.
    QTest::newRow("out of the return type of a function")
        << QByteArray("vo@id f(int a)\n"
                      "{\n"
                      "    g(a);\n"
                      "}\n")
        << QStringList({"void",
                        "void f(int a)",
                        "void f(int a)\n{\n    g(a);\n}",
                        "void f(int a)\n{\n    g(a);\n}\n"});
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
