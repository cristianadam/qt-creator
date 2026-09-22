// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "classviewparser.h"
#include "classviewparsertreeitem.h"
#include "classviewutils.h"

#include <utils/environment.h>
#include <utils/filepath.h>

#include <QStandardItem>
#include <QTemporaryDir>
#include <QTest>

using namespace Utils;

namespace ClassView::Internal {
namespace {

// The rows the pane would draw under \a item, in the order it draws them.
QStringList rowsUnder(const ParserTreeItem::ConstPtr &item)
{
    QStandardItem root;
    item->fetchMore(&root);

    QStringList names;
    for (int row = 0; row < root.rowCount(); ++row)
        names << symbolInformationFromItem(root.child(row)).name();
    return names;
}

// The subtree drawn under the row called \a name, or nothing where no row of
// that name is drawn.
ParserTreeItem::ConstPtr rowNamed(const ParserTreeItem::ConstPtr &item, const QString &name)
{
    QStandardItem root;
    item->fetchMore(&root);

    for (int row = 0; row < root.rowCount(); ++row) {
        const SymbolInformation information = symbolInformationFromItem(root.child(row));
        if (information.name() == name)
            return item->child(information);
    }
    return {};
}

// Whether a file can be read here at all without something having parsed it
// first.
//
// Only the cxx-frontend model reads a file when it is asked about one. What
// the built-in front end declares is taken out of the document an indexing
// pass left in its snapshot -- reading the file again would answer with
// everything its headers declare as well, the includes being one translation
// unit with it -- so with no pass behind it there is nothing to draw, and the
// question these tests ask does not arise.
bool readsAFileWhenAsked()
{
#ifdef QTC_WITH_CXX_FRONTEND
    return qtcEnvironmentVariableIsSet("QTC_CXX_FRONTEND_MODEL");
#else
    return false;
#endif
}

// The parser as the pane drives it, with whatever it last made of the tree.
//
// On this thread rather than on one of its own, so that a request and the
// tree it produces are one step: the pane hands the parser its own thread and
// hears back through a queued connection, which says nothing more about what
// is read.
class DrivenParser
{
public:
    DrivenParser()
    {
        QObject::connect(&parser, &Parser::treeRegenerated,
                         [this](const ParserTreeItem::ConstPtr &root) { tree = root; });
    }

    Parser parser;
    ParserTreeItem::ConstPtr tree;
};

} // namespace

class ClassViewParserTest final : public QObject
{
    Q_OBJECT

private slots:
    void testProjectFilesAreReadWithoutAParse();
    void testTreeFollowsAFileThatChanges();
};

// What the pane draws for a project nothing has parsed: the files come from
// the project itself, and what each declares is read when it is asked for.
//
// There is no project open here and so no indexing pass has run, which is the
// point -- this used to be drawn off the documents such a pass leaves in the
// built-in model's snapshot, and with no pass behind it the pane stayed
// empty.
void ClassViewParserTest::testProjectFilesAreReadWithoutAParse()
{
    if (!readsAFileWhenAsked())
        QSKIP("The built-in front end draws this off a pass's documents");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const FilePath root = FilePath::fromString(dir.path());

    const FilePath header = root / "declared.h";
    const FilePath source = root / "written.cpp";

    // A file a project is built from that is not C++, holding something that
    // would be read as a class if anything read it: what says that the files
    // are chosen by what they are rather than taken as they come.
    const FilePath qml = root / "view.qml";

    QVERIFY(header.writeFileContents("class Declared {};\n"));
    QVERIFY(source.writeFileContents("class Written {};\n"));
    QVERIFY(qml.writeFileContents("class NotRead {};\n"));

    DrivenParser driven;
    driven.parser.addProject(root / "project.files", "Test", {header, source, qml}, {});

    QVERIFY(driven.tree);
    QCOMPARE(rowsUnder(driven.tree), QStringList{"Test"});

    const ParserTreeItem::ConstPtr project = rowNamed(driven.tree, "Test");
    QVERIFY(project);
    QCOMPARE(rowsUnder(project), (QStringList{"Declared", "Written"}));
}

// A file read again is drawn as it now reads, the tree the project was drawn
// from being older than the file.
void ClassViewParserTest::testTreeFollowsAFileThatChanges()
{
    if (!readsAFileWhenAsked())
        QSKIP("The built-in front end draws this off a pass's documents");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const FilePath root = FilePath::fromString(dir.path());
    const FilePath source = root / "written.cpp";
    QVERIFY(source.writeFileContents("class Before {};\n"));

    DrivenParser driven;
    const FilePath project = root / "project.files";
    driven.parser.addProject(project, "Test", {source}, {});
    QVERIFY(driven.tree);
    ParserTreeItem::ConstPtr drawn = rowNamed(driven.tree, "Test");
    QVERIFY(drawn);
    QCOMPARE(rowsUnder(drawn), QStringList{"Before"});

    QVERIFY(source.writeFileContents("class After {};\n"));
    driven.parser.updateDocuments({source}, {});

    QVERIFY(driven.tree);
    drawn = rowNamed(driven.tree, "Test");
    QVERIFY(drawn);
    QCOMPARE(rowsUnder(drawn), QStringList{"After"});
}

QObject *createClassViewParserTest()
{
    return new ClassViewParserTest;
}

} // namespace ClassView::Internal

#include "classviewparser_test.moc"
