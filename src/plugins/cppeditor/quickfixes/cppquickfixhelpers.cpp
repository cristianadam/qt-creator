// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppquickfixhelpers.h"

#include "../cppcodestylesettings.h"
#include "../cppprojectfile.h"
#include "../includeutils.h"
#include "cppquickfixassistant.h"

#include <cplusplus/CppRewriter.h>
#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#include <cxx/translation_unit.h>
#endif

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {

#ifdef QTC_WITH_CXX_FRONTEND
const CxxFrontendDocument *cxxFrontendDocumentFor(const CppQuickFixInterface &interface)
{
    const CppRefactoringFilePtr file = interface.currentFile();
    const std::shared_ptr<const CxxFrontendSnapshot> model = cxxFrontendModel(file->filePath());
    if (!model)
        return nullptr;
    return model->document(file->filePath().toFSPathString());
}

Utils::Text::Position cxxNameOfDeclarator(const CxxFrontendDocument &document,
                                          cxx::DeclaratorAST *declarator)
{
    auto * const id = declarator
                          ? dynamic_cast<cxx::IdDeclaratorAST *>(declarator->coreDeclarator)
                          : nullptr;
    if (!id || !id->unqualifiedId)
        return {};

    cxx::SourceLocation at = id->unqualifiedId->firstSourceLocation();
    if (auto * const destructor = dynamic_cast<cxx::DestructorIdAST *>(id->unqualifiedId)) {
        if (!destructor->id)
            return {};
        at = destructor->id->firstSourceLocation();
    }

    const CxxAstRange name = cxxTokenRangeAt(document, at);
    if (!name.isValid())
        return {};
    return {name.startLine, name.startColumn};
}

bool cxxCanWriteADefinitionOf(const CxxFrontendDocument &document,
                              cxx::DeclaratorAST *declarator)
{
    if (!declarator)
        return false;

    for (auto *chunk : cxx::ListView{declarator->declaratorChunkList}) {
        auto * const parameters = dynamic_cast<cxx::FunctionDeclaratorChunkAST *>(chunk);
        if (parameters && parameters->trailingReturnType)
            return false;
    }

    if (auto * const id = dynamic_cast<cxx::IdDeclaratorAST *>(declarator->coreDeclarator);
        id && dynamic_cast<cxx::OperatorFunctionIdAST *>(id->unqualifiedId)) {
        return false;
    }

    cxx::TranslationUnit * const unit = document.translationUnit();
    if (!unit)
        return false;
    const unsigned first = declarator->firstSourceLocation().index();
    const unsigned last = declarator->lastSourceLocation().index();
    for (unsigned i = first; i < last; ++i) {
        if (unit->tokenAt(cxx::SourceLocation{i}).macroGenerated())
            return false;
    }
    return true;
}
#endif

void insertNewIncludeDirective(
    const QString &include,
    CppRefactoringFilePtr file,
    const Document::Ptr &cppDocument,
    ChangeSet &changes)
{
    // Find optimal position
    unsigned newLinesToPrepend = 0;
    unsigned newLinesToAppend = 0;
    const int insertLine = lineForNewIncludeDirective(
        file->filePath(),
        file->document(),
        cppDocument,
        IgnoreMocIncludes,
        AutoDetect,
        include,
        &newLinesToPrepend,
        &newLinesToAppend);
    QTC_ASSERT(insertLine >= 1, return);
    const int insertPosition = file->position(insertLine, 1);
    QTC_ASSERT(insertPosition >= 0, return);

    // Construct text to insert
    const QString includeLine = QLatin1String("#include ") + include + QLatin1Char('\n');
    QString prependedNewLines, appendedNewLines;
    while (newLinesToAppend--)
        appendedNewLines += QLatin1String("\n");
    while (newLinesToPrepend--)
        prependedNewLines += QLatin1String("\n");
    const QString textToInsert = prependedNewLines + includeLine + appendedNewLines;

    // Insert
    changes.insert(insertPosition, textToInsert);
}

ClassSpecifierAST *astForClassOperations(const CppQuickFixInterface &interface)
{
    const QList<AST *> &path = interface.path();
    if (path.isEmpty())
        return nullptr;
    if (const auto classSpec = path.last()->asClassSpecifier()) // Cursor inside class decl?
        return classSpec;

    // Cursor on a class name?
    if (path.size() < 2)
        return nullptr;
    const SimpleNameAST * const nameAST = path.at(path.size() - 1)->asSimpleName();
    if (!nameAST || !interface.isCursorOn(nameAST))
        return nullptr;
    if (const auto classSpec = path.at(path.size() - 2)->asClassSpecifier())
        return classSpec;
    return nullptr;
}

bool nameIncludesOperatorName(const Name *name)
{
    return name->asOperatorNameId()
           || (name->asQualifiedNameId() && name->asQualifiedNameId()->name()->asOperatorNameId());
}

QString inlinePrefix(const FilePath &targetFile, const std::function<bool()> &extraCondition)
{
    if (ProjectFile::isHeader(ProjectFile::classify(targetFile))
        && (!extraCondition || extraCondition())) {
        return "inline ";
    }
    return {};
}

Class *isMemberFunction(const CPlusPlus::LookupContext &context, CPlusPlus::Function *function)
{
    QTC_ASSERT(function, return nullptr);

    Scope *enclosingScope = function->enclosingScope();
    while (!(enclosingScope->asNamespace() || enclosingScope->asClass()))
        enclosingScope = enclosingScope->enclosingScope();
    QTC_ASSERT(enclosingScope != nullptr, return nullptr);

    const Name *functionName = function->name();
    if (!functionName)
        return nullptr;

    if (!functionName->asQualifiedNameId())
        return nullptr; // trying to add a declaration for a global function

    const QualifiedNameId *q = functionName->asQualifiedNameId();
    if (!q->base())
        return nullptr;

    if (ClassOrNamespace *binding = context.lookupType(q->base(), enclosingScope)) {
        const QList<Symbol *> symbols = binding->symbols();
        for (Symbol *s : symbols) {
            if (Class *matchingClass = s->asClass())
                return matchingClass;
        }
    }

    return nullptr;
}

CPlusPlus::Namespace *isNamespaceFunction(
    const CPlusPlus::LookupContext &context, CPlusPlus::Function *function)
{
    QTC_ASSERT(function, return nullptr);
    if (isMemberFunction(context, function))
        return nullptr;

    Scope *enclosingScope = function->enclosingScope();
    while (!(enclosingScope->asNamespace() || enclosingScope->asClass()))
        enclosingScope = enclosingScope->enclosingScope();
    QTC_ASSERT(enclosingScope != nullptr, return nullptr);

    const Name *functionName = function->name();
    if (!functionName)
        return nullptr;

    // global namespace
    if (!functionName->asQualifiedNameId()) {
        const QList<Symbol *> symbols = context.globalNamespace()->symbols();
        for (Symbol *s : symbols) {
            if (Namespace *matchingNamespace = s->asNamespace())
                return matchingNamespace;
        }
        return nullptr;
    }

    const QualifiedNameId *q = functionName->asQualifiedNameId();
    if (!q->base())
        return nullptr;

    if (ClassOrNamespace *binding = context.lookupType(q->base(), enclosingScope)) {
        const QList<Symbol *> symbols = binding->symbols();
        for (Symbol *s : symbols) {
            if (Namespace *matchingNamespace = s->asNamespace())
                return matchingNamespace;
        }
    }

    return nullptr;
}

QString nameString(const CPlusPlus::NameAST *name)
{
    return CppCodeStyleSettings::currentProjectCodeStyleOverview().prettyName(name->name);
}

CPlusPlus::FullySpecifiedType typeOfExpr(
    const ExpressionAST *expr,
    const CppRefactoringFilePtr &file,
    const Snapshot &snapshot,
    const LookupContext &context)
{
    TypeOfExpression typeOfExpression;
    typeOfExpression.init(file->cppDocument(), snapshot, context.bindings());
    Scope *scope = file->scopeAt(expr->firstToken());
    const QList<LookupItem> result
        = typeOfExpression(file->textOf(expr).toUtf8(), scope, TypeOfExpression::Preprocess);
    if (result.isEmpty())
        return {};

    SubstitutionEnvironment env;
    env.setContext(context);
    env.switchScope(result.first().scope());
    ClassOrNamespace *con = typeOfExpression.context().lookupType(scope);
    if (!con)
        con = typeOfExpression.context().globalNamespace();
    UseMinimalNames q(con);
    env.enter(&q);

    Control *control = context.bindings()->control().get();
    return rewriteType(result.first().type(), &env, control);
}

const QStringList magicQObjectFunctions()
{
    static QStringList list{"metaObject", "qt_metacast", "qt_metacall", "qt_static_metacall"};
    return list;
}

} // namespace CppEditor::Internal
