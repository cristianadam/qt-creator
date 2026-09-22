// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "movefunctiondefinition.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "../insertionpointlocator.h"
#include "../symbolfinder.h"
#include "cppquickfix.h"
#include "cppquickfixhelpers.h"

#include <cplusplus/ASTPath.h>
#include <cplusplus/CppRewriter.h>
#include <cplusplus/Overview.h>
#include <projectexplorer/projectmanager.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendDocument.h>

#include <cxx/ast.h>
#include <cxx/names.h>
#include <cxx/translation_unit.h>
#endif

using namespace CPlusPlus;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#include <QTest>
#endif

namespace CppEditor::Internal {
namespace {

static bool isDefaultedOrDeleted(
    const FunctionDefinitionAST *funcAST, const TranslationUnit *tu, Kind tokenKind)
{
    const DeclaratorAST * const declarator = funcAST->declarator;
    return declarator && declarator->initializer && declarator->initializer->asIdExpression()
           && declarator->initializer->asIdExpression()->name
           && declarator->initializer->asIdExpression()->name->asSimpleName()
           && tu->tokenKind(
                  declarator->initializer->asIdExpression()->name->asSimpleName()->identifier_token)
                  == tokenKind;
}

static bool isDefaulted(const FunctionDefinitionAST *funcAST, const TranslationUnit *tu)
{
    return isDefaultedOrDeleted(funcAST, tu, T_DEFAULT);
}

static bool isDeleted(const FunctionDefinitionAST *funcAST, const TranslationUnit *tu)
{
    return isDefaultedOrDeleted(funcAST, tu, T_DELETE);
}

static QString definitionTextForDefaulted(
    const FunctionDefinitionAST *funcAST, const CppRefactoringFilePtr &file, int *startPos = nullptr)
{
    const DeclaratorAST * const declarator = funcAST->declarator;
    const AST *preceding = nullptr;
    if (declarator->post_attribute_list)
        preceding = declarator->post_attribute_list->lastValue();
    else if (declarator->postfix_declarator_list)
        preceding = declarator->postfix_declarator_list->lastValue();
    else
        preceding = declarator->core_declarator;
    int start = file->endOf(preceding);
    if (startPos)
        *startPos = start;
    return file->textOf(start, file->endOf(declarator->initializer)) + ';';
}

static QString definitionSignature(
    const CppQuickFixInterface *assist,
    FunctionDefinitionAST *functionDefinitionAST,
    const CppRefactoringFilePtr &baseFile,
    const CppRefactoringFilePtr &targetFile,
    Scope *scope)
{
    QTC_ASSERT(assist, return QString());
    QTC_ASSERT(functionDefinitionAST, return QString());
    QTC_ASSERT(scope, return QString());
    Function *func = functionDefinitionAST->symbol;
    QTC_ASSERT(func, return QString());

    LookupContext cppContext(targetFile->cppDocument(), assist->snapshot());
    ClassOrNamespace *cppCoN = cppContext.lookupType(scope);
    if (!cppCoN)
        cppCoN = cppContext.globalNamespace();
    SubstitutionEnvironment env;
    env.setContext(assist->context());
    env.switchScope(func->enclosingScope());
    UseMinimalNames q(cppCoN);
    env.enter(&q);
    Control *control = assist->context().bindings()->control().get();
    Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();
    oo.showFunctionSignatures = true;
    oo.showReturnTypes = true;
    oo.showArgumentNames = true;
    oo.showEnclosingTemplate = true;
    oo.showTemplateParameters = true;
    if (!targetFile->cppDocument()->languageFeatures().cxxEnabled)
        oo.language = Language::C;
    oo.trailingReturnType = functionDefinitionAST->declarator
                            && functionDefinitionAST->declarator->postfix_declarator_list
                            && functionDefinitionAST->declarator->postfix_declarator_list->value
                            && functionDefinitionAST->declarator->postfix_declarator_list
                                   ->value->asFunctionDeclarator()
                            && functionDefinitionAST->declarator->postfix_declarator_list
                                   ->value->asFunctionDeclarator()->trailing_return_type;
    const Name *name = func->name();
    if (name && nameIncludesOperatorName(name)) {
        CoreDeclaratorAST *coreDeclarator = functionDefinitionAST->declarator->core_declarator;
        const QString operatorNameText = baseFile->textOf(coreDeclarator);
        oo.includeWhiteSpaceInOperatorName = operatorNameText.contains(QLatin1Char(' '));
    }
    const QString nameText = oo.prettyName(LookupContext::minimalName(func, cppCoN, control));
    const FullySpecifiedType tn = rewriteType(func->type(), &env, control);

    return oo.prettyType(tn, nameText);
}

// One function definition to move: where it is written, and what a
// declaration of it has to say where it is going. What either front end
// fills in, so that the moving itself reads no tree.
struct MovableDefinition
{
    // What the locator needs in order to say where the definition goes.
    DeclarationToDefine declaration;

    // The whole definition, which is what is taken away from where it
    // stands, and where its head stops: from there to bodyEnd is written
    // out again as it stands.
    ChangeSet::Range range;
    int bodyStart = 0;
    int bodyEnd = 0;

    // A definition written "= default" is the one that does not end in a
    // body, so the ";" that closed it has to be written after it.
    bool endsWithSemicolon = false;

    // What a declaration of it says where it is going. The one thing that
    // cannot be settled beforehand: a type is written with as little in
    // front of it as still finds it from there, so it takes the place.
    std::function<QString(const CppQuickFixOperation *op, const InsertionLocation &at,
                          const CppRefactoringFilePtr &toFile)> writeSignature;

    bool isValid() const { return bodyEnd > 0 && writeSignature != nullptr; }
};

// What the built-in front end says of a definition it read.
static MovableDefinition builtinMovableDefinition(const CppQuickFixInterface &interface,
                                                  FunctionDefinitionAST *funcAST)
{
    const CppRefactoringFilePtr fromFile = interface.currentFile();

    MovableDefinition definition;
    definition.declaration = declarationToDefine(funcAST->symbol,
                                                 CppRefactoringChanges(interface.snapshot()));
    definition.range = fromFile->range(funcAST);
    if (isDefaulted(funcAST, fromFile->cppDocument()->translationUnit())) {
        definitionTextForDefaulted(funcAST, fromFile, &definition.bodyStart);
        definition.bodyEnd = fromFile->endOf(funcAST->declarator->initializer);
        definition.endsWithSemicolon = true;
    } else {
        definition.bodyStart = fromFile->endOf(funcAST->declarator);
        definition.bodyEnd = fromFile->endOf(funcAST);
    }

    definition.writeSignature = [funcAST](const CppQuickFixOperation *op,
                                          const InsertionLocation &at,
                                          const CppRefactoringFilePtr &toFile) {
        Scope *scope = toFile->cppDocument()->scopeAt(at.line(), at.column());
        return definitionSignature(op, funcAST, op->currentFile(), toFile, scope);
    };
    return definition;
}

#ifdef QTC_WITH_CXX_FRONTEND

// The definition the cursor is on, as the cxx-frontend model reads it: the
// node, and the class it is written in where it is written in one. The rules
// are the built-in path's, said over this tree.
struct CxxWrittenDefinition
{
    cxx::FunctionDefinitionAST *function = nullptr;
    cxx::ClassSpecifierAST *writtenInClass = nullptr;

