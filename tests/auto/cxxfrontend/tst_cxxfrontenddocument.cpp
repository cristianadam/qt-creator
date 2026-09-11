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
#include <cplusplus/SimpleLexer.h>
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

// The names a completion offers, which is what most of these cases are
// about; the detail and the icon are compared against the built-in
// proposal in the plugin's own test.
QStringList namesOf(const QList<CxxFrontendDocument::Completion::Candidate> &candidates)
{
    QStringList names;
    for (const CxxFrontendDocument::Completion::Candidate &candidate : candidates)
        names.append(candidate.name);
    return names;
}


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

    void declarationOfATypeAt_data();
    void declarationOfATypeAt();

    void declarationOfAFunctionAt_data();
    void declarationOfAFunctionAt();
    void definitionHeadAt_data();
    void definitionHeadAt();

    void completeAfterAnArrow();
    void completeAfterADot();
    void completeAnUnqualifiedName();
    void completeOffersInheritedMembers();
    void completionSaysWhatItInserts();
    void completeOffersWhatAnAnonymousUnionHolds();
    void completionSaysWhenItCannotSeeEverything();
    void completeAfterADotOnAPointer();
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
    void localsFromTheParameterList();
    void localsUsedThroughAMacro();
    void noLocalsOutsideAFunction();

    void commentsOfAFile();
    void commentKinds_data();
    void commentKinds();
    void commentsOfAHeaderAreItsOwn();

    void signatureOfADeclaration();
    void signatureWritesATypeForTheOtherPlace();
    void signatureWritesAsLittleAsTheOtherPlaceNeeds();
    void signatureWritesAReturnTypeForOutsideTheFunction();
    void noSignatureOffAFunction();
    void signatureForAPlaceThatNamesNoFunction();
    void signatureOfADeclarationInAHeader();

    void memberFunctionsOfAClass_data();
    void memberFunctionsOfAClass();

    void classToMove_data();
    void classToMove();
    void partsOfAClass_data();
    void partsOfAClass();
    void virtuality_data();
    void virtuality();
    void usagesInAFile_data();
    void usagesInAFile();
    void classesWithTheirBases_data();
    void classesWithTheirBases();
    void enclosingFunction_data();
    void enclosingFunction();
    void typeDeclared_data();
    void typeDeclared();
    void usingDirectiveAt_data();
    void usingDirectiveAt();
    void usingDirectives_data();
    void usingDirectives();

    void literalInAFunction_data();
    void literalInAFunction();
    void noLiteralToExtract_data();
    void noLiteralToExtract();

    void discardedValue_data();
    void discardedValue();
    void noDiscardedValue_data();
    void noDiscardedValue();

    void switchOverAnEnum_data();
    void switchOverAnEnum();
    void noSwitchToComplete_data();
    void noSwitchToComplete();
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

    // A name that is a statement of its own, which is where the walk over
    // the tree used to lose an expression: a statement holds an attribute
    // list before its expression, and the cursor dropped what came after an
    // empty slot.
    QTest::newRow("a name that is the whole statement")
        << QByteArray("void f() { int x; $x; }\n") << QString("int");
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

// The same type written as a declaration, which is not the spelling above
// with a name after it: a declarator is written *around* the name.
void tst_cxxfrontenddocument::declarationOfATypeAt_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("declaration");

    QTest::newRow("a variable")
        << QByteArray("void f() { int x; int y = $x; }\n") << QString("int total");
    QTest::newRow("a pointer")
        << QByteArray("void f() { char *p; char *q = $p; }\n") << QString("char *total");
    QTest::newRow("a sum")
        << QByteArray("void f() { int a; long b; long c = a $+ b; }\n")
        << QString("long total");
    QTest::newRow("a pointer to a function")
        << QByteArray("void g(int);\nvoid f() { void (*p)(int) = g; void (*q)(int) = $p; }\n")
        << QString("void (*total)(int)");

    // Written for where it stands, so a class from another namespace is
    // named with it and one the scope reaches is not.
    QTest::newRow("a type from another namespace")
        << QByteArray("namespace N { struct T {}; }\n"
                      "void f(N::T t) { N::T u = $t; }\n")
        << QString("N::T total");
    QTest::newRow("a type the scope reaches")
        << QByteArray("namespace N { struct T {};\n"
                      "void f(T t) { T u = $t; } }\n")
        << QString("T total");

    QTest::newRow("not an expression")
        << QByteArray("$struct S {};\n") << QString();
}

void tst_cxxfrontenddocument::declarationOfATypeAt()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, declaration);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QCOMPARE(document.declarationOfTypeAt(positions.first().line,
                                          positions.first().column, "total"),
             declaration);
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
    QVERIFY(namesOf(completion.candidates).contains("m"));
    QVERIFY(namesOf(completion.candidates).contains("g"));
}

void tst_cxxfrontenddocument::completeAfterADot()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("struct S { int m; };\nvoid f(S s) { s.$ }\n");

    QCOMPARE(completion.kind, CxxFrontendDocument::Completion::Kind::Member);
    QCOMPARE(completion.objectType, QString("S"));
    QVERIFY(namesOf(completion.candidates).contains("m"));
}

void tst_cxxfrontenddocument::completeAnUnqualifiedName()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("void f() { int local; $ }\n");

    QCOMPARE(completion.kind, CxxFrontendDocument::Completion::Kind::Unqualified);
    QVERIFY(namesOf(completion.candidates).contains("local"));
}

void tst_cxxfrontenddocument::completeOffersInheritedMembers()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("struct B { int inherited; };\nstruct D : B { int own; };\n"
                     "void f(D *d) { d->$ }\n");

    QCOMPARE(completion.kind, CxxFrontendDocument::Completion::Kind::Member);
    QVERIFY(namesOf(completion.candidates).contains("own"));
    QVERIFY2(namesOf(completion.candidates).contains("inherited"),
             qPrintable(namesOf(completion.candidates).join(", ")));
}

// What a proposal has to know before it can write a chosen candidate down.
// Each of these is a different thing to type after the name, which is why
// they are answered here rather than guessed from the printed detail.
void tst_cxxfrontenddocument::completionSaysWhatItInserts()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("struct S {\n"
                     "    S();\n"
                     "    ~S();\n"
                     "    void run();\n"
                     "    int count(int from);\n"
                     "    int m_value;\n"
                     "private:\n"
                     "    int m_hidden;\n"
                     "};\n"
                     "void f(S s) { s.$ }\n");

    const auto candidate = [&](const QString &name) {
        for (const CxxFrontendDocument::Completion::Candidate &each : completion.candidates) {
            if (each.name == name)
                return each;
        }
        return CxxFrontendDocument::Completion::Candidate{};
    };

    // A function that takes nothing and returns nothing: the whole call can
    // be written, semicolon included.
    const auto run = candidate("run");
    QVERIFY2(run.isFunction, qPrintable(namesOf(completion.candidates).join(", ")));
    QVERIFY(!run.takesArguments);
    QVERIFY(run.returnsNothing);

    // One that takes something is written up to the open parenthesis, and
    // one that returns something does not end the statement.
    const auto count = candidate("count");
    QVERIFY(count.isFunction);
    QVERIFY(count.takesArguments);
    QVERIFY(!count.returnsNothing);

    // A destructor is called like any other function. A constructor is
    // never offered at all: what stands here for the class is its own
    // name, which is a type and not a call.
    QVERIFY(candidate("~S").isFunction);
    QVERIFY(candidate("S").isInjectedClassName);
    QVERIFY(!candidate("S").isFunction);

    // Not a function at all.
    QVERIFY(!candidate("m_value").isFunction);
    QVERIFY(!candidate("m_value").takesArguments);

    // And what the class says about who may write it, which is the order a
    // proposal shows them in.
    QVERIFY(candidate("m_value").isPublic);
    QVERIFY(!candidate("m_hidden").isPublic);
}

// A dot written where an arrow belongs. The members are offered anyway --
// which is what an editor wants, since it puts the arrow there itself --
// and how the object was written is reported so that it can.
void tst_cxxfrontenddocument::completeAfterADotOnAPointer()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("struct S { int m; };\nvoid f(S *s) { s.$ }\n");

    QCOMPARE(completion.kind, CxxFrontendDocument::Completion::Kind::Member);
    QVERIFY(namesOf(completion.candidates).contains("m"));
    QVERIFY(completion.objectIsPointer);
    QVERIFY(completion.dotWasWritten);

    // And an arrow written where one belongs is not a dot.
    const CxxFrontendDocument::Completion throughAnArrow
        = completeAt("struct S { int m; };\nvoid f(S *s) { s->$ }\n");
    QVERIFY(throughAnArrow.objectIsPointer);
    QVERIFY(!throughAnArrow.dotWasWritten);

    // Nor is a member of something that is not a pointer at all.
    const CxxFrontendDocument::Completion onAValue
        = completeAt("struct S { int m; };\nvoid f(S s) { s.$ }\n");
    QVERIFY(!onAValue.objectIsPointer);
    QVERIFY(onAValue.dotWasWritten);
}

