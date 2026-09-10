// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "splitsimpledeclaration.h"

#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

static bool checkDeclarationForSplit(SimpleDeclarationAST *declaration)
{
    if (!declaration->semicolon_token)
        return false;

    if (!declaration->decl_specifier_list)
        return false;

    for (SpecifierListAST *it = declaration->decl_specifier_list; it; it = it->next) {
        SpecifierAST *specifier = it->value;
        if (specifier->asEnumSpecifier() || specifier->asClassSpecifier())
            return false;
    }

    return declaration->declarator_list && declaration->declarator_list->next;
}

class SplitSimpleDeclarationOp : public CppQuickFixOperation
{
public:
    // The stretches of text to rewrite, rather than the declaration they were
    // read off: which node a declarator is depends on which front end read
    // the file, and what stands in the text does not. \a declarators are the
    // names as they are written, in that order; the specifiers are copied in
    // front of every one but the first.
    SplitSimpleDeclarationOp(const CppQuickFixInterface &interface, int priority,
                             int declSpecifiersStart, int declSpecifiersEnd,
                             int insertPos, const QList<ChangeSet::Range> &declarators)
        : CppQuickFixOperation(interface, priority)
        , m_declSpecifiersStart(declSpecifiersStart)
        , m_declSpecifiersEnd(declSpecifiersEnd)
        , m_insertPos(insertPos)
        , m_declarators(declarators)
    {
        setDescription(Tr::tr("Split Declaration"));
    }

    void perform() override
    {
        ChangeSet changes;

        int prevDeclEnd = m_declarators.first().end;

        for (const ChangeSet::Range &declarator : m_declarators.mid(1)) {
            changes.insert(m_insertPos, QLatin1String("\n"));
            changes.copy(m_declSpecifiersStart, m_declSpecifiersEnd, m_insertPos);
            changes.insert(m_insertPos, QLatin1String(" "));
            changes.move(declarator, m_insertPos);
            changes.insert(m_insertPos, QLatin1String(";"));

            changes.remove(prevDeclEnd, declarator.start);

            prevDeclEnd = declarator.end;
        }

        currentFile()->apply(changes);
    }

private:
    const int m_declSpecifiersStart;
    const int m_declSpecifiersEnd;
    const int m_insertPos;
    const QList<ChangeSet::Range> m_declarators;
};

