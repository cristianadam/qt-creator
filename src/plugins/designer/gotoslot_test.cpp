// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "formeditor.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/systemsettings.h>

#include <cppeditor/builtineditordocumentparser.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cpptoolstestcase.h>
#include <cppeditor/editordocumenthandle.h>

#include <cplusplus/CppDocument.h>
#include <cplusplus/Overview.h>

#include "designersettings.h"

#include <texteditor/texteditor.h>
#include <texteditor/textdocument.h>

#include <QDesignerFormEditorInterface>
#include <QDesignerIntegrationInterface>
#include <QElapsedTimer>
#include <QStringList>
#include <QTest>

using namespace Core;
using namespace CppEditor;
using namespace CPlusPlus;
using namespace Designer;
using namespace Designer::Internal;
using namespace Utils;

namespace Designer::Internal {

class DocumentContainsFunctionDefinition: protected SymbolVisitor
{
public:
    bool operator()(Scope *scope, const QString function)
    {
        if (!scope)
            return false;

        m_referenceFunction = function;
        m_result = false;

        accept(scope);
        return m_result;
    }

protected:
    bool preVisit(Symbol *) { return !m_result; }

    bool visit(Function *symbol)
    {
        const QString function = m_overview.prettyName(symbol->name());
        if (function == m_referenceFunction)
            m_result = true;
        return false;
    }

private:
    bool m_result;
    QString m_referenceFunction;
    Overview m_overview;
};

class DocumentContainsDeclaration: protected SymbolVisitor
{
public:
    bool operator()(Scope *scope, const QString &function)
    {
        if (!scope)
            return false;

        m_referenceFunction = function;
        m_result = false;

        accept(scope);
        return m_result;
    }

protected:
    bool preVisit(Symbol *) { return !m_result; }

    void postVisit(Symbol *symbol)
    {
        if (symbol->asClass())
            m_currentClass.clear();
    }

    bool visit(Class *symbol)
    {
        m_currentClass = m_overview.prettyName(symbol->name());
        return true;
    }

