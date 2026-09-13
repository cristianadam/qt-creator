// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// What the editor says about the thing under the cursor: the tooltip, the
// help it offers and where the name leads. Nothing covered it before, which
// is the reason for this file -- a tooltip is read by whoever hovers and by
// nobody else, so a wrong one goes unnoticed for a long time.

#include "cppelementevaluator_test.h"

#include "cppeditorwidget.h"
#include "cppelementevaluator.h"
#include "cppmodelmanager.h"
#include "cpptoolstestcase.h"

#include <texteditor/texteditor.h>

#include <QTest>
#include <QTextCursor>

using namespace Utils;

namespace CppEditor::Internal::Tests {

namespace {

QString categoryOf(Core::HelpItem::Category category)
{
    switch (category) {
    case Core::HelpItem::ClassOrNamespace: return "class-or-namespace";
    case Core::HelpItem::Enum: return "enum";
    case Core::HelpItem::Typedef: return "typedef";
    case Core::HelpItem::Macro: return "macro";
    case Core::HelpItem::Brief: return "brief";
    case Core::HelpItem::Function: return "function";
    case Core::HelpItem::Unknown: return "unknown";
    default: break;
    }
    return QString::number(category);
}

QString iconOf(CodeModelIcon::Type type)
{
    switch (type) {
    case CodeModelIcon::Class: return "class";
    case CodeModelIcon::Struct: return "struct";
    case CodeModelIcon::Enum: return "enum";
    case CodeModelIcon::Enumerator: return "enumerator";
    case CodeModelIcon::FuncPublic: return "public function";
    case CodeModelIcon::FuncProtected: return "protected function";
    case CodeModelIcon::FuncPrivate: return "private function";
    case CodeModelIcon::Namespace: return "namespace";
    case CodeModelIcon::VarPublic: return "public variable";
    case CodeModelIcon::VarPrivate: return "private variable";
    case CodeModelIcon::Macro: return "macro";
    case CodeModelIcon::Unknown: return "unknown";
    default: break;
    }
    return QString::number(type);
}

// Everything a reader is shown, in the order it is shown in. The link is
// said as the file's own name and the place in it, since the file is in a
// directory made for the test -- with the column as Utils::Link carries
// it, which is counted from zero where the line is counted from one.
QString describe(const std::shared_ptr<CppElement> &element)
{
    if (!element)
        return "nothing";

    QStringList said;
    said << "category: " + categoryOf(element->helpCategory);
    said << "mark: " + element->helpMark;
    said << "help: " + element->helpIdCandidates.join(", ");
    said << "tooltip: " + element->tooltip;
    said << "link: "
                + (element->link.hasValidTarget()
                       ? QString("%1:%2:%3").arg(element->link.targetFilePath.fileName())
                             .arg(element->link.target.line).arg(element->link.target.column)
                       : QString("none"));
    if (const auto declarable = std::dynamic_pointer_cast<CppDeclarableElement>(element)) {
        said << "name: " + declarable->name;
        said << "qualified: " + declarable->qualifiedName;
        said << "icon: " + iconOf(declarable->iconType);
    }
    if (element->toCppClass())
        said << "is a class";
    return said.join('\n');
}

} // namespace

void ElementEvaluatorTest::testElementUnderCursor_data()
{
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QString>("expected");

    QTest::newRow("a class")
        << QByteArray("class C {};\n"
                      "C@ c;\n")
        << QString("category: class-or-namespace\n"
                   "mark: C\n"
                   "help: C\n"
                   "tooltip: C\n"
                   "link: file.cpp:1:6\n"
                   "name: C\n"
                   "qualified: C\n"
                   "icon: class\n"
                   "is a class");

    QTest::newRow("a class in a namespace")
        << QByteArray("namespace N { class C {}; }\n"
                      "N::C@ c;\n")
        << QString("category: class-or-namespace\n"
                   "mark: C\n"
                   "help: N::C, C\n"
                   "tooltip: N::C\n"
                   "link: file.cpp:1:20\n"
                   "name: C\n"
                   "qualified: N::C\n"
                   "icon: class\n"
                   "is a class");

    QTest::newRow("a namespace")
        << QByteArray("namespace N { class C {}; }\n"
                      "N@::C c;\n")
        << QString("category: class-or-namespace\n"
                   "mark: N\n"
                   "help: N\n"
                   "tooltip: N\n"
                   "link: file.cpp:1:10\n"
                   "name: N\n"
                   "qualified: N\n"
                   "icon: namespace");

    // A function's mark carries its signature, since that is what tells one
    // overload from another; what documentation is looked up under does not.
    QTest::newRow("a function")
        << QByteArray("int f(int a, int b);\n"
                      "int g() { return f@(1, 2); }\n")
        << QString("category: function\n"
                   "mark: f(int, int)\n"
                   "help: f, f\n"
                   "tooltip: int f(int a, int b)\n"
                   "link: file.cpp:1:4\n"
                   "name: f\n"
                   "qualified: f\n"
                   "icon: public function");

    QTest::newRow("a member function")
        << QByteArray("struct S { void m(); };\n"
                      "void f(S *s) { s->m@(); }\n")
        << QString("category: function\n"
                   "mark: m()\n"
                   "help: S::m, m, m\n"
                   "tooltip: void S::m()\n"
                   "link: file.cpp:1:16\n"
                   "name: m\n"
                   "qualified: S::m\n"
                   "icon: public function");

    // A variable is shown as its own declaration, and where its type is a
    // class the help goes to that class: nobody documents a variable. The
    // icon says "public" for a local, which has no access to speak of.
    QTest::newRow("a variable of a class type")
        << QByteArray("class C {};\n"
                      "void f() { C c; c@; }\n")
        << QString("category: class-or-namespace\n"
                   "mark: C\n"
                   "help: C\n"
                   "tooltip: C\n"
                   "link: file.cpp:2:13\n"
                   "name: c\n"
                   "qualified: c\n"
                   "icon: public variable");

    QTest::newRow("a variable of a built-in type")
        << QByteArray("void f() { int i; i@; }\n")
        << QString("category: unknown\n"
                   "mark: i\n"
                   "help: i\n"
                   "tooltip: int i\n"
                   "link: file.cpp:1:15\n"
                   "name: i\n"
                   "qualified: i\n"
                   "icon: public variable");

    QTest::newRow("an enum")
        << QByteArray("enum E { A };\n"
                      "E@ e;\n")
        << QString("category: enum\n"
                   "mark: E\n"
                   "help: E\n"
                   "tooltip: E\n"
                   "link: file.cpp:1:5\n"
                   "name: E\n"
                   "qualified: E\n"
                   "icon: enum");

    // An enumerator is shown with the enum it belongs to and the value it
    // stands for. Its help goes under the bare name: the enclosing scope
    // recorded for it is the namespace an unscoped enum puts it in, not
    // the enum, so nothing qualifies it.
    QTest::newRow("an enumerator")
        << QByteArray("enum E { A = 1 };\n"
                      "E e = A@;\n")
        << QString("category: enum\n"
                   "mark: E\n"
                   "help: A\n"
                   "tooltip: E A = 1\n"
                   "link: file.cpp:1:9\n"
                   "name: A\n"
                   "qualified: A\n"
                   "icon: enumerator");

    // The name the typedef was given, not the class behind it: what a
    // tooltip says is what stands there. (The type hierarchy, which asks
    // the same machinery with followTypedef on, does go through to C.)
    QTest::newRow("a typedef")
        << QByteArray("class C {};\n"
                      "typedef C D;\n"
                      "D@ d;\n")
        << QString("category: typedef\n"
                   "mark: D\n"
                   "help: D\n"
                   "tooltip: C D\n"
                   "link: file.cpp:2:10\n"
                   "name: D\n"
                   "qualified: D\n"
                   "icon: public variable");

    // An alias is shown as a type rather than as a declaration: no name
    // for a parameter, and nothing said about what it hands back. That is
    // the one place a declaration is printed differently from everywhere
    // else, which is why how an alias reads is said apart from how a
    // declaration does.
    QTest::newRow("a typedef of a function type")
        << QByteArray("typedef void F(int a);\n"
                      "F@ *f;\n")
        << QString("category: typedef\n"
                   "mark: F\n"
                   "help: F\n"
                   "tooltip: F(int)\n"
                   "link: file.cpp:1:13\n"
                   "name: F\n"
                   "qualified: F\n"
                   "icon: public function");

    // A macro is not a name the parser resolved: it is read off what the
    // preprocessor recorded, with its replacement as the tooltip.
    QTest::newRow("a macro")
        << QByteArray("#define VALUE 42\n"
                      "int i = VAL@UE;\n")
        << QString("category: macro\n"
                   "mark: VALUE\n"
                   "help: VALUE\n"
                   "tooltip: #define VALUE 42\n"
                   "link: file.cpp:1:0");

    QTest::newRow("a position on nothing")
        << QByteArray("int i;\n"
                      "@\n")
        << QString("nothing");
}

void ElementEvaluatorTest::testElementUnderCursor()
{
    QFETCH(QByteArray, source);
    QFETCH(QString, expected);

    CppEditor::Tests::TestCase testCase;
    QVERIFY(testCase.succeededSoFar());

    const int cursorPosition = source.indexOf('@');
    QVERIFY(cursorPosition != -1);
    source.remove(cursorPosition, 1);

    CppEditor::Tests::TemporaryDir dir;
    QVERIFY(dir.isValid());
    CppTestDocument file("file.cpp", source);
    file.setBaseDirectory(dir.path());
    QVERIFY(file.writeToDisk());

    TextEditor::BaseTextEditor *editor = nullptr;
    CppEditorWidget *widget = nullptr;
    QVERIFY(CppEditor::Tests::TestCase::openCppEditor(file.filePath(), &editor, &widget));
    testCase.closeEditorAtEndOfTestCase(editor);
    QVERIFY(CppEditor::Tests::TestCase::waitForRehighlightedSemanticDocument(widget));

    QTextCursor cursor = widget->textCursor();
    cursor.setPosition(cursorPosition);
    CppElementEvaluator evaluator(widget);
    evaluator.setTextCursor(cursor);
    evaluator.execute();

    QCOMPARE(describe(evaluator.cppElement()), expected);
}

} // namespace CppEditor::Internal::Tests
