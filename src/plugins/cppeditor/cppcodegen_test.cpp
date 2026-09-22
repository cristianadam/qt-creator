// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodegen_test.h"

#include "cpptoolstestcase.h"
#include "insertionpointlocator.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"
#endif

#include <utils/qtcassert.h>
#include <utils/temporarydirectory.h>

#include <QDebug>
#include <QTest>

/*!
    Tests for various parts of the code generation. Well, okay, currently it only
    tests the InsertionPointLocator.
 */
using namespace CPlusPlus;
using namespace Utils;

using CppEditor::Tests::TemporaryDir;

namespace CppEditor::Internal {

static Document::Ptr createDocument(const FilePath &filePath, const QByteArray &text,
                                    int expectedGlobalSymbolCount)
{
    Document::Ptr document = Document::create(filePath);

    // With a #line marker in front, as the source processor's output has:
    // without one the built-in translation unit counts lines from zero, and
    // then every line these cases name is one less than the line the text
    // has it on. The marker's own line is consumed by it.
    document->setUtf8Source("#line 1 \"" + filePath.path().toUtf8() + "\"\n" + text);
    document->check();
    QTC_ASSERT(document->diagnosticMessages().isEmpty(), return Document::Ptr());
    QTC_ASSERT(document->globalSymbolCount() == expectedGlobalSymbolCount, return Document::Ptr());

#ifdef QTC_WITH_CXX_FRONTEND
    // What the editor's parser does when the other model is asked for: run it
    // over the file, so that the locator finds it and reads that tree
    // instead. The cases below are the same either way -- which tree answers
    // is decided by the environment, exactly as in the editor.
    if (cxxFrontendModelRequested()) {
        WorkingCopy workingCopy;
        workingCopy.insert(filePath, text);
        updateCxxFrontendModel(filePath, {}, workingCopy);
    }
#endif

    return document;
}

static Document::Ptr createDocumentAndFile(TemporaryDir *temporaryDir,
                                           const QByteArray relativeFilePath,
                                           const QByteArray text,
                                           int expectedGlobalSymbolCount)
{
    QTC_ASSERT(temporaryDir, return Document::Ptr());
    const FilePath absoluteFilePath = temporaryDir->createFile(relativeFilePath, text);
    QTC_ASSERT(!absoluteFilePath.isEmpty(), return Document::Ptr());

    return createDocument(absoluteFilePath, text, expectedGlobalSymbolCount);
}

/*!
    Should insert at line 4, column 1, with "public:\n" as prefix and without suffix.
 */
void CodegenTest::testPublicInEmptyClass()
{
    const QByteArray src = "\n"
            "class Foo\n" // line 1
            "{\n"
            "};\n"
            "\n";
    Document::Ptr doc = createDocument("public_in_empty_class", src, 1);
    QVERIFY(doc);

    Class *foo = doc->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);

    Snapshot snapshot;
    snapshot.insert(doc);
    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    InsertionLocation loc = find.methodDeclarationInClass(
                doc->filePath(),
                foo,
                InsertionPointLocator::Public);
    QVERIFY(loc.isValid());
    QCOMPARE(loc.prefix(), QLatin1String("public:\n"));
    QVERIFY(loc.suffix().isEmpty());
    QCOMPARE(loc.line(), 4);
    QCOMPARE(loc.column(), 1);
}

/*!
    Should insert at line 4, column 1, without prefix and without suffix.
 */
void CodegenTest::testPublicInNonemptyClass()
{
    const QByteArray src = "\n"
            "class Foo\n" // line 1
            "{\n"
            "public:\n"   // line 3
            "};\n"        // line 4
            "\n";
    Document::Ptr doc = createDocument("public_in_nonempty_class", src, 1);
    QVERIFY(doc);

    Class *foo = doc->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);

    Snapshot snapshot;
    snapshot.insert(doc);
    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    InsertionLocation loc = find.methodDeclarationInClass(
                doc->filePath(),
                foo,
                InsertionPointLocator::Public);
    QVERIFY(loc.isValid());
    QVERIFY(loc.prefix().isEmpty());
    QVERIFY(loc.suffix().isEmpty());
    QCOMPARE(loc.line(), 5);
    QCOMPARE(loc.column(), 1);
}

