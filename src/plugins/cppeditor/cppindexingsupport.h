// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"

#include "cppmodelmanager.h"

#include <utils/futuresynchronizer.h>

#include <QFuture>

#include <functional>

namespace Utils { class SearchResultItem; }

namespace CppEditor::Internal {

enum SymbolType {
    Classes      = 0x1,
    Functions    = 0x2,
    Enums        = 0x4,
    Declarations = 0x8,
    TypeAliases  = 0x16,
};

Q_DECLARE_FLAGS(SymbolTypes, SymbolType)

enum SearchScope {
    SearchProjectsOnly,
    SearchGlobal
};

struct Parameters
{
    QString text;
    Utils::FindFlags flags;
    SymbolTypes types;
    SearchScope scope;
};

CPPEDITOR_EXPORT void search(QPromise<Utils::SearchResultItem> &promise,
                             const CPlusPlus::Snapshot &snapshot,
                             const Parameters &parameters,
                             const QSet<Utils::FilePath> &filePaths);

CPPEDITOR_EXPORT bool isFindErrorsIndexingActive();

CPPEDITOR_EXPORT QFuture<void> refreshSourceFiles(
    const std::function<QSet<Utils::FilePath>()> &sourceFiles,
    CppModelManager::ProgressNotificationMode mode);

} // namespace CppEditor::Internal

Q_DECLARE_METATYPE(CppEditor::Internal::SearchScope)
Q_DECLARE_METATYPE(CppEditor::Internal::Parameters)
Q_DECLARE_METATYPE(CppEditor::Internal::SymbolTypes)