// An anonymous union or struct is written inside the class and named
// without it, so what it holds is offered where the class's own members
// are.
void tst_cxxfrontenddocument::completeOffersWhatAnAnonymousUnionHolds()
{
    const CxxFrontendDocument::Completion completion
        = completeAt("struct S { union { int i; char c; }; int named; };\n"
                     "void f(S s) { s.$ }\n");

    QVERIFY2(namesOf(completion.candidates).contains("i"),
             qPrintable(namesOf(completion.candidates).join(", ")));
    QVERIFY(namesOf(completion.candidates).contains("c"));
    QVERIFY(namesOf(completion.candidates).contains("named"));
    QVERIFY(!completion.membersMayBeMissing);
}

// And where it could not see everything the object has, it says so, because
// a part of a list of what can be written is worse than no list: the name
// somebody wants may be the missing one.
void tst_cxxfrontenddocument::completionSaysWhenItCannotSeeEverything()
{
    // A base this file does not have. Its members are inherited all the
    // same, and none of them are here.
    const CxxFrontendDocument::Completion fromAMissingBase
        = completeAt("struct D : Elsewhere { int own; };\nvoid f(D d) { d.$ }\n");
    QVERIFY(namesOf(fromAMissingBase.candidates).contains("own"));
    QVERIFY(fromAMissingBase.membersMayBeMissing);

    // A class this file only declares, whose members are all elsewhere.
    const CxxFrontendDocument::Completion fromADeclaration
        = completeAt("struct Later;\nvoid f(Later *l) { l->$ }\n");
    QVERIFY(fromADeclaration.membersMayBeMissing);

    // And a class it has whole says nothing of the kind.
    const CxxFrontendDocument::Completion whole
        = completeAt("struct B { int inherited; };\nstruct D : B { int own; };\n"
                     "void f(D d) { d.$ }\n");
    QVERIFY(!whole.membersMayBeMissing);
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

    // The entry, and both halves of the answer it is about. A partial
    // specialization written with a type the file declares is the one
    // instantiated...
    QVERIFY(unsupported.contains("which specialization of a template an object is, in a file "
                                 "that does not declare the types the specialization names"));
    const CxxFrontendDocument::Completion matched
        = completeAt("template <typename T> struct S {};\n"
                     "template <typename T, int N> struct S<T[N]> { int fromTheArrayOne; };\n"
                     "void f(S<int[3]> s) { s.$ }\n");
    QVERIFY2(namesOf(matched.candidates).contains("fromTheArrayOne"),
             qPrintable(namesOf(matched.candidates).join(", ")));

    // ...and written with one it does not, it is not, so what comes back is
    // the primary template's members. Nothing about them says so, which is
    // why the list has to.
    const CxxFrontendDocument::Completion notMatched
        = completeAt("template <typename T> struct S {};\n"
                     "template <typename T, size_t N> struct S<T[N]> { int fromTheArrayOne; };\n"
                     "void f(S<int[3]> s) { s.$ }\n");
    QVERIFY(!namesOf(notMatched.candidates).contains("fromTheArrayOne"));
    QVERIFY(!notMatched.membersMayBeMissing);

    // A slot is written with a macro that expands to an access specifier, so
    // the parser is handed a member function and nothing says otherwise --
    // asserted here rather than left to be found in an outline.
    QVERIFY(unsupported.contains("whether a member function is a signal or a slot"));
    const CxxFrontendDocument qtClass("#define slots\n"
                                      "class C { public slots: void s(); };\n",
                                      "<stdin>");
    QStringList slotIcons;
    for (const CxxFrontendDocument::Symbol &symbol : qtClass.symbols()) {
        if (symbol.name == "s")
            slotIcons.append(QString::number(int(symbol.icon)));
    }
    QCOMPARE(slotIcons, QStringList(QString::number(int(Utils::CodeModelIcon::FuncPublic))));

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

// A cursor on a parameter's own declaration is inside the function as much as
// one in its body: what the editor asks about is the definition the cursor is
// in, parameter list and all.
void tst_cxxfrontenddocument::localsFromTheParameterList()
{
    const CxxFrontendDocument document("int f(int a) { return a; }\n", "<stdin>");

    QCOMPARE(describeLocals(document.localsAt(1, 11)), QStringList("a @1:11+1 @1:23+1"));
}

// A local written as a macro's argument is used where it is written, the same
// place occurrencesOf() reports for any token an argument brought in.
void tst_cxxfrontenddocument::localsUsedThroughAMacro()
{
    const CxxFrontendDocument document("#define UNUSED(x) (void)x\n"
                                       "void f(int a) { UNUSED(a); }\n",
                                       "<stdin>");

    QCOMPARE(describeLocals(document.localsAt(2, 24)), QStringList("a @2:12+1 @2:24+1"));
}

void tst_cxxfrontenddocument::noLocalsOutsideAFunction()
{
    const CxxFrontendDocument document("int g;\nvoid f() { int a = g; }\n", "<stdin>");

    QVERIFY(document.localsAt(1, 5).isEmpty());
    // And a global used inside a function is not a local of it.
    QCOMPARE(describeLocals(document.localsAt(2, 16)), QStringList("a @2:16+1"));
}

// What the built-in front end calls each comment, so that the two agree on
// the four kinds -- a reader that tells them apart there tells them apart
// here. The built-in lexer is asked directly: it is the only thing that
// classifies a comment, and it does so while scanning tokens.
QString builtinKindOf(const QByteArray &source)
{
    SimpleLexer lexer;
    const Tokens tokens = lexer(QString::fromUtf8(source));
    for (const Token &token : tokens) {
        switch (token.kind()) {
        case T_COMMENT: return "c-style";
        case T_CPP_COMMENT: return "cpp-style";
        case T_DOXY_COMMENT: return "c-style-doxygen";
        case T_CPP_DOXY_COMMENT: return "cpp-style-doxygen";
        default: break;
        }
    }
    return {};
}

QString kindOf(CxxFrontendDocument::CommentKind kind)
{
    switch (kind) {
    case CxxFrontendDocument::CommentKind::CStyle: return "c-style";
    case CxxFrontendDocument::CommentKind::CppStyle: return "cpp-style";
    case CxxFrontendDocument::CommentKind::CStyleDoxygen: return "c-style-doxygen";
    case CxxFrontendDocument::CommentKind::CppStyleDoxygen: return "cpp-style-doxygen";
    }
    return {};
}

QStringList describeComments(const QList<CxxFrontendDocument::Comment> &comments)
{
    QStringList described;
    for (const CxxFrontendDocument::Comment &comment : comments) {
        described.append(QString("%1 @%2:%3-%4:%5")
                             .arg(kindOf(comment.kind))
                             .arg(comment.line).arg(comment.column)
                             .arg(comment.endLine).arg(comment.endColumn));
    }
    return described;
}

} // namespace

// A comment is not code, so nothing in the tree points at one: what a
// document knows about its comments is where each stands and how it is
// written. Which is what a reader needs -- the documentation of a
// declaration is the comment block directly above it.
void tst_cxxfrontenddocument::commentsOfAFile()
{
    const CxxFrontendDocument document("// what f does\n"
                                       "void f();\n"
                                       "\n"
                                       "/* and g */ void g(); // in passing\n",
                                       "<stdin>");

    QCOMPARE(describeComments(document.comments()),
             QStringList({"cpp-style @1:1-1:15",
                          "c-style @4:1-4:12",
                          "cpp-style @4:23-4:36"}));
}

void tst_cxxfrontenddocument::commentKinds_data()
{
    QTest::addColumn<QByteArray>("source");

    QTest::newRow("a line comment") << QByteArray("// text\n");
    QTest::newRow("a documented line") << QByteArray("/// text\n");
    QTest::newRow("a documented line with a bang") << QByteArray("//! text\n");
    QTest::newRow("four slashes") << QByteArray("//// text\n");
    QTest::newRow("a block comment") << QByteArray("/* text */\n");
    QTest::newRow("a documented block") << QByteArray("/** text */\n");
    QTest::newRow("a documented block with a bang") << QByteArray("/*! text */\n");
    QTest::newRow("a documented block pointing back") << QByteArray("/**< text */\n");
    QTest::newRow("a block of stars") << QByteArray("/*** text */\n");
    QTest::newRow("an empty block") << QByteArray("/**/\n");
    QTest::newRow("a block with no space after the stars") << QByteArray("/**text */\n");
}

// The same rules on both, case by case, because which kind a comment is
// decides whether two of them are one block.
void tst_cxxfrontenddocument::commentKinds()
{
    QFETCH(QByteArray, source);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QCOMPARE(document.comments().size(), 1);
    QCOMPARE(kindOf(document.comments().first().kind), builtinKindOf(source));
}

// A header is read into whoever includes it, and its comments are read with
// it -- but they are written in the header, so they are the header's own and
// no answer about this file.
void tst_cxxfrontenddocument::commentsOfAHeaderAreItsOwn()
{
    CxxFrontendDocument::Config config;
    config.onInclude = [](const QString &name, bool, const QString &)
        -> std::optional<CxxFrontendDocument::Config::Include> {
        if (name != "h.h")
            return std::nullopt;
        return CxxFrontendDocument::Config::Include{"h.h", "// in the header\nint fromHeader;\n"};
    };

    const CxxFrontendDocument document("#include \"h.h\"\n// here\nint here;\n",
                                       "<stdin>", config);

    QCOMPARE(describeComments(document.comments()), QStringList("cpp-style @2:1-2:8"));
}