    // "void C::f() {}" written at file scope: already outside its class, so
    // the only move left is into the implementation file, and the whole of
    // it goes rather than a declaration staying behind.
    bool isOutsideMemberDefinition = false;
};

std::optional<CxxWrittenDefinition> cxxWrittenDefinitionAt(
    const CxxFrontendDocument &document, int line, int column)
{
    const QList<cxx::AST *> path = cxxAstPathAt(document, line, column);
    if (path.isEmpty())
        return {};

    int index = -1;
    for (int i = path.size() - 1; i >= 0; --i) {
        if (dynamic_cast<cxx::FunctionDefinitionAST *>(path.at(i))) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return {};
    auto * const function = static_cast<cxx::FunctionDefinitionAST *>(path.at(index));

    // A definition written apart from its declaration has to carry the
    // "template<...>" of whatever it is written under, and writing one out
    // is what this model cannot do -- so it is handed back here rather than
    // offered and then answered with half a definition.
    for (cxx::AST * const node : path) {
        if (dynamic_cast<cxx::TemplateDeclarationAST *>(node))
            return {};
    }

    // On the head of the definition, not in its body -- and not where the
    // function is the innermost thing the cursor is in, which is on neither
    // and is what "void a() @ {" is.
    if (index == path.size() - 1)
        return {};
    if (function->functionBody && index + 1 < path.size()
        && path.at(index + 1) == static_cast<cxx::AST *>(function->functionBody)) {
        return {};
    }

    // "= delete" says where a function is not, so there is nothing to put
    // anywhere else.
    if (dynamic_cast<cxx::DeleteFunctionBodyAST *>(function->functionBody))
        return {};

    // A trailing return type, an operator, a declarator a macro wrote part
    // of, an exception specification with an expression in it.
    if (!cxxCanWriteADefinitionOf(document, function->declarator))
        return {};

    // A friend is written in a class without belonging to it, so the name
    // it is declared under is the enclosing namespace's and not the
    // class's. Which name that is takes a lookup the model does not do, and
    // writing the class in front of it would name nothing.
    for (auto *specifier : cxx::ListView{function->declSpecifierList}) {
        if (dynamic_cast<cxx::FriendSpecifierAST *>(specifier))
            return {};
    }

    // Error recovery moves where a construct ends, and this one takes text
    // from one place to another.
    if (cxxAstWasReadWithErrors(document, function))
        return {};

    CxxWrittenDefinition found;
    found.function = function;
    if (index > 0)
        found.writtenInClass = dynamic_cast<cxx::ClassSpecifierAST *>(path.at(index - 1));
    if (!found.writtenInClass) {
        auto * const id = dynamic_cast<cxx::IdDeclaratorAST *>(
            function->declarator ? function->declarator->coreDeclarator : nullptr);
        found.isOutsideMemberDefinition = id && id->nestedNameSpecifier;
    }
    return found;
}

// The same, where the cursor is.
std::optional<CxxWrittenDefinition> cxxWrittenDefinitionUnderCursor(
    const CxxFrontendDocument &document, const CppQuickFixInterface &interface)
{
    // The editor counts from zero and the tree from one.
    const QTextCursor cursor = interface.currentFile()->cursor();
    return cxxWrittenDefinitionAt(document, cursor.blockNumber() + 1,
                                  cursor.positionInBlock() + 1);
}

// What the cxx-frontend model says of the definition it read.
std::optional<MovableDefinition> cxxMovableDefinition(const CppQuickFixInterface &interface,
                                                      const CxxFrontendDocument &document,
                                                      const CxxWrittenDefinition &written)
{
    const CppRefactoringFilePtr fromFile = interface.currentFile();
    cxx::FunctionDefinitionAST * const function = written.function;

    const CxxAstRange whole = cxxAstRangeOf(document, function);
    const CxxAstRange head = cxxAstRangeOf(document, function->declarator);
    const Utils::Text::Position name = cxxNameOfDeclarator(document, function->declarator);
    if (!whole.isValid() || !head.isValid() || name.line <= 0)
        return {};

    const auto positionOf = [&](int line, int column) {
        return fromFile->position(line, column);
    };

    MovableDefinition definition;
    definition.range = {positionOf(whole.startLine, whole.startColumn),
                        positionOf(whole.endLine, whole.endColumn)};
    definition.bodyStart = positionOf(head.endLine, head.endColumn);

    if (auto * const defaulted
        = dynamic_cast<cxx::DefaultFunctionBodyAST *>(function->functionBody)) {
        const CxxAstRange keyword = cxxTokenRangeAt(document, defaulted->defaultLoc);
        if (!keyword.isValid())
            return {};
        definition.bodyEnd = positionOf(keyword.endLine, keyword.endColumn);
        definition.endsWithSemicolon = true;
    } else {
        definition.bodyEnd = positionOf(whole.endLine, whole.endColumn);
    }

    DeclarationToDefine &declaration = definition.declaration;
    declaration.filePath = interface.filePath();
    declaration.line = name.line;
    declaration.column = name.column;

    // What it is written inside, outermost first, which decides the
    // namespace the definition goes into. A class is written into the
    // definition's own name rather than opened around it, so only the
    // namespaces are what a file writing none of them has to be given.
    for (cxx::AST * const node : cxxAstPathAt(document, name.line, name.column)) {
        auto * const enclosing = dynamic_cast<cxx::NamespaceDefinitionAST *>(node);
        if (!enclosing || !enclosing->identifier)
            continue;
        declaration.enclosingNames << QString::fromStdString(enclosing->identifier->name());
        declaration.enclosingNamespaces << declaration.enclosingNames.last();
    }

    // Where a member's definition goes when nothing better is found: just
    // past the ";" of the class it is written in.
    if (written.writtenInClass) {
        const CxxAstRange brace = cxxTokenRangeAt(document, written.writtenInClass->rbraceLoc);
        if (brace.isValid()) {
            declaration.afterItsClass.line = brace.endLine;
            declaration.afterItsClass.column = brace.endColumn + 1; // Skipping the ";"
        }
    }

    definition.writeSignature = [filePath = interface.filePath(), line = name.line,
                                 column = name.column](
                                    const CppQuickFixOperation *,
                                    const InsertionLocation &at,
                                    const CppRefactoringFilePtr &toFile) -> QString {
        const std::optional<QString> head
            = cxxFrontendDefinitionHeadFor(filePath, line, column,
                                           toFile->filePath(), at.line(), at.column());
        return head ? *head : QString();
    };
    return definition;
}
#endif

class MoveFuncDefRefactoringHelper
{
public:
    enum MoveType {
        MoveOutside,
        MoveToCppFile,
        MoveOutsideMemberToCppFile
    };

    MoveFuncDefRefactoringHelper(CppQuickFixOperation *operation, MoveType type,
                                 const FilePath &toFile)
        : m_operation(operation), m_type(type), m_changes(m_operation->snapshot())
    {
        m_fromFile = operation->currentFile();
        m_toFile = (m_type == MoveOutside) ? m_fromFile : m_changes.cppFile(toFile);
    }

    void performMove(const MovableDefinition &definition)
    {
        // Determine file and insert position
        const InsertionLocation l = insertLocationForMethodDefinition(
            definition.declaration, false, NamespaceHandling::Ignore,
            m_changes, m_toFile->filePath());
        const QString prefix = l.prefix();
        const QString suffix = l.suffix();
        const int insertPos = m_toFile->position(l.line(), l.column());

        // construct definition
        const QString inlinePref = inlinePrefix(m_toFile->filePath(), [this] {
            return m_type == MoveOutside;
        });
        QString funcDec = definition.writeSignature(m_operation, l, m_toFile);

        // Nothing is moved rather than a definition written without a head:
        // whichever front end read this was meant to say beforehand that it
        // could not write one, and if it did not, doing nothing is the
        // answer that leaves the code as it was.
        if (funcDec.isEmpty())
            return;

        QString input = funcDec;
        int inlineIndex = 0;
        const QRegularExpression templateRegex("template\\s*<[^>]*>");
        while (input.startsWith("template")) {
            const QRegularExpressionMatch match = templateRegex.match(input);
            if (match.hasMatch()) {
                inlineIndex += match.captured().size() + 1;
                input = input.mid(match.captured().size() + 1);
            }
        }
        funcDec.insert(inlineIndex, inlinePref);

        QString funcDef = prefix + funcDec
                          + m_fromFile->textOf(definition.bodyStart, definition.bodyEnd);
        if (definition.endsWithSemicolon)
            funcDef += QLatin1Char(';');
        funcDef += suffix;

        // insert definition at new position
        m_toFileChangeSet.insert(insertPos, funcDef);
        m_toFile->setOpenEditor(true, insertPos);

        // remove definition from fromFile
        if (m_type == MoveOutsideMemberToCppFile) {
            m_fromFileChangeSet.remove(definition.range);
        } else {
            QString textFuncDecl = m_fromFile->textOf(definition.range.start,
                                                      definition.bodyStart);
            if (textFuncDecl.left(7) == QLatin1String("inline "))
                textFuncDecl = textFuncDecl.mid(7);
            else
                textFuncDecl.replace(" inline ", QLatin1String(" "));
            textFuncDecl = textFuncDecl.trimmed() + QLatin1Char(';');
            m_fromFileChangeSet.replace(definition.range, textFuncDecl);
        }
    }

    void applyChanges()
    {
        m_toFile->apply(m_toFileChangeSet);
        m_fromFile->apply(m_fromFileChangeSet);
    }

private:
    CppQuickFixOperation *m_operation;
    MoveType m_type;
    CppRefactoringChanges m_changes;
    CppRefactoringFilePtr m_fromFile;
    CppRefactoringFilePtr m_toFile;
    ChangeSet m_fromFileChangeSet;
    ChangeSet m_toFileChangeSet;
};

class MoveFuncDefOutsideOp : public CppQuickFixOperation
{
public:
    MoveFuncDefOutsideOp(const CppQuickFixInterface &interface,
                         MoveFuncDefRefactoringHelper::MoveType type,
                         const MovableDefinition &funcDef, const FilePath &cppFilePath)
        : CppQuickFixOperation(interface, 0)
        , m_funcDef(funcDef)
        , m_type(type)
        , m_cppFilePath(cppFilePath)
    {
        if (m_type == MoveFuncDefRefactoringHelper::MoveOutside) {
            setDescription(Tr::tr("Move Definition Outside Class"));
        } else {
            const QString resolved =
                m_cppFilePath.relativeNativePathFromDir(filePath().parentDir());
            setDescription(Tr::tr("Move Definition to %1").arg(resolved));
        }
    }

    void perform() override
    {
        MoveFuncDefRefactoringHelper helper(this, m_type, m_cppFilePath);
        helper.performMove(m_funcDef);
        helper.applyChanges();
    }

private:
    const MovableDefinition m_funcDef;
    MoveFuncDefRefactoringHelper::MoveType m_type;
    const FilePath m_cppFilePath;
};

class MoveAllFuncDefOutsideOp : public CppQuickFixOperation
{
public:
    MoveAllFuncDefOutsideOp(const CppQuickFixInterface &interface,
                            MoveFuncDefRefactoringHelper::MoveType type,
                            const QList<MovableDefinition> &classDef, const FilePath &cppFileName)
        : CppQuickFixOperation(interface, 0)
        , m_type(type)
        , m_classDef(classDef)
        , m_cppFilePath(cppFileName)
    {
        if (m_type == MoveFuncDefRefactoringHelper::MoveOutside) {
            setDescription(Tr::tr("Definitions Outside Class"));
        } else {
            const QString resolved =
                m_cppFilePath.relativeNativePathFromDir(filePath().parentDir());
            setDescription(Tr::tr("Move All Function Definitions to %1").arg(resolved));
        }
    }

    void perform() override
    {
        MoveFuncDefRefactoringHelper helper(this, m_type, m_cppFilePath);
        for (const MovableDefinition &definition : m_classDef)
            helper.performMove(definition);
        helper.applyChanges();
    }

private:
    MoveFuncDefRefactoringHelper::MoveType m_type;
    const QList<MovableDefinition> m_classDef;
    const FilePath m_cppFilePath;
};

// A definition and the declaration it is going to sit at: what is taken
// away, what goes along as it stands, and what is replaced. What either
// front end fills in, so that the moving itself reads no tree.
struct DefinitionAndItsDeclaration
{
    // Where the definition is written and how much of it goes -- the
    // template it is declared under included, since that is part of it.
    FilePath definitionFile;
    ChangeSet::Range definitionRange;

    // Where its head stops and what goes along ends, in that same file.
    int bodyStart = 0;
    int bodyEnd = 0;

    // A definition written "= default" is the one that does not end in a
    // body, so the ";" that closed it has to be written after it.
    bool endsWithSemicolon = false;

    // The declaration it is going to: what is replaced, and what it says
    // without the ";" that closes it.
    FilePath declarationFile;
    ChangeSet::Range declarationRange;
    QString declarationText;

    bool isValid() const
    {
        return bodyEnd > bodyStart && !declarationText.isEmpty()
               && declarationRange.end > declarationRange.start;
    }
};

// Where a definition's head stops and what goes along after it ends, as the
// built-in front end reads them.
static void builtinBodyOf(const CppRefactoringFilePtr &file, FunctionDefinitionAST *funcAST,
                          DefinitionAndItsDeclaration *into)
{
    if (isDefaulted(funcAST, file->cppDocument()->translationUnit())) {
        definitionTextForDefaulted(funcAST, file, &into->bodyStart);
        into->bodyEnd = file->endOf(funcAST->declarator->initializer);
        into->endsWithSemicolon = true;
        return;
    }
    into->bodyStart = file->endOf(funcAST->declarator);
    into->bodyEnd = file->endOf(funcAST->function_body);
}

// The definition written at a place in \a filePath, as the built-in front
// end reads it: how much of it goes -- the template it is declared under
// included -- and where its body begins and ends.
static bool builtinDefinitionAt(const CppQuickFixInterface &interface, const FilePath &filePath,
                                int line, int column, DefinitionAndItsDeclaration *into)
{
    const CppRefactoringChanges refactoring(interface.snapshot());
    const CppRefactoringFilePtr file = refactoring.cppFile(filePath);
    if (!file->isValid())
        return false;

    const QList<AST *> path = ASTPath(file->cppDocument())(line, column);
    for (auto it = std::rbegin(path); it != std::rend(path); ++it) {
        FunctionDefinitionAST * const funcAST = (*it)->asFunctionDefinition();
        if (!funcAST)
            continue;

        AST *whole = funcAST;
        if (const auto outer = std::next(it); outer != std::rend(path)) {
            if (TemplateDeclarationAST * const templated = (*outer)->asTemplateDeclaration())
                whole = templated;
        }
        into->definitionRange = file->range(whole);
        builtinBodyOf(file, funcAST, into);
        return true;
    }
    return false;
}

#ifdef QTC_WITH_CXX_FRONTEND
// Where the definition of the function declared at \a line and \a column is,
// as the cxx-frontend model reads it: which file, how much of it comes over,
// and where its body begins and ends.
//
// Only the definition's side of the move is filled in. Nothing where the
// model has not read the file, where it finds no definition, or where what
// it finds is a declaration after all.
std::optional<DefinitionAndItsDeclaration> cxxDefinitionOf(
    const CppQuickFixInterface &interface, int line, int column)
{
    const std::optional<Link> other = cxxFrontendCounterpart(interface.filePath(), line, column);
    if (!other || !other->hasValidTarget())
        return {};

    // A link counts columns from zero and the model from one.
    const CxxFrontendFunctionDeclaration found
        = cxxFrontendFunctionAt(CppModelManager::workingCopy(),
                                other->targetFilePath, other->target.line,
                                other->target.column + 1)
              .value_or(CxxFrontendFunctionDeclaration());
    if (!found.isValid() || !found.isDefinition || found.bodyEndLine <= 0)
        return {};

    const CppRefactoringChanges refactoring(interface.snapshot());
    const CppRefactoringFilePtr file = refactoring.cppFile(found.filePath);
    if (!file->isValid())
        return {};

    DefinitionAndItsDeclaration move;
    move.definitionFile = found.filePath;
    move.definitionRange = {file->position(found.startLine, found.startColumn),
                            file->position(found.endLine, found.endColumn)};
    move.bodyStart = file->position(found.bodyStartLine, found.bodyStartColumn);
    move.bodyEnd = file->position(found.bodyEndLine, found.bodyEndColumn);
    move.endsWithSemicolon = found.endsWithSemicolon;
    return move;
}
#endif

#ifdef QTC_WITH_CXX_FRONTEND
// Where the declaration of the function defined at \a line and \a column is,
// as the cxx-frontend model reads it: which file, what is replaced, and what
// it says without the ";" that closes it.
//
// Only the declaration's side of the move is filled in. Nothing where the
// model has not read the file, where it finds no declaration -- a
// constructor nobody declared is the case, since what every class declares
// for itself stands where the class is named -- or where what it finds
// defines the function after all.
std::optional<DefinitionAndItsDeclaration> cxxDeclarationOf(
    const CppQuickFixInterface &interface, int line, int column)
{
    const std::optional<Link> other = cxxFrontendCounterpart(interface.filePath(), line, column);
    if (!other || !other->hasValidTarget())
        return {};

    // A link counts columns from zero and the model from one.
    const CxxFrontendFunctionDeclaration found
        = cxxFrontendFunctionAt(CppModelManager::workingCopy(),
                                other->targetFilePath, other->target.line,
                                other->target.column + 1)
              .value_or(CxxFrontendFunctionDeclaration());
    if (!found.isValid() || found.isDefinition)
        return {};

    // A free function's declaration is pushed into the header beside this
    // file and nowhere else, which is what the built-in path allows too.
    if (!found.isWrittenInAClass) {
        bool isHeaderFile = false;
        const FilePath beside = correspondingHeaderOrSource(interface.filePath(), &isHeaderFile);
        if (isHeaderFile || beside != found.filePath)
            return {};
    }

    const CppRefactoringChanges refactoring(interface.snapshot());
    const CppRefactoringFilePtr file = refactoring.cppFile(found.filePath);
    if (!file->isValid())
        return {};

    DefinitionAndItsDeclaration move;
    move.declarationFile = found.filePath;
    move.declarationRange = {file->position(found.startLine, found.startColumn),
                             file->position(found.endLine, found.endColumn)};
    move.declarationText = file->textOf(move.declarationRange.start, move.declarationRange.end);
    if (!move.declarationText.endsWith(QLatin1Char(';')))
        return {};
    move.declarationText.chop(1);

    // A member defined in its class is inline already; a free function put
    // in a header is not, and has to say so.
    if (!found.isWrittenInAClass)
        move.declarationText.prepend(inlinePrefix(found.filePath));
    return move;
}
#endif

class MoveFuncDefToDeclOp : public CppQuickFixOperation
{
public:
    enum Type { Push, Pull };
    MoveFuncDefToDeclOp(const CppQuickFixInterface &interface,
                        const DefinitionAndItsDeclaration &move,
                        Type type)
        : CppQuickFixOperation(interface, 0)
        , m_move(move)
    {
        if (type == Type::Pull) {
            setDescription(Tr::tr("Move Definition Here"));
        } else if (m_move.declarationFile == m_move.definitionFile) {
            setDescription(Tr::tr("Move Definition to Class"));
        } else {
            const QString resolved = m_move.declarationFile.relativeNativePathFromDir(
                m_move.definitionFile.parentDir());
            setDescription(Tr::tr("Move Definition to %1").arg(resolved));
        }
    }

private:
    void perform() override
    {
        CppRefactoringChanges refactoring(snapshot());
        CppRefactoringFilePtr fromFile = refactoring.cppFile(m_move.definitionFile);
        CppRefactoringFilePtr toFile = refactoring.cppFile(m_move.declarationFile);

        QString wholeFunctionText = m_move.declarationText
                                    + fromFile->textOf(m_move.bodyStart, m_move.bodyEnd);
        if (m_move.endsWithSemicolon)
            wholeFunctionText += QLatin1Char(';');

        // Replace declaration with function and delete old definition
        ChangeSet toTarget;
        toTarget.replace(m_move.declarationRange, wholeFunctionText);
        if (m_move.declarationFile == m_move.definitionFile)
            toTarget.remove(m_move.definitionRange);
        toFile->setOpenEditor(true, m_move.declarationRange.start);
        toFile->apply(toTarget);
        if (m_move.declarationFile != m_move.definitionFile)
            fromFile->apply(ChangeSet::makeRemove(m_move.definitionRange));
    }

    const DefinitionAndItsDeclaration m_move;
};

/*!
 Moves the definition of a member function outside the class or moves the definition of a member
 function or a normal function to the implementation file.
 */
class MoveFuncDefOutside : public CppQuickFixFactory
{
public:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        // C, where a struct is named with the word "struct" in front of it
        // and writing the type out would leave that off. A header says
        // nothing about which it is, so the file it goes with is what
        // decides -- the same file the definition is going into.
        const bool isC = ProjectFile::isC(
            ProjectFile::classify(correspondingHeaderOrSource(interface.filePath())));
        if (const CxxFrontendDocument * const document
            = isC ? nullptr : cxxFrontendDocumentFor(interface)) {
            if (const std::optional<CxxWrittenDefinition> written
                = cxxWrittenDefinitionUnderCursor(*document, interface)) {
                if (const std::optional<MovableDefinition> definition
                    = cxxMovableDefinition(interface, *document, *written)) {
                    offer(interface, result, *definition, written->writtenInClass != nullptr,
                          written->isOutsideMemberDefinition);
                    return;
                }
            }
            // Where it declines -- a template, a construct it read with
            // errors -- the built-in path answers as it did before.
        }
#endif
        matchWithTheBuiltinModel(interface, result);
    }

private:
    // Which of the moves are on offer, once a front end has read the
    // definition: out of the class where it is written in one, and into the
    // implementation file where this is a header.
    static void offer(const CppQuickFixInterface &interface, QuickFixOperations &result,
                      const MovableDefinition &definition, bool isWrittenInItsClass,
                      bool isOutsideMemberDefinition)
    {
        bool isHeaderFile = false;
        const FilePath cppFileName = correspondingHeaderOrSource(interface.filePath(),
                                                                 &isHeaderFile);

        if (isHeaderFile && !cppFileName.isEmpty()) {
            const MoveFuncDefRefactoringHelper::MoveType type
                = isOutsideMemberDefinition
                      ? MoveFuncDefRefactoringHelper::MoveOutsideMemberToCppFile
                      : MoveFuncDefRefactoringHelper::MoveToCppFile;
            result << new MoveFuncDefOutsideOp(interface, type, definition, cppFileName);
        }

        if (isWrittenInItsClass) {
            result << new MoveFuncDefOutsideOp(interface,
                                               MoveFuncDefRefactoringHelper::MoveOutside,
                                               definition, FilePath());
        }
    }

    void matchWithTheBuiltinModel(const CppQuickFixInterface &interface,
                                  QuickFixOperations &result)
    {
        const QList<AST *> &path = interface.path();
        SimpleDeclarationAST *classAST = nullptr;
        FunctionDefinitionAST *funcAST = nullptr;
        bool moveOutsideMemberDefinition = false;

        const int pathSize = path.size();
        for (int idx = 1; idx < pathSize; ++idx) {
            if ((funcAST = path.at(idx)->asFunctionDefinition())) {
                // check cursor position
                if (idx != pathSize - 1 // Do not allow "void a() @ {..."
                    && (!funcAST->function_body || !interface.isCursorOn(funcAST->function_body))
                    && !isDeleted(funcAST, interface.currentFile()->cppDocument()->translationUnit())) {
                    if (path.at(idx - 1)->asTranslationUnit()) { // normal function
                        if (idx + 3 < pathSize && path.at(idx + 3)->asQualifiedName()) // Outside member
                            moveOutsideMemberDefinition = true;                        // definition
                        break;
                    }

                    if (idx > 1) {
                        if ((classAST = path.at(idx - 2)->asSimpleDeclaration())) // member function
                            break;
                        if (path.at(idx - 2)->asNamespace())  // normal function in namespace
                            break;
                    }
                    if (idx > 2 && path.at(idx - 1)->asTemplateDeclaration()) {
                        if ((classAST = path.at(idx - 3)->asSimpleDeclaration())) // member template
                            break;
                    }
                }
                funcAST = nullptr;
            }
        }

        if (!funcAST || !funcAST->symbol)
            return;

        const MovableDefinition definition = builtinMovableDefinition(interface, funcAST);
        if (!definition.isValid())
            return;

        offer(interface, result, definition, classAST != nullptr, moveOutsideMemberDefinition);
    }
};

//! Moves all member function definitions outside the class or to the implementation file.
class MoveAllFuncDefOutside : public CppQuickFixFactory
{
public:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        ClassSpecifierAST * const classAST = astForClassOperations(interface);
        if (!classAST)
            return;