    bool visit(Declaration *symbol)
    {
        QString declaration = m_overview.prettyName(symbol->name());
        if (!m_currentClass.isEmpty())
            declaration = m_currentClass + "::" + declaration;
        if (m_referenceFunction == declaration)
            m_result = true;
        return false;
    }

private:
    bool m_result;
    QString m_referenceFunction;
    QString m_currentClass;
    Overview m_overview;
};

static bool documentContainsFunctionDefinition(const Document::Ptr &document, const QString &function)
{
    return DocumentContainsFunctionDefinition()(document->globalNamespace(), function);
}

static bool documentContainsMemberFunctionDeclaration(const Document::Ptr &document,
                                               const QString &declaration)
{
    return DocumentContainsDeclaration()(document->globalNamespace(), declaration);
}

// What "Go To Slot" is expected to do: which slot it ends up on, what it
// connects the signal to, and which of the two halves of the slot it had to
// write -- a slot the code already has is navigated to rather than written.
struct GoToSlotExpectation
{
    QString slot;                // "Form::onPushButtonClicked"
    QString connectStatement;    // empty: nothing may be connected
    bool writesTheDeclaration = true;
    bool writesTheDefinition = true;
};

// The line "Go To Slot" is expected to leave the cursor on, or 0 where the
// row does not care. Counted from one, as an editor counts lines.
static int currentLine()
{
    const auto editor = qobject_cast<TextEditor::BaseTextEditor *>(EditorManager::currentEditor());
    return editor ? editor->currentLine() : 0;
}

// The line \a text writes \a what on, counted from one, or 0 where it does
// not write it at all.
static int lineWriting(const QString &text, const QString &what)
{
    const int offset = text.indexOf(what);
    return offset == -1 ? 0 : text.left(offset).count('\n') + 1;
}

class GoToSlotTestCase : public CppEditor::Tests::TestCase
{
public:
    GoToSlotTestCase(const FilePaths &files, bool pointerToMember,
                     const GoToSlotExpectation &expected)
    {
        QVERIFY(succeededSoFar());
        QCOMPARE(files.size(), 3);

        const bool savedPmf = designerSettings().generatePointerToMemberConnections.value();
        designerSettings().generatePointerToMemberConnections.setValue(pointerToMember);
        const QScopeGuard restorePmf([savedPmf] {
            designerSettings().generatePointerToMemberConnections.setValue(savedPmf);
        });

        QList<TextEditor::BaseTextEditor *> editors;
        for (const FilePath &file : files) {
            IEditor *editor = EditorManager::openEditor(file);
            TextEditor::BaseTextEditor *e = qobject_cast<TextEditor::BaseTextEditor *>(editor);
            QVERIFY(e);
            closeEditorAtEndOfTestCase(editor);
            editors << e;
        }

        const FilePath cppFile = files.at(0);
        const FilePath hFile = files.at(1);

        QCOMPARE(DocumentModel::openedDocuments().size(), files.size());
        waitForFilesInGlobalSnapshot({cppFile, hFile});

        // Execute "Go To Slot"
        QDesignerIntegrationInterface *integration = designerEditor()->integration();
        QVERIFY(integration);
        integration->emitNavigateToSlot("pushButton", "clicked()", QStringList());

        QCOMPARE(EditorManager::currentDocument()->filePath(), cppFile);
        QCOMPARE(EditorManager::currentDocument()->isModified(), expected.writesTheDefinition);

        // Where the navigation landed, which is all there is to check where it
        // wrote nothing. Read before waiting: a reparse moves no cursor, but
        // nothing below leaves it where it is either.
        const int landedOn = currentLine();

        // Wait for the documents it wrote to. A document it did not touch
        // never reaches a second revision, so waiting for one would be waiting
        // for the timeout.
        FilePaths written;
        if (expected.writesTheDefinition)
            written << cppFile;
        if (expected.writesTheDeclaration)
            written << hFile;
        for (TextEditor::BaseTextEditor *editor : std::as_const(editors)) {
            const FilePath filePath = editor->document()->filePath();
            if (!written.contains(filePath))
                continue;
            QElapsedTimer t;
            t.start();
            if (auto parser = BuiltinEditorDocumentParser::get(filePath)) {
                while (t.elapsed() < 2000) {
                    if (Document::Ptr document = parser->document()) {
                        if (document->editorRevision() == 2)
                            break;
                    }
                    QApplication::processEvents();
                }
            }
        }

        // Compare
        const auto cppDocumentParser = BuiltinEditorDocumentParser::get(cppFile);
        QVERIFY(cppDocumentParser);
        const Document::Ptr cppDocument = cppDocumentParser->document();
        QVERIFY(checkDiagsnosticMessages(cppDocument));

        const auto hDocumentParser = BuiltinEditorDocumentParser::get(hFile);
        QVERIFY(hDocumentParser);
        const Document::Ptr hDocument = hDocumentParser->document();
        QVERIFY(checkDiagsnosticMessages(hDocument));

        const QString cppText = editors.at(0)->textDocument()->plainText();
        const QString hText = editors.at(1)->textDocument()->plainText();

        QVERIFY(documentContainsFunctionDefinition(cppDocument, expected.slot));
        QVERIFY(documentContainsMemberFunctionDeclaration(hDocument, expected.slot));

        // Whichever way round it was written, the slot is declared once and
        // defined once -- a slot that was there already must not be written
        // a second time.
        const QString slotName = expected.slot.mid(expected.slot.lastIndexOf("::") + 2);
        QCOMPARE(hText.count(slotName), 1);
        QCOMPARE(cppText.count(slotName), expected.connectStatement.isEmpty() ? 1 : 2);

        if (expected.connectStatement.isEmpty())
            QVERIFY2(!cppText.contains("connect("), qPrintable(cppText));
        else
            QVERIFY2(cppText.contains(expected.connectStatement), qPrintable(cppText));

        // A slot that was there already is navigated to rather than written,
        // which is the body of its existing definition.
        if (!expected.writesTheDefinition) {
            const int definition = lineWriting(cppText, "void Form::" + slotName + "()");
            QVERIFY(definition > 0);
            QCOMPARE(landedOn, definition + 2);
        }
    }