// What one side of a function says. The names and the types come back as
// they are written here, which is what tells one signature from another.
void tst_cxxfrontenddocument::signatureOfADeclaration()
{
    const QByteArray source =
        "struct C {\n"
        "    int f(int a, const char *b) const noexcept;\n"
        "};\n"
        "int C::f(int a, const char *b) const noexcept { return a; }\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    // The declaration, written for where the definition stands.
    const CxxFrontendDocument::Signature signature = document.signatureAt({{}, 2, 9}, {{}, 4, 8});
    QVERIFY(signature.isValid());
    QCOMPARE(signature.name(), QString("C::f"));
    QCOMPARE(signature.returnType(), QString("int"));
    QCOMPARE(signature.parameterCount(), 2);
    QCOMPARE(signature.parameterName(0), QString("a"));
    QCOMPARE(signature.parameterType(0), QString("int"));
    QCOMPARE(signature.parameterName(1), QString("b"));
    QCOMPARE(signature.parameterType(1), QString("const char*"));
    QVERIFY(signature.isConst());
    QVERIFY(!signature.isVolatile());
    QCOMPARE(signature.exceptionSpecification(), QString("noexcept"));
}

// The point of asking a signature where its answer is going: a type named in
// one scope has to be named again in the other, and how much of the name has
// to be written is what differs.
void tst_cxxfrontenddocument::signatureWritesATypeForTheOtherPlace()
{
    const QByteArray source =
        "namespace N { struct T {}; }\n"
        "struct C {\n"
        "    void f(N::T t);\n"
        "};\n"
        "void C::f(N::T t) {}\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    const CxxFrontendDocument::Signature signature = document.signatureAt({{}, 3, 10}, {{}, 5, 9});
    QVERIFY(signature.isValid());
    QCOMPARE(signature.writeParameter(0, "t"), QString("N::T t"));
    QCOMPARE(signature.writeParameter(0, QString()), QString("N::T"));
    QCOMPARE(signature.writtenParameterType(0), QString("N::T"));
}

void tst_cxxfrontenddocument::signatureWritesAsLittleAsTheOtherPlaceNeeds()
{
    const QByteArray source =
        "namespace N {\n"
        "struct T {};\n"
        "struct C {\n"
        "    void f(T t);\n"
        "};\n"
        "void C::f(T t) {}\n"
        "} // namespace N\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    // The definition is inside N, so T is reached there and nothing has to
    // stand in front of it.
    const CxxFrontendDocument::Signature signature = document.signatureAt({{}, 4, 10}, {{}, 6, 9});
    QVERIFY(signature.isValid());
    QCOMPARE(signature.writeParameter(0, "t"), QString("T t"));
}

// A return type is written in front of the name, which is outside the
// function, and a parameter inside it -- so the two are read in different
// scopes and can come out spelled differently.
void tst_cxxfrontenddocument::signatureWritesAReturnTypeForOutsideTheFunction()
{
    const QByteArray source =
        "struct C {\n"
        "    struct T {};\n"
        "    T f(T t);\n"
        "};\n"
        "C::T C::f(C::T t) { return t; }\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    const CxxFrontendDocument::Signature signature = document.signatureAt({{}, 3, 7}, {{}, 5, 9});
    QVERIFY(signature.isValid());
    QCOMPARE(signature.writeReturnType("C::f"), QString("C::T C::f"));
    QCOMPARE(signature.writeParameter(0, "t"), QString("T t"));
}

void tst_cxxfrontenddocument::noSignatureOffAFunction()
{
    const CxxFrontendDocument document("int global;\nvoid f() {}\n", "<stdin>");

    // The first place has to name a function: that is what is being read.
    QVERIFY(!document.signatureAt({{}, 1, 5}, {{}, 2, 6}).isValid());

    // The second does not, but it does have to be somewhere in the file.
    QVERIFY(document.signatureAt({{}, 2, 6}, {{}, 1, 5}).isValid());
    QVERIFY(!document.signatureAt({{}, 2, 6}, {{}, 90, 1}).isValid());
}

// A definition written into a file that says nothing about the function yet
// is written at a place that names no function of its own. All the place
// decides is how much has to stand in front of each name.
void tst_cxxfrontenddocument::signatureForAPlaceThatNamesNoFunction()
{
    const QByteArray source =
        "namespace N {\n"
        "struct T {};\n"
        "struct C {\n"
        "    T f(T t);\n"
        "};\n"
        "}\n"
        "\n"
        "int here;\n";
    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");

    // Written at file scope, where nothing of N is in force.
    const CxxFrontendDocument::Signature outside
        = document.signatureAt({{}, 4, 7}, {{}, 8, 5});
    QVERIFY(outside.isValid());
    QCOMPARE(outside.writeReturnType("N::C::f"), QString("N::T N::C::f"));
    QCOMPARE(outside.writeParameter(0, "t"), QString("N::T t"));

    // And inside the namespace, where it is not.
    const CxxFrontendDocument::Signature inside
        = document.signatureAt({{}, 4, 7}, {{}, 2, 8});
    QVERIFY(inside.isValid());
    QCOMPARE(inside.writeReturnType("C::f"), QString("T C::f"));
    QCOMPARE(inside.writeParameter(0, "t"), QString("T t"));
}

// The case the whole thing is for: the declaration is in a header and the
// definition in the file that includes it, which is one translation unit and
// two files. A position therefore names a file as well as a place -- line 2
// of the header is not line 2 here.
void tst_cxxfrontenddocument::signatureOfADeclarationInAHeader()
{
    CxxFrontendDocument::Config config;
    config.onInclude = [](const QString &name, bool, const QString &)
        -> std::optional<CxxFrontendDocument::Config::Include> {
        if (name != "h.h")
            return std::nullopt;
        return CxxFrontendDocument::Config::Include{
            "h.h", "namespace N { struct T {}; }\nstruct C { void f(N::T t); };\n"};
    };

    const CxxFrontendDocument document("#include \"h.h\"\n"
                                       "\n"
                                       "void C::f(N::T t) {}\n",
                                       "<stdin>", config);

    // The declaration, which stands on line 2 of the header, written for the
    // definition on line 3 of this file.
    const CxxFrontendDocument::Signature signature
        = document.signatureAt({"h.h", 2, 18}, {{}, 3, 9});
    QVERIFY(signature.isValid());
    QCOMPARE(signature.name(), QString("C::f"));
    QCOMPARE(signature.parameterCount(), 1);
    QCOMPARE(signature.parameterName(0), QString("t"));
    QCOMPARE(signature.writeParameter(0, "t"), QString("N::T t"));

    // And the other way round, which is what somebody editing the definition
    // is doing.
    const CxxFrontendDocument::Signature back
        = document.signatureAt({{}, 3, 9}, {"h.h", 2, 18});
    QVERIFY(back.isValid());
    QCOMPARE(back.writeParameter(0, "t"), QString("N::T t"));
}

// The member functions a class declares without defining, in the order they
// are written: what putting their definitions in the same order works from.
void tst_cxxfrontenddocument::memberFunctionsOfAClass_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("declarations in the order they are written")
        << QByteArray("struct $S {\n"
                      "    void b();\n"
                      "    int a(int, int);\n"
                      "};\n")
        << QStringList({"S::b/0 @2:10", "S::a/2 @3:9"});

    // Defined here, so its definition is already where its declaration is.
    QTest::newRow("a function defined inside the class")
        << QByteArray("struct $S {\n"
                      "    void b() {}\n"
                      "    void c();\n"
                      "};\n")
        << QStringList("S::c/0 @3:10");

    QTest::newRow("a template member")
        << QByteArray("struct $S {\n"
                      "    template<typename T> void t(T);\n"
                      "};\n")
        << QStringList("S::t/1 @2:31");

    // Nobody wrote it where it stands, so there is no order to keep it in.
    QTest::newRow("a function a macro declared")
        << QByteArray("#define DECL void m();\n"
                      "struct $S {\n"
                      "    DECL\n"
                      "    void n();\n"
                      "};\n")
        << QStringList("S::n/0 @4:10");

    QTest::newRow("the innermost class wins")
        << QByteArray("struct Outer {\n"
                      "    void o();\n"
                      "    struct $Inner { void i(); };\n"
                      "};\n")
        << QStringList("Outer::Inner::i/0 @3:25");

    // Declared with "= 0", so this class does not define it -- said here so
    // that whoever looks for the definitions is not looking for this one's.
    QTest::newRow("a pure virtual function")
        << QByteArray("struct $S {\n"
                      "    virtual void p() = 0;\n"
                      "    void q();\n"
                      "};\n")
        << QStringList({"S::p/0 @2:18 pure", "S::q/0 @3:10"});

    // Written in the class without being one of its members: somebody else's
    // function, named here to let it in. So it is not among the ones whose
    // definitions belong with this class's.
    QTest::newRow("a friend")
        << QByteArray("struct $S {\n"
                      "    friend void f();\n"
                      "    void g();\n"
                      "};\n")
        << QStringList("S::g/0 @3:10");

    QTest::newRow("a position in no class")
        << QByteArray("$void f();\n") << QStringList();
}

