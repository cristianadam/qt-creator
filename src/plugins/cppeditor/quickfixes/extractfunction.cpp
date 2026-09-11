// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extractfunction.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "../insertionpointlocator.h"
#include "cppquickfix.h"
#include "cppquickfixhelpers.h"

#include <coreplugin/icore.h>
#include <cplusplus/CppRewriter.h>
#include <cplusplus/declarationcomments.h>
#include <cplusplus/Overview.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>

#include <functional>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendDocument.h>

#include <cxx/ast.h>
#include <cxx/names.h>
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {
using FunctionNameGetter = std::function<QString()>;

class ExtractFunctionOptions
{
public:
    static bool isValidFunctionName(const QString &name)
    {
        return !name.isEmpty() && isValidIdentifier(name);
    }

    bool hasValidFunctionName() const
    {
        return isValidFunctionName(funcName);
    }

    QString funcName;
    InsertionPointLocator::AccessSpec access = InsertionPointLocator::Public;
};

// What extracting a piece of a function comes down to, as either front end
// reads it: the text that moves, what has to be handed to it and handed
// back, and where the new function goes.
struct ExtractionSite
{
    // The statements the selection covers, which is the text that moves.
    int extractionStart = 0;
    int extractionEnd = 0;

    // The function it is taken out of: where its declaration begins -- the
    // new definition goes in front of that, and in front of its
    // documentation where it has some -- what it is called, and whether it
    // is const, which the extracted function has to be as well.
    QString functionName;
    Utils::Text::Position functionPosition;
    int functionStart = 0;
    bool functionIsConst = false;

    // The class the function is a member of, where it is one: what the
    // definition writes in front of the new function's name, and where the
    // declaration of it goes.
    QString classQualification; // "NS::C::", empty for a free function
    Utils::FilePath classFile;
    int classLine = 0;
    int classColumn = 0;
    bool isMemberFunction = false;

    // What the extracted function takes, in the order it takes them: the
    // name to write at the call, and the declaration as it is written
    // today -- which is kept rather than printed, so that whoever wrote it
    // recognises it.
    //
    // Where something is handed back, it is the first of them and is
    // written as an assignment at the call instead.
    QList<QPair<QString, QString>> relevantDeclarations;
    bool handsBackAValue = false;

    // The type of what is handed back, written twice: for the place the
    // definition goes, and for inside the class where a declaration of it
    // goes. "void" where nothing is handed back.
    QString returnTypeInTheDefinition = "void";
    QString returnTypeInTheClass = "void";

    bool isValid() const { return extractionEnd > extractionStart; }
};

class ExtractFunctionOperation : public CppQuickFixOperation
{
public:
    ExtractFunctionOperation(
        const CppQuickFixInterface &interface,
        const ExtractionSite &site,
        FunctionNameGetter functionNameGetter = {})
        : CppQuickFixOperation(interface)
        , m_site(site)
        , m_functionNameGetter(functionNameGetter)
    {
        setDescription(Tr::tr("Extract Function"));
    }

    void perform() override
    {
        QTC_ASSERT(!m_site.handsBackAValue || !m_site.relevantDeclarations.isEmpty(), return);

        CppRefactoringChanges refactoring(snapshot());
        ExtractFunctionOptions options;
        if (m_functionNameGetter)
            options.funcName = m_functionNameGetter();
        else
            options = getOptions();

        if (!options.hasValidFunctionName())
            return;
        const QString &funcName = options.funcName;

        QString funcDef;
        QString funcDecl; // We generate a declaration only in the case of a member function.
        QString funcCall;

        // Write return type.
        funcDef.append(m_site.returnTypeInTheDefinition + ' ');
        if (m_site.isMemberFunction)
            funcDecl.append(m_site.returnTypeInTheClass + ' ');

        // Write class qualification, if any.
        funcDef.append(m_site.classQualification);

        // Write the extracted function itself and its call.
        funcDef.append(funcName);
        if (m_site.isMemberFunction)
            funcDecl.append(funcName);
        funcCall.append(funcName);
        funcDef.append(QLatin1Char('('));
        if (m_site.isMemberFunction)
            funcDecl.append(QLatin1Char('('));
        funcCall.append(QLatin1Char('('));
        for (int i = m_site.handsBackAValue ? 1 : 0; i < m_site.relevantDeclarations.length(); ++i) {
            QPair<QString, QString> p = m_site.relevantDeclarations.at(i);
            funcCall.append(p.first);
            funcDef.append(p.second);
            if (m_site.isMemberFunction)
                funcDecl.append(p.second);
            if (i < m_site.relevantDeclarations.length() - 1) {
                funcCall.append(QLatin1String(", "));
                funcDef.append(QLatin1String(", "));
                if (m_site.isMemberFunction)
                    funcDecl.append(QLatin1String(", "));
            }
        }
        funcDef.append(QLatin1Char(')'));
        if (m_site.isMemberFunction)
            funcDecl.append(QLatin1Char(')'));
        funcCall.append(QLatin1Char(')'));
        if (m_site.functionIsConst) {
            funcDef.append(QLatin1String(" const"));
            funcDecl.append(QLatin1String(" const"));
        }
        funcDef.append(QLatin1String("\n{\n"));
        QString extract = currentFile()->textOf(m_site.extractionStart, m_site.extractionEnd);
        extract.replace(QChar::ParagraphSeparator, QLatin1String("\n"));
        if (!extract.endsWith(QLatin1Char('\n')) && m_site.handsBackAValue)
            extract.append(QLatin1Char('\n'));
        funcDef.append(extract);
        if (m_site.isMemberFunction)
            funcDecl.append(QLatin1String(";\n"));
        if (m_site.handsBackAValue) {
            funcDef.append(QLatin1String("\nreturn ")
                           + m_site.relevantDeclarations.at(0).first
                           + QLatin1Char(';'));
            funcCall.prepend(m_site.relevantDeclarations.at(0).second + QLatin1String(" = "));
        }
        funcDef.append(QLatin1String("\n}\n\n"));
        funcDef.replace(QChar::ParagraphSeparator, QLatin1String("\n"));
        funcDef.prepend(inlinePrefix(currentFile()->filePath()));
        funcCall.append(QLatin1Char(';'));

        // Do not insert right between the function and an associated comment.
        int position = m_site.functionStart;
        const QList<CommentRange> functionDoc = commentsForDeclaration(
            m_site.functionName, m_site.functionPosition, *currentFile()->document(),
            currentFile()->cppDocument());
        if (!functionDoc.isEmpty())
            position = functionDoc.first().start;

        ChangeSet change;
        change.insert(position, funcDef);
        change.replace(m_site.extractionStart, m_site.extractionEnd, funcCall);
        currentFile()->apply(change);

        // Write declaration, if necessary.
        if (m_site.isMemberFunction) {
            InsertionPointLocator locator(refactoring);
            const InsertionLocation &location = locator.methodDeclarationInClass(
                m_site.classFile, m_site.classLine, m_site.classColumn, options.access);
            CppRefactoringFilePtr declFile = refactoring.cppFile(m_site.classFile);
            declFile->apply(ChangeSet::makeInsert(
                declFile->position(location.line(), location.column()),
                location.prefix() + funcDecl + location.suffix()));
        }
    }

    ExtractFunctionOptions getOptions() const
    {
        QDialog dlg(Core::ICore::dialogParent());
        dlg.setWindowTitle(Tr::tr("Extract Function Refactoring"));
        auto layout = new QFormLayout(&dlg);

        auto funcNameEdit = new FancyLineEdit;
        funcNameEdit->setValidationFunction([](const QString &text) -> Result<> {
            if (ExtractFunctionOptions::isValidFunctionName(text))
                return ResultOk;
            return ResultError(QString());
        });
        layout->addRow(Tr::tr("Function name"), funcNameEdit);

        auto accessCombo = new QComboBox;
        accessCombo->addItem(
            InsertionPointLocator::accessSpecToString(InsertionPointLocator::Public),
            InsertionPointLocator::Public);
        accessCombo->addItem(
            InsertionPointLocator::accessSpecToString(InsertionPointLocator::PublicSlot),
            InsertionPointLocator::PublicSlot);
        accessCombo->addItem(
            InsertionPointLocator::accessSpecToString(InsertionPointLocator::Protected),
            InsertionPointLocator::Protected);
        accessCombo->addItem(
            InsertionPointLocator::accessSpecToString(InsertionPointLocator::ProtectedSlot),
            InsertionPointLocator::ProtectedSlot);
        accessCombo->addItem(
            InsertionPointLocator::accessSpecToString(InsertionPointLocator::Private),
            InsertionPointLocator::Private);
        accessCombo->addItem(
            InsertionPointLocator::accessSpecToString(InsertionPointLocator::PrivateSlot),
            InsertionPointLocator::PrivateSlot);
        layout->addRow(Tr::tr("Access"), accessCombo);

        auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        QPushButton *ok = buttonBox->button(QDialogButtonBox::Ok);
        ok->setEnabled(false);
        QObject::connect(funcNameEdit, &Utils::FancyLineEdit::validChanged,
                         ok, &QPushButton::setEnabled);
        layout->addWidget(buttonBox);

        if (dlg.exec() == QDialog::Accepted) {
            ExtractFunctionOptions options;
            options.funcName = funcNameEdit->text();
            options.access = static_cast<InsertionPointLocator::AccessSpec>(accessCombo->
                                                                            currentData().toInt());
            return options;
        }
        return ExtractFunctionOptions();
    }

    const ExtractionSite m_site;
    FunctionNameGetter m_functionNameGetter;
};

static QPair<QString, QString> assembleDeclarationData(
    const QString &specifiers,
    DeclaratorAST *decltr,
    const CppRefactoringFilePtr &file,
    const Overview &printer)
{
    QTC_ASSERT(decltr, return (QPair<QString, QString>()));
    if (decltr->core_declarator
        && decltr->core_declarator->asDeclaratorId()
        && decltr->core_declarator->asDeclaratorId()->name) {
        QString decltrText = file->textOf(file->startOf(decltr),
                                          file->endOf(decltr->core_declarator));
        if (!decltrText.isEmpty()) {
            const QString &name = printer.prettyName(
                decltr->core_declarator->asDeclaratorId()->name->name);
            QString completeDecl = specifiers;
            if (!decltrText.contains(QLatin1Char(' ')))
                completeDecl.append(QLatin1Char(' ') + decltrText);
            else
                completeDecl.append(decltrText);
            return {name, completeDecl};
        }
    }
    return QPair<QString, QString>();
}

class FunctionExtractionAnalyser : public ASTVisitor
{
public:
    FunctionExtractionAnalyser(TranslationUnit *unit,
                               const int selStart,
                               const int selEnd,
                               const CppRefactoringFilePtr &file,
                               const Overview &printer)
        : ASTVisitor(unit)
        , m_done(false)
        , m_failed(false)
        , m_selStart(selStart)
        , m_selEnd(selEnd)
        , m_extractionStart(0)
        , m_extractionEnd(0)
        , m_file(file)
        , m_printer(printer)
    {}

    bool operator()(FunctionDefinitionAST *refFunDef)
    {
        accept(refFunDef);

        if (!m_failed && m_extractionStart == m_extractionEnd)
            m_failed = true;

        return !m_failed;
    }

    bool preVisit(AST *) override
    {
        return !m_done;
    }

    void statement(StatementAST *stmt)
    {
        if (!stmt)
            return;

        const int stmtStart = m_file->startOf(stmt);
        const int stmtEnd = m_file->endOf(stmt);

        if (stmtStart >= m_selEnd
            || (m_extractionStart && stmtEnd > m_selEnd)) {
            m_done = true;
            return;
        }

        if (stmtStart >= m_selStart && !m_extractionStart)
            m_extractionStart = stmtStart;
        if (stmtEnd > m_extractionEnd && m_extractionStart)
            m_extractionEnd = stmtEnd;

        accept(stmt);
    }

    bool visit(CaseStatementAST *stmt) override
    {
        statement(stmt->statement);
        return false;
    }

    bool visit(CompoundStatementAST *stmt) override
    {
        for (StatementListAST *it = stmt->statement_list; it; it = it->next) {
            statement(it->value);
            if (m_done)
                break;
        }
        return false;
    }

    bool visit(DoStatementAST *stmt) override
    {
        statement(stmt->statement);
        return false;
    }

    bool visit(ForeachStatementAST *stmt) override
    {
        statement(stmt->statement);
        return false;
    }

    bool visit(RangeBasedForStatementAST *stmt) override
    {
        statement(stmt->statement);
        return false;
    }

    bool visit(ForStatementAST *stmt) override
    {
        statement(stmt->initializer);
        if (!m_done)
            statement(stmt->statement);
        return false;
    }

    bool visit(IfStatementAST *stmt) override
    {
        statement(stmt->statement);
        if (!m_done)
            statement(stmt->else_statement);
        return false;
    }

    bool visit(TryBlockStatementAST *stmt) override
    {
        statement(stmt->statement);
        for (CatchClauseListAST *it = stmt->catch_clause_list; it; it = it->next) {
            statement(it->value);
            if (m_done)
                break;
        }
        return false;
    }

    bool visit(WhileStatementAST *stmt) override
    {
        statement(stmt->statement);
        return false;
    }

    bool visit(DeclarationStatementAST *declStmt) override
    {
        // We need to collect the declarations we see before the extraction or even inside it.
        // They might need to be used as either a parameter or return value. Actually, we could
        // still obtain their types from the local uses, but it's good to preserve the original
        // typing style.
        if (declStmt
            && declStmt->declaration
            && declStmt->declaration->asSimpleDeclaration()) {
            SimpleDeclarationAST *simpleDecl = declStmt->declaration->asSimpleDeclaration();
            if (simpleDecl->decl_specifier_list
                && simpleDecl->declarator_list) {
                const QString &specifiers =
                    m_file->textOf(m_file->startOf(simpleDecl),
                                   m_file->endOf(simpleDecl->decl_specifier_list->lastValue()));
                for (DeclaratorListAST *decltrList = simpleDecl->declarator_list;
                     decltrList;
                     decltrList = decltrList->next) {
                    const QPair<QString, QString> p =
                        assembleDeclarationData(specifiers, decltrList->value, m_file, m_printer);
                    if (!p.first.isEmpty())
                        m_knownDecls.insert(p.first, p.second);
                }
            }
        }

        return false;
    }

    bool visit(ReturnStatementAST *) override
    {
        if (m_extractionStart) {
            m_done = true;
            m_failed = true;
        }

        return false;
    }

    bool m_done;
    bool m_failed;
    const int m_selStart;
    const int m_selEnd;
    int m_extractionStart;
    int m_extractionEnd;
    QHash<QString, QString> m_knownDecls;
    CppRefactoringFilePtr m_file;
    const Overview &m_printer;
};

// What the built-in front end says about extracting the selection.
std::optional<ExtractionSite> builtinExtractionSite(const CppQuickFixInterface &interface)
{
    const CppRefactoringFilePtr file = interface.currentFile();
    const QList<AST *> &path = interface.path();

    // The "reference" function, which we will extract from.
    FunctionDefinitionAST *refFuncDef = nullptr;
    for (int i = path.size() - 1; i >= 0; --i) {
        refFuncDef = path.at(i)->asFunctionDefinition();
        if (refFuncDef)
            break;
    }

    if (!refFuncDef
        || !refFuncDef->function_body
        || !refFuncDef->function_body->asCompoundStatement()
        || !refFuncDef->function_body->asCompoundStatement()->statement_list
        || !refFuncDef->symbol
        || !refFuncDef->symbol->name()
        || refFuncDef->symbol->enclosingScope()->asTemplate() /* TODO: Templates... */) {
        return {};
    }

    // Adjust selection ends.
    const QTextCursor cursor = file->cursor();
    int selStart = cursor.selectionStart();
    int selEnd = cursor.selectionEnd();
    if (selStart > selEnd)
        std::swap(selStart, selEnd);

    Overview printer;

    // Analyze the content to be extracted, which consists of determining the statements
    // which are complete and collecting the declarations seen.
    FunctionExtractionAnalyser analyser(interface.semanticInfo().doc->translationUnit(),
                                        selStart, selEnd,
                                        file,
                                        printer);
    if (!analyser(refFuncDef))
        return {};

    // We also need to collect the declarations of the parameters from the reference function.
    QSet<QString> refFuncParams;
    if (refFuncDef->declarator->postfix_declarator_list
        && refFuncDef->declarator->postfix_declarator_list->value
        && refFuncDef->declarator->postfix_declarator_list->value->asFunctionDeclarator()) {
        FunctionDeclaratorAST *funcDecltr =
            refFuncDef->declarator->postfix_declarator_list->value->asFunctionDeclarator();
        if (funcDecltr->parameter_declaration_clause
            && funcDecltr->parameter_declaration_clause->parameter_declaration_list) {
            for (ParameterDeclarationListAST *it =
                 funcDecltr->parameter_declaration_clause->parameter_declaration_list;
                 it;
                 it = it->next) {
                ParameterDeclarationAST *paramDecl = it->value->asParameterDeclaration();
                if (paramDecl->declarator) {
                    const QString &specifiers =
                        file->textOf(file->startOf(paramDecl),
                                     file->endOf(paramDecl->type_specifier_list->lastValue()));
                    const QPair<QString, QString> &p =
                        assembleDeclarationData(specifiers, paramDecl->declarator,
                                                file, printer);
                    if (!p.first.isEmpty()) {
                        analyser.m_knownDecls.insert(p.first, p.second);
                        refFuncParams.insert(p.first);
                    }
                }
            }
        }
    }

    // Identify what would be parameters for the new function and its return value, if any.
    Symbol *funcReturn = nullptr;
    QList<QPair<QString, QString> > relevantDecls;
    const SemanticInfo::LocalUseMap localUses = interface.semanticInfo().localUses;
    for (auto it = localUses.cbegin(), end = localUses.cend(); it != end; ++it) {
        bool usedBeforeExtraction = false;
        bool usedAfterExtraction = false;
        bool usedInsideExtraction = false;
        const QList<SemanticInfo::Use> &uses = it.value();
        for (const SemanticInfo::Use &use : uses) {
            if (use.isInvalid())
                continue;

            const int position = file->position(use.line, use.column);
            if (position < analyser.m_extractionStart)
                usedBeforeExtraction = true;
            else if (position >= analyser.m_extractionEnd)
                usedAfterExtraction = true;
            else
                usedInsideExtraction = true;
        }

        const QString &name = printer.prettyName(it.key()->name());

        if ((usedBeforeExtraction && usedInsideExtraction)
            || (usedInsideExtraction && refFuncParams.contains(name))) {
            QTC_ASSERT(analyser.m_knownDecls.contains(name), return {});
            relevantDecls.push_back({name, analyser.m_knownDecls.value(name)});
        }

        // We assume that the first use of a local corresponds to its declaration.
        if (usedInsideExtraction && usedAfterExtraction && !usedBeforeExtraction) {
            if (!funcReturn) {
                QTC_ASSERT(analyser.m_knownDecls.contains(name), return {});
                // The return, if any, is stored as the first item in the list.
                relevantDecls.push_front({name, analyser.m_knownDecls.value(name)});
                funcReturn = it.key();
            } else {
                // Would require multiple returns. (Unless we do fancy things, as pointed below.)
                return {};
            }
        }
    }

    ExtractionSite site;
    site.extractionStart = analyser.m_extractionStart;
    site.extractionEnd = analyser.m_extractionEnd;
    site.functionStart = file->startOf(refFuncDef);
    site.functionName = printer.prettyName(refFuncDef->symbol->name());
    file->lineAndColumn(file->startOf(refFuncDef->symbol->sourceLocation()),
                        &site.functionPosition.line, &site.functionPosition.column);
    --site.functionPosition.column; // A Text::Position counts its columns from zero.
    site.functionIsConst = refFuncDef->symbol->isConst();
    site.relevantDeclarations = relevantDecls;
    site.handsBackAValue = funcReturn != nullptr;

    // The type of what is handed back. Inside the class it is written as
    // it stands; where the definition goes, each name in it is written
    // with as little in front of it as still finds it from there.
    if (funcReturn) {
        Function * const refFunc = refFuncDef->symbol;
        SubstitutionEnvironment env;
        env.setContext(interface.context());
        env.switchScope(refFunc);
        ClassOrNamespace *targetCoN = interface.context().lookupType(refFunc->enclosingScope());
        if (!targetCoN)
            targetCoN = interface.context().globalNamespace();
        UseMinimalNames subs(targetCoN);
        env.enter(&subs);
        Control * const control = interface.context().bindings()->control().get();
        const Overview definitionPrinter = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        site.returnTypeInTheDefinition
            = definitionPrinter.prettyType(rewriteType(funcReturn->type(), &env, control));
        site.returnTypeInTheClass = definitionPrinter.prettyType(funcReturn->type());
    }

    // The class it is a member of, whose name the definition is written
    // under and whose body a declaration of it goes into.
    if (Class * const matchingClass = isMemberFunction(interface.context(), refFuncDef->symbol)) {
        SubstitutionEnvironment env;
        env.setContext(interface.context());
        env.switchScope(refFuncDef->symbol);
        ClassOrNamespace *targetCoN = interface.context().lookupType(
            refFuncDef->symbol->enclosingScope());
        if (!targetCoN)
            targetCoN = interface.context().globalNamespace();
        UseMinimalNames subs(targetCoN);
        env.enter(&subs);
        Control * const control = interface.context().bindings()->control().get();
        const Overview definitionPrinter = CppCodeStyleSettings::currentProjectCodeStyleOverview();

        const Scope *current = matchingClass;
        QList<const Name *> classes{matchingClass->name()};
        while (current->enclosingScope()->asClass()) {
            current = current->enclosingScope()->asClass();
            classes.prepend(current->name());
        }
        while (current->enclosingScope() && current->enclosingScope()->asNamespace()) {
            current = current->enclosingScope()->asNamespace();
            if (current->name())
                classes.prepend(current->name());
        }
        for (const Name *n : classes) {
            site.classQualification.append(
                definitionPrinter.prettyName(rewriteName(n, &env, control)));
            site.classQualification.append(QLatin1String("::"));
        }

        site.isMemberFunction = true;
        site.classFile = FilePath::fromUtf8(matchingClass->fileName());
        site.classLine = matchingClass->line();
        site.classColumn = matchingClass->column();
    }

    return site;
}

#ifdef QTC_WITH_CXX_FRONTEND

// The statements the selection covers, as the cxx-frontend model reads
// them, and the declarations seen on the way. The rules are the built-in
// analyser's, said over this tree: a statement is taken whole or not at
// all, and a return inside what is taken means there is nothing to offer.
class ModelExtractionAnalyser
{
public:
    ModelExtractionAnalyser(const CxxFrontendDocument &document,
                            const CppRefactoringFilePtr &file, int selStart, int selEnd)
        : m_document(document), m_file(file), m_selStart(selStart), m_selEnd(selEnd)
    {}

    bool operator()(cxx::FunctionDefinitionAST *definition)
    {
        visit(bodyOf(definition->functionBody));
        if (!m_failed && m_extractionStart == m_extractionEnd)
            m_failed = true;
        return !m_failed;
    }

    // What a declaration of each name looks like where it is written
    // today, for the declarations this walk saw.
    QHash<QString, QString> m_knownDecls;
    int m_extractionStart = 0;
    int m_extractionEnd = 0;

    void collectDeclaration(cxx::DeclarationAST *declaration)
    {
        auto * const simple = dynamic_cast<cxx::SimpleDeclarationAST *>(declaration);
        if (!simple || !simple->declSpecifierList || !simple->initDeclaratorList)
            return;
        const QString specifiers = textFromTo(simple->firstSourceLocation(),
                                              endOf(lastOf(simple->declSpecifierList)));
        for (auto *declared : cxx::ListView{simple->initDeclaratorList})
            collectDeclarator(specifiers, declared ? declared->declarator : nullptr);
    }

    // A parameter is declared the same way, with its specifiers written in
    // front of the one declarator rather than of a list of them.
    void collectParameter(cxx::ParameterDeclarationAST *parameter)
    {
        if (!parameter || !parameter->typeSpecifierList)
            return;
        collectDeclarator(textFromTo(parameter->firstSourceLocation(),
                                     endOf(lastOf(parameter->typeSpecifierList))),
                          parameter->declarator);
    }

private:
    template<typename T>
    static cxx::AST *lastOf(cxx::List<T *> *list)
    {
        cxx::AST *last = nullptr;
        for (auto *value : cxx::ListView{list}) {
            if (value)
                last = value;
        }
        return last;
    }

    static cxx::StatementAST *bodyOf(cxx::FunctionBodyAST *body)
    {
        auto * const compound = dynamic_cast<cxx::CompoundStatementFunctionBodyAST *>(body);
        return compound ? compound->statement : nullptr;
    }

    static cxx::SourceLocation endOf(cxx::AST *node)
    {
        return node ? node->lastSourceLocation() : cxx::SourceLocation{};
    }

    int startOf(const CxxAstRange &range) const
    {
        return m_file->position(range.startLine, range.startColumn);
    }

    int endOf(const CxxAstRange &range) const
    {
        return m_file->position(range.endLine, range.endColumn);
    }

    QString textFromTo(cxx::SourceLocation first, cxx::SourceLocation last) const
    {
        const CxxAstRange start = cxxTokenRangeAt(m_document, first);
        if (!start.isValid() || !last || last.index() == 0)
            return {};
        // A node's last location is the one after it, as cxx keeps them.
        const CxxAstRange end = cxxTokenRangeAt(m_document, cxx::SourceLocation{last.index() - 1});
        if (!end.isValid())
            return {};
        return m_file->textOf(startOf(start), endOf(end));
    }

    void collectDeclarator(const QString &specifiers, cxx::DeclaratorAST *declarator)
    {
        if (specifiers.isEmpty() || !declarator || !declarator->coreDeclarator)
            return;
        auto * const id = dynamic_cast<cxx::IdDeclaratorAST *>(declarator->coreDeclarator);
        if (!id || !id->unqualifiedId)
            return;
        auto * const name = dynamic_cast<cxx::NameIdAST *>(id->unqualifiedId);
        if (!name || !name->identifier)
            return;

        // Up to the core: an initializer or an array extent follows it,
        // and neither belongs in a declaration written somewhere else.
        const QString declaratorText = textFromTo(declarator->firstSourceLocation(),
                                                  endOf(declarator->coreDeclarator));
        if (declaratorText.isEmpty())
            return;
        QString completeDecl = specifiers;
        if (!declaratorText.contains(QLatin1Char(' ')))
            completeDecl.append(QLatin1Char(' ') + declaratorText);
        else
            completeDecl.append(declaratorText);
        m_knownDecls.insert(QString::fromStdString(name->identifier->name()), completeDecl);
    }

    void statement(cxx::StatementAST *stmt)
    {
        if (!stmt || m_done)
            return;
        const CxxAstRange range = cxxAstRangeOf(m_document, stmt);
        if (!range.isValid())
            return;
        const int stmtStart = startOf(range);
        const int stmtEnd = endOf(range);

        if (stmtStart >= m_selEnd || (m_extractionStart && stmtEnd > m_selEnd)) {
            m_done = true;
            return;
        }

        if (stmtStart >= m_selStart && !m_extractionStart)
            m_extractionStart = stmtStart;
        if (stmtEnd > m_extractionEnd && m_extractionStart)
            m_extractionEnd = stmtEnd;

        visit(stmt);
    }

    void visit(cxx::StatementAST *stmt)
    {
        if (!stmt || m_done)
            return;

        if (auto * const compound = dynamic_cast<cxx::CompoundStatementAST *>(stmt)) {
            for (auto *inner : cxx::ListView{compound->statementList}) {
                statement(inner);
                if (m_done)
                    break;
            }
            return;
        }
        if (auto * const ifStatement = dynamic_cast<cxx::IfStatementAST *>(stmt)) {
            statement(ifStatement->statement);
            if (!m_done)
                statement(ifStatement->elseStatement);
            return;
        }
        if (auto * const loop = dynamic_cast<cxx::WhileStatementAST *>(stmt)) {
            statement(loop->statement);
            return;
        }
        if (auto * const loop = dynamic_cast<cxx::DoStatementAST *>(stmt)) {
            statement(loop->statement);
            return;
        }
        if (auto * const loop = dynamic_cast<cxx::ForStatementAST *>(stmt)) {
            statement(loop->initializer);
            if (!m_done)
                statement(loop->statement);
            return;
        }
        if (auto * const loop = dynamic_cast<cxx::ForRangeStatementAST *>(stmt)) {
            statement(loop->statement);
            return;
        }
        if (auto * const tried = dynamic_cast<cxx::TryBlockStatementAST *>(stmt)) {
            statement(tried->statement);
            for (auto *handler : cxx::ListView{tried->handlerList}) {
                if (handler)
                    statement(handler->statement);
                if (m_done)
                    break;
            }
            return;
        }

        // The declarations seen before the extraction or inside it may be
        // needed as a parameter or as what is handed back, and keeping
        // what somebody wrote is the point of collecting them.
        if (auto * const declaration = dynamic_cast<cxx::DeclarationStatementAST *>(stmt)) {
            collectDeclaration(declaration->declaration);
            return;
        }

        if (dynamic_cast<cxx::ReturnStatementAST *>(stmt)) {
            if (m_extractionStart) {
                m_done = true;
                m_failed = true;
            }
            return;
        }
    }

    const CxxFrontendDocument &m_document;
    const CppRefactoringFilePtr m_file;
    const int m_selStart;
    const int m_selEnd;
    bool m_done = false;
    bool m_failed = false;
};

// What the cxx-frontend model says about extracting the selection.
std::optional<ExtractionSite> modelExtractionSite(const CppQuickFixInterface &interface)
{
    const CxxFrontendDocument * const document = cxxFrontendDocumentFor(interface);
    if (!document)
        return std::nullopt;

    const CppRefactoringFilePtr file = interface.currentFile();
    const QTextCursor cursor = file->cursor();
    int selStart = cursor.selectionStart();
    int selEnd = cursor.selectionEnd();
    if (selStart > selEnd)
        std::swap(selStart, selEnd);

    int line = 0;
    int column = 0;
    file->lineAndColumn(selStart, &line, &column);
    const CxxFrontendDocument::EnclosingFunction function
        = document->enclosingFunctionAt(line, column);
    if (!function.isValid() || function.name.isEmpty())
        return std::nullopt;

    // A function defined inside its class would have the new one written
    // into the class body, and the declaration put there as well would be
    // a second declaration of the same thing. Left to the other model,
    // which is what decides that today.
    if (function.isWrittenInAClass)
        return std::nullopt;

    // The definition itself, which the walk over the statements needs.
    cxx::FunctionDefinitionAST *definition = nullptr;
    for (cxx::AST * const node : cxxAstPathAt(*document, line, column)) {
        if (auto * const found = dynamic_cast<cxx::FunctionDefinitionAST *>(node))
            definition = found;
    }
    if (!definition || !definition->declarator || cxxAstWasReadWithErrors(*document, definition))
        return std::nullopt;

    ModelExtractionAnalyser analyser(*document, file, selStart, selEnd);
    if (!analyser(definition))
        return std::nullopt;

    // The parameters of the function it comes out of are declarations too,
    // and one of them used inside makes it a parameter of the new function
    // as well.
    QSet<QString> referenceParameters;
    for (auto *chunk : cxx::ListView{definition->declarator->declaratorChunkList}) {
        auto * const parameters = dynamic_cast<cxx::FunctionDeclaratorChunkAST *>(chunk);
        if (!parameters || !parameters->parameterDeclarationClause)
            continue;
        for (auto *parameter :
             cxx::ListView{parameters->parameterDeclarationClause->parameterDeclarationList}) {
            if (!parameter || !parameter->identifier)
                continue;
            analyser.collectParameter(parameter);
            referenceParameters.insert(QString::fromStdString(parameter->identifier->name()));
        }
    }

    // What the extracted function takes and what it hands back, read off
    // where each local of this function is written.
    QString returnValue;
    int returnValueLine = 0;
    int returnValueColumn = 0;
    QList<QPair<QString, QString>> relevantDecls;
    for (const CxxFrontendDocument::Local &local : document->localsAt(line, column)) {
        bool usedBeforeExtraction = false;
        bool usedAfterExtraction = false;
        bool usedInsideExtraction = false;
        for (const CxxFrontendDocument::Occurrence &place : local.places) {
            const int position = file->position(place.line, place.column);
            if (position < analyser.m_extractionStart)
                usedBeforeExtraction = true;
            else if (position >= analyser.m_extractionEnd)
                usedAfterExtraction = true;
            else
                usedInsideExtraction = true;
        }

        if ((usedBeforeExtraction && usedInsideExtraction)
            || (usedInsideExtraction && referenceParameters.contains(local.name))) {
            if (!analyser.m_knownDecls.contains(local.name))
                return std::nullopt;
            relevantDecls.push_back({local.name, analyser.m_knownDecls.value(local.name)});
        }

        // We assume that the first use of a local corresponds to its declaration.
        if (usedInsideExtraction && usedAfterExtraction && !usedBeforeExtraction) {
            if (!returnValue.isEmpty())
                return std::nullopt; // Would require multiple returns.
            if (!analyser.m_knownDecls.contains(local.name) || local.places.isEmpty())
                return std::nullopt;
            // The return, if any, is stored as the first item in the list.
            relevantDecls.push_front({local.name, analyser.m_knownDecls.value(local.name)});
            returnValue = local.name;
            returnValueLine = local.places.first().line;
            returnValueColumn = local.places.first().column;
        }
    }

    ExtractionSite site;
    site.extractionStart = analyser.m_extractionStart;
    site.extractionEnd = analyser.m_extractionEnd;
    site.functionStart = file->position(function.definition.startLine,
                                        function.definition.startColumn);
    site.functionName = function.name;
    site.functionPosition = {function.namePlace.line, function.namePlace.column - 1};
    site.functionIsConst = function.isConst;
    site.relevantDeclarations = relevantDecls;
    site.handsBackAValue = !returnValue.isEmpty();

    // The type of what is handed back, written for each of the two places
    // it is written at: in front of the definition's name, which stands
    // outside the class, and in the class, where what the class declares
    // needs nothing in front of it.
    if (site.handsBackAValue) {
        site.returnTypeInTheDefinition = document->typeDeclaredAt(
            returnValueLine, returnValueColumn, {},
            {{}, function.definition.startLine, function.definition.startColumn});
        // Inside the class, where what the class itself declares needs
        // nothing in front of it. Where the declaration's text stands is
        // no answer to that: a member is defined outside its class as a
        // rule, and there the class's own names need their path.
        site.returnTypeInTheClass
            = function.isMemberFunction
                  ? document->typeDeclaredAt(returnValueLine, returnValueColumn, {},
                                             function.classNamePlace)
                  : site.returnTypeInTheDefinition;
        if (site.returnTypeInTheDefinition.isEmpty() || site.returnTypeInTheClass.isEmpty())
            return std::nullopt;
    }

    // What the definition writes in front of its own name is what the new
    // one writes too: nothing is printed, so the author's spelling stands.
    if (function.isMemberFunction) {
        site.isMemberFunction = true;
        site.classFile = function.classNamePlace.filePath.isEmpty()
                             ? interface.filePath()
                             : FilePath::fromUserInput(function.classNamePlace.filePath);
        site.classLine = function.classNamePlace.line;
        site.classColumn = function.classNamePlace.column;
        if (function.writtenQualifier.isValid()) {
            site.classQualification
                = file->textOf(file->position(function.writtenQualifier.startLine,
                                              function.writtenQualifier.startColumn),
                               file->position(function.writtenQualifier.endLine,
                                              function.writtenQualifier.endColumn));
        }
    }

    return site;
}

#endif // QTC_WITH_CXX_FRONTEND

// What extracting the selection comes to, read by whichever front end can
// read it.
std::optional<ExtractionSite> extractionSite(const CppQuickFixInterface &interface)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<ExtractionSite> onTheModel = modelExtractionSite(interface))
        return onTheModel;
#endif
    return builtinExtractionSite(interface);
}

//! Extracts the selected code and puts it to a function
class ExtractFunction : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        // TODO: Fix upstream and uncomment; see QTCREATORBUG-28030.
        //    if (CppModelManager::usesClangd(file->editor()->textDocument())
        //            && file->cppDocument()->languageFeatures().cxxEnabled) {
        //        return;
        //    }

        if (!interface.currentFile()->cursor().hasSelection())
            return;

        const std::optional<ExtractionSite> site = extractionSite(interface);
        if (!site || !site->isValid())
            return;

        // The current implementation doesn't try to be too smart since it preserves the original
        // form of the declarations. This might be or not the desired effect. An improvement would
        // be to let the user somehow customize the function interface.
        FunctionNameGetter nameGetter;
        if (testMode())
            nameGetter = []() { return QLatin1String("extracted"); };
        result << new ExtractFunctionOperation(interface, *site, nameGetter);
    }
};

#ifdef WITH_TESTS
class ExtractFunctionTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerExtractFunctionQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(ExtractFunction);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <extractfunction.moc>
#endif