        // The definitions the class writes, in the order it writes them. One
        // it did not write itself is not moved, and neither is a deleted one
        // -- "= delete" says where the function is not, and there is nothing
        // to put anywhere else.
        QList<MovableDefinition> definitions;
        for (DeclarationListAST *it = classAST->member_specifier_list; it; it = it->next) {
            FunctionDefinitionAST * const funcAST = it->value->asFunctionDefinition();
            if (!funcAST || !funcAST->symbol || funcAST->symbol->isGenerated())
                continue;
            if (isDeleted(funcAST, interface.currentFile()->cppDocument()->translationUnit()))
                continue;
            const MovableDefinition definition = builtinMovableDefinition(interface, funcAST);
            if (definition.isValid())
                definitions << definition;
        }
        if (definitions.isEmpty())
            return;

#ifdef QTC_WITH_CXX_FRONTEND
        // Where the other model has the file, the heads come from it -- and
        // all of them or none, since a class whose definitions this partly
        // answers for would be moved by two different readings at once.
        if (const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface)) {
            bool onTheModelThroughout = true;
            QList<MovableDefinition> fromTheModel;
            for (const MovableDefinition &definition : std::as_const(definitions)) {
                const std::optional<CxxWrittenDefinition> written
                    = cxxWrittenDefinitionAt(*document, definition.declaration.line,
                                             definition.declaration.column);
                const std::optional<MovableDefinition> onTheModel
                    = written ? cxxMovableDefinition(interface, *document, *written)
                              : std::nullopt;
                if (!onTheModel) {
                    // One of them it cannot write, so none of them are
                    // taken: a class moved by two readings at once is not
                    // something to offer. The built-in answers stand.
                    onTheModelThroughout = false;
                    break;
                }
                fromTheModel << *onTheModel;
            }
            if (onTheModelThroughout)
                definitions = fromTheModel;
        }
#endif