void tst_cxxfrontenddocument::memberFunctionsOfAClass()
{
    QFETCH(QByteArray, marked);
    QFETCH(QStringList, expected);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QStringList described;
    const QList<CxxFrontendDocument::MemberFunction> functions
        = document.memberFunctionsAt(positions.first().line, positions.first().column);
    for (const CxxFrontendDocument::MemberFunction &function : functions) {
        described.append(QString("%1/%2 @%3:%4%5").arg(function.name)
                             .arg(function.parameterCount)
                             .arg(function.line).arg(function.column)
                             .arg(function.isPureVirtual ? " pure" : ""));
    }
    QCOMPARE(described, expected);
}

// The class a position is on, as the file it stands in reads it: what it is
// called, what it is written inside, and where its declaration begins and
// ends -- what moving it to files of its own takes away.
void tst_cxxfrontenddocument::classToMove_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("expected");

    QTest::newRow("a class at file scope")
        << QByteArray("void f();\n"
                      "class $C\n"
                      "{\n"
                      "    void g();\n"
                      "};\n")
        << QString("C|C||2:1-5:3|other");

    QTest::newRow("the cursor on the class's own name")
        << QByteArray("void f();\n"
                      "class $C {};\n")
        << QString("C|C||2:1-2:12|other");

    QTest::newRow("a class in nested namespaces")
        << QByteArray("namespace N {\n"
                      "namespace Inner {\n"
                      "void f();\n"
                      "class $C {};\n"
                      "}\n"
                      "}\n")
        << QString("C|N::Inner::C|N, Inner|4:1-4:12|other");

    QTest::newRow("a class written under one namespace name")
        << QByteArray("namespace N::Inner {\n"
                      "void f();\n"
                      "class $C {};\n"
                      "}\n")
        << QString("C|N::Inner::C|N, Inner|3:1-3:12|other");

    // The template header is written around the class and goes with it.
    QTest::newRow("a class template")
        << QByteArray("void f();\n"
                      "template<typename T>\n"
                      "class $C\n"
                      "{\n"
                      "    T t;\n"
                      "};\n")
        << QString("C|C||2:1-6:3|other");

    // A class that is all its file says is where it belongs already, and a
    // class named without being defined says nothing of its own.
    QTest::newRow("the only thing the file says")
        << QByteArray("class $C {};\n") << QString("C|C||1:1-1:12|alone");
    QTest::newRow("a class named but not defined is not something else")
        << QByteArray("class Other;\n"
                      "class $C {};\n")
        << QString("C|C||2:1-2:12|alone");

    // Nothing to answer: a class written inside another one is not at
    // namespace scope, and a position on a member is on the member.
    QTest::newRow("a nested class") << QByteArray("class Outer { class $C {}; };\n")
                                    << QString();
    QTest::newRow("a position on a member")
        << QByteArray("void f();\n"
                      "class C { void $g(); };\n")
        << QString();
    QTest::newRow("a position on nothing at all") << QByteArray("void f();\n$\n") << QString();

    // What the recovery made of it ends where the text does not, so there is
    // no range to carry away. A Qt class is one of these.
    QTest::newRow("a class this front end stumbled over")
        << QByteArray("void f();\n"
                      "class $C\n"
                      "{\n"
                      "signals:\n"
                      "    void s();\n"
                      "};\n")
        << QString();
}

void tst_cxxfrontenddocument::classToMove()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, expected);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::ClassToMove klass
        = document.classToMoveAt(positions.first().line, positions.first().column);
    if (expected.isEmpty()) {
        QVERIFY(!klass.isValid());
        return;
    }
    QVERIFY(klass.isValid());
    QCOMPARE(QString("%1|%2|%3|%4:%5-%6:%7|%8")
                 .arg(klass.className, klass.qualifiedName, klass.namespacePath.join(", "))
                 .arg(klass.declaration.startLine).arg(klass.declaration.startColumn)
                 .arg(klass.declaration.endLine).arg(klass.declaration.endColumn)
                 .arg(klass.hasOtherDeclarations ? "other" : "alone"),
             expected);
}

// Everything a file writes that belongs to a class though it stands outside
// it, which is what has to go along when the class moves.
void tst_cxxfrontenddocument::partsOfAClass_data()
{
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QString>("className");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("a member's definition")
        << QByteArray("class C { void f(); };\n"
                      "void C::f() {}\n")
        << QString("C") << QStringList("2:1-2:15");

    QTest::newRow("a static member's definition")
        << QByteArray("class C { static int i; };\n"
                      "int C::i = 1;\n")
        << QString("C") << QStringList("2:1-2:14");

    // A nested class is written under the class, and so is everything
    // written under it: one rule carries them both.
    QTest::newRow("a nested class and its members")
        << QByteArray("class C { class P; };\n"
                      "class C::P { void g(); };\n"
                      "void C::P::g() {}\n")
        << QString("C") << QStringList({"2:1-2:26", "3:1-3:18"});

    QTest::newRow("a template member's definition")
        << QByteArray("class C { template<typename T> T t() const; };\n"
                      "template<typename T> T C::t() const { return T(); }\n")
        << QString("C") << QStringList("2:1-2:52");

    QTest::newRow("written in the namespace the class is in")
        << QByteArray("namespace N {\n"
                      "class C { void f(); };\n"
                      "void C::f() {}\n"
                      "}\n")
        << QString("N::C") << QStringList("3:1-3:15");

    // The class's own declaration is not a part of it, nor is anything
    // written inside its body: what is asked for is what stays behind.
    QTest::newRow("nothing but the class itself")
        << QByteArray("class C { void f() {} };\n") << QString("C") << QStringList();

    QTest::newRow("another class's member")
        << QByteArray("class C {};\n"
                      "class D { void f(); };\n"
                      "void D::f() {}\n")
        << QString("C") << QStringList();

    // A class of the same name in another namespace is another class.
    QTest::newRow("the same name somewhere else")
        << QByteArray("class C { void f(); };\n"
                      "namespace N { class C { void f(); }; }\n"
                      "void N::C::f() {}\n")
        << QString("C") << QStringList();
}

void tst_cxxfrontenddocument::partsOfAClass()
{
    QFETCH(QByteArray, source);
    QFETCH(QString, className);
    QFETCH(QStringList, expected);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QStringList described;
    for (const CxxFrontendDocument::Extent &part : document.partsOfClass(className)) {
        described.append(QString("%1:%2-%3:%4").arg(part.startLine).arg(part.startColumn)
                             .arg(part.endLine).arg(part.endColumn));
    }
    QCOMPARE(described, expected);
}

// Whether the function at a place is virtual, and where the declarations
// that first made it so are written. The first marker is the function asked
// about; the rest are the places expected to come back.
void tst_cxxfrontenddocument::virtuality_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("expected");

    QTest::newRow("not virtual at all")
        << QByteArray("struct A { void $f(); };\n") << QString("plain");

    QTest::newRow("virtual where it is declared")
        << QByteArray("struct A { virtual void $f(); };\n") << QString("virtual @1:25");

    QTest::newRow("pure virtual")
        << QByteArray("struct A { virtual void $f() = 0; };\n") << QString("pure @1:25");

    // The function is virtual because a base said so, and the base's
    // declaration is what a reader is offered.
    QTest::newRow("virtual because a base says so")
        << QByteArray("struct A { virtual void f(); };\n"
                      "struct B : A { void $f(); };\n")
        << QString("virtual @1:25");

    QTest::newRow("override written out")
        << QByteArray("struct A { virtual void f(); };\n"
                      "struct B : A { void $f() override; };\n")
        << QString("virtual @1:25");

    // The declarations furthest up are the ones kept: what the middle class
    // says is not where it was first made virtual.
    QTest::newRow("the base furthest up wins")
        << QByteArray("struct A { virtual void f(); };\n"
                      "struct B : A { virtual void f(); };\n"
                      "struct C : B { void $f(); };\n")
        << QString("virtual @1:25");

    // Two bases declaring it, both as far up as the other.
    QTest::newRow("two bases at the same height")
        << QByteArray("struct A { virtual void f(); };\n"
                      "struct B { virtual void f(); };\n"
                      "struct C : A, B { void $f(); };\n")
        << QString("virtual @1:25, @2:25");

    // A base that declares it final ends the search: nothing below it
    // overrides anything.
    QTest::newRow("a base declares it final")
        << QByteArray("struct A { virtual void f(); };\n"
                      "struct B : A { void f() final; };\n"
                      "struct C : B { void $f(); };\n")
        << QString("plain");

    // Same name, another signature: another function.
    QTest::newRow("a base with another signature")
        << QByteArray("struct A { virtual void f(int); };\n"
                      "struct B : A { void $f(); };\n")
        << QString("plain");

    // Constness is part of it.
    QTest::newRow("a base whose function is const")
        << QByteArray("struct A { virtual void f() const; };\n"
                      "struct B : A { void $f(); };\n")
        << QString("plain");

    QTest::newRow("a position on no function")
        << QByteArray("struct A { int $i; };\n") << QString();
}

