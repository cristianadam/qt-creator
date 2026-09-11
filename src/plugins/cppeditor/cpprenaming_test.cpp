// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpprenaming_test.h"

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
    QTest::newRow("class name")
        << QStringList{"file.h", "file.cpp"}
        << QByteArrayList{origHeaderClassName, testClassSource}
        << QByteArrayList{newHeaderClassName, newSourceClassName} << QString("MyNewClass");

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
    QTest::newRow("method name")
        << QStringList{"file.h", "file.cpp"}
        << QByteArrayList{testClassHeader, origSourceMethodName}
        << QByteArrayList{newHeaderMethodName, newSourceMethodName} << QString("runAgain");

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
        << QString("renamed");

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
        << QString("walk");

    // A variable, where each place is a use rather than a declaration.
    QTest::newRow("a variable used in two files")
        << QStringList{"file.h", "file.cpp", "other.cpp"}
        << QByteArrayList{"extern int cou@nt;\n",
                          "#include \"file.h\"\nint count = 0;\nvoid f() { count = 1; }\n",
                          "#include \"file.h\"\nint g() { return count; }\n"}
        << QByteArrayList{"extern int total;\n",
                          "#include \"file.h\"\nint total = 0;\nvoid f() { total = 1; }\n",
                          "#include \"file.h\"\nint g() { return total; }\n"}
        << QString("total");
}

void GlobalRenamingTest::test()
{
    QFETCH(QStringList, fileNames);
    QFETCH(QByteArrayList, originals);
    QFETCH(QByteArrayList, expected);
    QFETCH(QString, replacement);

    QCOMPARE(originals.size(), fileNames.size());
    QCOMPARE(expected.size(), fileNames.size());

    QList<TestDocumentPtr> testDocuments;
    for (int i = 0; i < fileNames.size(); ++i) {
        testDocuments << CppTestDocument::create(fileNames.at(i).toUtf8(), originals.at(i),
                                                 expected.at(i));
    }
    RenamingTestRunner testRunner(testDocuments, replacement);
}

} // namespace CppEditor::Internal::Tests
