// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// Compares what the two front ends answer about a position in a file.
//
// CPlusPlus::Document is what the built-in code model is made of, and the
// questions the editor asks it are about a place in the text: which function
// is the cursor in, which symbol was last declared above it. The answers are
// on screen -- functionAt is what the editor shows above the text -- so the
// two models have to agree on them before Document can move.
//
// The source of each case marks the positions to ask about with $, so that a
// case reads as the file it is about rather than as a list of line numbers.

#include <cplusplus/AST.h>
#include <cplusplus/Bind.h>
#include <cplusplus/Control.h>
#include <functional>
#include <cplusplus/CxxFrontendDocument.h>
#include <cplusplus/Literals.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/Overview.h>
#include <cplusplus/Scope.h>
#include <cplusplus/Symbols.h>
#include <cplusplus/TranslationUnit.h>

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

// Takes the $ markers out of the source and says where they were. A marker
// stands at the position it precedes.
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

// What the built-in front end answers for each marked position.
QStringList builtIn(const QByteArray &source, const QList<Position> &positions)
{
    Control control;
    const StringLiteral *fileId = control.stringLiteral("<stdin>");
    TranslationUnit unit(&control, fileId);
    unit.setSource(source.constData(), source.size());
    unit.setLanguageFeatures(LanguageFeatures::defaultFeatures());
    unit.parse(TranslationUnit::ParseTranslationUnit);
    if (!unit.ast())
        return {};

    Namespace *globals = control.newNamespace(0, nullptr);
    Bind bind(&unit);
    bind(unit.ast()->asTranslationUnit(), globals);

    // Document owns its control and translation unit, so rather than build
    // one, walk what Document::functionAt walks: out from the last symbol
    // declared at or before the position to the function around it.
    QStringList result;
    for (const Position &position : positions) {
        CPlusPlus::Symbol *last = nullptr;
        const std::function<void(Scope *)> walk = [&](Scope *scope) {
            for (int i = 0, count = scope->memberCount(); i < count; ++i) {
                CPlusPlus::Symbol *member = scope->memberAt(i);
                if (member->line() < unsigned(position.line)
                    || (member->line() == unsigned(position.line)
                        && member->column() <= unsigned(position.column))) {
                    if (!member->asBlock())
                        last = member;
                }
                if (Scope *inner = member->asScope())
                    walk(inner);
            }
        };
        walk(globals);

        Scope *scope = last ? last->asScope() : nullptr;
        if (last && !scope)
            scope = last->enclosingScope();
        while (scope && !scope->asFunction())
            scope = scope->enclosingScope();

        if (!scope) {
            result.append(QString());
            continue;
        }
        result.append(Overview().prettyName(LookupContext::fullyQualifiedName(scope)));
    }
    return result;
}

QStringList cxxFrontend(const QByteArray &source, const QList<Position> &positions)
{
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    QStringList result;
    for (const Position &position : positions)
        result.append(document.functionAt(position.line, position.column));
    return result;
}

// Both print a qualified name, one with a leading :: and one without, which
// is not a disagreement about which function the position is in.
QStringList normalize(const QStringList &names)
{
    QStringList result;
    for (QString name : names) {
        if (name.startsWith("::"))
            name = name.mid(2);
        result.append(name);
    }
    return result;
}

} // namespace

class tst_cxxfrontenddocument : public QObject
{
    Q_OBJECT

private slots:
    void functionAt_data();
    void functionAt();

    void scopeAt_data();
    void scopeAt();

    void declarationAt_data();
    void declarationAt();

    void typeAt_data();
    void typeAt();

    void reportsDiagnostics();
    void unsupportedQueries();
};

void tst_cxxfrontenddocument::functionAt_data()
{
    QTest::addColumn<QByteArray>("marked");

    QTest::newRow("inside a free function")
        << QByteArray("void f()\n{\n    $int x;\n}\n");
    QTest::newRow("outside any function")
        << QByteArray("$int g;\nvoid f() {}\n");
    QTest::newRow("between two functions")
        << QByteArray("void a() {}\n$\nvoid b() {}\n");
    QTest::newRow("inside the second of two")
        << QByteArray("void a() {}\nvoid b()\n{\n    $int x;\n}\n");
    QTest::newRow("inside a member function")
        << QByteArray("struct S {\n    void m()\n    {\n        $int x;\n    }\n};\n");
    QTest::newRow("inside a function in a namespace")
        << QByteArray("namespace N {\nvoid f()\n{\n    $int x;\n}\n}\n");
    QTest::newRow("several positions")
        << QByteArray("void a()\n{\n    $int x;\n}\n$\nvoid b()\n{\n    $int y;\n}\n");
}