void tst_cxxfrontenddocument::virtuality()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, expected);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::Virtuality virtuality
        = document.virtualityAt(positions.first().line, positions.first().column);
    if (expected.isEmpty()) {
        QVERIFY(!virtuality.namesAFunction);
        return;
    }
    QVERIFY(virtuality.namesAFunction);

    QString described = virtuality.isPureVirtual ? "pure"
                                                 : (virtuality.isVirtual ? "virtual" : "plain");
    QStringList places;
    for (const CxxFrontendDocument::Place &place : virtuality.firstVirtuals)
        places.append(QString("@%1:%2").arg(place.line).arg(place.column));
    if (!places.isEmpty())
        described += ' ' + places.join(", ");
    QCOMPARE(described, expected);
}

// Find usages asked of one file, which is what a search over the project
// asks of each file in turn. The first marker is the declaration asked
// about, and the rest are the places expected to name it.
void tst_cxxfrontenddocument::usagesInAFile_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("a variable")
        << QByteArray("void f()\n"
                      "{\n"
                      "    int $x = 0;\n"
                      "    $x = 1;\n"
                      "    int y = $x;\n"
                      "}\n")
        << QStringList({"3:9 declaration", "4:5", "5:13"});

    // A name spelled the same and meaning something else is not a usage of
    // this one.
    QTest::newRow("a name that means something else")
        << QByteArray("void f()\n"
                      "{\n"
                      "    int $x = 0;\n"
                      "    $x = 1;\n"
                      "}\n"
                      "void g()\n"
                      "{\n"
                      "    int x = 0;\n"
                      "    x = 1;\n"
                      "}\n")
        << QStringList({"3:9 declaration", "4:5"});

    QTest::newRow("a function and its call")
        << QByteArray("void $f();\n"
                      "void g() { $f(); }\n")
        << QStringList({"1:6 declaration", "2:12"});

    // A definition written apart from its declaration declares the same
    // thing, which is what the canonical place says.
    QTest::newRow("a definition apart from its declaration")
        << QByteArray("struct C { void $f(); };\n"
                      "void C::$f() {}\n"
                      "void g(C &c) { c.$f(); }\n")
        << QStringList({"1:17 declaration", "2:9 declaration", "3:18"});

    QTest::newRow("a class")
        << QByteArray("class $C {};\n"
                      "$C c;\n"
                      "void f($C &) {}\n")
        << QStringList({"1:7 declaration", "2:1", "3:8"});

    // A member of another class of the same name is another member.
    QTest::newRow("a member of another class")
        << QByteArray("struct A { int $m; };\n"
                      "struct B { int m; };\n"
                      "void f(A &a, B &b) { a.$m = b.m; }\n")
        << QStringList({"1:16 declaration", "3:24"});

    // A constructor and a destructor are written under their class's name,
    // so a place naming one names the class -- which is what renaming a
    // class has to reach.
    QTest::newRow("a class, its constructor and its destructor")
        << QByteArray("class $C {\n"
                      "    $C() {}\n"
                      "    ~$C();\n"
                      "};\n"
                      "$C::~$C() {}\n"
                      "$C c;\n")
        << QStringList({"1:7 declaration", "2:5 declaration", "3:6 declaration",
                        "5:1", "5:5 declaration", "6:1"});

    QTest::newRow("a position that declares nothing")
        << QByteArray("void f() { int x = 0; $x = 1; }\n") << QStringList();
}

void tst_cxxfrontenddocument::usagesInAFile()
{
    QFETCH(QByteArray, marked);
    QFETCH(QStringList, expected);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QVERIFY(!positions.isEmpty());

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QStringList described;
    for (const CxxFrontendDocument::NamedPlace &place :
         document.usagesOf({{}, positions.first().line, positions.first().column})) {
        described.append(QString("%1:%2%3").arg(place.place.line).arg(place.place.column)
                             .arg(place.isDeclaration ? " declaration" : ""));
    }
    QCOMPARE(described, expected);
}

// Every class the file writes, with what its bases resolve to: what a search
// for the classes deriving from one of them compares against.
void tst_cxxfrontenddocument::classesWithTheirBases_data()
{
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("a chain")
        << QByteArray("class A {};\n"
                      "class B : public A {};\n"
                      "class C : public B {};\n")
        << QStringList({"A @1:7", "B @2:7 : A", "C @3:7 : B"});

    // The parser resolved the alias, so nothing here has to follow one.
    QTest::newRow("a base named through an alias")
        << QByteArray("class A {};\n"
                      "typedef A AA;\n"
                      "using AAA = AA;\n"
                      "class B : public AA {};\n"
                      "class C : public AAA {};\n")
        << QStringList({"A @1:7", "B @4:7 : A", "C @5:7 : A"});

    // Two classes of one name are two classes, which is what writing the
    // path out says.
    QTest::newRow("a name that means something else elsewhere")
        << QByteArray("class A {};\n"
                      "namespace N {\n"
                      "class A {};\n"
                      "class B : public A {};\n"
                      "}\n"
                      "class C : public A {};\n")
        << QStringList({"A @1:7", "N::A @3:7", "N::B @4:7 : N::A", "C @6:7 : A"});

    QTest::newRow("more than one base")
        << QByteArray("class A {};\n"
                      "class Other {};\n"
                      "class B : public Other, public A {};\n")
        << QStringList({"A @1:7", "Other @2:7", "B @3:7 : Other, A"});

    // How a class inherits says nothing about what it inherits.
    QTest::newRow("privately")
        << QByteArray("class A {};\n"
                      "class B : private A {};\n")
        << QStringList({"A @1:7", "B @2:7 : A"});

    QTest::newRow("a template deriving from it")
        << QByteArray("class A {};\n"
                      "template<typename T> class B : public A {};\n")
        << QStringList({"A @1:7", "B @2:28 : A"});

    QTest::newRow("a nested class")
        << QByteArray("class A {};\n"
                      "class Outer { class Inner : public A {}; };\n")
        << QStringList({"A @1:7", "Outer @2:7", "Outer::Inner @2:21 : A"});

    QTest::newRow("nothing that is not a class")
        << QByteArray("struct S {};\nenum E { E1 };\nvoid f();\n")
        << QStringList("S @1:8");
}

void tst_cxxfrontenddocument::classesWithTheirBases()
{
    QFETCH(QByteArray, source);
    QFETCH(QStringList, expected);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QStringList described;
    for (const CxxFrontendDocument::ClassWithBases &written : document.classesWithTheirBases()) {
        QString line = QString("%1 @%2:%3").arg(written.qualifiedName)
                           .arg(written.place.line).arg(written.place.column);
        if (!written.bases.isEmpty())
            line += " : " + written.bases.join(", ");
        described.append(line);
    }
    QCOMPARE(described, expected);
}

// The function written around a position, as a reader about to write
// another one beside it needs it.
void tst_cxxfrontenddocument::enclosingFunction_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("expected");

    QTest::newRow("a free function")
        << QByteArray("void f()\n"
                      "{\n"
                      "    $g();\n"
                      "}\n")
        << QString("f @1:6 1:1-4:2 ");

    QTest::newRow("a const member defined outside its class")
        << QByteArray("namespace NS {\n"
                      "class C { void f() const; };\n"
                      "}\n"
                      "void NS::C::f() const\n"
                      "{\n"
                      "    $g();\n"
                      "}\n")
        << QString("f @4:13 4:1-7:2 const NS::C:: @2:7");

    // Written in its class, where a second definition needs no
    // qualification at all.
    QTest::newRow("a member defined in its class")
        << QByteArray("class C {\n"
                      "    void f() { $g(); }\n"
                      "};\n")
        << QString("f @2:10 2:5-2:22 inside  @1:7");

    QTest::newRow("a position in no function") << QByteArray("$int i;\n") << QString();
}

void tst_cxxfrontenddocument::enclosingFunction()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, expected);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::EnclosingFunction function
        = document.enclosingFunctionAt(positions.first().line, positions.first().column);
    if (expected.isEmpty()) {
        QVERIFY(!function.isValid());
        return;
    }
    QVERIFY(function.isValid());

    QString described = QString("%1 @%2:%3 %4:%5-%6:%7")
                            .arg(function.name)
                            .arg(function.namePlace.line).arg(function.namePlace.column)
                            .arg(function.definition.startLine)
                            .arg(function.definition.startColumn)
                            .arg(function.definition.endLine).arg(function.definition.endColumn);
    described += function.isConst ? " const" : (function.isWrittenInAClass ? " inside" : "");
    described += ' ';
    if (function.writtenQualifier.isValid()) {
        described += QString::fromUtf8(source).sliced(
            0, 0); // keep the line count of the source out of the answer
        const QStringList lines = QString::fromUtf8(source).split('\n');
        const QString line = lines.at(function.writtenQualifier.startLine - 1);
        described += line.sliced(function.writtenQualifier.startColumn - 1,
                                 function.writtenQualifier.endColumn
                                     - function.writtenQualifier.startColumn);
    }
    if (function.isMemberFunction) {
        described += QString(" @%1:%2").arg(function.classNamePlace.line)
                         .arg(function.classNamePlace.column);
    }
    QCOMPARE(described, expected);
}

