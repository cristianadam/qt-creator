// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "synchronizememberfunctionorder.h"

#include "../cppeditortr.h"
#include "../cppeditorwidget.h"
#include "../cppmodelmanager.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"
#include "cppquickfixhelpers.h"

#include <coreplugin/editormanager/editormanager.h>
#include <cplusplus/ASTPath.h>
#include <cplusplus/declarationcomments.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/Overview.h>

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

#include <QList>
#include <QHash>

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#include <projectexplorer/kitmanager.h>
#include <texteditor/textdocument.h>
#include <QTest>
#endif

#include <memory>

using namespace Core;
using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

// A place in a file, both counted from one, as the code models count.
class Place
{
public:
    int line = 0;
    int column = 0;

    bool isValid() const { return line > 0; }
};

// A member function a class declares without defining there, in the order
// the class declares them -- which is the order this fix puts the
// definitions into.
class MemberFunctionDeclaration
{
public:
    QString name;      // written out in full, the scopes included
    QString shortName; // its own name, which its documentation is found by
    int parameterCount = 0;
    Place at;          // where its name stands
};

// Where one of them is defined, and how much of the file that definition
// takes up: the template it is written under is part of it, the
// documentation above it is not -- that is a question about comments, and
// commentsForDeclaration() answers it.
class DefinitionOf
{
public:
    int declaration = 0; // an index into the declarations, i.e. their order
    FilePath filePath;
    Place at;
    Place begins;
    Place ends;

    bool isValid() const { return at.isValid() && begins.isValid() && ends.isValid(); }

    // Which declaration it belongs to is what tells two of them apart: one
    // definition per declaration, and that is what is being ordered.
    bool operator==(const DefinitionOf &other) const
    {
        return declaration == other.declaration;
    }
};

// Finds where each declaration is defined and hands the lot over. A
// function because the built-in front end answers by following each
// declaration in turn, which it does on the event loop, while another model
// reads them off the files it has -- and the rest of this fix, the sorting
// and the moving, is the same either way.
using Definitions = QList<DefinitionOf>;
using FindTheDefinitions = std::function<void(std::function<void(const Definitions &)>)>;

// Puts the definitions of \a declarations into the order the declarations
// are in, one file at a time.
void reorder(const QList<MemberFunctionDeclaration> &declarations,
             const Definitions &definitions)
{
    CppRefactoringChanges factory{CppModelManager::snapshot()};

    QHash<FilePath, Definitions> byFile;
    for (const DefinitionOf &definition : definitions) {
        if (definition.isValid())
            byFile[definition.filePath].append(definition);
    }

    for (auto it = byFile.cbegin(); it != byFile.cend(); ++it) {
        const CppRefactoringFilePtr file = factory.cppFile(it.key());
        if (!file->isValid())
            continue;

        // Where each definition begins: the comments above it belong to it
        // and move with it.
        const auto rangeOf = [&](const DefinitionOf &definition) {
            const int begins = file->position(definition.begins.line,
                                              definition.begins.column);
            const int ends = file->position(definition.ends.line, definition.ends.column);
            const QList<CommentRange> comments = commentsForDeclaration(
                declarations.at(definition.declaration).shortName,
                {definition.at.line, definition.at.column - 1},
                *file->document(), file->cppDocument());
            return ChangeSet::Range{comments.isEmpty() ? begins : comments.first().start, ends};
        };

        // As they stand in the file, and as the class declares them.
        const Definitions actualOrder = Utils::sorted(
            it.value(), [](const DefinitionOf &a, const DefinitionOf &b) {
                if (a.begins.line != b.begins.line)
                    return a.begins.line < b.begins.line;
                return a.begins.column < b.begins.column;
            });
        const Definitions expectedOrder = Utils::sorted(
            actualOrder, [](const DefinitionOf &a, const DefinitionOf &b) {
                return a.declaration < b.declaration;
            });
        if (expectedOrder == actualOrder)
            continue;

        ChangeSet changes;
        for (int i = 0; i < actualOrder.size(); ++i) {
            int expectedPos = -1;
            for (int j = 0; j < expectedOrder.size(); ++j) {
                if (expectedOrder[j].declaration == actualOrder[i].declaration) {
                    expectedPos = j;
                    break;
                }
            }
            if (expectedPos == i)
                continue;
            const ChangeSet::Range actualRange = rangeOf(actualOrder[i]);
            const ChangeSet::Range expectedRange = rangeOf(actualOrder[expectedPos]);
            if (actualRange.end > actualRange.start && expectedRange.end > expectedRange.start)
                changes.move(actualRange, expectedRange.start);
        }
        QTC_ASSERT(!changes.hadErrors(), continue);
        file->setChangeSet(changes);
        file->apply();
    }
}