/*!
    Should insert at line 4, column 1, with "public:\n" as prefix and "\n suffix.
 */
void CodegenTest::testPublicBeforeProtected()
{
    const QByteArray src = "\n"
            "class Foo\n"  // line 1
            "{\n"
            "protected:\n" // line 3
            "};\n"
            "\n";
    Document::Ptr doc = createDocument("public_before_protected", src, 1);
    QVERIFY(doc);

    Class *foo = doc->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);

    Snapshot snapshot;
    snapshot.insert(doc);
    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    InsertionLocation loc = find.methodDeclarationInClass(
                doc->filePath(),
                foo,
                InsertionPointLocator::Public);
    QVERIFY(loc.isValid());
    QCOMPARE(loc.prefix(), QLatin1String("public:\n"));
    QCOMPARE(loc.suffix(), QLatin1String("\n"));
    QCOMPARE(loc.column(), 1);
    QCOMPARE(loc.line(), 4);
}

/*!
    Should insert at line 5, column 1, with "private:\n" as prefix and without
    suffix.
 */
void CodegenTest::testPrivateAfterProtected()
{
    const QByteArray src = "\n"
            "class Foo\n"  // line 1
            "{\n"
            "protected:\n" // line 3
            "};\n"
            "\n";
    Document::Ptr doc = createDocument("private_after_protected", src, 1);
    QVERIFY(doc);

    Class *foo = doc->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);

    Snapshot snapshot;
    snapshot.insert(doc);
    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    InsertionLocation loc = find.methodDeclarationInClass(
                doc->filePath(),
                foo,
                InsertionPointLocator::Private);
    QVERIFY(loc.isValid());
    QCOMPARE(loc.prefix(), QLatin1String("private:\n"));
    QVERIFY(loc.suffix().isEmpty());
    QCOMPARE(loc.column(), 1);
    QCOMPARE(loc.line(), 5);
}

/*!
    Should insert at line 5, column 1, with "protected:\n" as prefix and without
    suffix.
 */
void CodegenTest::testProtectedInNonemptyClass()
{
    const QByteArray src = "\n"
            "class Foo\n" // line 1
            "{\n"
            "public:\n"   // line 3
            "};\n"        // line 4
            "\n";
    Document::Ptr doc = createDocument("protected_in_nonempty_class", src, 1);
    QVERIFY(doc);

    Class *foo = doc->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);

    Snapshot snapshot;
    snapshot.insert(doc);
    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    InsertionLocation loc = find.methodDeclarationInClass(
                doc->filePath(),
                foo,
                InsertionPointLocator::Protected);
    QVERIFY(loc.isValid());
    QCOMPARE(loc.prefix(), QLatin1String("protected:\n"));
    QVERIFY(loc.suffix().isEmpty());
    QCOMPARE(loc.column(), 1);
    QCOMPARE(loc.line(), 5);
}

/*!
    Should insert at line 5, column 1, with "protected\n" as prefix and "\n" suffix.
 */
void CodegenTest::testProtectedBetweenPublicAndPrivate()
{
    const QByteArray src = "\n"
            "class Foo\n" // line 1
            "{\n"
            "public:\n"   // line 3
            "private:\n"  // line 4
            "};\n"        // line 5
            "\n";
    Document::Ptr doc = createDocument("protected_betwee_public_and_private", src, 1);
    QVERIFY(doc);

    Class *foo = doc->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);

    Snapshot snapshot;
    snapshot.insert(doc);
    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    InsertionLocation loc = find.methodDeclarationInClass(
                doc->filePath(),
                foo,
                InsertionPointLocator::Protected);
    QVERIFY(loc.isValid());
    QCOMPARE(loc.prefix(), QLatin1String("protected:\n"));
    QCOMPARE(loc.suffix(), QLatin1String("\n"));
    QCOMPARE(loc.column(), 1);
    QCOMPARE(loc.line(), 5);
}