// The type declared at a place, written for somewhere else -- a function
// handing a local back has to say it where its own name stands -- or
// written around a name, which is what rewriting the declaration needs.
void tst_cxxfrontenddocument::typeDeclared_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");

    // The first marker is the local, the second the place it is written
    // for.
    QTest::newRow("an int")
        << QByteArray("void f()\n"
                      "{\n"
                      "    int $i = 1;\n"
                      "}\n"
                      "$")
        << QString() << QString("int");

    QTest::newRow("a class of a namespace, written outside it")
        << QByteArray("namespace NS {\n"
                      "class C {};\n"
                      "void f()\n"
                      "{\n"
                      "    C $c;\n"
                      "}\n"
                      "}\n"
                      "$")
        << QString() << QString("NS::C");

    QTest::newRow("the same, written inside the namespace")
        << QByteArray("namespace NS {\n"
                      "class C {};\n"
                      "void f()\n"
                      "{\n"
                      "    C $c;\n"
                      "    $\n"
                      "}\n"
                      "}\n")
        << QString() << QString("C");

    // The body of a member defined outside its class hangs off the
    // definition, not off the declaration the class holds.
    QTest::newRow("in a member defined outside its class")
        << QByteArray("namespace NS {\n"
                      "class C { void f(); };\n"
                      "}\n"
                      "void NS::C::f()\n"
                      "{\n"
                      "    C $c2;\n"
                      "}\n"
                      "$")
        << QString() << QString("NS::C");

    QTest::newRow("a parameter")
        << QByteArray("void f(const char *$p)\n"
                      "{\n"
                      "}\n"
                      "$")
        << QString() << QString("const char *");

    // Written around the name the caller hands over, which is how a
    // declaration is rewritten where it stands: a declarator is written
    // around a name and no amount of putting it after the type gets there.
    QTest::newRow("a pointer, under another name")
        << QByteArray("char *$s;\n$")
        << QString("total") << QString("char *total");
    QTest::newRow("a pointer to a function, under another name")
        << QByteArray("void (*$p)(int);\n$")
        << QString("total") << QString("void (*total)(int)");

    // A function's own type is what it hands back: the part of it written
    // in front of its name.
    QTest::newRow("what a function hands back")
        << QByteArray("char *$f(int);\n$")
        << QString("f") << QString("char *f");

    // The name as it is written, qualification and all, so that nothing of
    // what somebody wrote is lost where the declaration is rewritten.
    QTest::newRow("a member defined out of line, named as written")
        << QByteArray("struct C { char *f(); };\n"
                      "char *C::$f() { return nullptr; }\n$")
        << QString("C::f") << QString("char *C::f");

    QTest::newRow("a position that declares nothing")
        << QByteArray("void f()\n"
                      "{\n"
                      "    int i = 1;\n"
                      "    $i = 2;\n"
                      "}\n"
                      "$")
        << QString() << QString();
}

void tst_cxxfrontenddocument::typeDeclared()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, name);
    QFETCH(QString, expected);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 2);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QCOMPARE(document.typeDeclaredAt(positions.first().line, positions.first().column, name,
                                     {{}, positions.last().line, positions.last().column}),
             expected);
}

// The using directive at the cursor.
void tst_cxxfrontenddocument::usingDirectiveAt_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("expected");

    QTest::newRow("on the directive")
        << QByteArray("namespace N {}\n"
                      "$using namespace N;\n")
        << QString("N 2:1-2:19 global");
    QTest::newRow("on the name it names")
        << QByteArray("namespace N {}\n"
                      "using namespace $N;\n")
        << QString("N 2:1-2:19 global");
    QTest::newRow("in a block")
        << QByteArray("namespace N {}\n"
                      "void f() { $using namespace N; }\n")
        << QString("N 2:12-2:30 scoped");

    // More than a name would have to be written in front of what it
    // found, which the fix reading this does not offer.
    QTest::newRow("a nested namespace")
        << QByteArray("namespace N { namespace M {} }\n"
                      "$using namespace N::M;\n")
        << QString();
    QTest::newRow("a using declaration is not one")
        << QByteArray("namespace N { int i; }\n"
                      "$using N::i;\n")
        << QString();
    QTest::newRow("on nothing of the sort") << QByteArray("$int i;\n") << QString();
}

void tst_cxxfrontenddocument::usingDirectiveAt()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, expected);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::UsingDirective directive
        = document.usingDirectiveAt(positions.first().line, positions.first().column);
    if (expected.isEmpty()) {
        QVERIFY(!directive.isValid());
        return;
    }
    QVERIFY(directive.isValid());
    QCOMPARE(QString("%1 %2:%3-%4:%5 %6").arg(directive.namespaceName)
                 .arg(directive.extent.startLine).arg(directive.extent.startColumn)
                 .arg(directive.extent.endLine).arg(directive.extent.endColumn)
                 .arg(directive.isAtGlobalScope ? "global" : "scoped"),
             expected);
}

// What taking a using directive away comes down to: which directives go,
// and which names have to say the namespace once they are gone.
void tst_cxxfrontenddocument::usingDirectives_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("namespaceName");
    QTest::addColumn<bool>("everyOneAtGlobalScope");
    QTest::addColumn<QString>("expected");

    // The marker stands just after the directive being taken away, which
    // is where the reading starts.
    QTest::newRow("a type and a value")
        << QByteArray("namespace N { struct C {}; int i; void f(); }\n"
                      "using namespace N;$\n"
                      "C c;\n"
                      "int j = i;\n"
                      "void g() { f(); }\n")
        << QString("N") << false
        << QString(" | 3:1, 4:9, 5:12 | global | clear");

    // What stands after a :: is looked up in what stands before it, so the
    // first component is the only one the directive can have found.
    QTest::newRow("a qualified name")
        << QByteArray("namespace N { struct C { static int i; }; }\n"
                      "using namespace N;$\n"
                      "int j = C::i;\n")
        << QString("N") << false
        << QString(" | 3:9 | global | clear");

    // An unscoped enumeration puts its values in the scope around it too,
    // so the namespace is what stands in front of them.
    QTest::newRow("an enumerator")
        << QByteArray("namespace N { enum E {E1, E2}; }\n"
                      "using namespace N;$\n"
                      "E val = E1;\n")
        << QString("N") << false
        << QString(" | 3:1, 3:9 | global | clear");

    // Nobody writes an inline namespace's name, so it is not part of the
    // path either.
    QTest::newRow("a class in an inline namespace")
        << QByteArray("namespace N { inline namespace V { struct C {}; } }\n"
                      "using namespace N;$\n"
                      "C c;\n")
        << QString("N") << false
        << QString(" | 3:1 | global | clear");

    QTest::newRow("a name that already says the namespace")
        << QByteArray("namespace N { struct C {}; }\n"
                      "using namespace N;$\n"
                      "N::C c;\n")
        << QString("N") << false
        << QString(" |  | global | clear");

    QTest::newRow("a name of something else")
        << QByteArray("namespace N { struct C {}; }\n"
                      "struct D {};\n"
                      "using namespace N;$\n"
                      "D d;\n")
        << QString("N") << false
        << QString(" |  | global | clear");

    // Inside the namespace itself nothing has to name it.
    QTest::newRow("written in the namespace itself")
        << QByteArray("namespace N { struct C {}; }\n"
                      "using namespace N;$\n"
                      "namespace N { C c; }\n")
        << QString("N") << false
        << QString(" |  | global | clear");

    // A name written before the directive never leaned on it.
    QTest::newRow("written before the directive")
        << QByteArray("namespace N { struct C {}; }\n"
                      "using N::C;\n"
                      "C before;\n"
                      "using namespace N;$\n"
                      "C after;\n")
        << QString("N") << false
        << QString(" | 5:1 | global | clear");

    // Another directive keeps the namespace in force, so nothing under it
    // has to say it -- and the files that include this one are none the
    // wiser either.
    QTest::newRow("a second directive at global scope")
        << QByteArray("namespace N { struct C {}; }\n"
                      "using namespace N;$\n"
                      "C first;\n"
                      "using namespace N;\n"
                      "C second;\n")
        << QString("N") << false
        << QString(" | 3:1 | global | shadowed");

    // Both of them go, and then everything does have to say it.
    QTest::newRow("every one at global scope")
        << QByteArray("namespace N { struct C {}; }\n"
                      "using namespace N;$\n"
                      "C first;\n"
                      "using namespace N;\n"
                      "C second;\n")
        << QString("N") << true
        << QString("4:1-4:19 | 3:1, 5:1 | global | clear");

    // A directive in a block is in force until the block ends, and it
    // reaches nothing that includes the file.
    QTest::newRow("a directive in a block")
        << QByteArray("namespace N { struct C {}; }\n"
                      "void f() {\n"
                      "using namespace N;$\n"
                      "C inside;\n"
                      "}\n"
                      "N::C outside;\n")
        << QString("N") << false
        << QString(" | 4:1 | scoped | clear");

    // Nothing to start after: find the file's own directive, take it away
    // and read on. What a file that merely includes the header is asked.
    QTest::newRow("find the directive")
        << QByteArray("namespace N { struct C {}; }\n"
                      "using namespace N;\n"
                      "C c;\n")
        << QString("N") << false
        << QString("2:1-2:19 | 3:1 | global | clear");

    // The file says it itself, so taking it out of the header changes
    // nothing here.
    QTest::newRow("a directive of its own before the start")
        << QByteArray("namespace N { struct C {}; }\n"
                      "using namespace N;\n"
                      "$C c;\n")
        << QString("N") << false
        << QString(" |  | global | clear");
}

