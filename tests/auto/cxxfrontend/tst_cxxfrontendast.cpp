// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// What the syntax tree says about a position, on both front ends.
//
// ASTPath is what the editor's AST-shaped work stands on: the quick fixes ask
// it which construct the cursor is in, the decl/def link asks it for the
// function around the cursor, expanding a selection walks it outwards. So
// cxxAstPathAt() has to answer the same way, and the two grammars are not the
// same -- the node kinds have different names and there are different numbers
// of them -- which is why what is compared here is what the callers actually
// read: whether the position is in a declaration, in a statement, in a
// function definition, and where that construct begins and ends.
//
// The rest is asserted on the new path directly, with the kinds written out,
// because a path nobody can read is a path nobody can port a quick fix onto.

#include <cplusplus/ASTPath.h>
#include <cplusplus/CppDocument.h>
#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendDocument.h>
#include <cplusplus/TranslationUnit.h>

#include <cxx/ast.h>
#include <cxx/translation_unit.h>

#include <utils/filepath.h>

#include <QObject>
#include <QTest>

//TESTED_COMPONENT=src/libs/cplusplus

using namespace CPlusPlus;

namespace {

struct Position
{
    int line = 0;
    int column = 0;
};

// Takes the $ markers out of the source and says where they were, counted
// from one. A marker stands at the position it precedes.
QByteArray takeMarkers(const QByteArray &marked, QList<Position> &positions)
{
    QByteArray source;
    int line = 1;
    int column = 1;
    for (const char c : marked) {
        if (c == '$') {
            positions.append({line, column});
            continue;
        }
        source.append(c);
        if (c == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return source;
}

QStringList kindsOf(const QList<cxx::AST *> &path)
{
    QStringList kinds;
    for (cxx::AST *node : path)
        kinds.append(QString::fromUtf8(cxx::to_string(node->kind()).data()));
    return kinds;
}

// Where the innermost construct of a kind the callers look for begins and
// ends, on the built-in model: "3:5-3:12", or nothing where the position is
// in no such construct.
QString builtinExtentOf(const QByteArray &source, const Position &position,
                        const std::function<bool(AST *)> &wanted)
{
    // With a #line marker in front, as the source processor's output has:
    // without one the built-in translation unit counts lines from zero and
    // nothing here would line up. The marker's own line is consumed by it.
    Document::Ptr document = Document::create(Utils::FilePath::fromPathPart(u"<stdin>"));
    document->setUtf8Source("#line 1 \"<stdin>\"\n" + source);
    document->check();

    const QList<AST *> path = ASTPath(document)(position.line, position.column);
    TranslationUnit * const unit = document->translationUnit();
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (!wanted(*it))
            continue;
        int startLine = 0, startColumn = 0, endLine = 0, endColumn = 0;
        unit->getTokenPosition((*it)->firstToken(), &startLine, &startColumn);
        unit->getTokenEndPosition((*it)->lastToken() - 1, &endLine, &endColumn);
        return QString("%1:%2-%3:%4").arg(startLine).arg(startColumn)
            .arg(endLine).arg(endColumn);
    }
    return {};
}

// And the same on the cxx-frontend model.
QString extentOf(const CxxFrontendDocument &document, const QList<cxx::AST *> &path,
                 const std::function<bool(cxx::AST *)> &wanted)
{
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (!wanted(*it))
            continue;
        const CxxAstRange range = cxxAstRangeOf(document, *it);
        if (!range.isValid())
            continue;
        return QString("%1:%2-%3:%4").arg(range.startLine).arg(range.startColumn)
            .arg(range.endLine).arg(range.endColumn);
    }
    return {};
}

} // namespace

class tst_cxxfrontendast : public QObject
{
    Q_OBJECT

private slots:
    void pathAtAPosition_data();
    void pathAtAPosition();

    void everyNodeOnThePathHoldsThePosition();
    void aPositionAtTheEdgeOfANodeIsInIt();
    void nothingOutsideEveryNode();
    void whatAMacroWroteIsNotOnThePath();

    void theEnclosingDeclaration_data();
    void theEnclosingDeclaration();
};

