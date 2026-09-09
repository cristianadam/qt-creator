// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"
#include "cppworkingcopy.h"

#include <utils/filepath.h>

#include <memory>

namespace CPlusPlus {
class CxxFrontendSnapshot;
class Snapshot;
}

namespace CppEditor::Internal {

// The cxx-frontend model of the file being edited, kept beside the built-in
// one.
//
// The lexer could be swapped underneath its callers because they all read the
// same tokens. A document cannot: what the built-in model hands out is a tree
// of Symbol pointers, and every consumer walks it. So the way across is to
// run the new model over the files that are open, ask it the questions it can
// answer, and move the consumers one at a time -- and this is where the answers
// live in the meantime.
//
// Whether to keep it at all. Off unless QTC_CXX_FRONTEND_MODEL is set in the
// environment, because it is a second parse of everything the edited file
// includes, on every reparse. The built-in model is untouched either way, and
// nothing reads this yet.
//
// Asked by whoever is about to do the work rather than inside it, so that the
// work can be asked for directly -- which is how the test drives it.
bool cxxFrontendModelRequested();

// Runs \a filePath and everything it includes through the cxx-frontend model,
// and keeps the result until the next call for that file.
//
// Includes are not resolved again: \a builtinSnapshot has just been built for
// this file, and it records which path each include resolved to. Resolving
// them a second way would mean a second answer, and the point of running the
// two models side by side is that they read the same code. Contents come from
// \a workingCopy, so that what is being typed is what is parsed, and from disk
// for everything else.
//
// \a configFile is the #define lines the project part contributes, which the
// built-in model feeds in as a synthetic file.
void updateCxxFrontendModel(const CPlusPlus::Snapshot &builtinSnapshot,
                            const Utils::FilePath &filePath,
                            const QByteArray &configFile,
                            const WorkingCopy &workingCopy);

// What the model made of \a filePath the last time it ran over it, or nothing.
// The snapshot is shared and replaced wholesale by the next run, so hold what
// this returns for as long as the answers are needed.
std::shared_ptr<const CPlusPlus::CxxFrontendSnapshot> cxxFrontendModel(
    const Utils::FilePath &filePath);

} // namespace CppEditor::Internal