void tst_cxxfrontenddocument::functionAt()
{
    QFETCH(QByteArray, marked);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QVERIFY(!positions.isEmpty());

    const QStringList expected = normalize(builtIn(source, positions));
    const QStringList actual = normalize(cxxFrontend(source, positions));

    QCOMPARE(actual, expected);
}

// Which scope a position is in, which is what the built-in Document answers
// out of each scope's start and end offset and what the other model could not
// answer at all until a scope learned how far it reaches.
void tst_cxxfrontenddocument::scopeAt_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("expected");

    QTest::newRow("file scope") << QByteArray("$int x;\n") << QString();
    QTest::newRow("inside a function")
        << QByteArray("void f()\n{\n    $int x;\n}\n") << QString("f");
    QTest::newRow("after a function")
        << QByteArray("void f() {}\n$int x;\n") << QString();
    QTest::newRow("inside a class")
        << QByteArray("struct S {\n    $int m;\n};\n") << QString("S");
    QTest::newRow("inside a member function")
        << QByteArray("struct S {\n    void m()\n    {\n        $int x;\n    }\n};\n")
        << QString("m");
    QTest::newRow("inside a namespace")
        << QByteArray("namespace N {\n$int x;\n}\n") << QString("N");
    QTest::newRow("innermost of several")
        << QByteArray("namespace N {\nstruct S {\n    void m() { $int x; }\n};\n}\n")
        << QString("m");
}

void tst_cxxfrontenddocument::scopeAt()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, expected);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QCOMPARE(document.scopeAt(positions.first().line, positions.first().column), expected);
}

// Follow symbol: the name at one position, and where it was declared.
//
// The source marks both with $ -- the first is the use to ask about, the
// second is the declaration the answer has to be. A case reads as the file it
// is about, and the positions cannot drift out of step with the text.
void tst_cxxfrontenddocument::declarationAt_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("name");

    QTest::newRow("a local variable")
        << QByteArray("void f()\n{\n    int $x;\n    $x = 1;\n}\n") << QString("f::x");
    QTest::newRow("a global variable")
        << QByteArray("int $g;\nvoid f() { $g = 1; }\n") << QString("g");
    QTest::newRow("a parameter")
        << QByteArray("void f(int $p)\n{\n    $p = 1;\n}\n") << QString("f::p");
    QTest::newRow("a member from a member function")
        << QByteArray("struct S {\n    int $m;\n    void f() { $m = 1; }\n};\n")
        << QString("S::m");
    QTest::newRow("a name in a namespace")
        << QByteArray("namespace N { int $v; }\nvoid f() { N::$v = 1; }\n")
        << QString("N::v");
    QTest::newRow("an enumerator")
        << QByteArray("enum E { $A };\nint x = $A;\n") << QString("E::A");
    QTest::newRow("a member inherited from a base")
        << QByteArray("struct B { int $m; };\nstruct D : B { void f() { $m = 1; } };\n")
        << QString("B::m");
    QTest::newRow("a member inherited two levels up")
        << QByteArray("struct A { int $m; };\nstruct B : A {};\n"
                      "struct C : B { void f() { $m = 1; } };\n")
        << QString("A::m");
    QTest::newRow("a name brought in by a using declaration")
        << QByteArray("namespace N { int $v; }\nusing N::v;\nvoid f() { $v = 1; }\n")
        << QString("N::v");
    QTest::newRow("a name found through a using directive")
        << QByteArray("namespace N { int $v; }\nusing namespace N;\nvoid f() { $v = 1; }\n")
        << QString("N::v");
    QTest::newRow("a type name")
        << QByteArray("struct $S {};\nvoid f() { $S s; }\n") << QString("S");
    QTest::newRow("a member through a pointer")
        << QByteArray("struct S { int $m; };\nvoid f(S *s) { s->$m = 1; }\n")
        << QString("S::m");
    QTest::newRow("a base in a member initializer")
        << QByteArray("struct $B { B(int); };\nstruct D : B { D() : $B(1) {} };\n")
        << QString("B");
    QTest::newRow("an elaborated type")
        << QByteArray("struct $S {};\nvoid f(struct $S *s);\n") << QString("S");
    QTest::newRow("the second use of the same name")
        << QByteArray("int $g;\nvoid f() { g = 1; $g = 2; }\n") << QString("g");
}

