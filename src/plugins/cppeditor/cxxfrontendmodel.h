// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppcursorinfo.h"
#include "cppeditor_global.h"
#include "cppworkingcopy.h"
#include "semantichighlighter.h"

#include <cplusplus/CxxFrontendDocument.h>

#include <texteditor/semantichighlighter.h>

#include <utils/filepath.h>
#include <utils/link.h>
#include <utils/utilsicons.h>

#include <memory>
#include <optional>

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
// (CxxFrontendSnapshot::unsupportedLookups()), it has only a declaration of
// what the name means and not the definition someone following it wants, or a
// using declaration brought the name in and the built-in answer for that is
// the using declaration itself.
// The caller then answers the way it did before, so this can only add
// answers, never change one.
Utils::Link cxxFrontendFollowSymbol(const Utils::FilePath &filePath, int line, int column,
                                    int linkTextStart, int linkTextEnd);

// A local variable of a function: its name, whether it is one of the
// function's parameters, the class its type names where it names one, and
// every place the file writes it -- the declaration first, the uses after.
struct CxxFrontendLocal
{
    QString name;
    bool isParameter = false;
    QString className;
    CursorInfo::Ranges places;
};

// The locals of the function written around \a line and \a column, both
// counted from one, as a CursorInfo::Range counts them.
//
// Nothing where the model was never run over this file, and the caller
// answers the way it did before. An answer with no locals in it is an answer:
// the position is outside any function, or the function has none.
//
// A local is the one question a single file settles completely -- a parameter
// or a block variable cannot be named anywhere else -- so what comes back
// here is the whole of what the code says. What it leaves out is what is
// written about the code: a parameter named in the function's documentation
// is highlighted with it, and comments are not in this model's token stream
// at all.
std::optional<QList<CxxFrontendLocal>> cxxFrontendLocalsAt(const Utils::FilePath &filePath,
                                                           int line, int column);

// One entry of what an outline draws: what to write, which icon to write it
// with, where it takes the reader, and where it sits in the tree.
struct CxxFrontendOutlineEntry
{
    QString name;
    QString signature; // a function's parameter list, empty otherwise
    QString valueType; // what follows the colon, empty for a scope
    int line = 0;      // one-based
    int column = 0;    // one-based
    // The entry this one is inside, as an index into the list, or -1 at file
    // scope. An entry always follows the one it is inside.
    int parent = -1;
    Utils::CodeModelIcon::Type icon = Utils::CodeModelIcon::Unknown;
    bool isGenerated = false;
    bool isForwardDeclaration = false;
};

// What \a filePath declares, in the order it declares it, or nothing where
// the model has no such file to read.
//
// A file's own structure is what a single document settles, so this is the
// one question the model answers whole. What it cannot read at all is
// Objective-C, so a file written in it is declined rather than answered with
// the little that parsed.
std::optional<QList<CxxFrontendOutlineEntry>> cxxFrontendOutline(const Utils::FilePath &filePath);

// What the editor colours in \a filePath: every name it writes, with the
// kind that decides the colour, in the order they are written. Nothing
// where the model has no such file.
//
// The macros are not here. The preprocessor reports those and the caller
// merges them in, which is what it does for the built-in model too, so
// this answers for the names and leaves that where it is.
std::optional<QList<TextEditor::HighlightingResult>> cxxFrontendHighlighting(
    const Utils::FilePath &filePath);

// What could be written at \a line and \a column of \a filePath -- both
// counted from one -- with \a source as the text stands in the editor,
// half-written expression and all.
//
// Not read off the last parse, as every other question here is: where the
// question is asked has to be settled before the file is preprocessed, so
// this reads \a source again, headers and all. That is affordable because
// completion already runs on a worker thread, and it is why the answer is
// not kept -- it belongs to one keystroke.
//
// \a builtinSnapshot is the snapshot the completion is running against, and
// includes are resolved through it exactly as updateCxxFrontendModel does,
// so both models read the same headers.
//
// Nothing unless the model was asked for, or where the file cannot be read;
// then the caller answers the way it did before.
std::optional<CPlusPlus::CxxFrontendDocument::Completion> cxxFrontendCompletion(
    const CPlusPlus::Snapshot &builtinSnapshot,
    const Utils::FilePath &filePath,
    const QString &source,
    int line,
    int column);

} // namespace CppEditor::Internal
