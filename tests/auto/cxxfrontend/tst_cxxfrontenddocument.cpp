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

    void completeAfterAnArrow();
    void completeAfterADot();
    void completeAnUnqualifiedName();
    void completeOffersInheritedMembers();
    void argumentHints();
    void noCompletionWhereNoneWasAsked();

    void reportsDiagnostics();
    void unsupportedQueries();
    void anOverloadedCallIsNotResolved();
    void theDefinitionIsPreferredToTheDeclaration();
    void aDeclarationWithoutItsDefinitionSaysSo();
    void aNameFromAUsingDeclarationSaysSo();
    void localsOfAFunction();
    void localsOfNestedBlocksAreTheirOwn();
    void localsOfALambdaBelongToItsFunction();
    void localsSayWhatTheyWereDeclaredAs();
    void noLocalsOutsideAFunction();
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

// Completion. The parser works out what could be written at the position on
// its way past it, which is also how it copes with the half-written
// expression that is there while someone is typing -- there is no valid file
// to parse at that moment, and none is needed.
namespace {

CxxFrontendDocument::Completion completeAt(const QByteArray &marked)
{
    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    Q_ASSERT(positions.size() == 1);

    CxxFrontendDocument::Config config;
    config.completionLine = positions.first().line;
    config.completionColumn = positions.first().column;

    return CxxFrontendDocument(QString::fromUtf8(source), "<stdin>", config).completion();
}

} // namespace

void tst_cxxfrontenddocument::completeAfterAnArrow()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("struct S { int m; void g(); };\nvoid f(S *s) { s->$ }\n");

    QCOMPARE(completion.kind, CxxFrontendDocument::Completion::Kind::Member);
    QCOMPARE(completion.objectType, QString("S*"));
    QVERIFY(completion.candidates.contains("m"));
    QVERIFY(completion.candidates.contains("g"));
}

void tst_cxxfrontenddocument::completeAfterADot()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("struct S { int m; };\nvoid f(S s) { s.$ }\n");

    QCOMPARE(completion.kind, CxxFrontendDocument::Completion::Kind::Member);
    QCOMPARE(completion.objectType, QString("S"));
    QVERIFY(completion.candidates.contains("m"));
}

void tst_cxxfrontenddocument::completeAnUnqualifiedName()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("void f() { int local; $ }\n");

    QCOMPARE(completion.kind, CxxFrontendDocument::Completion::Kind::Unqualified);
    QVERIFY(completion.candidates.contains("local"));
}

void tst_cxxfrontenddocument::completeOffersInheritedMembers()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("struct B { int inherited; };\nstruct D : B { int own; };\n"
                     "void f(D *d) { d->$ }\n");

    QCOMPARE(completion.kind, CxxFrontendDocument::Completion::Kind::Member);
    QVERIFY(completion.candidates.contains("own"));
    QVERIFY2(completion.candidates.contains("inherited"),
             qPrintable(completion.candidates.join(", ")));
}

void tst_cxxfrontenddocument::argumentHints()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("int g(int a, char b);\nvoid f() { g($ }\n");

    // A name can be written there too, so both are offered at once.
    QCOMPARE(completion.kind, CxxFrontendDocument::Completion::Kind::Unqualified);
    QCOMPARE(completion.activeParameter, 0);
    QVERIFY2(!completion.signatures.isEmpty(), "no candidate signature");
    QVERIFY2(completion.signatures.first().contains("g("),
             qPrintable(completion.signatures.join(", ")));
}

void tst_cxxfrontenddocument::noCompletionWhereNoneWasAsked()
{
    const CxxFrontendDocument document("struct S { int m; };\n", "<stdin>");
    QVERIFY(!document.completion().isValid());
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
    QVERIFY(unsupported.contains("the line each include is on"));

    // What the list said before, and it went stale the moment the document
    // began recording which macros it consulted. The document answers it, so
    // asking is enough to say the entry had to go; whether a header is reused
    // under a given environment is asserted on in tst_cxxfrontendsnapshot,
    // because that is where the decision is made.
    QVERIFY(!unsupported.contains("isValidForCurrentEnvironment"));

    const CxxFrontendDocument document("#ifdef FEATURE\nint a;\n#else\nint b;\n#endif\n",
                                       "<stdin>");
    QVERIFY(document.isValidFor({}));
    QVERIFY(!document.isValidFor({"FEATURE 1"}));
}

