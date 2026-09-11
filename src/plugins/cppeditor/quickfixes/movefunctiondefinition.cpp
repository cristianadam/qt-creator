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

class MoveFuncDefToDeclOp : public CppQuickFixOperation
{
public:
    enum Type { Push, Pull };
    MoveFuncDefToDeclOp(const CppQuickFixInterface &interface,
                        const FilePath &fromFilePath, const FilePath &toFilePath,
                        FunctionDefinitionAST *funcAst, Function *func, const QString &declText,
                        const ChangeSet::Range &fromRange,
                        const ChangeSet::Range &toRange,
                        Type type)
        : CppQuickFixOperation(interface, 0)
        , m_fromFilePath(fromFilePath)
        , m_toFilePath(toFilePath)
        , m_funcAST(funcAst)
        , m_func(func)
        , m_declarationText(declText)
        , m_fromRange(fromRange)
        , m_toRange(toRange)
    {
        if (type == Type::Pull) {
            setDescription(Tr::tr("Move Definition Here"));
        } else if (m_toFilePath == m_fromFilePath) {
            setDescription(Tr::tr("Move Definition to Class"));
        } else {
            const QString resolved =
                m_toFilePath.relativeNativePathFromDir(m_fromFilePath.parentDir());
            setDescription(Tr::tr("Move Definition to %1").arg(resolved));
        }
    }

private:
    void perform() override
    {
        CppRefactoringChanges refactoring(snapshot());
        CppRefactoringFilePtr fromFile = refactoring.cppFile(m_fromFilePath);
        CppRefactoringFilePtr toFile = refactoring.cppFile(m_toFilePath);

        ensureFuncDefAstAndRange(*fromFile);
        if (!m_funcAST)
            return;

        QString wholeFunctionText = m_declarationText;
        if (isDefaulted(m_funcAST, fromFile->cppDocument()->translationUnit())) {
            wholeFunctionText += definitionTextForDefaulted(m_funcAST, fromFile);
        } else {
            wholeFunctionText += fromFile->textOf(fromFile->endOf(m_funcAST->declarator),
                                                  fromFile->endOf(m_funcAST->function_body));
        }

        // Replace declaration with function and delete old definition
        ChangeSet toTarget;
        toTarget.replace(m_toRange, wholeFunctionText);
        if (m_toFilePath == m_fromFilePath)
            toTarget.remove(m_fromRange);
        toFile->setOpenEditor(true, m_toRange.start);
        toFile->apply(toTarget);
        if (m_toFilePath != m_fromFilePath)
            fromFile->apply(ChangeSet::makeRemove(m_fromRange));
    }

    void ensureFuncDefAstAndRange(CppRefactoringFile &defFile)
    {
        if (m_funcAST) {
            QTC_CHECK(m_fromRange.end > m_fromRange.start);
            return;
        }
        QTC_ASSERT(m_func, return);
        const QList<AST *> astPath = ASTPath(defFile.cppDocument())(m_func->line(),
                                                                    m_func->column());
        if (astPath.isEmpty())
            return;
        for (auto it = std::rbegin(astPath); it != std::rend(astPath); ++it) {
            m_funcAST = (*it)->asFunctionDefinition();
            if (!m_funcAST)
                continue;
            AST *astForRange = m_funcAST;
            const auto prev = std::next(it);
            if (prev != std::rend(astPath)) {
                if (const auto templAst = (*prev)->asTemplateDeclaration())
                    astForRange = templAst;
            }
            m_fromRange = defFile.range(astForRange);
            return;
        }
    }

    const FilePath m_fromFilePath;
    const FilePath m_toFilePath;
    FunctionDefinitionAST *m_funcAST;
    Function *m_func;
    const QString m_declarationText;
    ChangeSet::Range m_fromRange;
    const ChangeSet::Range m_toRange;
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

        bool isHeaderFile = false;
        const FilePath cppFileName = correspondingHeaderOrSource(interface.filePath(), &isHeaderFile);

        if (isHeaderFile && !cppFileName.isEmpty()) {
            const MoveFuncDefRefactoringHelper::MoveType type = moveOutsideMemberDefinition
                                                                    ? MoveFuncDefRefactoringHelper::MoveOutsideMemberToCppFile
                                                                    : MoveFuncDefRefactoringHelper::MoveToCppFile;
            result << new MoveFuncDefOutsideOp(interface, type, definition, cppFileName);
        }

        if (classAST)
            result << new MoveFuncDefOutsideOp(interface, MoveFuncDefRefactoringHelper::MoveOutside,
                                               definition, FilePath());

        return;
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
        const ChangeSet::Range defRange = defFile->range(completeDefAST);

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

        if (!declFilePath.isEmpty() && !declText.isEmpty())
            result << new MoveFuncDefToDeclOp(interface,
                                              interface.filePath(),
                                              declFilePath,
                                              funcAST, func, declText,
                                              defRange, declRange, MoveFuncDefToDeclOp::Push);
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

            QString declText = interface.currentFile()->textOf(simpleDecl);
            declText.chop(1); // semicolon
            declText.prepend(inlinePrefix(interface.filePath(), [funcDecl] {
                return !funcDecl->enclosingScope()->asClass();
            }));
            result << new MoveFuncDefToDeclOp(interface, funcDef->filePath(), decl->filePath(), nullptr,
                                              funcDef, declText, {},
                                              interface.currentFile()->range(simpleDecl),
                                              MoveFuncDefToDeclOp::Pull);
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