class SynchronizeMemberFunctionOrderOp : public CppQuickFixOperation
{
public:
    SynchronizeMemberFunctionOrderOp(const CppQuickFixInterface &interface,
                                     const QList<MemberFunctionDeclaration> &declarations,
                                     const FindTheDefinitions &findTheDefinitions)
        : CppQuickFixOperation(interface)
        , m_declarations(declarations)
        , m_findTheDefinitions(findTheDefinitions)
    {
        setDescription(
            Tr::tr("Re-order Member Function Definitions According to Declaration Order"));
    }

private:
    void perform() override
    {
        m_findTheDefinitions([declarations = m_declarations](const Definitions &definitions) {
            reorder(declarations, definitions);
        });
    }

    const QList<MemberFunctionDeclaration> m_declarations;
    const FindTheDefinitions m_findTheDefinitions;
};

// The definitions, as the built-in front end finds them: each declaration
// is followed to wherever it leads, and the answers arrive one at a time.
FindTheDefinitions builtinFindTheDefinitions(
    const CppQuickFixInterface &interface, const QList<MemberFunctionDeclaration> &declarations)
{
    const CppRefactoringFilePtr file = interface.currentFile();
    return [file, declarations](std::function<void(const Definitions &)> whenDone) {
        // The definitions found so far, and how many answers are still
        // outstanding: the last one to arrive hands the lot over.
        class State
        {
        public:
            Definitions definitions;
            int remaining = 0;
        };
        const auto state = std::make_shared<State>();

        for (int i = 0; i < declarations.size(); ++i) {
            const MemberFunctionDeclaration &declaration = declarations.at(i);
            const int declPos = file->position(declaration.at.line, declaration.at.column);
            QTextCursor cursor(const_cast<QTextDocument *>(file->document()));
            cursor.setPosition(declPos);
            const CursorInEditor cursorInEditor(
                cursor,
                file->filePath(),
                qobject_cast<CppEditorWidget *>(file->editor()),
                file->editor()->textDocument(),
                file->cppDocument());

            const auto callback = [i, declPos, file, state, whenDone](const Link &link) {
                class FinishedChecker
                {
                public:
                    FinishedChecker(const std::shared_ptr<State> &state,
                                    const std::function<void(const Definitions &)> &whenDone)
                        : m_state(state), m_whenDone(whenDone)
                    {}
                    ~FinishedChecker()
                    {
                        if (--m_state->remaining == 0)
                            m_whenDone(m_state->definitions);
                    }
                private:
                    const std::shared_ptr<State> &m_state;
                    const std::function<void(const Definitions &)> &m_whenDone;
                } finishedChecker(state, whenDone);

                if (!link.hasValidTarget())
                    return;
                if (file->filePath() == link.targetFilePath
                    && link.target.toPositionInDocument(file->document()) == declPos) {
                    return;
                }

                // How much of the file the definition takes up, which is
                // the outermost declaration written around its name.
                CppRefactoringChanges factory{CppModelManager::snapshot()};
                const CppRefactoringFilePtr target = factory.cppFile(link.targetFilePath);
                if (!target->isValid())
                    return;
                const QList<AST *> astPath = ASTPath(target->cppDocument())(
                    link.target.line, link.target.column + 1);
                for (auto it = astPath.rbegin(); it != astPath.rend(); ++it) {
                    if (!(*it)->asFunctionDefinition())
                        continue;
                    AST *ast = *it;
                    for (auto next = std::next(it);
                         next != astPath.rend() && (*next)->asTemplateDeclaration();
                         ++next) {
                        ast = *next;
                    }
                    DefinitionOf definition;
                    definition.declaration = i;
                    definition.filePath = link.targetFilePath;
                    definition.at = {link.target.line, link.target.column + 1};
                    target->lineAndColumn(target->startOf(ast), &definition.begins.line,
                                          &definition.begins.column);
                    target->lineAndColumn(target->endOf(ast), &definition.ends.line,
                                          &definition.ends.column);
                    state->definitions.append(definition);
                    break;
                }
            };

            ++state->remaining;

            // Force queued execution, as the built-in editor can run the callback synchronously.
            const auto followSymbol = [cursorInEditor, callback] {
                NonInteractiveFollowSymbolMarker niMarker;
                CppModelManager::followSymbol(
                    cursorInEditor, callback, true, false, FollowSymbolMode::Exact);
            };
            QMetaObject::invokeMethod(CppModelManager::instance(), followSymbol,
                                      Qt::QueuedConnection);
        }
        if (state->remaining == 0)
            whenDone({});
    };
}

