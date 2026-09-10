// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendAst.h"

#include "CxxFrontendDocument.h"

#include <cxx/ast.h>
#include <cxx/ast_slot.h>
#include <cxx/preprocessor.h>
#include <cxx/translation_unit.h>

namespace CPlusPlus {

namespace {

// Whether the token at \a location is one this file wrote.
//
// A translation unit holds more than the file: the front end's own
// declarations come first, and every header is read in where it is included.
// A position is a line and a column in one file, so a token from anywhere
// else cannot be at it -- line 3 of a header is not line 3 here.
bool isFromThisFile(cxx::TranslationUnit *unit, const QString &fileName,
                    cxx::SourceLocation location)
{
    const std::string name
        = unit->preprocessor()->sourceFileName(unit->tokenAt(location).fileId());
    return name.empty() || QString::fromStdString(name) == fileName;
}

// The first and the last token of \a node that this file wrote. Both edges
// move inwards past anything else: a macro's body, which nobody wrote where
// it stands, and the text of another file. ASTPath applies the same rule to
// generated tokens, and for the same reason -- a position can only be in
// what is on the screen.
//
// The node's locations are a half-open range, as cxx keeps them:
// lastSourceLocation() is the one after the node.
struct WrittenTokens
{
    cxx::SourceLocation first;
    cxx::SourceLocation last;

    explicit operator bool() const { return first && last; }
};

WrittenTokens writtenTokensOf(cxx::TranslationUnit *unit, const QString &fileName,
                              cxx::AST *node)
{
    const cxx::SourceLocation begin = node->firstSourceLocation();
    const cxx::SourceLocation end = node->lastSourceLocation();
    if (!begin || !end || end.index() <= begin.index())
        return {};

    const auto isWritten = [&](unsigned index) {
        const cxx::SourceLocation location{index};
        return !unit->tokenAt(location).macroGenerated()
               && isFromThisFile(unit, fileName, location);
    };

    unsigned first = begin.index();
    const unsigned end_ = end.index();
    while (first < end_ && !isWritten(first))
        ++first;
    if (first >= end_)
        return {};

    unsigned last = end_ - 1;
    while (last > first && !isWritten(last))
        --last;

    return {cxx::SourceLocation{first}, cxx::SourceLocation{last}};
}

// Whether \a line and \a column, counted from one, lie in the text of \a
// node, its first and last character included.
bool contains(cxx::TranslationUnit *unit, const WrittenTokens &tokens, int line, int column)
{
    const cxx::SourcePosition start = unit->tokenStartPosition(tokens.first);
    if (line < int(start.line) || (line == int(start.line) && column < int(start.column)))
        return false;

    const cxx::SourcePosition end = unit->tokenEndPosition(tokens.last);
    return line < int(end.line) || (line == int(end.line) && column <= int(end.column));
}

// The children of \a node, in the order they are written. ASTCursor walks the
// whole tree; a path needs one node's children, so that only the ones the
// position is in are descended into.
QList<cxx::AST *> childrenOf(cxx::AST *node)
{
    QList<cxx::AST *> children;
    // Not called "slots": Qt's keyword macro would eat the name.
    cxx::ASTSlot slotOf;
    const int slotCount = slotOf(node, 0).slotCount;
    for (int i = 0; i < slotCount; ++i) {
        const cxx::ASTSlot::SlotInfo slot = slotOf(node, i);
        if (slot.kind == cxx::ASTSlotKind::kNode) {
            if (auto *child = reinterpret_cast<cxx::AST *>(slot.handle))
                children.append(child);
        } else if (slot.kind == cxx::ASTSlotKind::kNodeList) {
            for (auto *it = reinterpret_cast<cxx::List<cxx::AST *> *>(slot.handle); it;
                 it = it->next) {
                if (it->value)
                    children.append(it->value);
            }
        }
    }
    return children;
}

} // namespace

QList<cxx::AST *> cxxAstPathAt(const CxxFrontendDocument &document, int line, int column)
{
    cxx::TranslationUnit *unit = document.translationUnit();
    if (!unit || !unit->ast())
        return {};
    const QString fileName = document.fileName();

    QList<cxx::AST *> path;

    // Down from the root, into the one child the position is in. Two children
    // cannot both contain it -- what they hold is written one after another --
    // so the first that does is the way down.
    cxx::AST *node = unit->ast();
    while (node) {
        const WrittenTokens tokens = writtenTokensOf(unit, fileName, node);
        if (!tokens || !contains(unit, tokens, line, column))
            break;

        path.append(node);

        cxx::AST *next = nullptr;
        for (cxx::AST *child : childrenOf(node)) {
            const WrittenTokens childTokens = writtenTokensOf(unit, fileName, child);
            if (childTokens && contains(unit, childTokens, line, column)) {
                next = child;
                break;
            }
        }
        node = next;
    }

    return path;
}

CxxAstRange cxxAstRangeOf(const CxxFrontendDocument &document, cxx::AST *node)
{
    cxx::TranslationUnit *unit = document.translationUnit();
    if (!unit || !node)
        return {};

    const WrittenTokens tokens = writtenTokensOf(unit, document.fileName(), node);
    if (!tokens)
        return {};

    const cxx::SourcePosition start = unit->tokenStartPosition(tokens.first);
    const cxx::SourcePosition end = unit->tokenEndPosition(tokens.last);
    return {int(start.line), int(start.column), int(end.line), int(end.column)};
}

} // namespace CPlusPlus
