// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "doxygengenerator.h"

#include <cplusplus/CppDocument.h>
#include <cplusplus/Overview.h>
#include <cplusplus/SimpleLexer.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendDocument.h>

#include <cxx/ast.h>
#include <cxx/names.h>
#include <cxx/symbols.h>
#include <cxx/translation_unit.h>
#include <cxx/types.h>
#endif

#include <utils/textutils.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace CPlusPlus;

namespace CppEditor::Internal {

DoxygenGenerator::DoxygenGenerator() = default;

namespace {

// The declaration, read off the built-in tree.
DoxygenGenerator::DeclarationFacts builtinFactsOf(DeclarationAST *decl)
{
    using Facts = DoxygenGenerator::DeclarationFacts;

    if (const TemplateDeclarationAST * const templDecl = decl->asTemplateDeclaration();
            templDecl && templDecl->declaration) {
        decl = templDecl->declaration;
    }

    SpecifierAST *spec = nullptr;
    DeclaratorAST *decltr = nullptr;
    if (SimpleDeclarationAST *simpleDecl = decl->asSimpleDeclaration()) {
        if (simpleDecl->declarator_list
                && simpleDecl->declarator_list->value) {
            decltr = simpleDecl->declarator_list->value;
        } else if (simpleDecl->decl_specifier_list
                   && simpleDecl->decl_specifier_list->value) {
            spec = simpleDecl->decl_specifier_list->value;
        }
    } else if (FunctionDefinitionAST * defDecl = decl->asFunctionDefinition()) {
        decltr = defDecl->declarator;
    }

    Overview printer;
    Facts facts;

    if (decltr
            && decltr->core_declarator
            && decltr->core_declarator->asDeclaratorId()
            && decltr->core_declarator->asDeclaratorId()->name) {
        NameAST * const nameAst = decltr->core_declarator->asDeclaratorId()->name;
        facts.kind = Facts::Declarator;
        facts.name = printer.prettyName(nameAst->name);

        if (decltr->postfix_declarator_list
                && decltr->postfix_declarator_list->value
                && decltr->postfix_declarator_list->value->asFunctionDeclarator()) {
            FunctionDeclaratorAST *funcDecltr =
                    decltr->postfix_declarator_list->value->asFunctionDeclarator();
            if (funcDecltr->parameter_declaration_clause
                    && funcDecltr->parameter_declaration_clause->parameter_declaration_list) {
                for (ParameterDeclarationListAST *it =
                        funcDecltr->parameter_declaration_clause->parameter_declaration_list;
                     it;
                     it = it->next) {
                    ParameterDeclarationAST *paramDecl = it->value;
                    if (paramDecl->declarator
                            && paramDecl->declarator->core_declarator
                            && paramDecl->declarator->core_declarator->asDeclaratorId()
                            && paramDecl->declarator->core_declarator->asDeclaratorId()->name) {
                        DeclaratorIdAST *paramId =
                                paramDecl->declarator->core_declarator->asDeclaratorId();
                        facts.parameters.append(printer.prettyName(paramId->name->name));
                    }
                }
            }
            // A destructor returns nothing, whatever the front end made of
            // the declaration on its own: read out of the class it belongs
            // to, "~C();" is a declaration with no type written in it at all,
            // and the implicit int of an old C rule is not something to
            // document.
            facts.returnsSomething = !nameAst->asDestructorName()
                    && funcDecltr->symbol
                    && funcDecltr->symbol->returnType().type()
                    && !funcDecltr->symbol->returnType()->asVoidType()
                    && !funcDecltr->symbol->returnType()->isUndefinedType();
        }
        return facts;
    }

    if (spec) {
        if (ClassSpecifierAST *classSpec = spec->asClassSpecifier()) {
            if (classSpec->name) {
                facts.kind = Facts::Aggregate;
                facts.name = printer.prettyName(classSpec->name->name);
                if (classSpec->symbol->asClass())
                    facts.aggregate = QLatin1String("class");
                else if (classSpec->symbol->isStruct())
                    facts.aggregate = QLatin1String("struct");
                else
                    facts.aggregate = QLatin1String("union");
            }
        } else if (EnumSpecifierAST *enumSpec = spec->asEnumSpecifier()) {
            if (enumSpec->name) {
                facts.kind = Facts::Aggregate;
                facts.name = printer.prettyName(enumSpec->name->name);
                facts.aggregate = QLatin1String("enum");
            }
        }
    }

    return facts;
}

#ifdef QTC_WITH_CXX_FRONTEND

// The name a declarator is written under, as it is written. Nothing for the
// names this does not spell out -- an operator, a conversion function -- so
// that the built-in front end answers for those rather than this guessing.
std::optional<QString> cxxNameOf(cxx::UnqualifiedIdAST *id)
{
    if (auto * const name = dynamic_cast<cxx::NameIdAST *>(id)) {
        if (!name->identifier)
            return {};
        return QString::fromStdString(name->identifier->name());
    }

    if (auto * const destructor = dynamic_cast<cxx::DestructorIdAST *>(id)) {
        const std::optional<QString> name = cxxNameOf(destructor->id);
        if (!name)
            return {};
        return QString('~') + *name;
    }

    return {};
}

bool returnsSomething(cxx::Symbol *symbol)
{
    if (!symbol)
        return false;
    auto * const function = cxx::type_cast<cxx::FunctionType>(symbol->type());
    if (!function)
        return false;

    const cxx::Type * const returnType = function->returnType();
    return returnType && !cxx::type_cast<cxx::VoidType>(returnType);
}

// The declaration, read off the cxx-frontend model's tree.
//
// The text handed in is a fragment of a file, and this front end reads a file
// -- so what it makes of a fragment that is not one on its own is nothing to
// go by. A macro standing in front of a declaration is such a fragment: the
// name is one nothing declares here. Whatever it stumbled over, or wrote a
// name this cannot spell out, is handed back for the built-in front end to
// read, which preprocesses the fragment first.
std::optional<DoxygenGenerator::DeclarationFacts> cxxFactsOf(const QString &declaration)
{
    using Facts = DoxygenGenerator::DeclarationFacts;

    const CxxFrontendDocument document(declaration, "<doxygen>");
    for (const CxxFrontendDocument::Diagnostic &diagnostic : document.diagnostics()) {
        if (diagnostic.isError)
            return {};
    }

    cxx::TranslationUnit * const unit = document.translationUnit();
    auto * const root = unit ? dynamic_cast<cxx::TranslationUnitAST *>(unit->ast()) : nullptr;
    if (!root)
        return {};

    // The first declaration the text itself writes. A translation unit begins
    // with the front end's own declarations -- __builtin_constant_p and the
    // rest -- and those are written nowhere, which is what tells them apart.
    cxx::DeclarationAST *declared = nullptr;
    for (auto *it = root->declarationList; it && !declared; it = it->next) {
        if (it->value && cxxAstRangeOf(document, it->value).isValid())
            declared = it->value;
    }
    if (!declared)
        return {};
    if (auto * const templated = dynamic_cast<cxx::TemplateDeclarationAST *>(declared);
        templated && templated->declaration) {
        declared = templated->declaration;
    }

    cxx::DeclaratorAST *declarator = nullptr;
    cxx::Symbol *symbol = nullptr;
    cxx::List<cxx::SpecifierAST *> *specifiers = nullptr;
    if (auto * const simple = dynamic_cast<cxx::SimpleDeclarationAST *>(declared)) {
        if (simple->initDeclaratorList && simple->initDeclaratorList->value) {
            declarator = simple->initDeclaratorList->value->declarator;
            symbol = simple->initDeclaratorList->value->symbol;
        } else {
            specifiers = simple->declSpecifierList;
        }
    } else if (auto * const definition = dynamic_cast<cxx::FunctionDefinitionAST *>(declared)) {
        declarator = definition->declarator;
        symbol = definition->symbol;
    }

    Facts facts;

    if (declarator) {
        auto * const id = dynamic_cast<cxx::IdDeclaratorAST *>(declarator->coreDeclarator);
        if (!id)
            return {};
        const std::optional<QString> name = cxxNameOf(id->unqualifiedId);
        if (!name)
            return {};

        facts.kind = Facts::Declarator;
        facts.name = *name;

        // A function's parameters and whether it returns anything. The first
        // chunk of the declarator is the one written directly under the name,
        // as the built-in front end's first postfix declarator is.
        if (declarator->declaratorChunkList) {
            if (auto * const chunk = dynamic_cast<cxx::FunctionDeclaratorChunkAST *>(
                    declarator->declaratorChunkList->value)) {
                if (chunk->parameterDeclarationClause) {
                    for (auto *it = chunk->parameterDeclarationClause->parameterDeclarationList;
                         it; it = it->next) {
                        if (it->value && it->value->identifier) {
                            facts.parameters.append(
                                QString::fromStdString(it->value->identifier->name()));
                        }
                    }
                }
                facts.returnsSomething = returnsSomething(symbol);
            }
        }

        return facts;
    }

    if (specifiers && specifiers->value) {
        if (auto * const classSpecifier
            = dynamic_cast<cxx::ClassSpecifierAST *>(specifiers->value)) {
            const std::optional<QString> name = cxxNameOf(classSpecifier->unqualifiedId);
            if (name) {
                facts.kind = Facts::Aggregate;
                facts.name = *name;
                facts.aggregate = QString::fromUtf8(cxx::Token::spell(classSpecifier->classKey));
            }
        } else if (auto * const enumSpecifier
                   = dynamic_cast<cxx::EnumSpecifierAST *>(specifiers->value)) {
            const std::optional<QString> name = cxxNameOf(enumSpecifier->unqualifiedId);
            if (name) {
                facts.kind = Facts::Aggregate;
                facts.name = *name;
                facts.aggregate = QLatin1String("enum");
            }
        }
    }

    return facts;
}

#endif // QTC_WITH_CXX_FRONTEND

} // namespace

QString DoxygenGenerator::generate(QTextCursor cursor,
                                   const CPlusPlus::Snapshot &snapshot,
                                   const Utils::FilePath &documentFilePath)
{
    const QChar &c = cursor.document()->characterAt(cursor.position());
    if (!c.isLetter() && c != QLatin1Char('_') && c != QLatin1Char('[') && c != QLatin1Char('~'))
        return QString();

    // Try to find what would be the declaration we are interested in.
    SimpleLexer lexer;
    QTextBlock block = cursor.block();
    while (block.isValid()) {
        const QString &text = block.text();
        const Tokens &tks = lexer(text);
        for (const Token &tk : tks) {
            if (tk.is(T_SEMICOLON)) {
                // No need to continue beyond this, we might already have something meaningful.
                cursor.setPosition(block.position() + tk.utf16charsEnd(), QTextCursor::KeepAnchor);
                break;
            }
        }

        if (cursor.hasSelection())
            break;

        block = block.next();
    }

    // For the edge case of no semicolons at all, which can e.g. happen if the file
    // consists only of empty function definitions.
    if (!cursor.hasSelection())
        cursor.setPosition(cursor.document()->characterCount() - 1, QTextCursor::KeepAnchor);

    if (!cursor.hasSelection())
        return QString();

    QString declCandidate = cursor.selectedText();

    // remove attributes like [[nodiscard]] because
    // Document::Ptr::parse(Document::ParseDeclaration) fails on attributes
    static const QRegularExpression attribute("\\[\\s*\\[.*\\]\\s*\\]");
    declCandidate.replace(attribute, "");

    declCandidate.replace("Q_INVOKABLE", "");
    static const QRegularExpression accessSpecifier(R"(\s*(public|protected|private)\s*:\s*)");
    declCandidate.remove(accessSpecifier);
    declCandidate.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));

    // Let's append a closing brace in the case we got content like 'class MyType {'
    if (declCandidate.endsWith(QLatin1Char('{')))
        declCandidate.append(QLatin1Char('}'));

