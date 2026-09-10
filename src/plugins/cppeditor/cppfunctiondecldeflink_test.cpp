// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// What "Apply Function Signature Changes" does, which nothing covered before.
//
// The feature is a thousand lines that read one syntax tree twice: the
// declaration the cursor is in, the matching declaration elsewhere, and the
// edits that make the second say what the first says. It is the last of the
// editor's own work still on the built-in tree, so what it writes has to be
// written down first.
//
// No editor is needed for that. The finder takes a cursor, a document and a
// snapshot, and the link it produces can be asked for its changes directly --
// only applying them wants a widget, for the tooltip and the jump.
//
// Which is why a case is written twice over: the file as the model read it,
// where the two sides of the function still say the same thing, and the file
// as it stands in the editor, where somebody has just changed one of them.
// Both are needed and neither on its own would do -- the matching declaration
// is found by the signatures agreeing in the *parse*, and what to change is
// read out of the *text*. That is also why the cursor sits on the function's
// name: it has to be in the declaration on both readings.

#include "cppfunctiondecldeflink_test.h"

#include "cppfunctiondecldeflink.h"
#include "cppmodelmanager.h"
#include "cpptoolstestcase.h"

#include <utils/changeset.h>
#include <utils/fileutils.h>

#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

using namespace CPlusPlus;
using namespace Utils;

using CppEditor::Tests::TemporaryDir;
using CppEditor::Tests::TestCase;

namespace CppEditor::Internal::Tests {
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

QByteArray headerWith(const QByteArray &declaration)
{
    return "struct C {\n    " + declaration + "\n};\n";
}

QByteArray sourceWith(const QByteArray &definition)
{
    return "#include \"header.h\"\n\n" + definition + "\n";
}

// Two files as the model read them, the link the cursor yields, and then the
// change somebody types into one of them -- in that order, because that is
// the order it happens in. The link holds cursors into the text and they
// follow what is typed; built after the fact they would sit where the old
// text ended.
class Driver
{
public:
    // \a editedFile is the one somebody is typing in, with @ for the cursor.
    // The other is the target, and what applied() gives back is that file
    // with the link's changes in it.
    Driver(const QByteArray &header, const QByteArray &source, bool editingTheHeader)
    {
        if (!m_dir.isValid() || !m_testCase.succeededSoFar())
            return;

        int position = 0;
        const QByteArray headerSource = editingTheHeader ? takeMarker(header, &position) : header;
        const QByteArray sourceSource = editingTheHeader ? source : takeMarker(source, &position);

        const FilePath headerPath = m_dir.createFile("header.h", headerSource);
        const FilePath sourcePath = m_dir.createFile("source.cpp", sourceSource);
        if (!TestCase::parseFiles({headerPath, sourcePath}))
            return;

        // The edited file, parsed as it stands on disk and with its syntax
        // tree kept: what the model manager leaves in the snapshot has none
        // -- the indexer lets it go -- and the editor hands over its own
        // parse, which does.
        m_snapshot = TestCase::globalSnapshot();
        const Document::Ptr document = m_snapshot.preprocessedDocument(
            editingTheHeader ? headerSource : sourceSource,
            editingTheHeader ? headerPath : sourcePath);
        document->parse();
        document->check();
        m_snapshot.insert(document);

        m_targetSource = QString::fromUtf8(editingTheHeader ? sourceSource : headerSource);

        m_text.setPlainText(QString::fromUtf8(editingTheHeader ? headerSource : sourceSource));
        QTextCursor cursor(&m_text);
        cursor.setPosition(position);

        // The link is looked for in a thread, and nothing is emitted where
        // there is none to find -- which is what a case expecting no link
        // waits for.
        QSignalSpy found(&m_finder, &FunctionDeclDefLinkFinder::foundLink);
        m_finder.startFindLinkAt(cursor, document, m_snapshot);
        if (found.isEmpty() && !found.wait())
            return;

        m_link = found.first().first().value<std::shared_ptr<FunctionDeclDefLink>>();
    }

    bool isValid() const { return m_link && m_link->isValid(); }
    bool foundNoLink() const { return m_dir.isValid() && !m_link; }

    // What somebody types: \a what is replaced by \a with, once, where it
    // stands in the edited file.
    void type(const QString &what, const QString &with)
    {
        const int at = m_text.toPlainText().indexOf(what);
        QVERIFY(at >= 0);
        QTextCursor cursor(&m_text);
        cursor.setPosition(at);
        cursor.setPosition(at + what.size(), QTextCursor::KeepAnchor);
        cursor.insertText(with);
    }