void tst_cxxfrontendast::pathAtAPosition_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QStringList>("expectedKinds");

    QTest::newRow("a name in a declaration")
        << QByteArray("int gl$obal;\n")
        << QStringList({"translation-unit", "simple-declaration", "init-declarator",
                        "declarator", "id-declarator", "name-id"});

    QTest::newRow("the type of a declaration")
        << QByteArray("in$t global;\n")
        << QStringList({"translation-unit", "simple-declaration", "integral-type-specifier"});

    QTest::newRow("a statement in a function")
        << QByteArray("void f()\n{\n    int loc$al = 1;\n}\n")
        << QStringList({"translation-unit", "function-definition",
                        "compound-statement-function-body", "compound-statement",
                        "declaration-statement", "simple-declaration", "init-declarator",
                        "declarator", "id-declarator", "name-id"});

    QTest::newRow("a name used in an expression")
        << QByteArray("void f(int p)\n{\n    p$ + 1;\n}\n")
        << QStringList({"translation-unit", "function-definition",
                        "compound-statement-function-body", "compound-statement",
                        "expression-statement", "binary-expression",
                        "implicit-cast-expression", "id-expression", "name-id"});

    QTest::newRow("a member function of a class")
        << QByteArray("struct S {\n    void m$ember();\n};\n")
        << QStringList({"translation-unit", "simple-declaration", "class-specifier",
                        "simple-declaration", "init-declarator", "declarator",
                        "id-declarator", "name-id"});
}

void tst_cxxfrontendast::pathAtAPosition()
{
    QFETCH(QByteArray, marked);
    QFETCH(QStringList, expectedKinds);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const QList<cxx::AST *> path
        = cxxAstPathAt(document, positions.first().line, positions.first().column);

    QCOMPARE(kindsOf(path), expectedKinds);
}

// The two properties every reader of a path relies on: it runs from the
// outermost node inwards, and every node on it holds the position asked
// about. Swept over every position in the file rather than a few, since a
// path that is wrong somewhere is wrong for whoever puts their cursor there.
void tst_cxxfrontendast::everyNodeOnThePathHoldsThePosition()
{
    const QByteArray source =
        "namespace N {\n"
        "struct S {\n"
        "    int m_value = 0;\n"
        "    int value() const { return m_value; }\n"
        "};\n"
        "}\n"
        "int use(N::S s)\n"
        "{\n"
        "    if (s.value() > 0)\n"
        "        return s.value();\n"
        "    return 0;\n"
        "}\n";

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const QList<QByteArray> lines = source.split('\n');

    for (int line = 1; line <= lines.size(); ++line) {
        for (int column = 1; column <= lines.at(line - 1).size() + 1; ++column) {
            const QList<cxx::AST *> path = cxxAstPathAt(document, line, column);
            CxxAstRange previous;
            for (cxx::AST *node : path) {
                const CxxAstRange range = cxxAstRangeOf(document, node);
                const QString where = QString("%1:%2 in %3")
                                          .arg(line).arg(column)
                                          .arg(QString::fromUtf8(
                                              cxx::to_string(node->kind()).data()));

                // The node holds the position.
                QVERIFY2(range.isValid(), qPrintable(where));
                QVERIFY2(range.startLine < line
                             || (range.startLine == line && range.startColumn <= column),
                         qPrintable(where));
                QVERIFY2(range.endLine > line
                             || (range.endLine == line && range.endColumn >= column),
                         qPrintable(where));

                // And it is inside the one before it.
                if (previous.isValid()) {
                    QVERIFY2(previous.startLine < range.startLine
                                 || (previous.startLine == range.startLine
                                     && previous.startColumn <= range.startColumn),
                             qPrintable(where));
                    QVERIFY2(previous.endLine > range.endLine
                                 || (previous.endLine == range.endLine
                                     && previous.endColumn >= range.endColumn),
                             qPrintable(where));
                }
                previous = range;
            }
        }
    }
}

// A cursor sits between characters, so both edges of a name are in it: at the
// start, where somebody is about to type into it, and at the end, which is
// where an editor leaves the cursor after a word.
void tst_cxxfrontendast::aPositionAtTheEdgeOfANodeIsInIt()
{
    const QByteArray source = "int value;\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    // "value" runs from column 5 to column 9, so 5 and 10 are its edges.
    for (const int column : {5, 10}) {
        const QList<cxx::AST *> path = cxxAstPathAt(document, 1, column);
        QVERIFY2(!path.isEmpty(), qPrintable(QString::number(column)));
        QCOMPARE(kindsOf(path).last(), QString("name-id"));
    }

    // And a position before the type is in the declaration but in no name.
    const QList<cxx::AST *> atTheStart = cxxAstPathAt(document, 1, 1);
    QCOMPARE(kindsOf(atTheStart).first(), QString("translation-unit"));
    QVERIFY(!kindsOf(atTheStart).contains("name-id"));
}

