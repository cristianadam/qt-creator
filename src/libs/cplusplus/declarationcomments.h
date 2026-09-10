// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/CppDocument.h>

#include <QList>

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

namespace Utils { namespace Text { class Position; } }

namespace CPlusPlus {
class AST;
class Symbol;

// Where a comment stands, in the positions a QTextCursor counts. What every
// reader of a declaration's documentation wants of it: the text is theirs to
// read, to move or to search through.
class CommentRange
{
public:
    int start = 0;
    int end = 0;
};

// Which of the two ways a comment is written, and whether it is written for a
// documentation tool. Two comments belong to one block only if they are of
// the same kind, which is the only reason this is asked at all.
enum class CommentStyle {
    CStyle,         // /* ... */
    CppStyle,       // // ...
    CStyleDoxygen,  // /** ... */ or /*! ... */
    CppStyleDoxygen // /// ... or //! ...
};

// A comment written above a declaration with nothing but comments between it
// and the declaration.
class PrecedingComment
{
public:
    CommentRange range;
    CommentStyle style = CommentStyle::CStyle;
};

// Which of \a comments are the documentation of the declaration that begins
// at \a declarationStart, in the order they are written, or none where the
// block does not belong to it.
//
// The rule, and it is a heuristic: a comment block documents a declaration if
// it precedes it directly, without an empty line in between, or if the
// symbol's name occurs in it. Which can of course yield a false positive for
// a very short name, but a symbol important enough to be documented should
// have a proper name. For a parameter the name always has to occur, since a
// parameter is documented in the function's comment rather than above itself.
//
// \a comments is what the front end that read the file found directly above
// the declaration, nearest last; this decides how much of that run belongs
// together -- the same kind of comment, no blank lines -- and whether the
// block is the declaration's at all. None of which is a question about a
// syntax tree, which is why it is asked of plain positions.
QList<CommentRange> CPLUSPLUS_EXPORT commentBlockAbove(const QList<PrecedingComment> &comments,
                                                       int declarationStart,
                                                       const QString &symbolName,
                                                       bool isParameter,
                                                       const QTextDocument &textDoc);

QList<CommentRange> CPLUSPLUS_EXPORT commentsForDeclaration(const Symbol *symbol,
                                                            const QTextDocument &textDoc,
                                                            const Document::Ptr &cppDoc);

QList<CommentRange> CPLUSPLUS_EXPORT commentsForDeclaration(const Symbol *symbol,
                                                            const AST *decl,
                                                            const QTextDocument &textDoc,
                                                            const Document::Ptr &cppDoc);

QList<CommentRange> CPLUSPLUS_EXPORT commentsForDeclaration(const QString &symbolName,
                                                            const Utils::Text::Position &pos,
                                                            const QTextDocument &textDoc,
                                                            const Document::Ptr &cppDoc);

} // namespace CPlusPlus