/*!
    Should insert at line 19, column 1, with "private slots:\n" as prefix and "\n"
    as suffix.

    This is the typical \QD case, with test-input like what the integration
    generates.
 */
void CodegenTest::testQtdesignerIntegration()
{
    const QByteArray src = "/**** Some long (C)opyright notice ****/\n"
            "#ifndef MAINWINDOW_H\n"
            "#define MAINWINDOW_H\n"
            "\n"
            "#include <QMainWindow>\n"
            "\n"
            "namespace Ui {\n"
            "    class MainWindow;\n"
            "}\n"
            "\n"
            "class MainWindow : public QMainWindow\n" // line 10
            "{\n"
            "    Q_OBJECT\n"
            "\n"
            "public:\n" // line 14
            "    explicit MainWindow(QWidget *parent = 0);\n"
            "    ~MainWindow();\n"
            "\n"
            "private:\n" // line 18
            "    Ui::MainWindow *ui;\n"
            "};\n"
            "\n"
            "#endif // MAINWINDOW_H\n";

    Document::Ptr doc = createDocument("qtdesigner_integration", src, 2);
    QVERIFY(doc);

    Class *foo = doc->globalSymbolAt(1)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 11);
    QCOMPARE(foo->column(), 7);

    Snapshot snapshot;
    snapshot.insert(doc);
    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    InsertionLocation loc = find.methodDeclarationInClass(
                doc->filePath(),
                foo,
                InsertionPointLocator::PrivateSlot);
    QVERIFY(loc.isValid());
    QCOMPARE(loc.prefix(), QLatin1String("private slots:\n"));
    QCOMPARE(loc.suffix(), QLatin1String("\n"));
    QCOMPARE(loc.line(), 19);
    QCOMPARE(loc.column(), 1);
}

void CodegenTest::testDefinitionEmptyClass()
{
    TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QByteArray headerText = "\n"
            "class Foo\n"  // line 1
            "{\n"
            "void foo();\n" // line 3
            "};\n"
            "\n";
    Document::Ptr headerDocument = createDocumentAndFile(&temporaryDir, "file.h", headerText, 1);
    QVERIFY(headerDocument);

    const QByteArray sourceText = "\n"
            "int x;\n"  // line 1
            "\n";
    Document::Ptr sourceDocument = createDocumentAndFile(&temporaryDir, "file.cpp", sourceText, 1);
    QVERIFY(sourceDocument);

    Snapshot snapshot;
    snapshot.insert(headerDocument);
    snapshot.insert(sourceDocument);

    Class *foo = headerDocument->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);
    QCOMPARE(foo->memberCount(), 1);
    Declaration *decl = foo->memberAt(0)->asDeclaration();
    QVERIFY(decl);
    QCOMPARE(decl->line(), 4);
    QCOMPARE(decl->column(), 6);

    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    QList<InsertionLocation> locList = find.methodDefinition(decl);
    QVERIFY(locList.size() == 1);
    InsertionLocation loc = locList.first();
    QCOMPARE(loc.filePath(), sourceDocument->filePath());
    // One new line rather than two, the definition going at the end of the
    // file. Which is what the editor has always written here: it asked about
    // a document the source processor had put a #line marker in front of, and
    // so about the line the text really has this on.
    QCOMPARE(loc.prefix(), QLatin1String("\n"));
    QCOMPARE(loc.suffix(), QLatin1String("\n"));
    QCOMPARE(loc.line(), 4);
    QCOMPARE(loc.column(), 1);
}