// The member functions of the class at the cursor, as the built-in front end
// reads them.
QList<MemberFunctionDeclaration> builtinMemberFunctionsAt(
    const CppQuickFixInterface &interface)
{
    ClassSpecifierAST * const classAst = astForClassOperations(interface);
    if (!classAst || !classAst->symbol)
        return {};

    const CppRefactoringFilePtr file = interface.currentFile();
    const TranslationUnit * const tu = file->cppDocument()->translationUnit();
    QList<MemberFunctionDeclaration> declarations;
    for (int i = 0; i < classAst->symbol->memberCount(); ++i) {
        Symbol *member = classAst->symbol->memberAt(i);

        // Skip macros
        if (tu->tokenAt(member->sourceLocation()).expanded())
            continue;

        if (const auto templ = member->asTemplate())
            member = templ->declaration();
        if (!member->type()->asFunctionType() || member->asFunction())
            continue;

        MemberFunctionDeclaration declaration;
        declaration.name = Overview().prettyName(
            LookupContext::fullyQualifiedName(member));
        const QStringList parts = declaration.name.split("::", Qt::SkipEmptyParts);
        declaration.shortName = parts.isEmpty() ? QString() : parts.last();
        if (const auto type = member->type()->asFunctionType())
            declaration.parameterCount = type->argumentCount();
        int line = 0;
        int column = 0;
        tu->getTokenPosition(member->sourceLocation(), &line, &column);
        declaration.at = {line, column};
        declarations.append(declaration);
    }
    return declarations;
}

//! Ensures relative order of member function implementations is the same as declaration order.
class SynchronizeMemberFunctionOrder : public CppQuickFixFactory
{
#ifdef WITH_TESTS
public:
    static QObject *createTest();
#endif

private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const QList<MemberFunctionDeclaration> declarations
            = builtinMemberFunctionsAt(interface);
        if (declarations.isEmpty())
            return;
        result << new SynchronizeMemberFunctionOrderOp(
            interface, declarations, builtinFindTheDefinitions(interface, declarations));
    }
};

#ifdef WITH_TESTS
using namespace Tests;

class SynchronizeMemberFunctionOrderTest : public QObject
{
    Q_OBJECT

private slots:
    void test_data()
    {
        QTest::addColumn<QString>("projectName");
        QTest::addColumn<bool>("expectChanges");

        QTest::newRow("no out-of-line definitions") << "no-out-of-line" << false;
        QTest::newRow("already sorted") << "already-sorted" << false;
        QTest::newRow("different impl locations") << "different-locations" << true;
        QTest::newRow("templates") << "templates" << true;
    }

