// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "typehierarchybuilder_test.h"

#include "cpptoolstestcase.h"
#include "typehierarchybuilder.h"

#include <cplusplus/Overview.h>
#include <cplusplus/SymbolVisitor.h>
#include <utils/algorithm.h>

#include <QDir>
#include <QTest>

using namespace CPlusPlus;
using namespace Utils;

using CppEditor::Internal::Tests::CppTestDocument;

Q_DECLARE_METATYPE(QList<CppTestDocument>)

namespace CppEditor::Internal {
namespace {

QString toString(const TypeHierarchy &hierarchy, int indent = 0)
{
    Symbol *symbol = hierarchy.symbol();
    QString result = QString(indent, QLatin1Char(' '))
        + Overview().prettyName(symbol->name()) + QLatin1Char('\n');

    Overview oo;
    const QList<TypeHierarchy> sortedHierarchy = Utils::sorted(hierarchy.hierarchy(),
            [&oo](const TypeHierarchy &h1, const TypeHierarchy &h2) -> bool {
        return oo.prettyName(h1.symbol()->name()) < oo.prettyName(h2.symbol()->name());
    });
    for (const TypeHierarchy &childHierarchy : std::as_const(sortedHierarchy))
        result += toString(childHierarchy, indent + 2);
    return result;
}

class FindFirstClassInDocument: private SymbolVisitor
{
public:
    FindFirstClassInDocument() = default;

    Class *operator()(const Document::Ptr &document)
    {
        accept(document->globalNamespace());
        return m_clazz;
    }

private:
    bool preVisit(Symbol *symbol) override
    {
        if (m_clazz)
            return false;

        if (Class *c = symbol->asClass()) {
            m_clazz = c;
            return false;
        }

        return true;
    }

private:
    Class *m_clazz = nullptr;
};

class TypeHierarchyBuilderTestCase : public CppEditor::Tests::TestCase
{
public:
    TypeHierarchyBuilderTestCase(const QList<CppTestDocument> &documents,
                                 const QString &expectedHierarchy)
    {
        QVERIFY(succeededSoFar());

        CppEditor::Tests::TemporaryDir temporaryDir;
        QVERIFY(temporaryDir.isValid());

        QList<CppTestDocument> documents_ = documents;

        // Write files
        QSet<FilePath> filePaths;
        for (auto &document : documents_) {
            document.setBaseDirectory(temporaryDir.path());
            QVERIFY(document.writeToDisk());
            filePaths << document.filePath();
        }

        // Parse files
        QVERIFY(parseFiles(filePaths));
        const Snapshot snapshot = globalSnapshot();

        // Get class for which to generate the hierarchy
        const Document::Ptr firstDocument = snapshot.document(documents_.first().filePath());
        QVERIFY(firstDocument);
        QVERIFY(firstDocument->diagnosticMessages().isEmpty());
        Class *clazz = FindFirstClassInDocument()(firstDocument);
        QVERIFY(clazz);

        // Generate and compare hierarchies
        const TypeHierarchy hierarchy
                = TypeHierarchyBuilder::buildDerivedTypeHierarchy(clazz, snapshot);

        const QString actualHierarchy = toString(hierarchy);
//        Uncomment for updating/generating reference data:
//        qDebug() << actualHierarchy;
        QCOMPARE(actualHierarchy, expectedHierarchy);
    }
};

} // anonymous namespace

void TypeHierarchyBuilderTest::test_data()
{
    QTest::addColumn<QList<CppTestDocument> >("documents");
    QTest::addColumn<QString>("expectedHierarchy");

    QTest::newRow("basic-single-document")
        << (QList<CppTestDocument>()
            << CppTestDocument("a.h",
                            "class A {};\n"
                            "class B : public A {};\n"
                            "class C1 : public B {};\n"
                            "class C2 : public B {};\n"
                            "class D : public C1 {};\n"))
        << QString::fromLatin1(
            "A\n"
            "  B\n"
            "    C1\n"
            "      D\n"
            "    C2\n" );

    QTest::newRow("basic-multiple-documents")
        << (QList<CppTestDocument>()
            << CppTestDocument("a.h",
                            "class A {};")
            << CppTestDocument("b.h",
                            "#include \"a.h\"\n"
                            "class B : public A {};")
            << CppTestDocument("c1.h",
                            "#include \"b.h\"\n"
                            "class C1 : public B {};")
            << CppTestDocument("c2.h",
                            "#include \"b.h\"\n"
                            "class C2 : public B {};")
            << CppTestDocument("d.h",
                            "#include \"c1.h\"\n"
                            "class D : public C1 {};"))
        << QString::fromLatin1(
            "A\n"
            "  B\n"
            "    C1\n"
            "      D\n"
            "    C2\n"
            );

    // A base named through an alias is the class it stands for, which is
    // what followTypedef is for and what nothing pinned before.
    QTest::newRow("through-a-typedef")
        << (QList<CppTestDocument>()
            << CppTestDocument("a.h",
                            "class A {};\n"
                            "typedef A AA;\n"
                            "using AAA = AA;\n"
                            "class B : public AA {};\n"
                            "class C : public AAA {};\n"))
        << QString::fromLatin1("A\n  B\n  C\n");

    QTest::newRow("in-a-namespace")
        << (QList<CppTestDocument>()
            << CppTestDocument("a.h",
                            "namespace N {\n"
                            "class A {};\n"
                            "class B : public A {};\n"
                            "}\n"
                            "class C : public N::A {};\n"))
        << QString::fromLatin1("A\n  B\n  C\n");

    // Two classes of one name are two classes: what derives from the one in
    // the namespace does not derive from the one outside it.
    QTest::newRow("a-name-that-means-something-else-elsewhere")
        << (QList<CppTestDocument>()
            << CppTestDocument("a.h",
                            "class A {};\n"
                            "namespace N {\n"
                            "class A {};\n"
                            "class B : public A {};\n"
                            "}\n"
                            "class C : public A {};\n"))
        << QString::fromLatin1("A\n  C\n");

    // A class with a base of its own besides the one being asked about,
    // which is what the cache of other bases is about.
    QTest::newRow("a-class-with-another-base-as-well")
        << (QList<CppTestDocument>()
            << CppTestDocument("a.h",
                            "class A {};\n"
                            "class Other {};\n"
                            "class B : public Other, public A {};\n"))
        << QString::fromLatin1("A\n  B\n");

    // How a class inherits says nothing about whether it derives.
    QTest::newRow("private-inheritance")
        << (QList<CppTestDocument>()
            << CppTestDocument("a.h",
                            "class A {};\n"
                            "class B : private A {};\n"
                            "class C : protected A {};\n"))
        << QString::fromLatin1("A\n  B\n  C\n");

    QTest::newRow("a-template-deriving-from-it")
        << (QList<CppTestDocument>()
            << CppTestDocument("a.h",
                            "class A {};\n"
                            "template<typename T> class B : public A {};\n"))
        << QString::fromLatin1("A\n  B\n");
}

void TypeHierarchyBuilderTest::test()
{
    QFETCH(QList<CppTestDocument>, documents);
    QFETCH(QString, expectedHierarchy);

    TypeHierarchyBuilderTestCase(documents, expectedHierarchy);
}

} // namespace CppEditor::Internal