void tst_cxxfrontenddocument::declarationAt()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, name);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 2);

    const Position declaration = positions.at(0);
    const Position use = positions.at(1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::Declaration found
        = document.declarationAt(use.line, use.column);

    QVERIFY2(found.isValid(), "nothing was resolved at the use");
    QCOMPARE(found.name, name);
    QCOMPARE(found.line, declaration.line);
    QCOMPARE(found.column, declaration.column);
}

// The type of the expression at a position: what a tooltip shows, and what
// completion has to know before it can offer anything after a dot.
//
// The answer is the innermost expression around the marker, so where the
// marker sits decides which one is meant: on the g of g() it is the function,
// on the ( it is the call; on the a of a + b it is a, on the + it is the sum.
// That is what someone pointing at either one means.
void tst_cxxfrontenddocument::typeAt_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("type");

    QTest::newRow("a variable")
        << QByteArray("void f() { int x; int y = $x; }\n") << QString("int");
    QTest::newRow("a pointer")
        << QByteArray("void f() { char *p; char *q = $p; }\n") << QString("char*");
    QTest::newRow("a literal")
        << QByteArray("void f() { int y = $42; }\n") << QString("int");
    QTest::newRow("a sum")
        << QByteArray("void f() { int a; long b; long c = a $+ b; }\n") << QString("long");
    QTest::newRow("a member")
        << QByteArray("struct S { int m; };\nvoid f(S s) { int y = s.$m; }\n") << QString("int");
    QTest::newRow("a member through a pointer")
        << QByteArray("struct S { int m; };\nvoid f(S *s) { int y = s->$m; }\n") << QString("int");
    QTest::newRow("the object of a member access")
        << QByteArray("struct S { int m; };\nvoid f(S *s) { int y = $s->m; }\n") << QString("S*");
    QTest::newRow("a call")
        << QByteArray("int g();\nvoid f() { int y = g$(); }\n") << QString("int");
    QTest::newRow("a comparison")
        << QByteArray("void f() { int a; bool b = a $== 1; }\n") << QString("bool");
    QTest::newRow("not an expression")
        << QByteArray("$struct S {};\n") << QString();

    // A statement that could be read as a declaration is one: x; declares
    // nothing and is not an expression, so there is no type to give. Worth a
    // case of its own, because it is why every row above puts the expression
    // somewhere a declaration cannot go.
    QTest::newRow("a statement that reads as a declaration")
        << QByteArray("void f() { int x; $x; }\n") << QString();
}

void tst_cxxfrontenddocument::typeAt()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, type);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::ExpressionType found
        = document.typeAt(positions.first().line, positions.first().column);

    QCOMPARE(found.type, type);
}

void tst_cxxfrontenddocument::reportsDiagnostics()
{
    const CxxFrontendDocument good("int x;\n", "<stdin>");
    QVERIFY(good.diagnostics().isEmpty());

    const CxxFrontendDocument bad("int x = ;\n", "<stdin>");
    QVERIFY(!bad.diagnostics().isEmpty());
    QCOMPARE(bad.diagnostics().first().line, 1);
    QVERIFY(bad.diagnostics().first().isError);
}

// Document's questions that cannot be answered on this model yet, asserted so
// the list cannot quietly go stale.
void tst_cxxfrontenddocument::unsupportedQueries()
{
    const QStringList unsupported = CxxFrontendDocument::unsupportedQueries();

    QVERIFY(!unsupported.contains("scopeAt"));
    QVERIFY(!unsupported.contains("Snapshot"));
    QVERIFY(unsupported.contains("isValidForCurrentEnvironment"));
}

QTEST_GUILESS_MAIN(tst_cxxfrontenddocument)

#include "tst_cxxfrontenddocument.moc"
