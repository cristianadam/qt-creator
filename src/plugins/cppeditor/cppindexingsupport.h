// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"

#include "cppmodelmanager.h"

#include <QFuture>

#include <functional>

namespace Utils { class SearchResultItem; }

namespace CppEditor::Internal {

// Which of \a all the built-in front end's indexing pass is to read.
//
// All of them where the pass is running, which is \a passRequested. Where it
// is skipped -- the experiment behind QTC_NO_BUILTIN_INDEX_PASS -- the
// Objective-C sources and nothing else: the cxx front end's index declines
// those outright rather than making something wrong of them, so with the
// pass gone as well nothing would describe them at all. No locator entry, no
// class in the Class View, no test found in one.
//
// Headers are not among them either way. Which language a header is read as
// is the including source saying so, and a header of an Objective-C source
// is read with it.
CPPEDITOR_EXPORT QSet<Utils::FilePath> filesTheBuiltinPassReads(
    const QSet<Utils::FilePath> &all, bool passRequested);

enum class SymbolType {
    Classes      = 0x1,
    Functions    = 0x2,
    Enums        = 0x4,
    Declarations = 0x8,
    TypeAliases  = 0x16,
    AllTypes     = Classes | Functions | Enums | Declarations
};
Q_DECLARE_FLAGS(SymbolTypes, SymbolType)
Q_DECLARE_OPERATORS_FOR_FLAGS(SymbolTypes)

enum SearchScope {
    SearchProjectsOnly,
    SearchGlobal
};

struct SearchParameters
{
    QString text;
    Utils::FindFlags flags;
    SymbolTypes types;
    SearchScope scope;
};

CPPEDITOR_EXPORT void searchForSymbols(QPromise<Utils::SearchResultItem> &promise,
                                       const CPlusPlus::Snapshot &snapshot,
                                       const SearchParameters &parameters,
                                       const QSet<Utils::FilePath> &filePaths);

CPPEDITOR_EXPORT bool isFindErrorsIndexingActive();

CPPEDITOR_EXPORT QFuture<void> refreshSourceFiles(
    const std::function<QSet<Utils::FilePath>()> &sourceFiles,
    CppModelManager::ProgressNotificationMode mode);

} // namespace CppEditor::Internal

Q_DECLARE_METATYPE(CppEditor::Internal::SearchScope)
Q_DECLARE_METATYPE(CppEditor::Internal::SearchParameters)
Q_DECLARE_METATYPE(CppEditor::Internal::SymbolTypes)