#ifdef QTC_WITH_CXX_FRONTEND
    if (cxxFrontendModelRequested()) {
        if (const std::optional<DeclarationFacts> facts = cxxFactsOf(declCandidate))
            return write(cursor, *facts);
    }
#endif

    Document::Ptr doc = snapshot.preprocessedDocument(declCandidate.toUtf8(),
                                                      documentFilePath,
                                                      true,
                                                      cursor.blockNumber());
    doc->parse(Document::ParseDeclaration);
    doc->check(Document::FastCheck);

    if (!doc->translationUnit()
            || !doc->translationUnit()->ast()
            || !doc->translationUnit()->ast()->asDeclaration()) {
        return QString();
    }

    return write(cursor, builtinFactsOf(doc->translationUnit()->ast()->asDeclaration()));
}


QString DoxygenGenerator::write(QTextCursor cursor, const DeclarationFacts &facts)
{
    assignCommentOffset(cursor);

    QString comment;
    writeNewLine(&comment);
    writeContinuation(&comment);

    if (facts.kind == DeclarationFacts::Declarator) {
        if (m_settings.generateBrief)
            writeBrief(&comment, facts.name);
        else
            writeNewLine(&comment);

        for (const QString &parameter : facts.parameters) {
            writeContinuation(&comment);
            writeCommand(&comment, ParamCommand, parameter);
        }

        if (facts.returnsSomething) {
            writeContinuation(&comment);
            writeCommand(&comment, ReturnCommand);
        }
    } else if (facts.kind == DeclarationFacts::Aggregate && m_settings.generateBrief) {
        writeBrief(&comment, facts.name, QLatin1String("The"), facts.aggregate);
    } else {
        writeNewLine(&comment);
    }

    writeEnd(&comment);

    return comment;
}