void tst_cxxfrontendast::nothingOutsideEveryNode()
{
    const CxxFrontendDocument document("int global;\n\n", "<stdin>");

    // The blank line after the last declaration is in nothing at all, which
    // is what tells a caller there is nothing here to work on.
    QVERIFY(cxxAstPathAt(document, 2, 1).isEmpty());
    QVERIFY(cxxAstPathAt(document, 40, 1).isEmpty());
}

// The edges of a node are the tokens somebody wrote. A declaration a macro
// wrote has none, so no cursor is ever in it -- the same rule ASTPath applies,
// and the reason quick fixes do not offer to rewrite the inside of an
// expansion.
void tst_cxxfrontendast::whatAMacroWroteIsNotOnThePath()
{
    const QByteArray source = "#define DECLARE_IT int fromTheMacro;\n"
                              "DECLARE_IT\n"
                              "int written;\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    // On the macro's name: what stands there is the use, and the declaration
    // it expands to is not something the file writes.
    const QStringList atTheMacro = kindsOf(cxxAstPathAt(document, 2, 3));
    QVERIFY2(!atTheMacro.contains("simple-declaration"), qPrintable(atTheMacro.join(", ")));

    // The declaration written under it is reached as usual, which says the
    // file was read at all.
    const QStringList atTheNextLine = kindsOf(cxxAstPathAt(document, 3, 6));
    QVERIFY2(atTheNextLine.contains("simple-declaration"), qPrintable(atTheNextLine.join(", ")));
}

void tst_cxxfrontendast::theEnclosingDeclaration_data()
{
    QTest::addColumn<QByteArray>("marked");

    QTest::newRow("a declaration at file scope") << QByteArray("int gl$obal = 1$;\n");
    QTest::newRow("a function definition")
        << QByteArray("void f$()\n{\n    int loc$al;\n$}\n");
    QTest::newRow("a member of a class")
        << QByteArray("struct S {\n    int m$_value;\n    void m$ember() {}\n};\n");
    QTest::newRow("a parameter")
        << QByteArray("void f(int par$am)\n{\n}\n");
    QTest::newRow("a statement in a body")
        << QByteArray("void f(int p)\n{\n    if (p$ > 0)\n        p$ = 0;\n}\n");
    QTest::newRow("a declaration in a namespace")
        << QByteArray("namespace N {\nstruct S$ { int i$; };\n}\n");
}

// The question the callers ask: which construct is the cursor in, and where
// does it begin and end. Compared with the built-in path, because that is the
// answer the editor gives today.
void tst_cxxfrontendast::theEnclosingDeclaration()
{
    QFETCH(QByteArray, marked);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QVERIFY(!positions.isEmpty());

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    for (const Position &position : positions) {
        const QList<cxx::AST *> path = cxxAstPathAt(document, position.line, position.column);
        const QString where = QString("%1:%2").arg(position.line).arg(position.column);

        const QString declaration = extentOf(document, path, [](cxx::AST *node) {
            return dynamic_cast<cxx::DeclarationAST *>(node) != nullptr;
        });
        const QString builtinDeclaration = builtinExtentOf(source, position, [](AST *node) {
            return node->asDeclaration() != nullptr;
        });
        QCOMPARE(declaration + " at " + where, builtinDeclaration + " at " + where);

        const QString statement = extentOf(document, path, [](cxx::AST *node) {
            return dynamic_cast<cxx::StatementAST *>(node) != nullptr;
        });
        const QString builtinStatement = builtinExtentOf(source, position, [](AST *node) {
            return node->asStatement() != nullptr;
        });
        QCOMPARE(statement + " at " + where, builtinStatement + " at " + where);
    }
}

QTEST_GUILESS_MAIN(tst_cxxfrontendast)

#include "tst_cxxfrontendast.moc"