        bool isHeaderFile = false;
        const FilePath cppFileName = correspondingHeaderOrSource(interface.filePath(), &isHeaderFile);
        if (isHeaderFile && !cppFileName.isEmpty()) {
            result << new MoveAllFuncDefOutsideOp(interface,
                                                  MoveFuncDefRefactoringHelper::MoveToCppFile,
                                                  definitions, cppFileName);
        }
        result << new MoveAllFuncDefOutsideOp(interface, MoveFuncDefRefactoringHelper::MoveOutside,
                                              definitions, FilePath());
    }
};

//! Moves the definition of a function to its declaration, with the cursor on the definition.
class MoveFuncDefToDeclPush : public CppQuickFixFactory
{
public:
#ifdef WITH_TESTS
    static QObject *createTest();
#endif

private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const QList<AST *> &path = interface.path();
        AST *completeDefAST = nullptr;
        FunctionDefinitionAST *funcAST = nullptr;

        const int pathSize = path.size();
        for (int idx = 1; idx < pathSize; ++idx) {
            if ((funcAST = path.at(idx)->asFunctionDefinition())) {
                AST *enclosingAST = path.at(idx - 1);
                if (enclosingAST->asClassSpecifier())
                    return;

                // check cursor position
                if (idx != pathSize - 1 // Do not allow "void a() @ {..."
                    && (!funcAST->function_body || !interface.isCursorOn(funcAST->function_body))) {
                    completeDefAST = enclosingAST->asTemplateDeclaration() ? enclosingAST : funcAST;
                    break;
                }
                funcAST = nullptr;
            }
        }