    static bool checkDiagsnosticMessages(const Document::Ptr &document)
    {
        if (!document)
            return false;

        // Since no project is opened and the ui_*.h is not generated,
        // the following diagnostic messages will be ignored.
        const QStringList ignoreList = QStringList({"ui_form.h: No such file or directory",
                                                    "QWidget: No such file or directory"});
        QList<Document::DiagnosticMessage> cleanedDiagnosticMessages;
        const auto diagnosticMessages = document->diagnosticMessages();
        for (const Document::DiagnosticMessage &message : diagnosticMessages) {
            if (!ignoreList.contains(message.text()))
                cleanedDiagnosticMessages << message;
        }

        return cleanedDiagnosticMessages.isEmpty();
    }
};

class GoToSlotTest final : public QObject
{
    Q_OBJECT

private slots:
    void test_gotoslot();
    void test_gotoslot_data();
};

/// Check: Executes "Go To Slot..." on a QPushButton in a *.ui file and checks if the respective
/// header and source files are correctly updated.
void GoToSlotTest::test_gotoslot()
{
    class SystemSettingsMgr {
    public:
        SystemSettingsMgr()
            : m_saveAfterRefactor(Core::Internal::systemSettings().autoSaveAfterRefactoring.value())
        {
            Core::Internal::systemSettings().autoSaveAfterRefactoring.setValue(false);
        }
        ~SystemSettingsMgr()
        {
            Core::Internal::systemSettings().autoSaveAfterRefactoring.setValue(m_saveAfterRefactor);
        }
    private:
        const bool m_saveAfterRefactor;
    } systemSettingsMgr;

    QFETCH(FilePaths, files);
    QFETCH(bool, pointerToMember);
    QFETCH(GoToSlotExpectation, expected);
    (GoToSlotTestCase(files, pointerToMember, expected));
}

void GoToSlotTest::test_gotoslot_data()
{
    QTest::addColumn<FilePaths>("files");
    QTest::addColumn<bool>("pointerToMember");
    QTest::addColumn<GoToSlotExpectation>("expected");

    // The access prefix of the generated connect() is taken from the setupUi() call.
    const auto connectVia = [](const QString &prefix) {
        return GoToSlotExpectation{"Form::onPushButtonClicked",
                                   QString("connect(%1pushButton, &QPushButton::clicked, "
                                           "this, &Form::onPushButtonClicked);").arg(prefix),
                                   true, true};
    };

    const auto dataDir = [](const QString &subdir) {
        return FilePath::fromUserInput(SRCDIR "/../../../tests/designer/" + subdir);
    };

    FilePath testDataDirWithoutProject = dataDir("gotoslot_withoutProject");
    QVERIFY(testDataDirWithoutProject.exists());
    const FilePaths withoutProjectFiles({testDataDirWithoutProject / "form.cpp",
                                         testDataDirWithoutProject / "form.h",
                                         testDataDirWithoutProject / "form.ui"});
    QTest::newRow("withoutProject") << withoutProjectFiles << true << connectVia("ui->");
    QTest::newRow("withoutProject_legacy")
        << withoutProjectFiles << false
        << GoToSlotExpectation{"Form::on_pushButton_clicked", {}, true, true};

    const FilePath testDataDirMemberUi = dataDir("gotoslot_m_ui");
    QVERIFY(testDataDirMemberUi.exists());
    QTest::newRow("memberPrefixedUi")
        << FilePaths({testDataDirMemberUi / "form.cpp", testDataDirMemberUi / "form.h",
                      testDataDirWithoutProject / "form.ui"}) // reuse
        << true << connectVia("m_ui->");

    // A slot the code already has: nothing is written and the navigation goes
    // to the definition it found. With the declaration there already, no
    // connect() is written either -- whoever wrote the slot wrote that too.
    const FilePath testDataDirExisting = dataDir("gotoslot_existingSlot");
    QVERIFY(testDataDirExisting.exists());
    QTest::newRow("existingSlot")
        << FilePaths({testDataDirExisting / "form.cpp", testDataDirExisting / "form.h",
                      testDataDirWithoutProject / "form.ui"}) // reuse
        << true << GoToSlotExpectation{"Form::onPushButtonClicked", {}, false, false};

    // Declared but never defined: only the definition is written, and into the
    // class the declaration was found in.
    const FilePath testDataDirDeclared = dataDir("gotoslot_declaredNotDefined");
    QVERIFY(testDataDirDeclared.exists());
    QTest::newRow("declaredNotDefined")
        << FilePaths({testDataDirDeclared / "form.cpp", testDataDirDeclared / "form.h",
                      testDataDirWithoutProject / "form.ui"}) // reuse
        << true << GoToSlotExpectation{"Form::onPushButtonClicked", {}, false, true};

    // Finding the right class for inserting definitions/declarations is based on
    // finding a class with a member whose type is the class from the "ui_xxx.h" header.
    // In the following test data the header files contain an extra class referencing
    // the same class name.

    FilePath testDataDir;

    testDataDir = dataDir("gotoslot_insertIntoCorrectClass_pointer");
    QVERIFY(testDataDir.exists());
    QTest::newRow("insertIntoCorrectClass_pointer")
        << FilePaths({testDataDir / "form.cpp", testDataDir / "form.h",
                      testDataDirWithoutProject / "form.ui"}) // reuse
        << true << connectVia("ui->");

    testDataDir = dataDir("gotoslot_insertIntoCorrectClass_non-pointer");
    QVERIFY(testDataDir.exists());
    QTest::newRow("insertIntoCorrectClass_non-pointer")
        << FilePaths({testDataDir / "form.cpp", testDataDir / "form.h",
                        testDataDirWithoutProject / "form.ui"}) // reuse
        << true << connectVia("ui.");

    testDataDir = dataDir("gotoslot_insertIntoCorrectClass_pointer_ns_using");
    QVERIFY(testDataDir.exists());
    QTest::newRow("insertIntoCorrectClass_pointer_ns_using")
        << FilePaths({testDataDir / "form.cpp", testDataDir / "form.h",
                      testDataDir / "form.ui"}) << true << connectVia("ui->");
}

QObject *createGoToSlotTest()
{
    return new GoToSlotTest;
}

} // Designer::Internal

#include "gotoslot_test.moc"