void tst_cxxfrontenddocument::usingDirectives()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, namespaceName);
    QFETCH(bool, everyOneAtGlobalScope);
    QFETCH(QString, expected);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QVERIFY(positions.size() <= 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::UsingDirectives read = document.usingDirectivesOf(
        namespaceName,
        positions.isEmpty() ? 0 : positions.first().line,
        positions.isEmpty() ? 0 : positions.first().column,
        everyOneAtGlobalScope);

    QStringList directives;
    for (const CxxFrontendDocument::Extent &directive : read.directivesToRemove) {
        directives.append(QString("%1:%2-%3:%4").arg(directive.startLine)
                              .arg(directive.startColumn)
                              .arg(directive.endLine).arg(directive.endColumn));
    }
    QStringList places;
    for (const CxxFrontendDocument::Place &place : read.placesNeedingTheNamespace)
        places.append(QString("%1:%2").arg(place.line).arg(place.column));

    QCOMPARE(QString("%1 | %2 | %3 | %4")
                 .arg(directives.join(", "), places.join(", "),
                      read.isGlobalUsingNamespace ? "global" : "scoped",
                      read.foundGlobalUsingNamespace ? "shadowed" : "clear"),
             expected);
}

// A literal inside a function: its type, and every place that function
// writes the same thing -- which is what turning it into one parameter
// rests on.
void tst_cxxfrontenddocument::literalInAFunction_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("expectedType");
    QTest::addColumn<QStringList>("expectedPlaces");

    QTest::newRow("an int")
        << QByteArray("int foo() { return $156; }\n") << "int"
        << QStringList("1:20+3");

    QTest::newRow("a suffixed int")
        << QByteArray("unsigned long long foo() { return $156ull; }\n")
        << "unsigned long long" << QStringList("1:35+6");

    QTest::newRow("a string")
        << QByteArray("const char *foo() { return $\"narf\"; }\n")
        << "const char *" << QStringList("1:28+6");

    QTest::newRow("a bool")
        << QByteArray("bool foo() { return $true; }\n") << "bool"
        << QStringList("1:21+4");

    QTest::newRow("a char")
        << QByteArray("char foo() { return $'c'; }\n") << "char"
        << QStringList("1:21+3");

    // Every place the function writes the same literal, since one parameter
    // stands for all of them -- and nothing else, however similar.
    QTest::newRow("the same literal written more than once")
        << QByteArray("int foo() { return $156 + 123 + 156; }\n") << "int"
        << QStringList({"1:20+3", "1:32+3"});

    QTest::newRow("a literal of another kind that reads the same")
        << QByteArray("int foo() { char c = '1'; return $1; }\n") << "int"
        << QStringList("1:34+1");
}

void tst_cxxfrontenddocument::literalInAFunction()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, expectedType);
    QFETCH(QStringList, expectedPlaces);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::LiteralInAFunction found
        = document.literalInAFunctionAt(positions.first().line, positions.first().column);
    QVERIFY(found.isValid());
    QCOMPARE(found.type, expectedType);

    QStringList places;
    for (const CxxFrontendDocument::Occurrence &place : found.places) {
        places.append(QString("%1:%2+%3").arg(place.line).arg(place.column).arg(place.length));
    }
    QCOMPARE(places, expectedPlaces);
}

void tst_cxxfrontenddocument::noLiteralToExtract_data()
{
    QTest::addColumn<QByteArray>("marked");

    QTest::newRow("a literal outside any function")
        << QByteArray("int global = $156;\n");
    QTest::newRow("a position on a name")
        << QByteArray("int foo(int a) { return $a; }\n");
    QTest::newRow("a position on no literal at all")
        << QByteArray("int foo(int a) { return a $+ 1; }\n");
}

void tst_cxxfrontenddocument::noLiteralToExtract()
{
    QFETCH(QByteArray, marked);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QVERIFY(!document.literalInAFunctionAt(positions.first().line,
                                            positions.first().column).isValid());
}

// A call whose value is thrown away, which is what offering to assign it to
// a variable rests on: what is called, and what has to be written in front
// of it to keep the value.
void tst_cxxfrontenddocument::discardedValue_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("expectedName");
    QTest::addColumn<QString>("expectedDeclaration");

    QTest::newRow("a free function")
        << QByteArray("int foo();\nvoid bar() { $foo(); }\n")
        << "foo" << "int foo";

    QTest::newRow("a member function through a pointer")
        << QByteArray("struct Foo { int *fooFunc(); };\n"
                      "void bar() { Foo *f = nullptr; f->$fooFunc(); }\n")
        << "fooFunc" << "int *fooFunc";

    QTest::newRow("a static member function")
        << QByteArray("struct Foo { static int *s(); };\n"
                      "void bar() { Foo::$s(); }\n")
        << "s" << "int *s";

    QTest::newRow("a new expression")
        << QByteArray("struct Foo {};\nvoid bar() { $new Foo; }\n")
        << "Foo" << "Foo *Foo";

    // The type is written for where it is going, so a class in a namespace
    // the statement is not in has to be named with it.
    QTest::newRow("a value of a type from another namespace")
        << QByteArray("namespace N { struct T {}; T make(); }\n"
                      "void bar() { N::$make(); }\n")
        << "make" << "N::T make";

    QTest::newRow("a value of a type the scope reaches")
        << QByteArray("namespace N { struct T {}; T make();\n"
                      "void bar() { $make(); } }\n")
        << "make" << "T make";
}

void tst_cxxfrontenddocument::discardedValue()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, expectedName);
    QFETCH(QString, expectedDeclaration);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::DiscardedValue found
        = document.discardedValueAt(positions.first().line, positions.first().column);
    QVERIFY(found.isValid());
    QCOMPARE(found.name, expectedName);
    QCOMPARE(found.declaration, expectedDeclaration);
}

void tst_cxxfrontenddocument::noDiscardedValue_data()
{
    QTest::addColumn<QByteArray>("marked");

    QTest::newRow("a value that is used as an argument")
        << QByteArray("int foo(int);\nint bar();\nvoid baz() { foo($bar()); }\n");
    QTest::newRow("a value that is returned")
        << QByteArray("int bar();\nint baz() { return $bar(); }\n");
    QTest::newRow("a value that is assigned already")
        << QByteArray("int bar();\nvoid baz() { int a = $bar(); }\n");
    QTest::newRow("a call of something that returns nothing")
        << QByteArray("void foo();\nvoid bar() { $foo(); }\n");
    QTest::newRow("a call the front end could not resolve")
        << QByteArray("int someFunc(int);\nvoid f() { $someFunc(); }\n");
    QTest::newRow("a position on no call at all")
        << QByteArray("int bar();\nvoid baz() { $int a = 1; }\n");
}

void tst_cxxfrontenddocument::noDiscardedValue()
{
    QFETCH(QByteArray, marked);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QVERIFY(!document.discardedValueAt(positions.first().line,
                                        positions.first().column).isValid());
}

// Which values a switch over an enumeration does not handle, and how each
// has to be written where the switch is -- the two halves of one question,
// since telling a handled value from a missing one means naming both the
// same way.
void tst_cxxfrontenddocument::switchOverAnEnum_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QStringList>("expectedValues");

    QTest::newRow("an unscoped enum, named where it is written")
        << QByteArray("enum E { V1, V2 };\n"
                      "void f(E e) { $switch (e) { } }\n")
        << QStringList({"V1", "V2"});

    QTest::newRow("a scoped enum, named under itself")
        << QByteArray("enum class E { V1, V2 };\n"
                      "void f(E e) { $switch (e) { } }\n")
        << QStringList({"E::V1", "E::V2"});

    QTest::newRow("an unscoped enum in a namespace")
        << QByteArray("namespace N { enum E { V1, V2 }; }\n"
                      "void f(N::E e) { $switch (e) { } }\n")
        << QStringList({"N::V1", "N::V2"});

    QTest::newRow("a scoped enum in a namespace")
        << QByteArray("namespace N { enum class E { V1, V2 }; }\n"
                      "void f(N::E e) { $switch (e) { } }\n")
        << QStringList({"N::E::V1", "N::E::V2"});

    QTest::newRow("the values it already handles are left out")
        << QByteArray("enum E { V1, V2, V3 };\n"
                      "void f(E e) { $switch (e) { case V2: break; } }\n")
        << QStringList({"V1", "V3"});

    // A case of a switch of its own is that switch's, not this one's.
    QTest::newRow("a case of a switch nested inside it")
        << QByteArray("enum E { V1, V2 };\n"
                      "void f(E e, E o) {\n"
                      "    $switch (e) {\n"
                      "    default:\n"
                      "        switch (o) { case V1: break; }\n"
                      "    }\n"
                      "}\n")
        << QStringList({"V1", "V2"});

    QTest::newRow("an enum with nothing left to handle")
        << QByteArray("enum E { V1 };\n"
                      "void f(E e) { $switch (e) { case V1: break; } }\n")
        << QStringList();
}

