// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"
#include "cppworkingcopy.h"

#include <utils/filepath.h>
#include <utils/link.h>

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

// Drops what was kept for \a filePath, for when its parser lets go of its
// resources. Only the last few files parsed are kept in any case: a model
// holds a document per file in the include closure, and one per file ever
// edited is how a session runs out of memory.
void forgetCxxFrontendModel(const Utils::FilePath &filePath);

// Where the name at a position was declared, as a link the editor can follow.
// The first consumer, and a small one on purpose: a link is a file and a
// place, which is the whole of what the model has to produce.
//
// \a line is one-based and \a column zero-based, the way the editor counts.
// \a linkTextStart and \a linkTextEnd are the extent of the name in the
// document, which is what gets underlined; the model does not work them out,
// the caller already has them.
//
// An invalid link means the model has nothing to say -- it was never run over
// this file, the name is one of the things it cannot resolve
// (CxxFrontendSnapshot::unsupportedLookups()), or it has only a declaration
// of what the name means and not the definition someone following it wants.
// The caller then answers the way it did before, so this can only add
// answers, never change one.
Utils::Link cxxFrontendFollowSymbol(const Utils::FilePath &filePath, int line, int column,
                                    int linkTextStart, int linkTextEnd);

} // namespace CppEditor::Internal
