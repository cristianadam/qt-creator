// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "classviewparsertreeitem.h"
#include "classviewutils.h"

#include <cppeditor/cppcodemodelqueries.h>

#include <cplusplus/CppDocument.h>

#include <utils/filepath.h>
#include <utils/utilsicons.h>

#include <QStandardItem>
#include <QTest>

using namespace CPlusPlus;
using namespace Utils;

namespace ClassView::Internal {
namespace {

QString iconOf(int type)
{
    switch (type) {
    case CodeModelIcon::Class: return "class";
    case CodeModelIcon::Struct: return "struct";
    case CodeModelIcon::Enum: return "enum";
    case CodeModelIcon::Enumerator: return "enumerator";
    case CodeModelIcon::FuncPublic: return "public function";
    case CodeModelIcon::FuncProtected: return "protected function";
    case CodeModelIcon::FuncPrivate: return "private function";
    case CodeModelIcon::FuncPublicStatic: return "public static function";
    case CodeModelIcon::FuncProtectedStatic: return "protected static function";
    case CodeModelIcon::FuncPrivateStatic: return "private static function";
    case CodeModelIcon::Namespace: return "namespace";
    case CodeModelIcon::VarPublic: return "public variable";
    case CodeModelIcon::VarProtected: return "protected variable";
    case CodeModelIcon::VarPrivate: return "private variable";
    case CodeModelIcon::VarPublicStatic: return "public static variable";
    case CodeModelIcon::VarProtectedStatic: return "protected static variable";
    case CodeModelIcon::VarPrivateStatic: return "private static variable";
    case CodeModelIcon::Signal: return "signal";
    case CodeModelIcon::SlotPublic: return "public slot";
    case CodeModelIcon::SlotProtected: return "protected slot";
    case CodeModelIcon::SlotPrivate: return "private slot";
    case CodeModelIcon::Keyword: return "keyword";
    case CodeModelIcon::Macro: return "macro";
    case CodeModelIcon::Property: return "property";
    case CodeModelIcon::Unknown: return "unknown";
    default: break;
    }
    return QString::number(type);
}

// Every row the pane would draw under \a item, in the order it draws them and
// one string each: what stands in the row, the icon beside it, and the places
// it takes a reader to.
//
// Read the way the pane reads it -- fetchMore() fills in the rows of a
// QStandardItem, and a row's own subtree is looked up by the information in
// it, which is what the tree item model does to walk down.
void describe(const ParserTreeItem::ConstPtr &item, int depth, QStringList *rows)
{
    QStandardItem root;
    item->fetchMore(&root);

    for (int row = 0; row < root.rowCount(); ++row) {
        const QStandardItem *drawn = root.child(row);
        const SymbolInformation information = symbolInformationFromItem(drawn);
        const ParserTreeItem::ConstPtr subtree = item->child(information);

        // Sorted, because a set is not in an order of its own and two
        // declarations of one thing are both kept.
        QStringList places;
        if (subtree) {
            const QSet<SymbolLocation> locations = subtree->symbolLocations();
            for (const SymbolLocation &location : locations)
                places << QString("%1:%2").arg(location.line()).arg(location.column());
            places.sort();
        }

        QString said = QString(2 * depth, ' ') + information.name();
        if (!information.type().isEmpty())
            said += " [" + information.type() + ']';
        said += " (" + iconOf(information.iconType()) + ')';
        if (!places.isEmpty())
            said += " at " + places.join(", ");
        rows->append(said);

        if (subtree)
            describe(subtree, depth + 1, rows);
    }
}

// What the pane makes of one file, with nothing else in the tree: the
// document a project's file would arrive as, parsed here instead.
QStringList treeOf(const QByteArray &source)
{
    // With a #line marker in front, as the source processor's output has:
    // without one the built-in translation unit counts lines from zero, and
    // then no place it reports is the place in the text. The marker's own
    // line is consumed by it.
    const FilePath filePath = FilePath::fromPathPart(u"<test>");
    const Document::Ptr document = Document::create(filePath);
    document->setUtf8Source("#line 1 \"<test>\"\n" + source);
    document->check();

    Snapshot snapshot;
    snapshot.insert(document);

    // The source as it stands, which is what a file being edited would be
    // read from -- and the only place this one can be read from, there being
    // no such file on disk.
    CppEditor::WorkingCopy workingCopy;
    workingCopy.insert(filePath, source);

    const CppEditor::CodeModelQueries queries(snapshot, workingCopy);

    QStringList rows;
    describe(ParserTreeItem::fromDeclarations(queries.declarationsIn(filePath)), 0, &rows);
    return rows;
}

} // namespace

class ClassViewTreeTest final : public QObject
{
    Q_OBJECT

private slots:
    void testDocumentTree_data();
    void testDocumentTree();
};

void ClassViewTreeTest::testDocumentTree_data()
{
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QStringList>("expectedRows");

    // A scope stands under its own name, so the pane leaves the type out of
    // the row; a function's type is the parameters alone, the return type
    // being no part of what tells two of one name apart to a reader.
    QTest::newRow("a class and what it declares")
        << QByteArray(
               "class C {\n"
               "public:\n"
               "    C();\n"
               "    void f(int a);\n"
               "protected:\n"
               "    int m_count;\n"
               "private:\n"
               "    static void g();\n"
               "};\n")
        << QStringList{
               "C [C] (class) at 1:7",
               "  C [()] (public function) at 3:5",
               "  f [(int)] (public function) at 4:10",
               "  g [()] (private static function) at 8:17",
               "  m_count [int] (protected variable) at 6:9",
           };

    QTest::newRow("a class in a namespace, and a class in a class")
        << QByteArray(
               "namespace N {\n"
               "class Outer {\n"
               "    class Inner {\n"
               "        void f();\n"
               "    };\n"
               "};\n"
               "}\n")
        << QStringList{
               "N [N] (namespace) at 1:11",
               "  Outer [Outer] (class) at 2:7",
               "    Inner [Inner] (class) at 3:11",
               "      f [()] (private function) at 4:14",
           };

    // A namespace is drawn for what is in it, so one holding nothing this
    // pane shows is no row at all.
    QTest::newRow("an empty namespace is no row")
        << QByteArray("namespace Empty {\n}\n")
        << QStringList{};

    // What a file only names, or borrows, or promises elsewhere: none of it
    // is something the file declares here.
    QTest::newRow("what is named rather than declared")
        << QByteArray(
               "class Elsewhere;\n"
               "extern int outside;\n"
               "namespace N { int inside; }\n"
               "using namespace N;\n"
               "using N::inside;\n")
        << QStringList{
               "N [N] (namespace) at 3:11",
               "  inside [int] (public variable) at 3:19",
           };

    // What a class lets somebody else at is declared by that somebody else,
    // and the walk says so -- but only for what it is told is a friend.
    // Pinned as it is: a friend function is drawn beside the class, with no
    // type and under the icon of a variable.
    QTest::newRow("a friend is drawn beside the class")
        << QByteArray(
               "class C {\n"
               "    friend void g();\n"
               "};\n")
        << QStringList{
               "C [C] (class) at 1:7",
               "g (public variable) at 2:17",
           };

    // A definition written apart from its declaration is written under a
    // qualified name, and the pane keeps the declaration's place.
    QTest::newRow("a definition written outside its class")
        << QByteArray(
               "class C {\n"
               "    void f();\n"
               "};\n"
               "void C::f() {}\n")
        << QStringList{
               "C [C] (class) at 1:7",
               "  f [()] (private function) at 2:10",
           };

    // The contents of a function are nobody's business here, and a
    // declaration and a definition in one file are one row with one place.
    QTest::newRow("a function body is not walked into")
        << QByteArray(
               "void f();\n"
               "void f()\n"
               "{\n"
               "    int local = 0;\n"
               "    class Local {};\n"
               "}\n")
        << QStringList{
               "f [()] (public function) at 1:6, 2:6",
           };

    // An enumerator's type is what it counts as rather than the
    // enumeration it belongs to, and an alias stands for its own right-hand
    // side, under the icon of a variable. Both are pinned as they are; a
    // reader sees them beside one another.
    QTest::newRow("an enumeration and an alias")
        << QByteArray(
               "enum Plain { One, Two };\n"
               "enum class Scoped { Three };\n"
               "typedef int Number;\n"
               "using Count = int;\n")
        << QStringList{
               "Plain [Plain] (enum) at 1:6",
               "  One [int] (enumerator) at 1:14",
               "  Two [int] (enumerator) at 1:19",
               "Scoped [Scoped] (enum) at 2:12",
               "  Three [int] (enumerator) at 2:21",
               "Count [int] (public variable) at 4:7",
               "Number [int] (public variable) at 3:13",
           };

    // Pinned as it is, warts and all: a template's own parameter stands as a
    // row of its own under the icon of a class, and a template class is
    // drawn inside itself -- the template and the class it declares are two
    // symbols of one name at one place.
    QTest::newRow("a template and a function template")
        << QByteArray(
               "template <typename T>\n"
               "class Holder {\n"
               "    T m_value;\n"
               "};\n"
               "template <typename T>\n"
               "void take(T t) {}\n")
        << QStringList{
               "Holder [Holder] (class) at 2:7",
               "  Holder [Holder] (class) at 2:7",
               "    m_value [T] (private variable) at 3:7",
               "  T (class) at 1:20",
               "take [(T)] (public function) at 6:6",
               "  T (class) at 5:20",
               "  take [(T)] (public function) at 6:6",
           };

    // Two declarations of one thing are one row that takes a reader to
    // either place, which is what the pane cycles through.
    QTest::newRow("one thing declared twice")
        << QByteArray(
               "void f();\n"
               "void f();\n")
        << QStringList{
               "f [()] (public function) at 1:6, 2:6",
           };

    // Two functions of one name are two rows: what tells them apart is the
    // type written after the name.
    QTest::newRow("two functions of one name")
        << QByteArray(
               "void f(int);\n"
               "void f(double);\n")
        << QStringList{
               "f [(double)] (public function) at 2:6",
               "f [(int)] (public function) at 1:6",
           };

    // A scope with no name of its own has nothing to stand under, so the
    // row it gets is a blank one -- pinned because what is inside it is
    // reached through that row. An anonymous member stands under the name
    // the front end made up for it instead, which is where the file it is
    // in ends.
    QTest::newRow("a scope written without a name")
        << QByteArray(
               "namespace {\n"
               "int hidden;\n"
               "}\n"
               "struct S {\n"
               "    union {\n"
               "        int a;\n"
               "    };\n"
               "};\n")
        << QStringList{
               " (namespace) at 1:1",
               "  hidden [int] (public variable) at 2:5",
               "S [S] (struct) at 4:8",
               "  Anonymous:10 [Anonymous:10] (class) at 5:5",
               "    a [int] (public variable) at 6:13",
           };
}

void ClassViewTreeTest::testDocumentTree()
{
    QFETCH(QByteArray, source);
    QFETCH(QStringList, expectedRows);

    // Compared as one string, so that a failure says the whole tree rather
    // than the first row that differs.
    QCOMPARE(treeOf(source).join('\n'), expectedRows.join('\n'));
}

QObject *createClassViewTreeTest()
{
    return new ClassViewTreeTest;
}

} // namespace ClassView::Internal

#include "classviewparsertreeitem_test.moc"