void tst_cxxfrontenddocument::switchOverAnEnum()
{
    QFETCH(QByteArray, marked);
    QFETCH(QStringList, expectedValues);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    const CxxFrontendDocument::Switch found
        = document.switchAt(positions.first().line, positions.first().column);
    QVERIFY(found.isValid());
    QCOMPARE(found.missingValues, expectedValues);
}

void tst_cxxfrontenddocument::noSwitchToComplete_data()
{
    QTest::addColumn<QByteArray>("marked");

    QTest::newRow("a position in no switch")
        << QByteArray("enum E { V1 };\nvoid f(E e) { $int x = 1; }\n");
    QTest::newRow("a switch over something that is not an enum")
        << QByteArray("void f(int i) { $switch (i) { } }\n");
    // "switch (e) case V1: ;" has no block to write a case into.
    QTest::newRow("a switch whose body is not a block")
        << QByteArray("enum E { V1, V2 };\nvoid f(E e) { $switch (e) case V1: ; }\n");
    // A variable named like its own enumeration, which C++ says wins as an
    // expression. This front end does not resolve the name at all there, so
    // the condition has no type and there is nothing to read -- on
    // unsupportedQueries(), and the built-in front end answers these.
    QTest::newRow("a variable named like its own enum")
        << QByteArray("enum class E { V1, V2 };\n"
                      "void f() { enum E E; $switch (E) { } }\n");
}

void tst_cxxfrontenddocument::noSwitchToComplete()
{
    QFETCH(QByteArray, marked);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 1);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QVERIFY(!document.switchAt(positions.first().line, positions.first().column).isValid());
}

// A function written out as a declaration for somewhere else, which is what
// moving a definition has to write there.
void tst_cxxfrontenddocument::declarationOfAFunctionAt_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("declaration");

    // The first marker is the function, the second the place it is written
    // at.
    QTest::newRow("a member, written inside its class")
        << QByteArray("struct C {\n"
                      "    void $f(int a);\n"
                      "    void $g();\n"
                      "};\n")
        << QString("f") << QString("void f(int a)");

    QTest::newRow("a member, written outside its class")
        << QByteArray("struct C {\n"
                      "    void $f(int a);\n"
                      "};\n"
                      "void C::$f(int a) {}\n")
        << QString("C::f") << QString("void C::f(int a)");

    QTest::newRow("what it says about itself comes along")
        << QByteArray("struct C {\n"
                      "    int $f() const noexcept;\n"
                      "    void $g();\n"
                      "};\n")
        << QString("f") << QString("int f() const noexcept");

    // A type of the class's own needs the class written in front of it
    // outside, and nothing within.
    QTest::newRow("a type of the class, written inside it")
        << QByteArray("struct C {\n"
                      "    struct T {};\n"
                      "    T $f();\n"
                      "    void $g();\n"
                      "};\n")
        << QString("f") << QString("T f()");

    QTest::newRow("a type of the class, written outside it")
        << QByteArray("struct C {\n"
                      "    struct T {};\n"
                      "    T $f();\n"
                      "};\n"
                      "C::T C::$f() {}\n")
        << QString("C::f") << QString("C::T C::f()");

    // A name is written around a parameter and not after its type, which
    // is why the printer writes it rather than the caller.
    QTest::newRow("a parameter whose name goes inside its declarator")
        << QByteArray("struct C {\n"
                      "    void $f(void (*cb)(int), int a[4]);\n"
                      "    void $g();\n"
                      "};\n")
        << QString("f") << QString("void f(void (*cb)(int), int *a)");

    QTest::newRow("a parameter the function leaves unnamed")
        << QByteArray("struct C {\n"
                      "    void $f(int a, double, char c);\n"
                      "    void $g();\n"
                      "};\n")
        << QString("f") << QString("void f(int a, double, char c)");

    QTest::newRow("a position on no function")
        << QByteArray("struct C {\n"
                      "    int $m;\n"
                      "    void $g();\n"
                      "};\n")
        << QString("f") << QString();
}

void tst_cxxfrontenddocument::declarationOfAFunctionAt()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, name);
    QFETCH(QString, declaration);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 2);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QCOMPARE(document.declarationOfFunctionAt({{}, positions.first().line,
                                               positions.first().column},
                                              {{}, positions.last().line,
                                               positions.last().column},
                                              name),
             declaration);
}

// The head of a definition written somewhere else: the same as above, less
// the caller's say over the name -- a definition is of one function and how
// much of its path stands in front of it is settled by where it goes.
void tst_cxxfrontenddocument::definitionHeadAt_data()
{
    QTest::addColumn<QByteArray>("marked");
    QTest::addColumn<QString>("head");

    // The first marker is the function, the second the place it goes.
    QTest::newRow("a member, going just outside its class")
        << QByteArray("struct C {\n"
                      "    void $f(int a);\n"
                      "};\n"
                      "$\n")
        << QString("void C::f(int a)");

    QTest::newRow("a member of a class in a namespace, going into the namespace")
        << QByteArray("namespace N {\n"
                      "struct C {\n"
                      "    void $f();\n"
                      "};\n"
                      "$\n"
                      "}\n")
        << QString("void C::f()");

    QTest::newRow("the same, going outside the namespace")
        << QByteArray("namespace N {\n"
                      "struct C {\n"
                      "    void $f();\n"
                      "};\n"
                      "}\n"
                      "$\n")
        << QString("void N::C::f()");

    // A type of the class needs the class in front of it outside, and the
    // name it is a definition of needs it too.
    QTest::newRow("a type of the class comes along")
        << QByteArray("struct C {\n"
                      "    struct T {};\n"
                      "    T $f(T t);\n"
                      "};\n"
                      "$\n")
        << QString("C::T C::f(C::T t)");

    QTest::newRow("what it says about itself comes along")
        << QByteArray("struct C {\n"
                      "    int $f() const noexcept;\n"
                      "};\n"
                      "$\n")
        << QString("int C::f() const noexcept");

    // A free function is written under its own name wherever it goes.
    QTest::newRow("a free function")
        << QByteArray("void $f(int a);\n"
                      "$\n")
        << QString("void f(int a)");

    // The "template<...>" is part of a definition written apart from its
    // declaration, and this does not write one -- so it hands back rather
    // than writing half of it.
    QTest::newRow("a function under a template")
        << QByteArray("template<typename T>\n"
                      "struct C {\n"
                      "    void $f(T t);\n"
                      "};\n"
                      "$\n")
        << QString();

    // The type records that a function is noexcept and not the expression
    // somebody wrote in it, so a head written from the type would say
    // something other than what the declaration says.
    QTest::newRow("an exception specification with an expression in it")
        << QByteArray("struct C {\n"
                      "    void $f() noexcept(false);\n"
                      "};\n"
                      "$\n")
        << QString();

    // Plain noexcept is in the type and does come along.
    QTest::newRow("a plain exception specification")
        << QByteArray("struct C {\n"
                      "    void $f() noexcept;\n"
                      "};\n"
                      "$\n")
        << QString("void C::f() noexcept");

    // A friend is written in a class without belonging to it, so the name
    // it is declared under is not the class's -- and which name it is takes
    // a lookup this does not do.
    QTest::newRow("a friend")
        << QByteArray("namespace N {\n"
                      "void f();\n"
                      "struct C {\n"
                      "    friend void $f();\n"
                      "};\n"
                      "$\n"
                      "}\n")
        << QString();

    QTest::newRow("a position on no function")
        << QByteArray("struct C {\n"
                      "    int $m;\n"
                      "};\n"
                      "$\n")
        << QString();
}

void tst_cxxfrontenddocument::definitionHeadAt()
{
    QFETCH(QByteArray, marked);
    QFETCH(QString, head);

    QList<Position> positions;
    const QByteArray source = takeMarkers(marked, positions);
    QCOMPARE(positions.size(), 2);

    const CxxFrontendDocument document(QString::fromUtf8(source), "<stdin>");
    QCOMPARE(document.definitionHeadAt({{}, positions.first().line,
                                        positions.first().column},
                                       {{}, positions.last().line,
                                        positions.last().column}),
             head);
}

QTEST_GUILESS_MAIN(tst_cxxfrontenddocument)

#include "tst_cxxfrontenddocument.moc"
