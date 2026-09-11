// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../cpprefactoringchanges.h"

#include <QStringList>

namespace CPlusPlus { class CxxFrontendDocument; }

namespace CppEditor::Internal {
class CppQuickFixInterface;

#ifdef QTC_WITH_CXX_FRONTEND
// What every model path begins with: the file the cursor is in, as the
// cxx-frontend model read it, or nothing where it has not read it -- the
// model is off unless asked for. See cxxfrontendmodel.h.
const CPlusPlus::CxxFrontendDocument *cxxFrontendDocumentFor(
    const CppQuickFixInterface &interface);
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