void CodegenTest::testDefinitionFirstMember()
{
    TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QByteArray headerText = "\n"
            "class Foo\n"  // line 1
            "{\n"
            "void foo();\n" // line 3
            "void bar();\n" // line 4
            "};\n"
            "\n";
    Document::Ptr headerDocument = createDocumentAndFile(&temporaryDir, "file.h", headerText, 1);
    QVERIFY(headerDocument);

    const QByteArray sourceText = QString::fromLatin1(
            "\n"
            "#include \"%1/file.h\"\n" // line 1
            "int x;\n"
            "\n"
            "void Foo::bar()\n" // line 4
            "{\n"
            "\n"
            "}\n"
            "\n"
            "int y;\n").arg(temporaryDir.path()).toLatin1();
    Document::Ptr sourceDocument = createDocumentAndFile(&temporaryDir, "file.cpp", sourceText, 3);
    QVERIFY(sourceDocument);
    // The name as the source writes it, which is the whole point of
    // recording it: whoever resolves that directive again looks it up by
    // what is written there.
    sourceDocument->addIncludeFile(Document::Include(headerDocument->filePath().path(),
                                                     headerDocument->filePath(), 1,
                                                     Client::IncludeLocal));

    Snapshot snapshot;
    snapshot.insert(headerDocument);
    snapshot.insert(sourceDocument);

    Class *foo = headerDocument->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);
    QCOMPARE(foo->memberCount(), 2);
    Declaration *decl = foo->memberAt(0)->asDeclaration();
    QVERIFY(decl);
    QCOMPARE(decl->line(), 4);
    QCOMPARE(decl->column(), 6);

    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    QList<InsertionLocation> locList = find.methodDefinition(decl);
    QVERIFY(locList.size() == 1);
    InsertionLocation loc = locList.first();
    QCOMPARE(loc.filePath(), sourceDocument->filePath());
    QCOMPARE(loc.line(), 5);
    QCOMPARE(loc.column(), 1);
    QCOMPARE(loc.suffix(), QLatin1String("\n\n"));
    QCOMPARE(loc.prefix(), QString());
}

void CodegenTest::testDefinitionLastMember()
{
    TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QByteArray headerText = "\n"
            "class Foo\n"  // line 1
            "{\n"
            "void foo();\n" // line 3
            "void bar();\n" // line 4
            "};\n"
            "\n";
    Document::Ptr headerDocument = createDocumentAndFile(&temporaryDir, "file.h", headerText, 1);
    QVERIFY(headerDocument);

    const QByteArray sourceText = QString::fromLatin1(
            "\n"
            "#include \"%1/file.h\"\n" // line 1
            "int x;\n"
            "\n"
            "void Foo::foo()\n" // line 4
            "{\n"
            "\n"
            "}\n" // line 7
            "\n"
            "int y;\n").arg(temporaryDir.path()).toLatin1();

    Document::Ptr sourceDocument = createDocumentAndFile(&temporaryDir, "file.cpp", sourceText, 3);
    QVERIFY(sourceDocument);
    // The name as the source writes it, which is the whole point of
    // recording it: whoever resolves that directive again looks it up by
    // what is written there.
    sourceDocument->addIncludeFile(Document::Include(headerDocument->filePath().path(),
                                                     headerDocument->filePath(), 1,
                                                     Client::IncludeLocal));

    Snapshot snapshot;
    snapshot.insert(headerDocument);
    snapshot.insert(sourceDocument);

    Class *foo = headerDocument->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);
    QCOMPARE(foo->memberCount(), 2);
    Declaration *decl = foo->memberAt(1)->asDeclaration();
    QVERIFY(decl);
    QCOMPARE(decl->line(), 5);
    QCOMPARE(decl->column(), 6);

    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    QList<InsertionLocation> locList = find.methodDefinition(decl);
    QVERIFY(locList.size() == 1);
    InsertionLocation loc = locList.first();
    QCOMPARE(loc.filePath(), sourceDocument->filePath());
    QCOMPARE(loc.line(), 8);
    QCOMPARE(loc.column(), 2);
    QCOMPARE(loc.prefix(), QLatin1String("\n\n"));
    QCOMPARE(loc.suffix(), QString());
}