    // The other file after the link's changes, or as it was where the link
    // has none to make.
    QString applied()
    {
        QString text = m_targetSource;
        ChangeSet changes = m_link->changes(m_snapshot);
        changes.apply(&text);
        return text;
    }

private:
    // First member, so that the model manager is collected before these files
    // are parsed into it and again once they are gone.
    TestCase m_testCase;
    TemporaryDir m_dir;
    QString m_targetSource;
    QTextDocument m_text;
    Snapshot m_snapshot;
    FunctionDeclDefLinkFinder m_finder;
    std::shared_ptr<FunctionDeclDefLink> m_link;
};

} // namespace

void DeclDefLinkTest::testSyncsTheOtherSide_data()
{
    QTest::addColumn<QByteArray>("declaration");
    QTest::addColumn<QByteArray>("definition");
    QTest::addColumn<QString>("typedOver");
    QTest::addColumn<QString>("typed");
    QTest::addColumn<QByteArray>("expectedDefinition");

    QTest::newRow("a renamed parameter")
        << QByteArray("void f@(int original);")
        << QByteArray("void C::f(int original) {}")
        << "original" << "renamed"
        << QByteArray("void C::f(int renamed) {}");

    QTest::newRow("a changed parameter type")
        << QByteArray("void f@(int a);")
        << QByteArray("void C::f(int a) {}")
        << "int a" << "double a"
        << QByteArray("void C::f(double a) {}");

    QTest::newRow("a changed return type")
        << QByteArray("void f@(int a);")
        << QByteArray("void C::f(int a) {}")
        << "void" << "int"
        << QByteArray("int C::f(int a) {}");

    QTest::newRow("a parameter turned into a reference")
        << QByteArray("void f@(int a);")
        << QByteArray("void C::f(int a) {}")
        << "int a" << "int &a"
        << QByteArray("void C::f(int &a) {}");

    QTest::newRow("an added parameter")
        << QByteArray("void f@(int a);")
        << QByteArray("void C::f(int a) {}")
        << "int a" << "int a, int b"
        << QByteArray("void C::f(int a, int b) {}");

    // A parameter is named in the function's documentation as well, and that
    // is written above the definition.
    QTest::newRow("a parameter named in the documentation")
        << QByteArray("void f@(int original);")
        << QByteArray("/**\n"
                      " * @param original what it is for\n"
                      " */\n"
                      "void C::f(int original) {}")
        << "original" << "renamed"
        << QByteArray("/**\n"
                      " * @param renamed what it is for\n"
                      " */\n"
                      "void C::f(int renamed) {}");
}

void DeclDefLinkTest::testSyncsTheOtherSide()
{
    QFETCH(QByteArray, declaration);
    QFETCH(QByteArray, definition);
    QFETCH(QString, typedOver);
    QFETCH(QString, typed);
    QFETCH(QByteArray, expectedDefinition);

    Driver driver(headerWith(declaration), sourceWith(definition), true);
    QVERIFY(driver.isValid());
    driver.type(typedOver, typed);
    QCOMPARE(driver.applied(), QString::fromUtf8(sourceWith(expectedDefinition)));
}

// The other direction: what is being typed in is the definition, and the
// declaration is what the changes are for.
void DeclDefLinkTest::testSyncsTheDeclaration()
{
    Driver driver(headerWith("void f(int original);"),
                  sourceWith("void C::f@(int original) {}"),
                  false);
    QVERIFY(driver.isValid());
    driver.type("original", "renamed");
    QCOMPARE(driver.applied(), QString::fromUtf8(headerWith("void f(int renamed);")));
}

// Where nothing was typed the two sides still say the same thing and there is
// nothing to apply, which is what keeps the marker out of the editor while
// nobody is changing anything.
void DeclDefLinkTest::testNoChangesWhereTheSignaturesAgree()
{
    Driver driver(headerWith("void f@(int a);"), sourceWith("void C::f(int a) {}"), true);
    QVERIFY(driver.isValid());
    QCOMPARE(driver.applied(), QString::fromUtf8(sourceWith("void C::f(int a) {}")));
}

void DeclDefLinkTest::testNoLinkOffAFunction()
{
    Driver driver("int glo@bal;\n", sourceWith("void f(int a) {}"), true);
    QVERIFY(driver.foundNoLink());
}

} // namespace CppEditor::Internal::Tests