    void test()
    {
        QFETCH(QString, projectName);
        QFETCH(bool, expectChanges);
        using namespace CppEditor::Tests;
        using namespace ProjectExplorer;
        using namespace TextEditor;

        // Set up project.
        Kit * const kit  = Utils::findOr(KitManager::kits(), nullptr, [](const Kit *k) {
            return k->isValid() && !k->hasWarning() && k->value("QtSupport.QtInformation").isValid();
        });
        if (!kit)
            QSKIP("The test requires at least one valid kit with a valid Qt");
        const auto projectDir = std::make_unique<TemporaryCopiedDir>(
            ":/cppeditor/testcases/reorder-member-impls/" + projectName);
        SourceFilesRefreshGuard refreshGuard;
        ProjectOpenerAndCloser projectMgr;
        QVERIFY(projectMgr.open(projectDir->absolutePath(projectName + ".pro"), kit));
        QVERIFY(refreshGuard.wait());

        // Open header file and locate class.
        const auto headerFilePath = projectDir->absolutePath("header.h");
        QVERIFY2(headerFilePath.exists(), qPrintable(headerFilePath.toUserOutput()));
        const auto editor = qobject_cast<BaseTextEditor *>(EditorManager::openEditor(headerFilePath));
        QVERIFY(editor);
        const auto doc = qobject_cast<TextEditor::TextDocument *>(editor->document());
        QVERIFY(doc);
        QTextCursor classCursor = doc->document()->find("struct S");
        QVERIFY(!classCursor.isNull());
        editor->setCursorPosition(classCursor.position());
        const auto editorWidget = qobject_cast<CppEditorWidget *>(editor->editorWidget());
        QVERIFY(editorWidget);
        QVERIFY(TestCase::waitForRehighlightedSemanticDocument(editorWidget));

        // Query factory.
        SynchronizeMemberFunctionOrder factory;
        CppQuickFixInterface quickFixInterface(editorWidget, ExplicitlyInvoked);
        QuickFixOperations operations;
        factory.match(quickFixInterface, operations);
        operations.first()->perform();
        if (expectChanges)
            QVERIFY(waitForSignalOrTimeout(doc, &IDocument::saved, 30000));
        QTest::qWait(1000);

        // Compare all files.
        const FileFilter filter({"*_expected"}, DirFilterFlag::Files);
        const FilePaths expectedDocuments = projectDir->filePath().dirEntries(filter);
        QVERIFY(!expectedDocuments.isEmpty());
        for (const FilePath &expected : expectedDocuments) {
            static const QString suffix = "_expected";
            const FilePath actual = expected.parentDir()
                                        .pathAppended(expected.fileName().chopped(suffix.size()));
            QVERIFY(actual.exists());
            const auto actualContents = actual.fileContents();
            QVERIFY(actualContents);
            const auto expectedContents = expected.fileContents();
            const QByteArrayList actualLines = actualContents->split('\n');
            const QByteArrayList expectedLines = expectedContents->split('\n');
            if (actualLines.size() != expectedLines.size()) {
                qDebug().noquote().nospace() << "---\n" << *expectedContents << "EOF";
                qDebug().noquote().nospace() << "+++\n" << *actualContents << "EOF";
            }
            QCOMPARE(actualLines.size(), expectedLines.size());
            for (int i = 0; i < actualLines.size(); ++i) {
                const QByteArray actualLine = actualLines.at(i);
                const QByteArray expectedLine = expectedLines.at(i);
                if (actualLine != expectedLine)
                    qDebug() << "Unexpected content in line" << (i + 1) << "of file"
                             << actual.fileName();
                QCOMPARE(actualLine, expectedLine);
            }
        }
    }
};

QObject *SynchronizeMemberFunctionOrder::createTest()
{
    return new SynchronizeMemberFunctionOrderTest;
}

#endif
} // namespace

void registerSynchronizeMemberFunctionOrderQuickfix()
{
    CppQuickFixFactory::registerFactory<SynchronizeMemberFunctionOrder>();
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <synchronizememberfunctionorder.moc>
#endif