/*!
  Rewrite
    int *a, b;

  As
    int *a;
    int b;

  Activates on: the type or the variable names.
*/
class SplitSimpleDeclaration : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        // The other model first, where it has read this file; it declines when
        // it has not, and the built-in path below then answers as it always
        // did. See cxxfrontendmodel.h.
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        CoreDeclaratorAST *core_declarator = nullptr;
        const QList<AST *> &path = interface.path();
        CppRefactoringFilePtr file = interface.currentFile();
        const int cursorPosition = file->cursor().selectionStart();

        for (int index = path.size() - 1; index != -1; --index) {
            AST *node = path.at(index);

            if (CoreDeclaratorAST *coreDecl = node->asCoreDeclarator()) {
                core_declarator = coreDecl;
            } else if (SimpleDeclarationAST *simpleDecl = node->asSimpleDeclaration()) {
                if (checkDeclarationForSplit(simpleDecl)) {
                    SimpleDeclarationAST *declaration = simpleDecl;

                    const int startOfDeclSpecifier = file->startOf(declaration->decl_specifier_list->firstToken());
                    const int endOfDeclSpecifier = file->endOf(declaration->decl_specifier_list->lastToken() - 1);

                    QList<ChangeSet::Range> declarators;
                    for (DeclaratorListAST *it = declaration->declarator_list; it; it = it->next)
                        declarators << file->range(it->value);

                    const auto add = [&] {
                        result << new SplitSimpleDeclarationOp(
                            interface, index, startOfDeclSpecifier, endOfDeclSpecifier,
                            file->endOf(declaration->semicolon_token), declarators);
                    };

                    if (cursorPosition >= startOfDeclSpecifier && cursorPosition <= endOfDeclSpecifier) {
                        // the AST node under cursor is a specifier.
                        add();
                        return;
                    }

                    if (core_declarator && interface.isCursorOn(core_declarator)) {
                        // got a core-declarator under the text cursor.
                        add();
                        return;
                    }
                }

                return;
            }
        }
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same operation, worked out on the cxx-frontend model's syntax tree:
    // the declaration the cursor is in, its specifiers, and the names written
    // after them.
    //
    // False where the model has not read this file -- it is off unless asked
    // for -- and where the cursor is in no declaration at all, which is the
    // answer either model gives there.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CppRefactoringFilePtr file = interface.currentFile();
        const std::shared_ptr<const CxxFrontendSnapshot> model
            = cxxFrontendModel(file->filePath());
        if (!model)
            return false;
        const CxxFrontendDocument * const document
            = model->document(file->filePath().toFSPathString());
        if (!document)
            return false;

        // The editor counts from zero and the tree from one.
        const QTextCursor cursor = file->cursor();
        const QList<cxx::AST *> path
            = cxxAstPathAt(*document, cursor.blockNumber() + 1, cursor.positionInBlock() + 1);

        // The declaration the cursor is in, and the outermost name inside it
        // that still holds the cursor -- the two places this fix is offered
        // from.
        cxx::SimpleDeclarationAST *declaration = nullptr;
        bool onADeclaredName = false;
        int index = path.size() - 1;
        for (; index >= 0; --index) {
            if (auto * const simple = dynamic_cast<cxx::SimpleDeclarationAST *>(path.at(index))) {
                declaration = simple;
                break;
            }
            if (dynamic_cast<cxx::CoreDeclaratorAST *>(path.at(index)))
                onADeclaredName = true;
        }
        if (!declaration)
            return false;

        // What the built-in checkDeclarationForSplit() asks: a finished
        // declaration of more than one name, whose specifiers can be written
        // again in front of each -- which a class or enum definition's cannot.
        if (!declaration->semicolonLoc || !declaration->declSpecifierList
            || !declaration->initDeclaratorList || !declaration->initDeclaratorList->next) {
            return true;
        }
        for (auto *it = declaration->declSpecifierList; it; it = it->next) {
            if (dynamic_cast<cxx::ClassSpecifierAST *>(it->value)
                || dynamic_cast<cxx::EnumSpecifierAST *>(it->value)) {
                return true;
            }
        }

        // The specifiers as one stretch of text, from the first to the last.
        cxx::SpecifierAST *lastSpecifier = nullptr;
        for (auto *it = declaration->declSpecifierList; it; it = it->next)
            lastSpecifier = it->value;
        const CxxAstRange firstSpecifierRange
            = cxxAstRangeOf(*document, declaration->declSpecifierList->value);
        const CxxAstRange lastSpecifierRange = cxxAstRangeOf(*document, lastSpecifier);
        if (!firstSpecifierRange.isValid() || !lastSpecifierRange.isValid())
            return true;
        const int startOfDeclSpecifier
            = file->position(firstSpecifierRange.startLine, firstSpecifierRange.startColumn);
        const int endOfDeclSpecifier
            = file->position(lastSpecifierRange.endLine, lastSpecifierRange.endColumn);

        // A name and whatever it is given, since that travels with it: the
        // built-in declarator holds the initializer, cxx's holds it beside.
        QList<ChangeSet::Range> declarators;
        for (auto *it = declaration->initDeclaratorList; it; it = it->next) {
            const CxxAstRange range = cxxAstRangeOf(*document, it->value);
            if (!range.isValid())
                return true;
            declarators << ChangeSet::Range(file->position(range.startLine, range.startColumn),
                                            file->position(range.endLine, range.endColumn));
        }

        // The declaration ends with its semicolon, so its own end is where
        // the further declarations go.
        const CxxAstRange declarationRange = cxxAstRangeOf(*document, declaration);
        if (!declarationRange.isValid())
            return true;
        const int insertPos
            = file->position(declarationRange.endLine, declarationRange.endColumn);

        const int cursorPosition = cursor.selectionStart();
        if ((cursorPosition >= startOfDeclSpecifier && cursorPosition <= endOfDeclSpecifier)
            || onADeclaredName) {
            result << new SplitSimpleDeclarationOp(interface, index, startOfDeclSpecifier,
                                                   endOfDeclSpecifier, insertPos, declarators);
        }

        // Answered, even where the cursor is on neither the type nor a name:
        // the built-in path would find the same nothing.
        return true;
    }
#endif
};

#ifdef WITH_TESTS
class SplitSimpleDeclarationTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerSplitSimpleDeclarationQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(SplitSimpleDeclaration);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <splitsimpledeclaration.moc>
#endif