QChar DoxygenGenerator::styleMark() const
{
    switch (m_settings.commandPrefix) {
    case TextEditor::CommentsSettings::CommandPrefix::At: return '@';
    case TextEditor::CommentsSettings::CommandPrefix::Backslash: return '\\';
    case TextEditor::CommentsSettings::CommandPrefix::Auto: break;
    }

    if (m_style == QtStyle || m_style == CppStyleA || m_style == CppStyleB)
        return QLatin1Char('\\');
    return QLatin1Char('@');
}

QString DoxygenGenerator::commandSpelling(Command command)
{
    if (command == ParamCommand)
        return QLatin1String("param ");
    if (command == ReturnCommand)
        return QLatin1String("return ");

    QTC_ASSERT(command == BriefCommand, return QString());
    return QLatin1String("brief ");
}

void DoxygenGenerator::writeEnd(QString *comment) const
{
    if (m_style == CppStyleA)
        comment->append(QLatin1String("///"));
    else if (m_style == CppStyleB)
        comment->append(QLatin1String("//!"));
    else
        comment->append(offsetString() + " */");
}

void DoxygenGenerator::writeContinuation(QString *comment) const
{
    if (m_style == CppStyleA)
        comment->append(offsetString() + "///");
    else if (m_style == CppStyleB)
        comment->append(offsetString() + "//!");
    else if (m_settings.leadingAsterisks)
        comment->append(offsetString() + " *");
    else
        comment->append(offsetString() + "  ");
}

void DoxygenGenerator::writeNewLine(QString *comment) const
{
    comment->append(QLatin1Char('\n'));
}

void DoxygenGenerator::writeCommand(QString *comment,
                                    Command command,
                                    const QString &commandContent) const
{
    comment->append(' ' + styleMark() + commandSpelling(command) + commandContent + '\n');
}

void DoxygenGenerator::writeBrief(QString *comment,
                                  const QString &brief,
                                  const QString &prefix,
                                  const QString &suffix)
{
    QString content = prefix + ' ' + brief + ' ' + suffix;
    writeCommand(comment, BriefCommand, content.trimmed());
}

void DoxygenGenerator::assignCommentOffset(QTextCursor cursor)
{
    if (cursor.hasSelection()) {
        if (cursor.anchor() < cursor.position())
            cursor.setPosition(cursor.anchor());
    }

    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    m_commentOffset = cursor.selectedText();
}

QString DoxygenGenerator::offsetString() const
{
    return m_commentOffset;
}

} // namespace CppEditor::Internal
