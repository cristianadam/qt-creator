// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// Compares what the two front ends would show for the same declarations.
//
// Everything in Qt Creator that displays a symbol -- the locator, the outline,
// completion, tooltips, the class view -- asks Overview to turn a type into a
// string. Moving Document onto cxx-frontend means those strings come out of
// the other model, so before any of it moves, the two have to agree on what a
// declaration looks like.
//
// Both sides parse the same source, walk the symbols it declares, and print
// each one. What is compared is the list of strings, which is the whole of
// what a user would see.

#include <cplusplus/AST.h>
#include <cplusplus/Bind.h>
#include <cplusplus/Control.h>
#include <functional>
#include <cplusplus/CxxFrontendOverview.h>
#include <cplusplus/Literals.h>
#include <cplusplus/Overview.h>
#include <cplusplus/Scope.h>
#include <cplusplus/Symbols.h>
#include <cplusplus/TranslationUnit.h>

#include <QObject>
#include <QTest>

//TESTED_COMPONENT=src/libs/cplusplus

using namespace CPlusPlus;

namespace {

// What the built-in front end would show for each symbol the source declares,
// outermost first, each scope's members after it.
QStringList builtIn(const QByteArray &source, const Overview &settings)
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

    QStringList result;
    const std::function<void(Scope *)> walk = [&](Scope *scope) {
        for (int i = 0, count = scope->memberCount(); i < count; ++i) {
            CPlusPlus::Symbol *member = scope->memberAt(i);
            if (!member->name())
                continue;
            result.append(settings.prettyType(member->type(), member->name()));
            if (Scope *inner = member->asScope())
                walk(inner);
        }
    };
    walk(globals);
    return result;
}

QStringList cxxFrontend(const QByteArray &source, const Overview &settings)
{
    CxxFrontendOverview overview;
    overview.settings = settings;

    QStringList result;
    for (const CxxFrontendOverview::Symbol &symbol :
         overview.parse(QString::fromUtf8(source), "<stdin>")) {
        result.append(symbol.type);
    }
    return result;
}

QString firstDifference(const QStringList &expected, const QStringList &actual)
{
    const int count = qMin(expected.size(), actual.size());
    for (int i = 0; i < count; ++i) {
        if (expected.at(i) != actual.at(i)) {
            return QString("symbol %1:\n  built-in:      %2\n  cxx-frontend:  %3")
                .arg(i).arg(expected.at(i), actual.at(i));
        }
    }
    if (expected.size() != actual.size()) {
        return QString("symbol count: built-in %1, cxx-frontend %2\n"
                       "  built-in:     %3\n  cxx-frontend: %4")
            .arg(expected.size()).arg(actual.size())
            .arg(expected.join(", "), actual.join(", "));
    }
    return {};
}

// Why the two are allowed to disagree, or nullptr if they are not. Each entry
// is a piece of TypePrettyPrinter still to bring across, so the list is the
// remaining work rather than a set of excuses. A ratchet both ways, as in the
// other tests here: a listed row that starts agreeing fails too.
const char *knownDivergence(const QString &row)
{
    // Overview puts a space in front of a const that binds to the right,
    // unless BindToRightSpecifier says otherwise: "char * const p". The
    // printer writes "char *const p" and has no say in it.
    if (row == "const pointer")
        return "Overview spaces a right-hand const, which the printer does not";

    // Overview prints an array without its extent, "int a[]". The printer
    // prints "int a[10]", which is what the source said.
    if (row == "array")
        return "Overview leaves out the extent of an array";

    // An unscoped enumerator has the type of its enum. The built-in front end
    // gives it int, the printer gives it the enum, and the printer is right.
    if (row == "enum")
        return "the two disagree about the type of an enumerator";

    // A template parameter prints as its position rather than its name, and
    // the two models disagree about what a template declares.
    if (row == "template")
        return "the printer names a template parameter by its position";

    return nullptr;
}

} // namespace

class tst_cxxfrontendoverview : public QObject
{
    Q_OBJECT

private slots:
    void declarations_data();
    void declarations();

    void starBinding();
    void unsupportedSettings();
};

void tst_cxxfrontendoverview::declarations_data()
{
    QTest::addColumn<QByteArray>("source");

    QTest::newRow("int variable") << QByteArray("int x;");
    QTest::newRow("pointer") << QByteArray("char *p;");
    QTest::newRow("reference") << QByteArray("int &r = *(int*)0;");
    QTest::newRow("const pointer") << QByteArray("const char *const p = 0;");
    QTest::newRow("array") << QByteArray("int a[10];");
    QTest::newRow("function") << QByteArray("void f(int a, char b);");
    QTest::newRow("function returning pointer") << QByteArray("char *f();");
    QTest::newRow("function pointer") << QByteArray("void (*f)(int);");
    QTest::newRow("class") << QByteArray("class C { int m; void f(); };");
    QTest::newRow("struct") << QByteArray("struct S { int a; int b; };");
    QTest::newRow("namespace") << QByteArray("namespace N { int x; }");
    QTest::newRow("nested namespace") << QByteArray("namespace A { namespace B { int x; } }");
    QTest::newRow("enum") << QByteArray("enum E { A, B };");
    QTest::newRow("typedef") << QByteArray("typedef int Integer;");
    QTest::newRow("using alias") << QByteArray("using Integer = int;");
    QTest::newRow("static member") << QByteArray("struct S { static int s; };");
    QTest::newRow("const member function") << QByteArray("struct S { int f() const; };");
    QTest::newRow("template") << QByteArray("template <class T> class C { T t; };");
    QTest::newRow("overloaded operator") << QByteArray("struct S { bool operator==(S) const; };");
}

void tst_cxxfrontendoverview::declarations()
{
    QFETCH(QByteArray, source);

    Overview settings;
    settings.showReturnTypes = true;

    const QString difference = firstDifference(builtIn(source, settings),
                                               cxxFrontend(source, settings));
    if (const char *reason = knownDivergence(QTest::currentDataTag()))
        QEXPECT_FAIL("", reason, Abort);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
}

// The one Overview knob that is honoured, since it is the one that decides
// what a pointer declaration looks like and Qt Creator defaults it away from
// the printer's own choice.
void tst_cxxfrontendoverview::starBinding()
{
    const QByteArray source = "char *p;";

    Overview settings;
    settings.showReturnTypes = true;

    settings.starBindFlags = Overview::BindToIdentifier;
    QCOMPARE(cxxFrontend(source, settings), builtIn(source, settings));

    settings.starBindFlags = Overview::BindToTypeName;
    QCOMPARE(cxxFrontend(source, settings), builtIn(source, settings));
}

// The knobs that are not honoured, asserted so the list cannot go stale: each
// one is a piece of TypePrettyPrinter still to bring across.
void tst_cxxfrontendoverview::unsupportedSettings()
{
    const QStringList unsupported = CxxFrontendOverview::unsupported();
    QVERIFY(!unsupported.isEmpty());
    QVERIFY(unsupported.contains("showArgumentNames"));
    QVERIFY(unsupported.contains("showTemplateParameters"));
}

QTEST_GUILESS_MAIN(tst_cxxfrontendoverview)

#include "tst_cxxfrontendoverview.moc"
