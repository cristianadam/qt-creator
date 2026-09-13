// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpprenaming_test.h"

#include "cppcodemodelsettings.h"
#include "cppeditorwidget.h"
#include "cppmodelmanager.h"
#include "quickfixes/cppquickfix_test.h"

#include <texteditor/texteditor.h>

#include <QEventLoop>
#include <QTest>
#include <QTimer>

namespace CppEditor::Internal::Tests {

class RenamingTestRunner : public BaseQuickFixTestCase
{
public:
    RenamingTestRunner(const QList<TestDocumentPtr> &testDocuments, const QString &replacement)
        : BaseQuickFixTestCase(testDocuments, {})
    {
        QVERIFY(succeededSoFar());
        const TestDocumentPtr &doc = m_documentWithMarker;
        const CursorInEditor cursorInEditor(doc->m_editor->textCursor(), doc->filePath(),
                                            doc->m_editorWidget, doc->m_editor->textDocument());

        QEventLoop loop;
        CppModelManager::globalRename(cursorInEditor, replacement, [&loop]{ loop.quit(); });
        QTimer::singleShot(10000, &loop, [&loop] { loop.exit(1); });
        QVERIFY(loop.exec() == 0);

        // Compare all files
        for (const TestDocumentPtr &testDocument : std::as_const(m_testDocuments)) {
            QString result = testDocument->m_editorWidget->document()->toPlainText();
            if (result != testDocument->m_expectedSource) {
                qDebug() << "---" << testDocument->m_expectedSource;
                qDebug() << "+++" << result;
            }
            QCOMPARE(result, testDocument->m_expectedSource);

            // Undo the change
            for (int i = 0; i < 100; ++i)
                testDocument->m_editorWidget->undo();
            result = testDocument->m_editorWidget->document()->toPlainText();
            QCOMPARE(result, testDocument->m_source);
        }
    }
};

void GlobalRenamingTest::test_data()
{
    QTest::addColumn<QStringList>("fileNames");
    QTest::addColumn<QByteArrayList>("originals");
    QTest::addColumn<QByteArrayList>("expected");
    // What it writes when the search is asked to say what each place does
    // with the thing, where that is not the same. Empty where it is.
    QTest::addColumn<QByteArrayList>("whenCategorized");
    QTest::addColumn<QString>("replacement");

    const char testClassHeader[] = R"cpp(
/**
 * \brief MyClass
 */
class MyClass {
  /** \brief MyClass::MyClass */
  MyClass() {}
  ~MyClass();
  /** \brief MyClass::run */
  void run();
};
)cpp";
    const char testClassSource[] = R"cpp(
#include "file.h"
/** \brief MyClass::~MyClass */
MyClass::~MyClass() {}

void MyClass::run() {}
)cpp";

    QByteArray origHeaderClassName(testClassHeader);
    const int classOffset = origHeaderClassName.indexOf("class MyClass");
    QVERIFY(classOffset != -1);
    origHeaderClassName.insert(classOffset + 6, '@');
    const QByteArray newHeaderClassName = R"cpp(
/**
 * \brief MyNewClass
 */
class MyNewClass {
  /** \brief MyNewClass::MyNewClass */
  MyNewClass() {}
  ~MyNewClass();
  /** \brief MyNewClass::run */
  void run();
};
)cpp";
    const QByteArray newSourceClassName = R"cpp(
#include "file.h"
/** \brief MyNewClass::~MyNewClass */
MyNewClass::~MyNewClass() {}

void MyNewClass::run() {}
)cpp";
    // What the comments say is left alone when the results are categorised,
    // which is a wart of that setting rather than of either front end: the
    // occurrences in comments are found and added to the results, and then
    // the replacement does not reach them. Pinned so that it is visible.
    const QByteArray categorizedHeaderClassName = R"cpp(
/**
 * \brief MyClass
 */
class MyNewClass {
  /** \brief MyClass::MyClass */
  MyNewClass() {}
  ~MyNewClass();
  /** \brief MyClass::run */
  void run();
};
)cpp";
    const QByteArray categorizedSourceClassName = R"cpp(
#include "file.h"
/** \brief MyClass::~MyClass */
MyNewClass::~MyNewClass() {}

void MyNewClass::run() {}
)cpp";
    QTest::newRow("class name")
        << QStringList{"file.h", "file.cpp"}
        << QByteArrayList{origHeaderClassName, testClassSource}
        << QByteArrayList{newHeaderClassName, newSourceClassName}
        << QByteArrayList{categorizedHeaderClassName, categorizedSourceClassName}
        << QString("MyNewClass");

    QByteArray origSourceMethodName(testClassSource);
    const int methodOffset = origSourceMethodName.indexOf("::run()");
    QVERIFY(methodOffset != -1);
    origSourceMethodName.insert(methodOffset + 2, '@');
    const QByteArray newHeaderMethodName = R"cpp(
/**
 * \brief MyClass
 */
class MyClass {
  /** \brief MyClass::MyClass */
  MyClass() {}
  ~MyClass();
  /** \brief MyClass::runAgain */
  void runAgain();
};
)cpp";
    const QByteArray newSourceMethodName = R"cpp(
#include "file.h"
/** \brief MyClass::~MyClass */
MyClass::~MyClass() {}

