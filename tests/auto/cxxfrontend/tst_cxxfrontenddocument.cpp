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
    void signatureOfADeclarationInAHeader();

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

    QVERIFY(!document.signatureAt({{}, 1, 5}, {{}, 2, 6}).isValid());
    QVERIFY(!document.signatureAt({{}, 2, 6}, {{}, 1, 5}).isValid());
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

QTEST_GUILESS_MAIN(tst_cxxfrontenddocument)

#include "tst_cxxfrontenddocument.moc"
