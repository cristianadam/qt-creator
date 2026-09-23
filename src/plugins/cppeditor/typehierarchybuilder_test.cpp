// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "typehierarchybuilder_test.h"

#include "cpptoolstestcase.h"
#include "typehierarchybuilder.h"

#include "cppmodelmanager.h"
#include "cpplocatordata.h"
#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"
#endif

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
    QString result = QString(indent, QLatin1Char(' '))
        + hierarchy.klass().name + QLatin1Char('\n');

    const QList<TypeHierarchy> sortedHierarchy = Utils::sorted(hierarchy.hierarchy(),
            [](const TypeHierarchy &h1, const TypeHierarchy &h2) -> bool {
        return h1.klass().name < h2.klass().name;
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
                = TypeHierarchyBuilder::buildDerivedTypeHierarchy(clazz, snapshot,
                                                                  CppModelManager::workingCopy());

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

// What derives from a class, out of files no built-in indexing pass has
// read: the index says which of them reach the one the class is written
// in, the files' own tokens say which of those even write its name, and
// what is left is read.
//
// The snapshot here holds the header alone, which is what a session with no
// pass has once somebody opens it -- every derived class is in a file it has
// no document for, and every one of them used to be dropped.
void TypeHierarchyBuilderTest::testWithNoIndexingPass()
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (!cxxFrontendModelRequested())
        QSKIP("Only that model reads a file the snapshot has no document for");
#else
    QSKIP("Only that model reads a file the snapshot has no document for");
#endif

    CppEditor::Tests::TestCase testCase;
    QVERIFY(testCase.succeededSoFar());

    CppEditor::Tests::TemporaryDir dir;
    QVERIFY(dir.isValid());
    const FilePath base = dir.createFile("thbase.h", "class ThBase {};\n");
    // Two deep, so that the walk is told from one step of it, and one file
    // that reaches the header without deriving from anything in it.
    const FilePath middle = dir.createFile("thmiddle.h", "#include \"thbase.h\"\n"
                                                         "struct ThMiddle : public ThBase {};\n");
    const FilePath leaf = dir.createFile("thleaf.h", "#include \"thmiddle.h\"\n"
                                                     "class ThLeaf : public ThMiddle {};\n");
    const FilePath bystander = dir.createFile("thbystander.h", "#include \"thbase.h\"\n"
                                                               "class ThBystander {};\n");
    QVERIFY(!base.isEmpty() && !middle.isEmpty() && !leaf.isEmpty() && !bystander.isEmpty());

    CppLocatorData * const locatorData = CppModelManager::locatorData();
    QVERIFY(locatorData);
    QVERIFY(CppEditor::Tests::TestCase::parseFiles({base, middle, leaf, bystander}));
    QVERIFY(QTest::qWaitFor([locatorData] {
        return locatorData->cxxFrontendFilesOutstanding() == 0;
    }, 60000));

    const Snapshot parsed = CppModelManager::snapshot();
    const Document::Ptr baseDocument = parsed.document(base);
    QVERIFY(baseDocument);
    Class * const clazz = FindFirstClassInDocument()(baseDocument);
    QVERIFY(clazz);

    // The header on its own, as one open editor would leave it.
    Snapshot asIfOneEditorWereOpen;
    asIfOneEditorWereOpen.insert(baseDocument);

    const TypeHierarchy hierarchy = TypeHierarchyBuilder::buildDerivedTypeHierarchy(
        clazz, asIfOneEditorWereOpen, CppModelManager::workingCopy());
    QCOMPARE(toString(hierarchy), QString::fromLatin1("ThBase\n  ThMiddle\n    ThLeaf\n"));

    // And the icon is the model's own answer, not a default: ThMiddle is
    // written as a struct and ThLeaf as a class.
    QCOMPARE(hierarchy.hierarchy().size(), 1);
    QCOMPARE(hierarchy.hierarchy().first().klass().iconType, Utils::CodeModelIcon::Struct);
    QCOMPARE(hierarchy.hierarchy().first().hierarchy().first().klass().iconType,
             Utils::CodeModelIcon::Class);
}

} // namespace CppEditor::Internal