        if (!funcAST || !funcAST->symbol)
            return;

        const CppRefactoringChanges refactoring(interface.snapshot());
        const CppRefactoringFilePtr defFile = interface.currentFile();

        DefinitionAndItsDeclaration move;
        move.definitionFile = interface.filePath();
        move.definitionRange = defFile->range(completeDefAST);
        builtinBodyOf(defFile, funcAST, &move);

        // Determine declaration (file, range, text);
        ChangeSet::Range declRange;
        QString declText;
        FilePath declFilePath;

        Function *func = funcAST->symbol;
        if (Class *matchingClass = isMemberFunction(interface.context(), func)) {
            // Dealing with member functions
            const QualifiedNameId *qName = func->name()->asQualifiedNameId();
            for (Symbol *symbol = matchingClass->find(qName->identifier());
                 symbol; symbol = symbol->next()) {
                Symbol *s = symbol;
                if (func->enclosingScope()->asTemplate()) {
                    if (const Template *templ = s->type()->asTemplateType()) {
                        if (Symbol *decl = templ->declaration()) {
                            if (decl->type()->asFunctionType())
                                s = decl;
                        }
                    }
                }
                if (!s->name()
                    || !qName->identifier()->match(s->identifier())
                    || !s->type()->asFunctionType()
                    || !s->type().match(func->type())
                    || s->asFunction()) {
                    continue;
                }

                declFilePath = matchingClass->filePath();
                const CppRefactoringFilePtr declFile = refactoring.cppFile(declFilePath);
                ASTPath astPath(declFile->cppDocument());
                const QList<AST *> path = astPath(s->line(), s->column());
                for (int idx = path.size() - 1; idx > 0; --idx) {
                    AST *node = path.at(idx);
                    if (SimpleDeclarationAST *simpleDecl = node->asSimpleDeclaration()) {
                        if (simpleDecl->symbols && !simpleDecl->symbols->next) {
                            declRange = declFile->range(simpleDecl);
                            declText = declFile->textOf(simpleDecl);
                            declText.remove(-1, 1); // remove ';' from declaration text
                            break;
                        }
                    }
                }

                if (!declText.isEmpty())
                    break;
            }
        } else if (Namespace *matchingNamespace = isNamespaceFunction(interface.context(), func)) {
            // Dealing with free functions
            bool isHeaderFile = false;
            declFilePath = correspondingHeaderOrSource(interface.filePath(), &isHeaderFile);
            if (isHeaderFile)
                return;

            const CppRefactoringFilePtr declFile = refactoring.cppFile(declFilePath);
            const LookupContext lc(declFile->cppDocument(), interface.snapshot());
            const QList<LookupItem> candidates = lc.lookup(func->name(), matchingNamespace);
            for (const LookupItem &candidate : candidates) {
                if (Symbol *s = candidate.declaration()) {
                    if (s->asDeclaration()) {
                        ASTPath astPath(declFile->cppDocument());
                        const QList<AST *> path = astPath(s->line(), s->column());
                        for (AST *node : path) {
                            if (SimpleDeclarationAST *simpleDecl = node->asSimpleDeclaration()) {
                                declRange = declFile->range(simpleDecl);
                                declText = declFile->textOf(simpleDecl);
                                declText.remove(-1, 1); // remove ';' from declaration text
                                break;
                            }
                        }
                    }
                }

                if (!declText.isEmpty()) {
                    declText.prepend(inlinePrefix(declFilePath));
                    break;
                }
            }
        }

