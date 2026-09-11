// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppfunctiondecldeflink.h"

#include "cppcodestylesettings.h"
#include "cppeditorconstants.h"
#include "cppeditortr.h"
#include "cppeditorwidget.h"
#include "cpplocalsymbols.h"
#include "cppmodelmanager.h"
#include "cppworkingcopy.h"
#include "cpptoolsreuse.h"
#include "quickfixes/cppquickfixassistant.h"
#include "symbolfinder.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"
#endif

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>

#include <cplusplus/ASTPath.h>
#include <cplusplus/CppRewriter.h>
#include <cplusplus/declarationcomments.h>
#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>

#include <texteditor/refactoroverlay.h>
#include <texteditor/texteditorconstants.h>

#include <utils/async.h>
#include <utils/proxyaction.h>
#include <utils/qtcassert.h>
#include <utils/textutils.h>
#include <utils/tooltip/tooltip.h>

#include <QRegularExpression>
#include <QVarLengthArray>

using namespace CPlusPlus;
using namespace QtTaskTree;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {

FunctionDeclDefLinkFinder::FunctionDeclDefLinkFinder(QObject *parent)
    : QObject(parent)
{}

QTextCursor FunctionDeclDefLinkFinder::scannedSelection() const
{
    return m_scannedSelection;
}

// parent is either a FunctionDefinitionAST or a SimpleDeclarationAST
// line and column are 1-based
static bool findDeclOrDef(const Document::Ptr &doc, int line, int column,
                          DeclarationAST **parent, DeclaratorAST **decl,
                          FunctionDeclaratorAST **funcDecl)
{
    QList<AST *> path = ASTPath(doc)(line, column);

    // for function definitions, simply scan for FunctionDefinitionAST not preceded
    //    by CompoundStatement/CtorInitializer
    // for function declarations, look for SimpleDeclarations with a single Declarator
    //    with a FunctionDeclarator postfix
    *decl = nullptr;
    for (int i = path.size() - 1; i > 0; --i) {
        AST *ast = path.at(i);
        if (ast->asCompoundStatement() || ast->asCtorInitializer())
            break;
        if (FunctionDefinitionAST *funcDef = ast->asFunctionDefinition()) {
            *parent = funcDef;
            *decl = funcDef->declarator;
            break;
        }
        if (SimpleDeclarationAST *simpleDecl = ast->asSimpleDeclaration()) {
            *parent = simpleDecl;
            if (!simpleDecl->declarator_list || !simpleDecl->declarator_list->value)
                break;
            *decl = simpleDecl->declarator_list->value;
            break;
        }
    }
    if (!*parent || !*decl)
        return false;
    if (!(*decl)->postfix_declarator_list || !(*decl)->postfix_declarator_list->value)
        return false;
    *funcDecl = (*decl)->postfix_declarator_list->value->asFunctionDeclarator();
    return *funcDecl;
}

static DeclaratorIdAST *getDeclaratorId(DeclaratorAST *declarator)
{
    if (!declarator || !declarator->core_declarator)
        return nullptr;
    if (DeclaratorIdAST *id = declarator->core_declarator->asDeclaratorId())
        return id;
    if (NestedDeclaratorAST *nested = declarator->core_declarator->asNestedDeclarator())
        return getDeclaratorId(nested->declarator);
    return nullptr;
}

// does consider foo(void) to have one argument
static int declaredParameterCount(Function *function)
{
    int argc = function->argumentCount();
    if (argc == 0 && function->memberCount() > 0 && function->memberAt(0)->type().type()->asVoidType())
        return 1;
    return argc;
}

// The overview a declaration is written out with: the project's own code
// style, plus everything a signature is made of.
static Overview declarationOverview()
{
    Overview overview = CppCodeStyleSettings::currentProjectCodeStyleOverview();
    overview.showReturnTypes = true;
    overview.showTemplateParameters = true;
    overview.showArgumentNames = true;
    overview.showFunctionSignatures = true;
    return overview;
}

// The one a parameter is written out with, which shows no argument names of
// its own: what is being written is a single parameter, not a signature.
static Overview parameterOverview()
{
    Overview overview = CppCodeStyleSettings::currentProjectCodeStyleOverview();
    overview.showReturnTypes = true;
    overview.showTemplateParameters = true;
    return overview;
}

// How a type is spelled where it is only going to be compared -- not with the
// project's code style, which is about how somebody likes their text and has
// no place in deciding whether two types are the same one.
static QString canonicalType(const FullySpecifiedType &type)
{
    Overview overview;
    overview.showReturnTypes = true;
    overview.showTemplateParameters = true;
    return overview.prettyType(type);
}

// What the built-in front end says a function's signature is.
static FunctionSignature signatureOf(Function *function)
{
    FunctionSignature signature;
    signature.name = declarationOverview().prettyName(function->name());
    signature.returnType = canonicalType(function->returnType());
    const int count = declaredParameterCount(function);
    for (int i = 0; i < count; ++i) {
        FunctionSignature::Parameter parameter;
        if (Symbol * const argument = function->argumentAt(i)) {
            parameter.name = Overview().prettyName(argument->name());
            parameter.type = canonicalType(argument->type());
        }
        signature.parameters.append(parameter);
    }
    signature.isConst = function->isConst();
    signature.isVolatile = function->isVolatile();
    if (const StringLiteral * const spec = function->exceptionSpecification())
        signature.exceptionSpecification = QString::fromUtf8(spec->chars());
    return signature;
}

Q_GLOBAL_STATIC(QRegularExpression, commentArgNameRegexp)

static bool hasCommentedName(
        TranslationUnit *unit,
        const QString &source,
        FunctionDeclaratorAST *declarator,
        int i)
{
    if (!declarator
            || !declarator->parameter_declaration_clause
            || !declarator->parameter_declaration_clause->parameter_declaration_list)
        return false;

    if (Function *f = declarator->symbol) {
        QTC_ASSERT(f, return false);
        if (Symbol *a = f->argumentAt(i)) {
            QTC_ASSERT(a, return false);
            if (a->name())
                return false;
        }
    }

    ParameterDeclarationListAST *list = declarator->parameter_declaration_clause->parameter_declaration_list;
    while (list && i) {
        list = list->next;
        --i;
    }
    if (!list || !list->value || i)
        return false;

    ParameterDeclarationAST *param = list->value;
    if (param->symbol && param->symbol->name())
        return false;

    // maybe in a comment but in the right spot?
    int nameStart = 0;
    if (param->declarator)
        nameStart = unit->tokenAt(param->declarator->lastToken() - 1).utf16charsEnd();
    else if (param->type_specifier_list)
        nameStart = unit->tokenAt(param->type_specifier_list->lastToken() - 1).utf16charsEnd();
    else
        nameStart = unit->tokenAt(param->firstToken()).utf16charsBegin();

    int nameEnd = 0;
    if (param->equal_token)
        nameEnd = unit->tokenAt(param->equal_token).utf16charsBegin();
    else
        nameEnd = unit->tokenAt(param->lastToken()).utf16charsBegin(); // one token after

    QString text = source.mid(nameStart, nameEnd - nameStart);

    if (commentArgNameRegexp()->pattern().isEmpty())
        *commentArgNameRegexp() = QRegularExpression(QLatin1String("/\\*\\s*(\\w*)\\s*\\*/"));
    return text.indexOf(*commentArgNameRegexp()) != -1;
}

static bool canReplaceSpecifier(TranslationUnit *translationUnit, SpecifierAST *specifier)
{
    if (SimpleSpecifierAST *simple = specifier->asSimpleSpecifier()) {
        switch (translationUnit->tokenAt(simple->specifier_token).kind()) {
        case T_CONST:
        case T_VOLATILE:
        case T_CHAR:
        case T_CHAR16_T:
        case T_CHAR32_T:
        case T_WCHAR_T:
        case T_BOOL:
        case T_SHORT:
        case T_INT:
        case T_LONG:
        case T_SIGNED:
        case T_UNSIGNED:
        case T_FLOAT:
        case T_DOUBLE:
        case T_VOID:
        case T_AUTO:
        case T___TYPEOF__:
        case T___ATTRIBUTE__:
        case T___DECLSPEC:
            return true;
        default:
            return false;
        }
    }
    return !specifier->asAttributeSpecifier();
}

static SpecifierAST *findFirstReplaceableSpecifier(TranslationUnit *translationUnit, SpecifierListAST *list)
{
    for (SpecifierListAST *it = list; it; it = it->next) {
        if (canReplaceSpecifier(translationUnit, it->value))
            return it->value;
    }
    return nullptr;
}

static unsigned findCommaTokenBetween(const CppRefactoringFileConstPtr &file,
                                      ParameterDeclarationAST *left, ParameterDeclarationAST *right)
{
    unsigned last = left->lastToken() - 1;
    for (unsigned tokenIndex = right->firstToken();
         tokenIndex > last;
         --tokenIndex) {
        if (file->tokenAt(tokenIndex).kind() == T_COMMA)
            return tokenIndex;
    }
    return 0;
}

// Where the declaration the link covers begins and ends, and where its name
// stands in it. The whole of what the side being edited needs -- what the
// cursor is inside of, and what may not change under it -- and where the
// other side's reading starts from.
static void writtenExtentOf(const CppRefactoringFileConstPtr &file,
                            DeclarationAST *declaration, DeclaratorAST *coreDeclarator,
                            FunctionDeclaratorAST *declarator, WrittenDeclaration *written)
{
    written->start = file->startOf(declaration);
    if (declarator->trailing_return_type)
        written->end = file->endOf(declarator->trailing_return_type);
    else if (declarator->exception_specification)
        written->end = file->endOf(declarator->exception_specification);
    else if (declarator->cv_qualifier_list)
        written->end = file->endOf(declarator->cv_qualifier_list->lastValue());
    else
        written->end = file->endOf(declarator->rparen_token);

    if (DeclaratorIdAST * const id = getDeclaratorId(coreDeclarator)) {
        written->nameStart = file->startOf(id);
        written->nameEnd = file->endOf(id);
    }
}

// Where the built-in front end says each part of a declaration is written.
static WrittenDeclaration writtenDeclarationOf(const CppRefactoringFileConstPtr &file,
                                               DeclarationAST *declaration,
                                               DeclaratorAST *coreDeclarator,
                                               FunctionDeclaratorAST *declarator,
                                               Function *function)
{
    WrittenDeclaration written;
    TranslationUnit * const unit = file->cppDocument()->translationUnit();

    writtenExtentOf(file, declaration, coreDeclarator, declarator, &written);
    FunctionDefinitionAST * const definition = declaration->asFunctionDefinition();
    written.isDefinition = definition != nullptr;

    // Where a new return type goes: over the first specifier that may be
    // replaced, or in front of the declarator where there is none.
    SpecifierAST *firstReplaceableSpecifier = nullptr;
    bool hasSpecifiers = false;
    if (SimpleDeclarationAST * const simple = declaration->asSimpleDeclaration()) {
        hasSpecifiers = true;
        firstReplaceableSpecifier
            = findFirstReplaceableSpecifier(unit, simple->decl_specifier_list);
    } else if (definition) {
        hasSpecifiers = true;
        firstReplaceableSpecifier
            = findFirstReplaceableSpecifier(unit, definition->decl_specifier_list);
    }
    if (hasSpecifiers) {
        written.returnTypeMayBeWritten = true;
        written.returnTypeStart = firstReplaceableSpecifier
                                      ? file->startOf(firstReplaceableSpecifier)
                                      : file->startOf(coreDeclarator);
    }

    written.lparenStart = file->startOf(declarator->lparen_token);
    written.lparenEnd = file->endOf(declarator->lparen_token);
    written.rparenStart = file->startOf(declarator->rparen_token);
    written.rparenEnd = file->endOf(declarator->rparen_token);

    // there is no parameter declaration clause if the function has no arguments
    QVarLengthArray<ParameterDeclarationAST *, 10> parameterDecls;
    if (declarator->parameter_declaration_clause) {
        for (ParameterDeclarationListAST *it
                 = declarator->parameter_declaration_clause->parameter_declaration_list;
             it; it = it->next) {
            parameterDecls.append(it->value);
        }
    }

    const QString source = QString::fromUtf8(file->cppDocument()->utf8Source());
    for (int i = 0; i < parameterDecls.size(); ++i) {
        ParameterDeclarationAST * const parameterAst = parameterDecls.at(i);
        WrittenDeclaration::Parameter parameter;
        parameter.range = file->range(parameterAst);

        parameter.slot.start = written.lparenEnd;
        if (i > 0) {
            const unsigned comma = findCommaTokenBetween(file, parameterDecls.at(i - 1),
                                                         parameterAst);
            if (comma > 0)
                parameter.slot.start = file->endOf(comma);
        }
        parameter.slot.end = written.rparenStart;
        if (i + 1 < parameterDecls.size()) {
            const unsigned comma = findCommaTokenBetween(file, parameterAst,
                                                         parameterDecls.at(i + 1));
            if (comma > 0)
                parameter.slot.end = file->startOf(comma);
        }

        if (parameterAst->declarator)
            parameter.typeEnd = file->endOf(parameterAst->declarator);
        else if (parameterAst->type_specifier_list)
            parameter.typeEnd = file->endOf(parameterAst->type_specifier_list->lastToken() - 1);
        else
            parameter.typeEnd = file->startOf(parameterAst);

        if (DeclaratorIdAST * const id = getDeclaratorId(parameterAst->declarator)) {
            parameter.nameStart = file->startOf(id);
            parameter.nameEnd = file->endOf(id);
            const int nextToken = file->tokenAt(id->lastToken()).kind(); // token after id
            parameter.nameMayBeDropped = nextToken == T_COMMA || nextToken == T_EQUAL
                                         || nextToken == T_RPAREN;
            parameter.anEqualFollowsTheName = nextToken == T_EQUAL;
        }
        if (parameterAst->equal_token)
            parameter.defaultValueStart = file->startOf(parameterAst->equal_token);

        parameter.nameIsInAComment = hasCommentedName(unit, source, declarator, i);

        written.parameters.append(parameter);
    }

    for (SpecifierListAST *it = declarator->cv_qualifier_list; it; it = it->next) {
        SimpleSpecifierAST * const simple = it->value->asSimpleSpecifier();
        if (!simple)
            continue;
        WrittenDeclaration::Qualifier qualifier;
        qualifier.start = file->startOf(simple);
        qualifier.end = file->endOf(simple);
        qualifier.removeFrom = file->endOf(simple->specifier_token - 1);
        const int kind = file->tokenAt(simple->specifier_token).kind();
        if (kind == T_CONST)
            written.constQualifier = qualifier;
        else if (kind == T_VOLATILE)
            written.volatileQualifier = qualifier;
    }

    if (declarator->exception_specification) {
        const ChangeSet::Range range = file->range(declarator->exception_specification);
        written.exceptionSpecificationStart = range.start;
        written.exceptionSpecificationEnd = range.end;
    }
    unsigned beforeExceptionSpecification = declarator->ref_qualifier_token;
    if (!beforeExceptionSpecification) {
        const SpecifierListAST * const cvList = declarator->cv_qualifier_list;
        if (cvList && cvList->lastValue()->asSimpleSpecifier())
            beforeExceptionSpecification = cvList->lastValue()->asSimpleSpecifier()->specifier_token;
    }
    if (!beforeExceptionSpecification)
        beforeExceptionSpecification = declarator->rparen_token;
    written.exceptionSpecificationInsertAt = file->endOf(beforeExceptionSpecification);

    // Where a definition writes its parameters in its body, which a rename of
    // one has to follow. The places inside the parentheses are not among them:
    // those are rewritten as part of the parameter list.
    if (definition) {
        const LocalSymbols locals(file->cppDocument(), {}, definition);
        for (int i = 0; i < written.parameters.size(); ++i) {
            Symbol * const argument = function->argumentAt(i);
            if (!argument)
                continue;
            const QList<SemanticInfo::Use> uses = locals.uses.value(argument);
            for (const SemanticInfo::Use &use : uses) {
                if (use.isInvalid())
                    continue;
                const int start = file->position(use.line, use.column);
                if (start <= written.rparenEnd)
                    continue;
                written.parameters[i].uses.append({start, start + int(use.length)});
            }
        }
    }

    return written;
}

// The built-in front end's reading of the declaration as it now stands in the
// editor. It parses the text as a file of its own -- a declaration is a file
// -- and rewrites each of its types for the scope the other side is written
// in, which is what UseMinimalNames does: the shortest spelling that still
// resolves to the same thing there.
class BuiltinEditedDeclaration : public EditedDeclaration
{
public:
    BuiltinEditedDeclaration(const QString &text, const Snapshot &snapshot,
                             const Document::Ptr &sourceDocument, Function *sourceFunction,
                             const Document::Ptr &targetDocument, Function *targetFunction)
        : m_targetFunction(targetFunction)
        , m_sourceContext(sourceDocument, snapshot)
        , m_targetContext(targetDocument, snapshot)
    {
        TypeOfExpression typeOfExpression; // ### just need to preprocess...
        typeOfExpression.init(sourceDocument, snapshot);

        // A selection's line breaks come out as paragraph separators, which
        // the preprocessor does not read as line breaks.
        QString declarationText = text;
        for (int i = 0; i < declarationText.size(); ++i) {
            if (declarationText.at(i).toLatin1() == 0)
                declarationText[i] = QLatin1Char('\n');
        }
        declarationText.append(QLatin1String("{}"));

        m_document = Document::create(FilePath::fromPathPart(u"<decl>"));
        m_document->setUtf8Source(typeOfExpression.preprocess(declarationText.toUtf8()));
        m_document->parse(Document::ParseDeclaration);
        m_document->check();

        if (!m_document->translationUnit()->ast())
            return;
        FunctionDefinitionAST * const definition
            = m_document->translationUnit()->ast()->asFunctionDefinition();
        if (!definition || !definition->symbol)
            return;
        DeclaratorIdAST * const id = getDeclaratorId(definition->declarator);
        if (!id || !id->name || !id->name->name)
            return;
        m_function = definition->symbol;
        m_name = declarationOverview().prettyName(id->name->name);

        // A type of the edited declaration is written where the other side
        // stands, so it is read in the scope the edited one is in and written
        // in the scope the other one is in. A return type sits outside the
        // function, its parameters inside it.
        m_returnTypeEnvironment.setContext(m_sourceContext);
        m_returnTypeEnvironment.switchScope(sourceFunction->enclosingScope());
        m_returnTypeNames.reset(new UseMinimalNames(
            typeScopeOf(targetFunction->enclosingScope())));
        m_returnTypeEnvironment.enter(m_returnTypeNames.get());

        m_parameterEnvironment.setContext(m_sourceContext);
        m_parameterEnvironment.switchScope(sourceFunction);
        m_parameterNames.reset(new UseMinimalNames(typeScopeOf(targetFunction)));
        m_parameterEnvironment.enter(m_parameterNames.get());
    }

    bool isValid() const override { return m_function != nullptr; }

    FunctionSignature signature() const override
    {
        FunctionSignature signature = signatureOf(m_function);
        signature.name = m_name;
        return signature;
    }

    QString returnTypeDeclaration() const override
    {
        const FullySpecifiedType type = rewrite(m_function->returnType(),
                                                &m_returnTypeEnvironment);
        return declarationOverview().prettyType(type, m_targetFunction->name());
    }

    QString parameterDeclaration(int index, const QString &name) const override
    {
        return parameterOverview().prettyType(rewrittenParameterTypeAt(index), name);
    }

    QString rewrittenParameterType(int index) const override
    {
        return canonicalType(rewrittenParameterTypeAt(index));
    }

private:
    // Where a name has to be resolvable from, which is what decides how much
    // of it has to be written: the scope the other side stands in, or the
    // whole file where that cannot be looked up.
    ClassOrNamespace *typeScopeOf(Scope *scope)
    {
        if (ClassOrNamespace * const found = m_targetContext.lookupType(scope))
            return found;
        return m_targetContext.globalNamespace();
    }

    FullySpecifiedType rewrite(const FullySpecifiedType &type,
                               SubstitutionEnvironment *environment) const
    {
        return rewriteType(type, environment, m_sourceContext.bindings()->control().get());
    }

    FullySpecifiedType rewrittenParameterTypeAt(int index) const
    {
        Symbol * const parameter = m_function->argumentAt(index);
        QTC_ASSERT(parameter, return {});
        return rewrite(parameter->type(), &m_parameterEnvironment);
    }

    Function * const m_targetFunction;
    LookupContext m_sourceContext;
    LookupContext m_targetContext;
    Document::Ptr m_document;
    Function *m_function = nullptr;
    QString m_name;
    mutable SubstitutionEnvironment m_returnTypeEnvironment;
    mutable SubstitutionEnvironment m_parameterEnvironment;
    std::unique_ptr<UseMinimalNames> m_returnTypeNames;
    std::unique_ptr<UseMinimalNames> m_parameterNames;
};

// What the finder found at the cursor, which is what it needs in order to go
// looking for the other side.
class LinkSource
{
public:
    Document::Ptr document;
    DeclarationAST *declaration = nullptr;
    FunctionDeclaratorAST *declarator = nullptr;
    Function *function = nullptr;

    // Where the function's name stands, one-based. The other model is asked
    // from there rather than from wherever the cursor happens to be, since
    // a position on the name is what reaches a function in it.
    int nameLine = 0;
    int nameColumn = 0;

    // Taken on the editor's own thread, because that is where the editors
    // are, and read on the one that goes looking.
    WorkingCopy workingCopy;
};

#ifdef QTC_WITH_CXX_FRONTEND
// Fills the link off the cxx-frontend model, or answers false and leaves it
// alone where that model has nothing to say -- and then the built-in one
// does the work, exactly as before.
static bool findLinkOnTheModel(const std::shared_ptr<FunctionDeclDefLink> &link,
                               const LinkSource &source, CppRefactoringChanges &changes)
{
    if (!cxxFrontendModelRequested())
        return false;

    // Held for as long as the model is reading them: what it is handed is the
    // text, and the text belongs to the file.
    QList<CppRefactoringFileConstPtr> read;
    const auto textOf = [&](const FilePath &path) -> const QTextDocument * {
        const CppRefactoringFileConstPtr file = changes.fileNoEditor(path);
        if (!file || !file->isValid())
            return nullptr;
        read.append(file);
        return file->document();
    };

    const std::optional<CxxFrontendDeclDefLink> found
        = cxxFrontendDeclDefLink(changes.snapshot(), source.document->filePath(),
                                 source.nameLine, source.nameColumn, source.workingCopy,
                                 textOf);
    if (!found)
        return false;

    const CppRefactoringFileConstPtr targetFile = changes.fileNoEditor(found->targetFilePath);
    if (!targetFile->isValid())
        return false;

    link->targetFile = targetFile;
    link->sourceSignature = found->sourceSignature;
    link->targetSignature = found->targetSignature;
    link->targetWritten = found->targetWritten;
    link->targetNameLine = found->targetNameLine;
    link->targetNameColumn = found->targetNameColumn;
    link->targetShortName = found->targetShortName;
    targetFile->lineAndColumn(link->targetWritten.start, &link->targetLine,
                              &link->targetColumn);
    link->targetInitial = targetFile->textOf(link->targetWritten.start,
                                             link->targetWritten.end);

    // The snapshot is the built-in model's and this one has no use for it.
    const auto readEdited = found->readEditedDeclaration;
    link->readEditedDeclaration = [readEdited](const QTextCursor &linkSelection,
                                               const QTextCursor &nameSelection,
                                               const Snapshot &) {
        return readEdited(linkSelection, nameSelection);
    };
    return true;
}
#endif

static std::shared_ptr<FunctionDeclDefLink> findLinkHelper(
    std::shared_ptr<FunctionDeclDefLink> link, LinkSource source,
    CppRefactoringChanges changes)
{
    std::shared_ptr<FunctionDeclDefLink> noResult;
    const Snapshot &snapshot = changes.snapshot();

#ifdef QTC_WITH_CXX_FRONTEND
    if (findLinkOnTheModel(link, source, changes))
        return link;
#endif

    // find the matching decl/def symbol
    Symbol *target = nullptr;
    SymbolFinder finder;
    if (FunctionDefinitionAST *funcDef = source.declaration->asFunctionDefinition()) {
        QList<Declaration *> nameMatch, argumentCountMatch, typeMatch;
        finder.findMatchingDeclaration(LookupContext(source.document, snapshot),
                                       funcDef->symbol,
                                       &typeMatch, &argumentCountMatch, &nameMatch);
        if (!typeMatch.isEmpty())
            target = typeMatch.first();
    } else if (source.declaration->asSimpleDeclaration()) {
        target = finder.findMatchingDefinition(source.declarator->symbol, snapshot, true);
    }
    if (!target)
        return noResult;

    // parse the target file to get the linked decl/def
    CppRefactoringFileConstPtr targetFile = changes.fileNoEditor(target->filePath());
    if (!targetFile->isValid())
        return noResult;

    DeclarationAST *targetParent = nullptr;
    FunctionDeclaratorAST *targetFuncDecl = nullptr;
    DeclaratorAST *targetDeclarator = nullptr;
    if (!findDeclOrDef(targetFile->cppDocument(), target->line(), target->column(),
                       &targetParent, &targetDeclarator, &targetFuncDecl))
        return noResult;

    // the parens are necessary for finding good places for changes
    if (!targetFuncDecl->lparen_token || !targetFuncDecl->rparen_token)
        return noResult;
    QTC_ASSERT(targetFuncDecl->symbol, return noResult);
    // if the source and target argument counts differ, something is wrong
    QTC_ASSERT(targetFuncDecl->symbol->argumentCount() == source.function->argumentCount(),
               return noResult);

    link->targetFile = targetFile;

    Function * const targetFunction = targetFuncDecl->symbol;
    link->sourceSignature = signatureOf(source.function);
    link->targetSignature = signatureOf(targetFunction);
    link->targetWritten = writtenDeclarationOf(targetFile, targetParent, targetDeclarator,
                                               targetFuncDecl, targetFunction);

    targetFile->lineAndColumn(link->targetWritten.start, &link->targetLine,
                              &link->targetColumn);
    link->targetInitial = targetFile->textOf(link->targetWritten.start,
                                             link->targetWritten.end);
    link->targetNameLine = targetFunction->line();
    link->targetNameColumn = targetFunction->column();
    // The name a comment above it documents it under, which is its own name
    // rather than the path to it.
    const QStringList nameParts = link->targetSignature.name.split("::", Qt::SkipEmptyParts);
    link->targetShortName = nameParts.isEmpty() ? QString() : nameParts.last();

    const Document::Ptr sourceDocument = source.document;
    Function * const sourceFunction = source.function;
    const Document::Ptr targetDocument = targetFile->cppDocument();
    link->readEditedDeclaration =
        [sourceDocument, sourceFunction, targetDocument, targetFunction](
            const QTextCursor &linkSelection, const QTextCursor &, const Snapshot &snapshot)
        -> std::shared_ptr<EditedDeclaration> {
            return std::make_shared<BuiltinEditedDeclaration>(
                linkSelection.selectedText(), snapshot, sourceDocument, sourceFunction,
                targetDocument, targetFunction);
        };

    return link;
}

void FunctionDeclDefLinkFinder::startFindLinkAt(
        QTextCursor cursor, const Document::Ptr &doc, const Snapshot &snapshot)
{
    // check if cursor is on function decl/def
    DeclarationAST *parent = nullptr;
    FunctionDeclaratorAST *funcDecl = nullptr;
    DeclaratorAST *declarator = nullptr;
    if (!findDeclOrDef(doc, cursor.blockNumber() + 1, cursor.columnNumber() + 1,
                       &parent, &declarator, &funcDecl))
        return;

    // find the start/end offsets
    CppRefactoringChanges refactoringChanges(snapshot);
    CppRefactoringFilePtr sourceFile = refactoringChanges.cppFile(doc->filePath());
    sourceFile->setCppDocument(doc);
    WrittenDeclaration written;
    writtenExtentOf(sourceFile, parent, declarator, funcDecl, &written);

    // if already scanning, don't scan again
    if (!m_scannedSelection.isNull()
            && m_scannedSelection.selectionStart() == written.start
            && m_scannedSelection.selectionEnd() == written.end) {
        return;
    }

    // build the selection for the currently scanned area
    m_scannedSelection = cursor;
    m_scannedSelection.setPosition(written.end);
    m_scannedSelection.setPosition(written.start, QTextCursor::KeepAnchor);
    m_scannedSelection.setKeepPositionOnInsert(true);

    // build selection for the name
    m_nameSelection = cursor;
    m_nameSelection.setPosition(written.nameEnd);
    m_nameSelection.setPosition(written.nameStart, QTextCursor::KeepAnchor);
    m_nameSelection.setKeepPositionOnInsert(true);

    using ResultType = std::shared_ptr<FunctionDeclDefLink>;
    // set up a base result
    ResultType result(new FunctionDeclDefLink);
    result->nameInitial = m_nameSelection.selectedText();
    result->sourceDocument = doc;

    LinkSource source;
    source.document = doc;
    source.declaration = parent;
    source.declarator = funcDecl;
    source.function = funcDecl->symbol;
    sourceFile->lineAndColumn(written.nameStart, &source.nameLine, &source.nameColumn);
#ifdef QTC_WITH_CXX_FRONTEND
    if (cxxFrontendModelRequested())
        source.workingCopy = CppModelManager::workingCopy();
#endif

    // handle the rest in a thread
    const auto onSetup = [result, source, refactoringChanges](Async<ResultType> &task) {
        task.setConcurrentCallData(findLinkHelper, result, source, refactoringChanges);
    };
    const auto onDone = [this](const Async<ResultType> &task) {
        ResultType link = task.result();
        if (link) {
            link->linkSelection = m_scannedSelection;
            link->nameSelection = m_nameSelection;
            if (m_nameSelection.selectedText() != link->nameInitial)
                link.reset();
        }
        m_scannedSelection = {};
        m_nameSelection = {};
        if (link)
            emit foundLink(link);
    };
    m_taskTreeRunner.start({
        AsyncTask<ResultType>(onSetup, onDone, CallDoneFlag::OnSuccess)
    });
}

bool FunctionDeclDefLink::isValid() const
{
    return !linkSelection.isNull();
}

bool FunctionDeclDefLink::isMarkerVisible() const
{
    return hasMarker;
}

void FunctionDeclDefLink::apply(CppEditorWidget *editor, bool jumpToMatch)
{
    Snapshot snapshot = editor->semanticInfo().snapshot;

    // first verify the interesting region of the target file is unchanged
    CppRefactoringChanges refactoringChanges(snapshot);
    CppRefactoringFilePtr newTargetFile = refactoringChanges.cppFile(targetFile->filePath());
    if (!newTargetFile->isValid())
        return;
    const int targetStart = newTargetFile->position(targetLine, targetColumn);
    const int targetEnd = targetStart + targetInitial.size();
    if (targetInitial == newTargetFile->textOf(targetStart, targetEnd)) {
        if (jumpToMatch) {
            const int jumpTarget = newTargetFile->position(targetNameLine, targetNameColumn);
            newTargetFile->setOpenEditor(true, jumpTarget);
        }
        ChangeSet changeSet = changes(snapshot, targetStart);
        for (ChangeSet::EditOp &op : changeSet.operationList()) {
            if (op.type() == ChangeSet::EditOp::Replace)
                op.setFormat1(true);
        }
        newTargetFile->apply(changeSet);
    } else {
        ToolTip::show(editor->toolTipPosition(linkSelection),
                      Tr::tr("Target file was changed, could not apply changes"));
    }
}

void FunctionDeclDefLink::hideMarker(CppEditorWidget *editor)
{
    if (!hasMarker)
        return;
    editor->clearRefactorMarkers(Constants::CPP_FUNCTION_DECL_DEF_LINK_MARKER_ID);
    hasMarker = false;
}

void FunctionDeclDefLink::showMarker(CppEditorWidget *editor)
{
    if (hasMarker)
        return;

    RefactorMarkers markers;
    RefactorMarker marker;

    // show the marker at the end of the linked area, with a special case
    // to avoid it overlapping with a trailing semicolon
    marker.cursor = editor->textCursor();
    marker.cursor.setPosition(linkSelection.selectionEnd());
    const int endBlockNr = marker.cursor.blockNumber();
    marker.cursor.setPosition(linkSelection.selectionEnd() + 1, QTextCursor::KeepAnchor);
    if (marker.cursor.blockNumber() != endBlockNr
            || marker.cursor.selectedText() != QLatin1String(";")) {
        marker.cursor.setPosition(linkSelection.selectionEnd());
    }

    QString message;
    if (targetWritten.isDefinition)
        message = Tr::tr("Apply changes to definition");
    else
        message = Tr::tr("Apply changes to declaration");

    Core::Command *quickfixCommand = Core::ActionManager::command(TextEditor::Constants::QUICKFIX_THIS);
    if (quickfixCommand)
        message = ProxyAction::stringWithAppendedShortcut(message, quickfixCommand->keySequence());

    marker.tooltip = message;
    marker.type = Constants::CPP_FUNCTION_DECL_DEF_LINK_MARKER_ID;
    marker.callback = [](TextEditor::TextEditorWidget *widget) {
        if (auto cppEditor = qobject_cast<CppEditorWidget *>(widget))
            cppEditor->applyDeclDefLinkChanges(true);
    };
    markers += marker;
    editor->setRefactorMarkers(markers, Constants::CPP_FUNCTION_DECL_DEF_LINK_MARKER_ID);

    hasMarker = true;
}

using IndicesList = QVarLengthArray<int, 10>;

template <class IndicesListType>
static int findUniqueTypeMatch(int sourceParamIndex,
                               const FunctionSignature &sourceSignature,
                               const FunctionSignature &newSignature,
                               const IndicesListType &sourceParams,
                               const IndicesListType &newParams)
{
    const QString &sourceType = sourceSignature.parameters.at(sourceParamIndex).type;

    // if other sourceParams have the same type, we can't do anything
    for (int i = 0; i < sourceParams.size(); ++i) {
        int otherSourceParamIndex = sourceParams.at(i);
        if (sourceParamIndex == otherSourceParamIndex)
            continue;
        if (sourceType == sourceSignature.parameters.at(otherSourceParamIndex).type)
            return -1;
    }

    // if there's exactly one newParam with the same type, bind to that
    // this is primarily done to catch moves of unnamed parameters
    int newParamWithSameTypeIndex = -1;
    for (int i = 0; i < newParams.size(); ++i) {
        int newParamIndex = newParams.at(i);
        if (sourceType == newSignature.parameters.at(newParamIndex).type) {
            if (newParamWithSameTypeIndex != -1)
                return -1;
            newParamWithSameTypeIndex = newParamIndex;
        }
    }
    return newParamWithSameTypeIndex;
}

static IndicesList unmatchedIndices(const IndicesList &indices)
{
    IndicesList ret;
    ret.reserve(indices.size());
    for (int i = 0; i < indices.size(); ++i) {
        if (indices[i] == -1)
            ret.append(i);
    }
    return ret;
}

static QString ensureCorrectParameterSpacing(const QString &text, bool isFirstParam)
{
    if (isFirstParam) { // drop leading spaces
        int newlineCount = 0;
        int firstNonSpace = 0;
        while (firstNonSpace + 1 < text.size() && text.at(firstNonSpace).isSpace()) {
            if (text.at(firstNonSpace) == QChar::ParagraphSeparator)
                ++newlineCount;
            ++firstNonSpace;
        }
        return QString(newlineCount, QChar::ParagraphSeparator) + text.mid(firstNonSpace);
    } else { // ensure one leading space
        if (text.isEmpty() || !text.at(0).isSpace())
            return QLatin1Char(' ') + text;
    }
    return text;
}

ChangeSet FunctionDeclDefLink::changes(const Snapshot &snapshot, int targetOffset)
{
    ChangeSet changes;

    // Everything prefixed with 'new' in this function relates to the state of the 'source'
    // function *after* the user did his changes.

    // The 'newTarget' prefix indicates something relates to the changes we plan to do
    // to the 'target' function.

    QTC_ASSERT(readEditedDeclaration, return changes);
    const std::shared_ptr<EditedDeclaration> edited
        = readEditedDeclaration(linkSelection, nameSelection, snapshot);
    if (!edited || !edited->isValid())
        return changes;
    const FunctionSignature newSignature = edited->signature();

    // abort if the name of the newly parsed function is not the expected one
    if (newSignature.name != normalizedInitialName())
        return changes;

    // sync return type
    if (targetWritten.returnTypeMayBeWritten
        && newSignature.returnType != sourceSignature.returnType
        && newSignature.returnType != targetSignature.returnType) {
        changes.replace(targetWritten.returnTypeStart, targetWritten.lparenStart,
                        edited->returnTypeDeclaration());
    }

    // sync parameters
    {
        // the number of parameters in the source or the target function
        const int existingParamCount = sourceSignature.parameters.size();
        if (existingParamCount != targetSignature.parameters.size())
            return changes;
        if (existingParamCount != targetWritten.parameters.size())
            return changes;

        const int newParamCount = newSignature.parameters.size();

        // When syncing parameters we need to take care that parameters inserted or
        // removed in the middle or parameters being reshuffled are treated correctly.
        // To do that, we construct a newParam -> sourceParam map, based on parameter
        // names and types.
        // Initially they start out with -1 to indicate a new parameter.
        IndicesList newParamToSourceParam(newParamCount);
        for (int i = 0; i < newParamCount; ++i)
            newParamToSourceParam[i] = -1;

        // fill newParamToSourceParam
        {
            IndicesList sourceParamToNewParam(existingParamCount);
            for (int i = 0; i < existingParamCount; ++i)
                sourceParamToNewParam[i] = -1;

            QMultiHash<QString, int> sourceParamNameToIndex;
            for (int i = 0; i < existingParamCount; ++i)
                sourceParamNameToIndex.insert(sourceSignature.parameters.at(i).name, i);

            QMultiHash<QString, int> newParamNameToIndex;
            for (int i = 0; i < newParamCount; ++i)
                newParamNameToIndex.insert(newSignature.parameters.at(i).name, i);

            // name-based binds (possibly disambiguated by type)
            for (int sourceParamIndex = 0; sourceParamIndex < existingParamCount; ++sourceParamIndex) {
                const QString &name = sourceSignature.parameters.at(sourceParamIndex).name;
                QList<int> newParams = newParamNameToIndex.values(name);
                QList<int> sourceParams = sourceParamNameToIndex.values(name);

                if (newParams.isEmpty())
                    continue;

                // if the names match uniquely, bind them
                // this catches moves of named parameters
                if (newParams.size() == 1 && sourceParams.size() == 1) {
                    sourceParamToNewParam[sourceParamIndex] = newParams.first();
                    newParamToSourceParam[newParams.first()] = sourceParamIndex;
                } else {
                    // if the name match is not unique, try to find a unique
                    // type match among the same-name parameters
                    const int newParamWithSameTypeIndex = findUniqueTypeMatch(
                                sourceParamIndex, sourceSignature, newSignature,
                                sourceParams, newParams);
                    if (newParamWithSameTypeIndex != -1) {
                        sourceParamToNewParam[sourceParamIndex] = newParamWithSameTypeIndex;
                        newParamToSourceParam[newParamWithSameTypeIndex] = sourceParamIndex;
                    }
                }
            }

            // find unique type matches among the unbound parameters
            const IndicesList &freeSourceParams = unmatchedIndices(sourceParamToNewParam);
            const IndicesList &freeNewParams = unmatchedIndices(newParamToSourceParam);
            for (int i = 0; i < freeSourceParams.size(); ++i) {
                int sourceParamIndex = freeSourceParams.at(i);
                const int newParamWithSameTypeIndex = findUniqueTypeMatch(
                            sourceParamIndex, sourceSignature, newSignature,
                            freeSourceParams, freeNewParams);
                if (newParamWithSameTypeIndex != -1) {
                    sourceParamToNewParam[sourceParamIndex] = newParamWithSameTypeIndex;
                    newParamToSourceParam[newParamWithSameTypeIndex] = sourceParamIndex;
                }
            }

            // add position based binds if possible
            for (int i = 0; i < existingParamCount && i < newParamCount; ++i) {
                if (newParamToSourceParam[i] == -1 && sourceParamToNewParam[i] == -1) {
                    newParamToSourceParam[i] = i;
                    sourceParamToNewParam[i] = i;
                }
            }
        }

        // build the new parameter declarations
        QString newTargetParameters;
        bool hadChanges = newParamCount < existingParamCount; // below, additions and changes set this to true as well
        QHash<int, QString> renamedTargetParameters;
        bool switchedOnly = !hadChanges;
        for (int newParamIndex = 0; newParamIndex < newParamCount; ++newParamIndex) {
            const int existingParamIndex = newParamToSourceParam[newParamIndex];
            const FunctionSignature::Parameter &newParam
                = newSignature.parameters.at(newParamIndex);
            const bool isFirstNewParam = newParamIndex == 0;

            if (!isFirstNewParam)
                newTargetParameters += QLatin1Char(',');

            QString newTargetParam;

            // if it's genuinely new, add it
            if (existingParamIndex == -1) {
                newTargetParam = edited->parameterDeclaration(newParamIndex, newParam.name);
                hadChanges = true;
                switchedOnly = false;
            // otherwise preserve as much as possible from the existing parameter
            } else {
                const FunctionSignature::Parameter &targetParam
                    = targetSignature.parameters.at(existingParamIndex);
                const FunctionSignature::Parameter &sourceParam
                    = sourceSignature.parameters.at(existingParamIndex);
                const WrittenDeclaration::Parameter &written
                    = targetWritten.parameters.at(existingParamIndex);

                const int parameterStart = written.slot.start;
                const int parameterEnd = written.slot.end;

                // if the name wasn't changed, don't change the target name even if it's different
                QString replacementName = newParam.name;
                bool nameCameFromTheTarget = false;
                if (replacementName == sourceParam.name) {
                    replacementName = targetParam.name;
                    nameCameFromTheTarget = true;
                }

                // don't change the name if it's in a comment
                if (written.nameIsInAComment) {
                    replacementName.clear();
                    nameCameFromTheTarget = false;
                }

                // track renames
                if (!nameCameFromTheTarget && !replacementName.isEmpty())
                    renamedTargetParameters[existingParamIndex] = replacementName;

                // need to change the type (and name)?
                const QString replacementType = edited->rewrittenParameterType(newParamIndex);
                if (newParam.type != sourceParam.type && replacementType != targetParam.type) {
                    switchedOnly = false;
                    newTargetParam = targetFile->textOf(parameterStart, written.range.start);
                    newTargetParam += edited->parameterDeclaration(newParamIndex, replacementName);
                    newTargetParam += targetFile->textOf(written.typeEnd, parameterEnd);
                    hadChanges = true;
                // change the name only?
                } else if (targetParam.name != replacementName) {
                    switchedOnly = false;
                    if (written.nameStart != -1) {
                        newTargetParam += targetFile->textOf(parameterStart, written.nameStart);
                        QString rest = targetFile->textOf(written.nameEnd, parameterEnd);
                        if (replacementName.isEmpty()) {
                            if (written.nameMayBeDropped) {
                                if (!written.anEqualFollowsTheName)
                                    newTargetParam = newTargetParam.trimmed();
                                newTargetParam += rest.trimmed();
                            }
                        } else {
                            newTargetParam += replacementName;
                            newTargetParam += rest;
                        }
                    } else {
                        // add name to unnamed parameter
                        int insertPos = parameterEnd;
                        if (written.defaultValueStart != -1)
                            insertPos = written.defaultValueStart;
                        newTargetParam += targetFile->textOf(parameterStart, insertPos);

                        // prepend a space, unless ' ', '*', '&'
                        QChar lastChar;
                        if (!newTargetParam.isEmpty())
                            lastChar = newTargetParam.at(newTargetParam.size() - 1);
                        if (!lastChar.isSpace() && lastChar != QLatin1Char('*') && lastChar != QLatin1Char('&'))
                            newTargetParam += QLatin1Char(' ');

                        newTargetParam += replacementName;

                        // append a space, unless unnecessary
                        const QString &rest = targetFile->textOf(insertPos, parameterEnd);
                        if (!rest.isEmpty() && !rest.at(0).isSpace())
                            newTargetParam += QLatin1Char(' ');

                        newTargetParam += rest;
                    }
                    hadChanges = true;
                // change nothing - though the parameter might still have moved
                } else {
                    if (existingParamIndex != newParamIndex)
                        hadChanges = true;
                    newTargetParam = targetFile->textOf(parameterStart, parameterEnd);
                }
            }

            // apply
            newTargetParameters += ensureCorrectParameterSpacing(newTargetParam, isFirstNewParam);
        }
        if (hadChanges) {
            // Special case for when there was purely a parameter switch:
            // This operation can simply be mapped to the "flip" change operation, with
            // no heuristics as to the formatting.
            if (switchedOnly) {
                QList<int> srcIndices;
                for (int tgtIndex = 0; tgtIndex < newParamToSourceParam.size(); ++tgtIndex) {
                    if (srcIndices.contains(tgtIndex))
                        continue;
                    const int srcIndex = newParamToSourceParam[tgtIndex];
                    srcIndices << srcIndex;
                    changes.flip(targetWritten.parameters.at(srcIndex).range,
                                 targetWritten.parameters.at(tgtIndex).range);
                }
            } else {
                changes.replace(targetWritten.lparenEnd, targetWritten.rparenStart,
                                newTargetParameters);
            }
        }

        // Change parameter names in function documentation.
        [&] {
            if (renamedTargetParameters.isEmpty())
                return;
            const QList<CommentRange> functionComments = commentsForDeclaration(
                targetShortName, {targetNameLine, targetNameColumn - 1},
                *targetFile->document(), targetFile->cppDocument());
            if (functionComments.isEmpty())
                return;
            const QString &content = targetFile->document()->toPlainText();
            const QStringView docView = QStringView(content);
            for (auto it = renamedTargetParameters.cbegin();
                 it != renamedTargetParameters.cend(); ++it) {
                const QString paramName = targetSignature.parameters.at(it.key()).name;
                if (paramName.isEmpty())
                    continue;
                for (const CommentRange &comment : functionComments) {
                    const QStringView commentView = docView.mid(comment.start,
                                                                comment.end - comment.start);
                    const QList<Text::Range> ranges = symbolOccurrencesInText(
                        *targetFile->document(), commentView, comment.start, paramName);
                    for (const Text::Range &r : ranges) {
                        const int startPos = r.begin.toPositionInDocument(targetFile->document());
                        const int endPos = r.end.toPositionInDocument(targetFile->document());
                        changes.replace(startPos, endPos, it.value());
                    }
                }
            }
        }();

        // for function definitions, rename the local usages
        if (targetWritten.isDefinition && !renamedTargetParameters.isEmpty()) {
            for (auto it = renamedTargetParameters.cbegin(), end = renamedTargetParameters.cend();
                    it != end; ++it) {
                const QList<ChangeSet::Range> uses
                    = targetWritten.parameters.at(it.key()).uses;
                for (const ChangeSet::Range &use : uses)
                    changes.replace(use.start, use.end, it.value());
            }
        }
    }

    // sync cv qualification
    if (targetSignature.isConst != newSignature.isConst
            || targetSignature.isVolatile != newSignature.isVolatile) {
        QString cvString;
        if (newSignature.isConst)
            cvString += QLatin1String("const");
        if (newSignature.isVolatile) {
            if (!cvString.isEmpty())
                cvString += QLatin1Char(' ');
            cvString += QLatin1String("volatile");
        }

        // if the target function is neither const or volatile, just add the new specifiers after the closing ')'
        if (!targetSignature.isConst && !targetSignature.isVolatile) {
            cvString.prepend(QLatin1Char(' '));
            changes.insert(targetWritten.rparenEnd, cvString);
        // modify/remove existing specifiers
        } else {
            const WrittenDeclaration::Qualifier &constQualifier = targetWritten.constQualifier;
            const WrittenDeclaration::Qualifier &volatileQualifier
                = targetWritten.volatileQualifier;
            // if there are both, we just need to remove
            if (constQualifier.isWritten() && volatileQualifier.isWritten()) {
                if (!newSignature.isConst)
                    changes.remove(constQualifier.removeFrom, constQualifier.end);
                if (!newSignature.isVolatile)
                    changes.remove(volatileQualifier.removeFrom, volatileQualifier.end);
            // otherwise adjust, remove or extend the one existing specifier
            } else {
                const WrittenDeclaration::Qualifier &qualifier
                    = constQualifier.isWritten() ? constQualifier : volatileQualifier;
                QTC_ASSERT(qualifier.isWritten(), return changes);

                if (!newSignature.isConst && !newSignature.isVolatile)
                    changes.remove(qualifier.removeFrom, qualifier.end);
                else
                    changes.replace(qualifier.start, qualifier.end, cvString);
            }
        }
    }

    // sync noexcept/throw()
    if (targetSignature.exceptionSpecification != newSignature.exceptionSpecification) {
        if (!targetSignature.exceptionSpecification.isEmpty()
                && !newSignature.exceptionSpecification.isEmpty()) {
            changes.replace(targetWritten.exceptionSpecificationStart,
                            targetWritten.exceptionSpecificationEnd,
                            newSignature.exceptionSpecification);
        } else if (targetSignature.exceptionSpecification.isEmpty()) {
            changes.insert(targetWritten.exceptionSpecificationInsertAt,
                           ' ' + newSignature.exceptionSpecification);
        } else {
            changes.remove(targetWritten.exceptionSpecificationStart,
                           targetWritten.exceptionSpecificationEnd);
        }
    }

    if (targetOffset != -1) {
        // move all change operations to have the right start offset
        const int moveAmount = targetOffset - targetWritten.start;
        QList<ChangeSet::EditOp> ops = changes.operationList();
        for (int i = 0; i < ops.size(); ++i) {
            ops[i].pos1 += moveAmount;
            ops[i].pos2 += moveAmount;
        }
        changes = ChangeSet(ops);
    }

    return changes;
}

// Only has an effect with operators.
// Makes sure there is exactly one space between the "operator" string
// and the actual operator, as that is what it will be compared against.
QString FunctionDeclDefLink::normalizedInitialName() const
{
    QString n = nameInitial;
    const QString op = "operator";
    int index = n.indexOf(op);
    if (index == -1)
        return n;
    if (index > 0 && n.at(index - 1).isLetterOrNumber())
        return n;
    index += op.size();
    if (index == n.size())
        return n;
    if (n.at(index).isLetterOrNumber())
        return n;
    n.insert(index++, ' ');
    while (index < n.size() && n.at(index) == ' ')
        n.remove(index, 1);
    return n;
}

} // namespace CppEditor::Internal