void CodegenTest::testDefinitionMiddleMember()
{
    TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QByteArray headerText = "\n"
            "class Foo\n"  // line 1
            "{\n"
            "void foo();\n" // line 3
            "void bar();\n" // line 4
            "void car();\n" // line 5
            "};\n"
            "\n";

    Document::Ptr headerDocument = createDocumentAndFile(&temporaryDir, "file.h", headerText, 1);
    QVERIFY(headerDocument);

    const QByteArray sourceText = QString::fromLatin1(
            "\n"
            "#include \"%1/file.h\"\n" // line 1
            "int x;\n"
            "\n"
            "void Foo::foo()\n" // line 4
            "{\n"
            "\n"
            "}\n" // line 7
            "\n"
            "void Foo::car()\n" // line 9
            "{\n"
            "\n"
            "}\n"
            "\n"
            "int y;\n").arg(temporaryDir.path()).toLatin1();

    Document::Ptr sourceDocument = createDocumentAndFile(&temporaryDir, "file.cpp", sourceText, 4);
    QVERIFY(sourceDocument);
    // The name as the source writes it, which is the whole point of
    // recording it: whoever resolves that directive again looks it up by
    // what is written there.
    sourceDocument->addIncludeFile(Document::Include(headerDocument->filePath().path(),
                                                     headerDocument->filePath(), 1,
                                                     Client::IncludeLocal));

    Snapshot snapshot;
    snapshot.insert(headerDocument);
    snapshot.insert(sourceDocument);

    Class *foo = headerDocument->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);
    QCOMPARE(foo->memberCount(), 3);
    Declaration *decl = foo->memberAt(1)->asDeclaration();
    QVERIFY(decl);
    QCOMPARE(decl->line(), 5);
    QCOMPARE(decl->column(), 6);

    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    QList<InsertionLocation> locList = find.methodDefinition(decl);
    QVERIFY(locList.size() == 1);
    InsertionLocation loc = locList.first();
    QCOMPARE(loc.filePath(), sourceDocument->filePath());
    QCOMPARE(loc.line(), 8);
    QCOMPARE(loc.column(), 2);
    QCOMPARE(loc.prefix(), QLatin1String("\n\n"));
    QCOMPARE(loc.suffix(), QString());
}

void CodegenTest::testDefinitionMiddleMemberSurroundedByUndefined()
{
    TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QByteArray headerText = "\n"
            "class Foo\n"  // line 1
            "{\n"
            "void foo();\n" // line 3
            "void bar();\n" // line 4
            "void baz();\n" // line 5
            "void car();\n" // line 6
            "};\n"
            "\n";
    Document::Ptr headerDocument = createDocumentAndFile(&temporaryDir, "file.h", headerText, 1);
    QVERIFY(headerDocument);

    const QByteArray sourceText = QString::fromLatin1(
            "\n"
            "#include \"%1/file.h\"\n" // line 1
            "int x;\n"
            "\n"
            "void Foo::car()\n" // line 4
            "{\n"
            "\n"
            "}\n"
            "\n"
            "int y;\n").arg(temporaryDir.path()).toLatin1();
    Document::Ptr sourceDocument = createDocumentAndFile(&temporaryDir, "file.cpp", sourceText, 3);
    QVERIFY(sourceDocument);
    // The name as the source writes it, which is the whole point of
    // recording it: whoever resolves that directive again looks it up by
    // what is written there.
    sourceDocument->addIncludeFile(Document::Include(headerDocument->filePath().path(),
                                                     headerDocument->filePath(), 1,
                                                     Client::IncludeLocal));

    Snapshot snapshot;
    snapshot.insert(headerDocument);
    snapshot.insert(sourceDocument);

    Class *foo = headerDocument->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);
    QCOMPARE(foo->memberCount(), 4);
    Declaration *decl = foo->memberAt(1)->asDeclaration();
    QVERIFY(decl);
    QCOMPARE(decl->line(), 5);
    QCOMPARE(decl->column(), 6);

    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    QList<InsertionLocation> locList = find.methodDefinition(decl);
    QVERIFY(locList.size() == 1);
    InsertionLocation loc = locList.first();
    QCOMPARE(loc.filePath(), sourceDocument->filePath());
    QCOMPARE(loc.line(), 5);
    QCOMPARE(loc.column(), 1);
    QCOMPARE(loc.prefix(), QString());
    QCOMPARE(loc.suffix(), QLatin1String("\n\n"));
}

