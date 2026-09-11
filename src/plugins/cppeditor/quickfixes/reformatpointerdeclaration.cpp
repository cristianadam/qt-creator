// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "reformatpointerdeclaration.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cpppointerdeclarationformatter.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#include <cplusplus/Overview.h>

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#include <QTest>
#endif

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

class ReformatPointerDeclarationOp: public CppQuickFixOperation
{
public:
    ReformatPointerDeclarationOp(const CppQuickFixInterface &interface, const ChangeSet change)
        : CppQuickFixOperation(interface)
        , m_change(change)
    {
        QString description;
        if (m_change.operationList().size() == 1) {
            description = Tr::tr(
                              "Reformat to \"%1\"").arg(m_change.operationList().constFirst().text());
        } else { // > 1
            description = Tr::tr("Reformat Pointers or References");
        }
        setDescription(description);
    }

    void perform() override
    {
        currentFile()->apply(m_change);
    }

private:
    ChangeSet m_change;
};

/*!
  Reformats a pointer, reference or rvalue reference type/declaration.

  Works also with selections (except when the cursor is not on any AST).

  Activates on: simple declarations, parameters and return types of function
                declarations and definitions, control flow statements (if,
                while, for, foreach) with declarations.
*/
class ReformatPointerDeclaration : public CppQuickFixFactory
{
#ifdef WITH_TESTS
public:
    static QObject *createTest();
#endif

private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        CppRefactoringFilePtr file = interface.currentFile();

        Overview overview = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        overview.showArgumentNames = true;
        overview.showReturnTypes = true;

        const QTextCursor cursor = file->cursor();
        PointerDeclarationFormatter formatter(file, overview,
                                              PointerDeclarationFormatter::RespectCursor);

        // A selection asks for everything in it, which the cursor rule
        // sorts out; without one, the construct the cursor is in.
        //
        // This will not work always as expected since this function is only called if
        // interface->path() is not empty. If the user selects the whole document via
        // ctrl-a and there is an empty line in the end, then the cursor is not on
        // any AST and therefore no quick fix will be triggered.
        const ChangeSet change
            = cursor.hasSelection()
                  ? formatter.formatEverything()
                  : formatter.formatAt(Utils::Text::Position::fromPositionInDocument(
                        interface.textDocument(), interface.position()));
        if (!change.isEmpty())
            result << new ReformatPointerDeclarationOp(interface, change);
    }
};

#ifdef WITH_TESTS
using namespace Tests;

class ReformatPointerDeclarationTest : public QObject
{
    Q_OBJECT

private slots:
    void test()
    {
        // Check: Just a basic test since the main functionality is tested in
        // cpppointerdeclarationformatter_test.cpp
        ReformatPointerDeclaration factory;
        QuickFixOperationTest(
            singleDocument(QByteArray("char@*s;"), QByteArray("char *s;")), &factory);
    }
};

QObject *ReformatPointerDeclaration::createTest() { return new ReformatPointerDeclarationTest; }

#endif // WITH_TESTS
}

void registerReformatPointerDeclarationQuickfix()
{
    CppQuickFixFactory::registerFactory<ReformatPointerDeclaration>();
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <reformatpointerdeclaration.moc>
#endif