        move.declarationFile = declFilePath;
        move.declarationRange = declRange;
        move.declarationText = declText;

#ifdef QTC_WITH_CXX_FRONTEND
        // Where the declaration is, on the other model where it has read
        // this file. Its counterpart search subsumes the walk above: what
        // that is looking for is the other side of this very function.
        //
        // Where it declines the built-in answer stands. This move carries
        // text rather than writing a declaration out, so there is nothing
        // the other reading could get wrong that this one does not.
        if (cxxFrontendDocumentFor(interface)) {
            // The editor counts from zero and the model from one.
            const QTextCursor cursor = interface.currentFile()->cursor();
            if (const std::optional<DefinitionAndItsDeclaration> onTheModel
                = cxxDeclarationOf(interface, cursor.blockNumber() + 1,
                                   cursor.positionInBlock() + 1)) {
                move.declarationFile = onTheModel->declarationFile;
                move.declarationRange = onTheModel->declarationRange;
                move.declarationText = onTheModel->declarationText;
            }
        }
#endif

        if (move.isValid())
            result << new MoveFuncDefToDeclOp(interface, move, MoveFuncDefToDeclOp::Push);
    }
};

//! Moves the definition of a function to its declaration, with the cursor on the declaration.
class MoveFuncDefToDeclPull : public CppQuickFixFactory
{
public:
#ifdef WITH_TESTS
    static QObject *createTest();
#endif

private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const QList<AST *> &path = interface.path();
        for (auto it = std::rbegin(path); it != std::rend(path); ++it) {
            SimpleDeclarationAST * const simpleDecl = (*it)->asSimpleDeclaration();
            if (!simpleDecl)
                continue;
            const auto prev = std::next(it);
            if (prev != std::rend(path) && (*prev)->asStatement())
                return;
            if (!simpleDecl->symbols || !simpleDecl->symbols->value || simpleDecl->symbols->next)
                return;
            Declaration * const decl = simpleDecl->symbols->value->asDeclaration();
            if (!decl)
                return;
            Function * const funcDecl = decl->type()->asFunctionType();
            if (!funcDecl)
                return;
            if (funcDecl->isSignal() || funcDecl->isPureVirtual() || funcDecl->isFriend())
                return;

            // Is there a definition in the same product?
            Project * const declProject = ProjectManager::projectForFile(funcDecl->filePath());
            const ProjectNode * const declProduct
                = declProject ? declProject->productNodeForFilePath(funcDecl->filePath()) : nullptr;

            SymbolFinder symbolFinder;
            const QList<Function *> defs
                = symbolFinder.findMatchingDefinitions(decl, interface.snapshot(), true, false);
            Function *funcDef = nullptr;
            for (Function * const f : defs) {
                Project * const defProject = ProjectManager::projectForFile(f->filePath());
                if (defProject == declProject) {
                    if (!declProduct) {
                        funcDef = f;
                        break;
                    }
                    const ProjectNode * const defProduct
                        = defProject ? defProject->productNodeForFilePath(f->filePath()) : nullptr;
                    if (!defProduct || declProduct == defProduct)
                        funcDef = f;
                    break;
                }
            }
            if (!funcDef)
                return;

            DefinitionAndItsDeclaration move;
            move.declarationFile = decl->filePath();
            move.declarationRange = interface.currentFile()->range(simpleDecl);
            move.declarationText = interface.currentFile()->textOf(simpleDecl);
            move.declarationText.chop(1); // semicolon
            move.declarationText.prepend(inlinePrefix(interface.filePath(), [funcDecl] {
                return !funcDecl->enclosingScope()->asClass();
            }));

            // Where the definition being pulled over is, which is read here
            // rather than while performing: what the operation is handed is
            // the places, and finding them is the reading.
            bool read = false;
#ifdef QTC_WITH_CXX_FRONTEND
            // Which file it is in and how much of it comes over, on the
            // other model where it has read this one. Which declaration
            // this is stays the built-in walk's answer: the fix declines a
            // signal, and a Qt keyword is one of the things that front end
            // does not have.
            if (const std::optional<DefinitionAndItsDeclaration> onTheModel
                = cxxDefinitionOf(interface, decl->line(), decl->column())) {
                move.definitionFile = onTheModel->definitionFile;
                move.definitionRange = onTheModel->definitionRange;
                move.bodyStart = onTheModel->bodyStart;
                move.bodyEnd = onTheModel->bodyEnd;
                move.endsWithSemicolon = onTheModel->endsWithSemicolon;
                read = true;
            }
#endif
            if (!read) {
                move.definitionFile = funcDef->filePath();
                if (!builtinDefinitionAt(interface, move.definitionFile, funcDef->line(),
                                         funcDef->column(), &move)) {
                    return;
                }
            }

            if (move.isValid())
                result << new MoveFuncDefToDeclOp(interface, move, MoveFuncDefToDeclOp::Pull);
            return;
        }
    }
};