void CodegenTest::testDefinitionMemberSpecificFile()
{
    TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QByteArray headerText = "\n"
            "class Foo\n"  // line 1
            "{\n"
            "void foo();\n" // line 3
            "void bar();\n" // line 4
            "void baz();\n" // line 5
            "};\n"
            "\n"
            "void Foo::bar()\n"
            "{\n"
            "\n"
            "}\n";
    Document::Ptr headerDocument = createDocumentAndFile(&temporaryDir, "file.h", headerText, 2);
    QVERIFY(headerDocument);

    const QByteArray sourceText = QString::fromLatin1(
            "\n"
            "#include \"%1/file.h\"\n" // line 1
            "int x;\n"
            "\n"
            "void Foo::foo()\n" // line 4
            "{\n"
            "\n"
            "}\n" // line 7
            "\n"
            "int y;\n").arg(temporaryDir.path()).toLatin1();
    Document::Ptr sourceDocument = createDocumentAndFile(&temporaryDir, "file.cpp", sourceText, 3);
    QVERIFY(sourceDocument);
    // The name as the source writes it, which is the whole point of
    // recording it: whoever resolves that directive again looks it up by
    // what is written there.
    sourceDocument->addIncludeFile(Document::Include(headerDocument->filePath().path(),
                                                     headerDocument->filePath(), 1,
                                                     Client::IncludeLocal));

    Snapshot snapshot;
    snapshot.insert(headerDocument);
    snapshot.insert(sourceDocument);

    Class *foo = headerDocument->globalSymbolAt(0)->asClass();
    QVERIFY(foo);
    QCOMPARE(foo->line(), 2);
    QCOMPARE(foo->column(), 7);
    QCOMPARE(foo->memberCount(), 3);
    Declaration *decl = foo->memberAt(2)->asDeclaration();
    QVERIFY(decl);
    QCOMPARE(decl->line(), 6);
    QCOMPARE(decl->column(), 6);

    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    QList<InsertionLocation> locList =
            find.methodDefinition(decl, true, sourceDocument->filePath());
    QVERIFY(locList.size() == 1);
    InsertionLocation loc = locList.first();
    QCOMPARE(loc.filePath(), sourceDocument->filePath());
    QCOMPARE(loc.line(), 8);
    QCOMPARE(loc.column(), 2);
    QCOMPARE(loc.prefix(), QLatin1String("\n\n"));
    QCOMPARE(loc.suffix(), QString());
}

/*!
    A definition goes inside the namespace the class is declared in, where the
    file it is going into writes that namespace: just in front of its "}".
 */
void CodegenTest::testDefinitionInTheNamespaceTheSourceWrites()
{
    TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QByteArray headerText = "\n"
            "namespace N {\n" // line 2
            "class Foo\n"
            "{\n"
            "void foo();\n"   // line 5
            "};\n"
            "}\n";
    Document::Ptr headerDocument = createDocumentAndFile(&temporaryDir, "file.h", headerText, 1);
    QVERIFY(headerDocument);

    const QByteArray sourceText = "\n"
            "namespace N {\n" // line 2
            "int x;\n"        // line 3
            "}\n";            // line 4
    Document::Ptr sourceDocument = createDocumentAndFile(&temporaryDir, "file.cpp", sourceText, 1);
    QVERIFY(sourceDocument);

    Snapshot snapshot;
    snapshot.insert(headerDocument);
    snapshot.insert(sourceDocument);

    Namespace *ns = headerDocument->globalSymbolAt(0)->asNamespace();
    QVERIFY(ns);
    Class *foo = ns->memberAt(0)->asClass();
    QVERIFY(foo);
    Declaration *decl = foo->memberAt(0)->asDeclaration();
    QVERIFY(decl);

    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    const QList<InsertionLocation> locList
        = find.methodDefinition(decl, true, sourceDocument->filePath());
    QVERIFY(locList.size() == 1);
    const InsertionLocation loc = locList.first();
    QCOMPARE(loc.filePath(), sourceDocument->filePath());
    QCOMPARE(loc.line(), 3);
    QCOMPARE(loc.column(), 7);
    QCOMPARE(loc.prefix(), QLatin1String("\n\n"));
    QCOMPARE(loc.suffix(), QLatin1String("\n"));
}