// Which of several functions a call means. The front end resolves a member
// call to one of the candidates without looking at the arguments, so both of
// these answer the same, and the answer is right for at most one of them.
// Written down so that a consumer knows not to ask this about a call -- and
// as a ratchet: the day the two answers differ, this fails and the limit comes
// off the list.
void tst_cxxfrontenddocument::anOverloadedCallIsNotResolved()
{
    const QByteArray source =
        "struct B {\n"
        "    int f(int) {}\n"
        "};\n"
        "class D : public B {\n"
        "public:\n"
        "    using B::f;\n"
        "    double f(double) {}\n"
        "};\n"
        "void g(D *pd) {\n"
        "    pd->f(2);\n"
        "    pd->f(2.3);\n"
        "}\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    const CxxFrontendDocument::Declaration fromInt = document.declarationAt(10, 9);
    const CxxFrontendDocument::Declaration fromDouble = document.declarationAt(11, 9);
    QVERIFY(fromInt.isValid());
    QCOMPARE(fromInt.line, fromDouble.line);

    QVERIFY(CxxFrontendDocument::unsupportedQueries().contains("which overload a call means"));
}

// What follow symbol wants: the place that defines the thing, not the place
// that promised it. Where this file has both, the definition is the answer.
void tst_cxxfrontenddocument::theDefinitionIsPreferredToTheDeclaration()
{
    const QByteArray source =
        "class Foo;\n"
        "class Foo { int m; };\n"
        "void f(Foo *p);\n"
        "void f(Foo *p) {}\n"
        "void g() { Foo a; f(&a); }\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    // Foo in "Foo a;", forward declared on line 1 and defined on line 2.
    const CxxFrontendDocument::Declaration type = document.declarationAt(5, 12);
    QVERIFY(type.isValid());
    QCOMPARE(type.name, QString("Foo"));
    QCOMPARE(type.line, 2);
    QVERIFY(type.isDefinition);

    // And the same for a function declared before it is defined.
    const CxxFrontendDocument::Declaration function = document.lookup({}, "f");
    QVERIFY(function.isValid());
    QCOMPARE(function.line, 4);
    QVERIFY(function.isDefinition);
}

// And when the file has only the promise, it says so, so that a caller with
// somewhere else to look knows to look there rather than sending someone to a
// line that declares nothing.
void tst_cxxfrontenddocument::aDeclarationWithoutItsDefinitionSaysSo()
{
    const QByteArray source =
        "class Foo;\n"
        "void f();\n"
        "void g() { Foo *p; f(); }\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    const CxxFrontendDocument::Declaration type = document.declarationAt(3, 12);
    QVERIFY(type.isValid());
    QCOMPARE(type.line, 1);
    QVERIFY(!type.isDefinition);

    const CxxFrontendDocument::Declaration function = document.lookup({}, "f");
    QVERIFY(function.isValid());
    QCOMPARE(function.line, 2);
    QVERIFY(!function.isDefinition);

    // A variable is declared where it stands, and nothing is pending about it.
    const CxxFrontendDocument other("int x;\nvoid h() { x = 1; }\n", "<stdin>");
    const CxxFrontendDocument::Declaration variable = other.declarationAt(2, 12);
    QVERIFY(variable.isValid());
    QVERIFY(variable.isDefinition);
}

// A using declaration brings a name in, and the built-in model answers such a
// name with the using declaration itself -- QTCREATORBUG7903 asked for that.
// This model resolves the name to what it actually names, so it says which
// names came in that way and leaves the choice to whoever has to agree with
// the built-in answer.
void tst_cxxfrontenddocument::aNameFromAUsingDeclarationSaysSo()
{
    const QByteArray source =
        "namespace NS {\n"
        "class Foo {};\n"
        "class Bar {};\n"
        "}\n"
        "using NS::Foo;\n"
        "void f() {\n"
        "    Foo brought;\n"
        "    NS::Bar qualified;\n"
        "}\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    const CxxFrontendDocument::Declaration brought = document.declarationAt(7, 5);
    QVERIFY(brought.isValid());
    QCOMPARE(brought.name, QString("NS::Foo"));
    QVERIFY(brought.throughUsingDeclaration);

    // A name nothing brought in is answered without that caveat.
    const CxxFrontendDocument::Declaration qualified = document.declarationAt(8, 9);
    QVERIFY(qualified.isValid());
    QCOMPARE(qualified.name, QString("NS::Bar"));
    QVERIFY(!qualified.throughUsingDeclaration);

    // And a using declaration inside a function body counts as much as one at
    // file scope, which takes looking into the bodies to see.
    const QByteArray inFunction =
        "namespace NS {\n"
        "class Foo {};\n"
        "}\n"
        "void f() {\n"
        "    using NS::Foo;\n"
        "    Foo brought;\n"
        "}\n";
    const CxxFrontendDocument inner(QString::fromUtf8(inFunction), "<stdin>");
    const CxxFrontendDocument::Declaration fromBlock = inner.declarationAt(6, 5);
    QVERIFY(fromBlock.isValid());
    QVERIFY(fromBlock.throughUsingDeclaration);
}

