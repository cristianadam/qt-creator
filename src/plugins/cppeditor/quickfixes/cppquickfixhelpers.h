// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../cpprefactoringchanges.h"

#include <utils/textutils.h>

#include <QStringList>

namespace CPlusPlus { class CxxFrontendDocument; }
namespace cxx { class DeclaratorAST; }

namespace CppEditor::Internal {
class CppQuickFixInterface;

#ifdef QTC_WITH_CXX_FRONTEND
// What every model path begins with: the file the cursor is in, as the
// cxx-frontend model read it, or nothing where it has not read it -- the
// model is off unless asked for. See cxxfrontendmodel.h.
const CPlusPlus::CxxFrontendDocument *cxxFrontendDocumentFor(
    const CppQuickFixInterface &interface);

// Where the name of whatever \a declarator declares is written, past the
// scopes in front of it and past a destructor's tilde -- which is where
// every front end records the thing it declares, and so the one place a
// reading on one model can be lined up with a reading on the other.
//
// An invalid position where there is no name to point at.
Utils::Text::Position cxxNameOfDeclarator(const CPlusPlus::CxxFrontendDocument &document,
                                          cxx::DeclaratorAST *declarator);

// Whether a definition of what \a declarator declares can be written out of
// what this front end knows, rather than by keeping the text somebody wrote.
//
// Four things say no. Three because the built-in path keeps the written form
// and writing from the type would not: a trailing return type, which is one
// of two ways of saying the same thing; an operator, whose name is spaced the
// way it was written; and a declarator a macro wrote part of, where the text
// says the macro's name and the front end read its replacement. The fourth
// because it would write something *other* than what was written: an
// exception specification with an expression in it, which a type does not
// record -- and a definition that disagrees with its declaration there does
// not compile.
//
// Not asked here, because it is about what the declarator is written inside
// rather than about the declarator: a template, whose "template<...>" a
// definition has to carry.
bool cxxCanWriteADefinitionOf(const CPlusPlus::CxxFrontendDocument &document,
                              cxx::DeclaratorAST *declarator);
#endif

// These are generated functions that should not be offered in quickfixes.
const QStringList magicQObjectFunctions();

// Given include is e.g. "afile.h" or <afile.h> (quotes/angle brackets included!).
void insertNewIncludeDirective(
    const QString &include,
    CppRefactoringFilePtr file,
    const CPlusPlus::Document::Ptr &cppDocument,
    Utils::ChangeSet &changes);

// Returns a non-null value if and only if the cursor is on the name of a (proper) class
// declaration or at some place inside the body of a class declaration that does not
// correspond to an AST of its own, i.e. on "empty space".
CPlusPlus::ClassSpecifierAST *astForClassOperations(const CppQuickFixInterface &interface);

bool nameIncludesOperatorName(const CPlusPlus::Name *name);

QString inlinePrefix(const Utils::FilePath &targetFile,
                     const std::function<bool()> &extraCondition = {});

CPlusPlus::Class *isMemberFunction(
    const CPlusPlus::LookupContext &context, CPlusPlus::Function *function);

CPlusPlus::Namespace *isNamespaceFunction(
    const CPlusPlus::LookupContext &context, CPlusPlus::Function *function);

QString nameString(const CPlusPlus::NameAST *name);

CPlusPlus::FullySpecifiedType typeOfExpr(
    const CPlusPlus::ExpressionAST *expr,
    const CppRefactoringFilePtr &file,
    const CPlusPlus::Snapshot &snapshot,
    const CPlusPlus::LookupContext &context);

} // namespace CppEditor::Internal