/*!
    The innermost of the namespaces it is declared in that the file writes.
 */
void CodegenTest::testDefinitionInTheInnermostNamespaceTheSourceWrites()
{
    TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QByteArray headerText = "\n"
            "namespace N {\n"
            "namespace M {\n"
            "class Foo\n"
            "{\n"
            "void foo();\n"
            "};\n"
            "}\n"
            "}\n";
    Document::Ptr headerDocument = createDocumentAndFile(&temporaryDir, "file.h", headerText, 1);
    QVERIFY(headerDocument);

    const QByteArray sourceText = "\n"
            "namespace N {\n" // line 2
            "namespace M {\n" // line 3
            "int x;\n"        // line 4
            "}\n"             // line 5
            "}\n";            // line 6
    Document::Ptr sourceDocument = createDocumentAndFile(&temporaryDir, "file.cpp", sourceText, 1);
    QVERIFY(sourceDocument);

    Snapshot snapshot;
    snapshot.insert(headerDocument);
    snapshot.insert(sourceDocument);

    Namespace *outer = headerDocument->globalSymbolAt(0)->asNamespace();
    QVERIFY(outer);
    Namespace *inner = outer->memberAt(0)->asNamespace();
    QVERIFY(inner);
    Class *foo = inner->memberAt(0)->asClass();
    QVERIFY(foo);
    Declaration *decl = foo->memberAt(0)->asDeclaration();
    QVERIFY(decl);

    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    const QList<InsertionLocation> locList
        = find.methodDefinition(decl, true, sourceDocument->filePath());
    QVERIFY(locList.size() == 1);
    const InsertionLocation loc = locList.first();
    QCOMPARE(loc.filePath(), sourceDocument->filePath());
    QCOMPARE(loc.line(), 4);
    QCOMPARE(loc.column(), 7);
    QCOMPARE(loc.prefix(), QLatin1String("\n\n"));
    QCOMPARE(loc.suffix(), QLatin1String("\n"));
}

/*!
    Where the file writes no such namespace there is nothing to go inside of,
    so the definition goes at the end of it -- the caller writing the
    namespace around it.
 */
void CodegenTest::testDefinitionWhereTheSourceWritesNoSuchNamespace()
{
    TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QByteArray headerText = "\n"
            "namespace N {\n"
            "class Foo\n"
            "{\n"
            "void foo();\n"
            "};\n"
            "}\n";
    Document::Ptr headerDocument = createDocumentAndFile(&temporaryDir, "file.h", headerText, 1);
    QVERIFY(headerDocument);

    const QByteArray sourceText = "\n"
            "int x;\n" // line 2
            "\n";
    Document::Ptr sourceDocument = createDocumentAndFile(&temporaryDir, "file.cpp", sourceText, 1);
    QVERIFY(sourceDocument);

    Snapshot snapshot;
    snapshot.insert(headerDocument);
    snapshot.insert(sourceDocument);

    Namespace *ns = headerDocument->globalSymbolAt(0)->asNamespace();
    QVERIFY(ns);
    Class *foo = ns->memberAt(0)->asClass();
    QVERIFY(foo);
    Declaration *decl = foo->memberAt(0)->asDeclaration();
    QVERIFY(decl);

    CppRefactoringChanges changes(snapshot);
    InsertionPointLocator find(changes);
    const QList<InsertionLocation> locList
        = find.methodDefinition(decl, true, sourceDocument->filePath());
    QVERIFY(locList.size() == 1);
    const InsertionLocation loc = locList.first();
    QCOMPARE(loc.filePath(), sourceDocument->filePath());
    QCOMPARE(loc.line(), 4);
    QCOMPARE(loc.column(), 1);
    QCOMPARE(loc.prefix(), QLatin1String("\n"));
    QCOMPARE(loc.suffix(), QLatin1String("\n"));
}

} // CppEditor::Internal