void MyClass::runAgain() {}
)cpp";
    const QByteArray categorizedHeaderMethodName = R"cpp(
/**
 * \brief MyClass
 */
class MyClass {
  /** \brief MyClass::MyClass */
  MyClass() {}
  ~MyClass();
  /** \brief MyClass::run */
  void runAgain();
};
)cpp";
    QTest::newRow("method name")
        << QStringList{"file.h", "file.cpp"}
        << QByteArrayList{testClassHeader, origSourceMethodName}
        << QByteArrayList{newHeaderMethodName, newSourceMethodName}
        << QByteArrayList{categorizedHeaderMethodName, newSourceMethodName}
        << QString("runAgain");

    // A file the search has to reach on its own: nothing in the header says
    // that this one uses what it declares.
    QTest::newRow("used in a second source")
        << QStringList{"file.h", "file.cpp", "other.cpp"}
        << QByteArrayList{"void fu@nc();\n",
                          "#include \"file.h\"\nvoid func() {}\n",
                          "#include \"file.h\"\nvoid g() { func(); }\n"}
        << QByteArrayList{"void renamed();\n",
                          "#include \"file.h\"\nvoid renamed() {}\n",
                          "#include \"file.h\"\nvoid g() { renamed(); }\n"}
        << QByteArrayList() << QString("renamed");

    // A name spelled the same and meaning something else is left alone,
    // which is the difference between this and a text replacement.
    QTest::newRow("a name that means something else")
        << QStringList{"file.h", "file.cpp", "other.h"}
        << QByteArrayList{"struct A { void ru@n(); };\n",
                          "#include \"file.h\"\n"
                          "#include \"other.h\"\n"
                          "void A::run() {}\n"
                          "void B::run() {}\n"
                          "void g(A &a, B &b) { a.run(); b.run(); }\n",
                          "struct B { void run(); };\n"}
        << QByteArrayList{"struct A { void walk(); };\n",
                          "#include \"file.h\"\n"
                          "#include \"other.h\"\n"
                          "void A::walk() {}\n"
                          "void B::run() {}\n"
                          "void g(A &a, B &b) { a.walk(); b.run(); }\n",
                          "struct B { void run(); };\n"}
        << QByteArrayList() << QString("walk");

    // A variable a lambda in the same function uses. What the lambda
    // captured is its own thing, and a rename that stops at the capture
    // leaves the body naming something that is no longer there.
    QTest::newRow("used inside a lambda")
        << QStringList{"file.cpp"}
        << QByteArrayList{"void f() {\n"
                          "    int cou@nt = 0;\n"
                          "    auto byRef = [&count] { count = 1; };\n"
                          "    auto byValue = [count] { return count; };\n"
                          "    auto byDefault = [&] { count = 2; };\n"
                          "}\n"}
        << QByteArrayList{"void f() {\n"
                          "    int total = 0;\n"
                          "    auto byRef = [&total] { total = 1; };\n"
                          "    auto byValue = [total] { return total; };\n"
                          "    auto byDefault = [&] { total = 2; };\n"
                          "}\n"}
        << QByteArrayList() << QString("total");

    // A variable, where each place is a use rather than a declaration.
    QTest::newRow("a variable used in two files")
        << QStringList{"file.h", "file.cpp", "other.cpp"}
        << QByteArrayList{"extern int cou@nt;\n",
                          "#include \"file.h\"\nint count = 0;\nvoid f() { count = 1; }\n",
                          "#include \"file.h\"\nint g() { return count; }\n"}
        << QByteArrayList{"extern int total;\n",
                          "#include \"file.h\"\nint total = 0;\nvoid f() { total = 1; }\n",
                          "#include \"file.h\"\nint g() { return total; }\n"}
        << QByteArrayList() << QString("total");
}

void GlobalRenamingTest::test()
{
    QFETCH(QStringList, fileNames);
    QFETCH(QByteArrayList, originals);
    QFETCH(QByteArrayList, expected);
    QFETCH(QByteArrayList, whenCategorized);
    QFETCH(QString, replacement);

    QCOMPARE(originals.size(), fileNames.size());
    QCOMPARE(expected.size(), fileNames.size());

    // The same rename with the search saying what each place does with the
    // thing and without. It is one search either way and the places it finds
    // must be the same ones; the setting only decides whether they are
    // sorted into reads and writes for the view.
    for (const bool categorize : {false, true}) {
        const bool saved = CppCodeModelSettings::categorizeFindReferences();
        CppCodeModelSettings::setCategorizeFindReferences(categorize);
        const QScopeGuard restore([saved] {
            CppCodeModelSettings::setCategorizeFindReferences(saved);
        });

        const QByteArrayList &wanted = categorize && !whenCategorized.isEmpty() ? whenCategorized
                                                                                : expected;
        QList<TestDocumentPtr> testDocuments;
        for (int i = 0; i < fileNames.size(); ++i) {
            testDocuments << CppTestDocument::create(fileNames.at(i).toUtf8(), originals.at(i),
                                                     wanted.at(i));
        }
        RenamingTestRunner testRunner(testDocuments, replacement);
        if (QTest::currentTestFailed())
            return;
    }
}

} // namespace CppEditor::Internal::Tests