namespace {

// Each local as "name @line:column+length ...", the declaration first, so that
// a wrong answer says which place it got wrong.
QStringList describeLocals(const QList<CxxFrontendDocument::Local> &locals)
{
    QStringList result;
    for (const CxxFrontendDocument::Local &local : locals) {
        QStringList places;
        for (const CxxFrontendDocument::Occurrence &place : local.places) {
            places.append(QString("@%1:%2+%3")
                              .arg(place.line).arg(place.column).arg(place.length));
        }
        result.append(local.name + ' ' + places.join(' '));
    }
    return result;
}

// Each local as "name kind class", the two things a caller has to know about
// it besides where it is written.
QStringList describeDeclarations(const QList<CxxFrontendDocument::Local> &locals)
{
    QStringList result;
    for (const CxxFrontendDocument::Local &local : locals) {
        result.append(QString("%1 %2 %3")
                          .arg(local.name,
                               local.isParameter ? "parameter" : "variable",
                               local.className.isEmpty() ? QString("-") : local.className));
    }
    return result;
}

} // namespace

// The parameters and the variables of a function, each with every place it is
// written. Nothing outside the file can be missing from this: a local cannot
// be named anywhere else.
void tst_cxxfrontenddocument::localsOfAFunction()
{
    const QByteArray source =
        "int f(int a)\n"
        "{\n"
        "    int b = a;\n"
        "    b = b + a;\n"
        "    return b;\n"
        "}\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    QCOMPARE(describeLocals(document.localsAt(3, 9)),
             QStringList({"a @1:11+1 @3:13+1 @4:13+1",
                          "b @3:9+1 @4:5+1 @4:9+1 @5:12+1"}));
}

// A name declared again in an inner block is a different local, and its uses
// are its own -- which is the whole reason to answer per local rather than per
// name.
void tst_cxxfrontenddocument::localsOfNestedBlocksAreTheirOwn()
{
    const QByteArray source =
        "void f()\n"
        "{\n"
        "    int x = 1;\n"
        "    {\n"
        "        int x = 2;\n"
        "        x = x + 1;\n"
        "    }\n"
        "    x = 3;\n"
        "}\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    QCOMPARE(describeLocals(document.localsAt(3, 9)),
             QStringList({"x @3:9+1 @8:5+1", "x @5:13+1 @6:9+1 @6:13+1"}));
}

// A lambda's parameter is written inside the function that holds it, and is
// highlighted with that function's own locals, so it is one of them here.
void tst_cxxfrontenddocument::localsOfALambdaBelongToItsFunction()
{
    const QByteArray source =
        "void f()\n"
        "{\n"
        "    auto func = [](int arg) { return arg; };\n"
        "    func(1);\n"
        "}\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    QCOMPARE(describeLocals(document.localsAt(4, 5)),
             QStringList({"func @3:10+4 @4:5+4", "arg @3:24+3 @3:38+3"}));
}

// Besides where a local is written, two things about it decide what the
// editor does with it: only a parameter can be named in the function's
// documentation, and a local whose type is a class may be doing its work by
// existing -- a lock, a scoped pointer -- so that never naming it again is
// not a mistake.
void tst_cxxfrontenddocument::localsSayWhatTheyWereDeclaredAs()
{
    const QByteArray source =
        "class QMutexLocker { public: QMutexLocker(int *m); };\n"
        "void f(int a)\n"
        "{\n"
        "    QMutexLocker locker(&a);\n"
        "    const QMutexLocker guard(&a);\n"
        "    QMutexLocker *handle = &locker;\n"
        "    QMutexLocker &alias = locker;\n"
        "    int plain = 0;\n"
        "}\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    // Const is not part of what the type names, and a pointer or a reference
    // to a lock is not a lock.
    QCOMPARE(describeDeclarations(document.localsAt(4, 18)),
             QStringList({"a parameter -",
                          "locker variable QMutexLocker",
                          "guard variable QMutexLocker",
                          "handle variable -",
                          "alias variable -",
                          "plain variable -"}));

    // A lambda's parameter reaches this twice, as the parameter and as the
    // variable standing for it in the body, and it is a parameter either way.
    const CxxFrontendDocument lambda("void f() { auto g = [](int p) { return p; }; }\n",
                                     "<stdin>");
    QCOMPARE(describeDeclarations(lambda.localsAt(1, 17)),
             QStringList({"g variable -", "p parameter -"}));
}

void tst_cxxfrontenddocument::noLocalsOutsideAFunction()
{
    const CxxFrontendDocument document("int g;\nvoid f() { int a = g; }\n", "<stdin>");

    QVERIFY(document.localsAt(1, 5).isEmpty());
    // And a global used inside a function is not a local of it.
    QCOMPARE(describeLocals(document.localsAt(2, 16)), QStringList("a @2:16+1"));
}

QTEST_GUILESS_MAIN(tst_cxxfrontenddocument)

#include "tst_cxxfrontenddocument.moc"
