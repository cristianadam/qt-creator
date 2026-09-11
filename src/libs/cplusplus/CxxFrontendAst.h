// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cxx/ast_fwd.h>
#include <cxx/source_location.h>

#include <QList>
#include <QString>

// Where cxx's own syntax tree begins.
//
// Everything else in this library answers questions -- what is declared here,
// what this name means, what could be written at this position -- and hands
// back plain data, so that a caller needs nothing of cxx and can be compiled
// like the rest of Qt Creator. That works because a question has one answer.
//
// The rest of the editor's C++ work is not like that. The quick fixes, the
// decl/def link and expanding a selection are about the shape of the code:
// which construct the cursor is in, what its parts are, where each part
// begins and ends. There is no small answer to hand over -- what they need is
// the tree -- so they read cxx's tree directly, and whatever includes this
// header must be compiled as C++23, which cxx's headers require.

namespace CPlusPlus {

class CxxFrontendDocument;

// The nodes a position is inside, outermost first, ending with the innermost
// one -- what ASTPath answers on the built-in model.
//
// \a line and \a column are counted from one. A node is on the path when the
// position is from the start of its first token to the end of its last one,
// both included: a cursor sits between characters, and at either edge of a
// node it is still in it. Tokens a macro wrote are not counted as edges,
// since a place nobody wrote is a place no cursor can be in.
//
// Which is why the path is a list and not a chain: a position between two
// nodes is in both, and both are here, in the order they are written. So a
// node's neighbour on the list is usually its child, and where the cursor sat
// at a boundary it is a sibling of it instead -- the cursor after a name is
// both in that name and at the start of whatever follows it. ASTPath, being a
// visitor that keeps every node it finds the position in, answers the same way.
//
// Empty where the file did not parse into a tree at all, and where the
// position is outside every node -- the blank line after the last
// declaration, for instance.
//
// \a inFile names which of the files this translation unit read the position
// is in, empty for the document's own. A header is read into the file that
// includes it, so one tree holds both, and line 3 of a header is not line 3
// here -- which is why a position is a file as well as a place. Whoever is
// looking at one file, which is nearly everybody, leaves it empty.
QList<cxx::AST *> cxxAstPathAt(const CxxFrontendDocument &document, int line, int column,
                               const QString &inFile = {});

// The extent of \a node in \a inFile -- the document's own where that is
// empty, as above: the start of its first token there and the end of its
// last, counted from one, with the macro-written edges left out as well.
// Zero lines where the node has no token that file wrote.
struct CxxAstRange
{
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;

    bool isValid() const { return startLine > 0 && endLine > 0; }
};
CxxAstRange cxxAstRangeOf(const CxxFrontendDocument &document, cxx::AST *node,
                          const QString &inFile = {});

// The extent of one token, by the same rule. A node is not the only thing a
// reader rewrites: an operator, a keyword or a brace is a token the tree
// points at rather than a node of its own, and a fix that replaces one needs
// to know where it stands. Zero lines where the token is not that file's --
// what a macro wrote has no place here to rewrite.
CxxAstRange cxxTokenRangeAt(const CxxFrontendDocument &document, cxx::SourceLocation location,
                            const QString &inFile = {});

// Whether the front end stumbled over anything inside \a node: an error
// reported at a position the node covers.
//
// A construct it could not read is not one to rewrite. Recovering from an
// error, the parser makes a tree that no longer matches the text -- a
// statement can come out ending before its semicolon -- and a fix that moves
// text by that tree moves the wrong text. Whoever rewrites code asks this
// first and leaves the construct alone when the answer is true.
bool cxxAstWasReadWithErrors(const CxxFrontendDocument &document, cxx::AST *node);

} // namespace CPlusPlus