#ifdef WITH_TESTS
using namespace Tests;

class MoveFuncDefToDeclTest : public QObject
{
    Q_OBJECT

private slots:
    void test_data()
    {
        QTest::addColumn<QByteArrayList>("headers");
        QTest::addColumn<QByteArrayList>("sources");

        QByteArray originalHeader;
        QByteArray expectedHeader;
        QByteArray originalSource;
        QByteArray expectedSource;

        originalHeader =
            "class Foo {\n"
            "    inline int @number() const;\n"
            "};\n";
        expectedHeader =
            "class Foo {\n"
            "    inline int number() const {return 5;}\n"
            "};\n";
        originalSource =
            "#include \"file.h\"\n"
            "\n"
            "int Foo::num@ber() const {return 5;}\n";
        expectedSource =
            "#include \"file.h\"\n"
            "\n\n";
        QTest::newRow("member function, two files") << QByteArrayList{originalHeader, expectedHeader}
                                                    << QByteArrayList{originalSource, expectedSource};

        originalSource =
            "class Foo {\n"
            "  inline int @number() const;\n"
            "};\n"
            "\n"
            "int Foo::num@ber() const\n"
            "{\n"
            "    return 5;\n"
            "}\n";

        expectedSource =
            "class Foo {\n"
            "    inline int number() const\n"
            "    {\n"
            "        return 5;\n"
            "    }\n"
            "};\n\n\n";
        QTest::newRow("member function, one file") << QByteArrayList()
                                                   << QByteArrayList{originalSource, expectedSource};

        originalHeader =
            "namespace MyNs {\n"
            "class Foo {\n"
            "  inline int @number() const;\n"
            "};\n"
            "}\n";
        expectedHeader =
            "namespace MyNs {\n"
            "class Foo {\n"
            "    inline int number() const\n"
            "    {\n"
            "        return 5;\n"
            "    }\n"
            "};\n"
            "}\n";
        originalSource =
            "#include \"file.h\"\n"
            "\n"
            "int MyNs::Foo::num@ber() const\n"
            "{\n"
            "    return 5;\n"
            "}\n";
        expectedSource = "#include \"file.h\"\n\n\n";
        QTest::newRow("member function, two files, namespace")
            << QByteArrayList{originalHeader, expectedHeader}
            << QByteArrayList{originalSource, expectedSource};

        originalHeader =
            "namespace MyNs {\n"
            "class Foo {\n"
            "  inline int numbe@r() const;\n"
            "};\n"
            "}\n";
        expectedHeader =
            "namespace MyNs {\n"
            "class Foo {\n"
            "    inline int number() const\n"
            "    {\n"
            "        return 5;\n"
            "    }\n"
            "};\n"
            "}\n";
        originalSource =
            "#include \"file.h\"\n"
            "using namespace MyNs;\n"
            "\n"
            "int Foo::num@ber() const\n"
            "{\n"
            "    return 5;\n"
            "}\n";
        expectedSource =
            "#include \"file.h\"\n"
            "using namespace MyNs;\n"
            "\n\n";
        QTest::newRow("member function, two files, namespace with using-directive")
            << QByteArrayList{originalHeader, expectedHeader}
            << QByteArrayList{originalSource, expectedSource};

        originalSource =
            "namespace MyNs {\n"
            "class Foo {\n"
            "  inline int @number() const;\n"
            "};\n"
            "\n"
            "int Foo::numb@er() const\n"
            "{\n"
            "    return 5;\n"
            "}"
            "\n}\n";
        expectedSource =
            "namespace MyNs {\n"
            "class Foo {\n"
            "    inline int number() const\n"
            "    {\n"
            "        return 5;\n"
            "    }\n"
            "};\n\n\n}\n";

        QTest::newRow("member function, one file, namespace")
            << QByteArrayList() << QByteArrayList{originalSource, expectedSource};

        originalHeader = "int nu@mber() const;\n";
        expectedHeader =
            "inline int number() const\n"
            "{\n"
            "    return 5;\n"
            "}\n";
        originalSource =
            "#include \"file.h\"\n"
            "\n"
            "\n"
            "int numb@er() const\n"
            "{\n"
            "    return 5;\n"
            "}\n";
        expectedSource = "#include \"file.h\"\n\n\n\n";
        QTest::newRow("free function") << QByteArrayList{originalHeader, expectedHeader}
                                       << QByteArrayList{originalSource, expectedSource};

        originalHeader =
            "namespace MyNamespace {\n"
            "int n@umber() const;\n"
            "}\n";
        expectedHeader =
            "namespace MyNamespace {\n"
            "inline int number() const\n"
            "{\n"
            "    return 5;\n"
            "}\n"
            "}\n";
        originalSource =
            "#include \"file.h\"\n"
            "\n"
            "int MyNamespace::nu@mber() const\n"
            "{\n"
            "    return 5;\n"
            "}\n";
        expectedSource =
            "#include \"file.h\"\n"
            "\n\n";
        QTest::newRow("free function, namespace") << QByteArrayList{originalHeader, expectedHeader}
                                                  << QByteArrayList{originalSource, expectedSource};

        originalHeader =
            "class Foo {\n"
            "public:\n"
            "    Fo@o();\n"
            "private:\n"
            "    int a;\n"
            "    float b;\n"
            "};\n";
        expectedHeader =
            "class Foo {\n"
            "public:\n"
            "    Foo() : a(42), b(3.141) {}\n"
            "private:\n"
            "    int a;\n"
            "    float b;\n"
            "};\n";
        originalSource =
            "#include \"file.h\"\n"
            "\n"
            "Foo::F@oo() : a(42), b(3.141) {}"
            ;
        expectedSource ="#include \"file.h\"\n\n";
        QTest::newRow("constructor") << QByteArrayList{originalHeader, expectedHeader}
                                     << QByteArrayList{originalSource, expectedSource};

        originalSource =
            "struct Foo\n"
            "{\n"
            "    void f@oo();\n"
            "} bar;\n"
            "void Foo::fo@o()\n"
            "{\n"
            "    return;\n"
            "}";
        expectedSource =
            "struct Foo\n"
            "{\n"
            "    void foo()\n"
            "    {\n"
            "        return;\n"
            "    }\n"
            "} bar;\n";
        QTest::newRow("QTCREATORBUG-10303") << QByteArrayList()
                                            << QByteArrayList{originalSource, expectedSource};

        originalSource =
            "struct Base {\n"
            "    virtual int foo() = 0;\n"
            "};\n"
            "struct Derived : Base {\n"
            "    int @foo() override;\n"
            "};\n"
            "\n"
            "int Derived::fo@o()\n"
            "{\n"
            "    return 5;\n"
            "}\n";
        expectedSource =
            "struct Base {\n"
            "    virtual int foo() = 0;\n"
            "};\n"
            "struct Derived : Base {\n"
            "    int foo() override\n"
            "    {\n"
            "        return 5;\n"
            "    }\n"
            "};\n\n\n";
        QTest::newRow("overridden virtual") << QByteArrayList()
                                            << QByteArrayList{originalSource, expectedSource};

        originalSource =
            "template<class T>\n"
            "class Foo { void @func(); };\n"
            "\n"
            "template<class T>\n"
            "void Foo<T>::fu@nc() {}\n";
        expectedSource =
            "template<class T>\n"
            "class Foo { void fu@nc() {} };\n\n\n";
        QTest::newRow("class template") << QByteArrayList()
                                        << QByteArrayList{originalSource, expectedSource};

        originalSource =
            "class Foo\n"
            "{\n"
            "    template<class T>\n"
            "    void @func();\n"
            "};\n"
            "\n"
            "template<class T>\n"
            "void Foo::fu@nc() {}\n";
        expectedSource =
            "class Foo\n"
            "{\n"
            "    template<class T>\n"
            "    void func() {}\n"
            "};\n\n\n";
        QTest::newRow("function template") << QByteArrayList()
                                           << QByteArrayList{originalSource, expectedSource};

        originalSource =
            "class Foo\n"
            "{\n"
            "    @Foo();\n"
            "};\n"
            "\n"
            "Foo::Fo@o() = default;\n";
        expectedSource =
            "class Foo\n"
            "{\n"
            "    Foo() = default;\n"
            "};\n\n\n";
        QTest::newRow("defaulted constructor") << QByteArrayList()
                                               << QByteArrayList{originalSource, expectedSource};

        originalSource =
            "class Foo\n"
            "{\n"
            "    ~@Foo();\n"
            "};\n"
            "\n"
            "Foo::~Fo@o() = default;\n";
        expectedSource =
            "class Foo\n"
            "{\n"
            "    ~Foo() = default;\n"
            "};\n\n\n";
        QTest::newRow("defaulted destructor") << QByteArrayList()
                                              << QByteArrayList{originalSource, expectedSource};

        originalSource =
            "class Foo\n"
            "{\n"
            "    Foo& @operator=(const Foo &);\n"
            "};\n"
            "\n"
            "Foo& Foo::@operator=(const Foo &) = default;\n";
        expectedSource =
            "class Foo\n"
            "{\n"
            "    Foo& operator=(const Foo &) = default;\n"
            "};\n\n\n";
        QTest::newRow("defaulted operator") << QByteArrayList()
                                            << QByteArrayList{originalSource, expectedSource};
    }

    void test()
    {
        QFETCH(QByteArrayList, headers);
        QFETCH(QByteArrayList, sources);

        QVERIFY(headers.isEmpty() || headers.size() == 2);
        QVERIFY(sources.size() == 2);

        QByteArray &declDoc = !headers.empty() ? headers.first() : sources.first();
        const int declCursorPos = declDoc.indexOf('@');
        QVERIFY(declCursorPos != -1);
        const int defCursorPos = sources.first().lastIndexOf('@');
        QVERIFY(defCursorPos != -1);
        QVERIFY(declCursorPos != defCursorPos);

        declDoc.remove(declCursorPos, 1);
        QList<TestDocumentPtr> testDocuments;
        if (!headers.isEmpty())
            testDocuments << CppTestDocument::create("file.h", headers.first(), headers.last());
        testDocuments << CppTestDocument::create("file.cpp", sources.first(), sources.last());

        if (QString::fromLatin1(QTest::currentDataTag()) == QLatin1String("defaulted operator")) {
            qDebug() << "FIXME: MoveFuncDefToDeclPush does not support operators";
        } else {
            MoveFuncDefToDeclPush pushFactory;
            QuickFixOperationTest(testDocuments, &pushFactory);
        }

        declDoc.insert(declCursorPos, '@');
        sources.first().remove(defCursorPos, 1);
        testDocuments.clear();
        if (!headers.isEmpty())
            testDocuments << CppTestDocument::create("file.h", headers.first(), headers.last());
        testDocuments << CppTestDocument::create("file.cpp", sources.first(), sources.last());

        MoveFuncDefToDeclPull pullFactory;
        QuickFixOperationTest(testDocuments, &pullFactory);
    }

    void testMacroUses()
    {
        QByteArray original =
            "#define CONST const\n"
            "#define VOLATILE volatile\n"
            "class Foo\n"
            "{\n"
            "    int func(int a, int b) CONST VOLATILE;\n"
            "};\n"
            "\n"
            "\n"
            "int Foo::fu@nc(int a, int b) CONST VOLATILE"
            "{\n"
            "    return 42;\n"
            "}\n";
        QByteArray expected =
            "#define CONST const\n"
            "#define VOLATILE volatile\n"
            "class Foo\n"
            "{\n"
            "    int func(int a, int b) CONST VOLATILE\n"
            "    {\n"
            "        return 42;\n"
            "    }\n"
            "};\n\n\n\n";

        MoveFuncDefToDeclPush factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory,
                              ProjectExplorer::HeaderPaths(), 0, "QTCREATORBUG-12314");
    }
};

QObject *MoveFuncDefToDeclPush::createTest()
{
    return new MoveFuncDefToDeclTest;
}

QObject *MoveFuncDefToDeclPull::createTest()
{
    return new QObject; // The test for the push factory handled both cases.
}

class MoveFuncDefOutsideTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};

class MoveAllFuncDefOutsideTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};

#endif // WITH_TESTS

} // namespace

void registerMoveFunctionDefinitionQuickfixes()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(MoveFuncDefOutside);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(MoveAllFuncDefOutside);
    CppQuickFixFactory::registerFactory<MoveFuncDefToDeclPush>();
    CppQuickFixFactory::registerFactory<MoveFuncDefToDeclPull>();
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <movefunctiondefinition.moc>
#endif
