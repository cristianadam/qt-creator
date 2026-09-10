// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "declarationcomments.h"

#include <cplusplus/ASTPath.h>
#include <cplusplus/CppDocument.h>
#include <cplusplus/Overview.h>

#include <utils/algorithm.h>
#include <utils/textutils.h>

#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextDocument>

namespace CPlusPlus {

static QString nameFromSymbol(const Symbol *symbol)
{
    const QStringList symbolParts = Overview().prettyName(symbol->name())
                                        .split("::", Qt::SkipEmptyParts);
    if (symbolParts.isEmpty())
        return {};
    return symbolParts.last();
}

QList<CommentRange> commentBlockAbove(const QList<PrecedingComment> &comments,
                                      int declarationStart, const QString &symbolName,
                                      bool isParameter, const QTextDocument &textDoc)
{
    if (symbolName.isEmpty() || comments.isEmpty())
        return {};

    // How much of the run belongs together, from the one nearest the
    // declaration upwards: the same kind of comment, and no empty line
    // between one and the next.
    QList<PrecedingComment> block;
    CommentStyle style = comments.last().style;
    bool needsSymbolReference = isParameter;

    for (auto it = comments.crbegin(); it != comments.crend(); ++it) {
        const QTextBlock endBlock = textDoc.findBlock(it->range.end);

        if (block.isEmpty()) {
            // The nearest one. Where it does not end on the line above the
            // declaration, something stands between them, and then the block
            // is only the declaration's if it says the name.
            if (endBlock.next() != textDoc.findBlock(declarationStart))
                needsSymbolReference = true;
            style = it->style;
        } else {
            if (it->style != style)
                break;
            if (endBlock.next() != textDoc.findBlock(block.first().range.start))
                break;
        }

        block.prepend(*it);
    }

    const auto ranges = [&] {
        return Utils::transform<QList<CommentRange>>(block, &PrecedingComment::range);
    };

    if (!needsSymbolReference)
        return ranges();

    // The name has to be written in the block. For a parameter of a
    // documented function, under the command that documents a parameter.
    const bool isDoxygenComment = style == CommentStyle::CStyleDoxygen
                                  || style == CommentStyle::CppStyleDoxygen;
    const QRegularExpression symbolRegExp(QString("%1\\b%2\\b").arg(
        isParameter && isDoxygenComment ? "[\\@]param\\s+" : QString(), symbolName));
    for (const PrecedingComment &comment : std::as_const(block)) {
        const QTextBlock last = textDoc.findBlock(comment.range.end);
        for (QTextBlock b = textDoc.findBlock(comment.range.start);
             b.blockNumber() <= last.blockNumber(); b = b.next()) {
            if (b.text().contains(symbolRegExp))
                return ranges();
        }
    }

    return {};
}

// The comments written directly above \a decl, as the built-in front end's
// token stream has them: the run of comment tokens before the declaration's
// first token, with nothing else in between.
static QList<PrecedingComment> precedingComments(const AST *decl, const QTextDocument &textDoc,
                                                 const Document::Ptr &cppDoc)
{
    TranslationUnit * const tu = cppDoc->translationUnit();
    QTC_ASSERT(tu && tu->isParsed(), return {});
    const Token &declToken = tu->tokenAt(decl->firstToken());
    const std::vector<Token> allTokens = tu->allTokens();
    QTC_ASSERT(!allTokens.empty(), return {});

    int tokenPos = -1;
    for (int i = 0; i < int(allTokens.size()); ++i) {
        if (allTokens.at(i).byteOffset == declToken.byteOffset) {
            tokenPos = i;
            break;
        }
    }
    if (tokenPos == -1)
        return {};

    const auto styleOf = [](const Token &token) {
        switch (token.kind()) {
        case T_CPP_COMMENT: return CommentStyle::CppStyle;
        case T_DOXY_COMMENT: return CommentStyle::CStyleDoxygen;
        case T_CPP_DOXY_COMMENT: return CommentStyle::CppStyleDoxygen;
        default: return CommentStyle::CStyle;
        }
    };

    QList<PrecedingComment> comments;
    for (int i = tokenPos - 1; i >= 0; --i) {
        const Token &token = allTokens.at(i);
        if (!token.isComment())
            break;
        comments.prepend({{tu->getTokenPositionInDocument(token, &textDoc),
                           tu->getTokenEndPositionInDocument(token, &textDoc)},
                          styleOf(token)});
    }

    return comments;
}

static QList<CommentRange> commentsForDeclaration(
    const AST *decl, const QString &symbolName, const QTextDocument &textDoc,
    const Document::Ptr &cppDoc, bool isParameter)
{
    if (symbolName.isEmpty())
        return {};

    TranslationUnit * const tu = cppDoc->translationUnit();
    QTC_ASSERT(tu && tu->isParsed(), return {});
    const int declarationStart
        = tu->getTokenPositionInDocument(tu->tokenAt(decl->firstToken()), &textDoc);

    return commentBlockAbove(precedingComments(decl, textDoc, cppDoc), declarationStart,
                             symbolName, isParameter, textDoc);
}

QList<CommentRange> commentsForDeclaration(const Symbol *symbol, const QTextDocument &textDoc,
                                           const Document::Ptr &cppDoc)
{
    QTC_ASSERT(cppDoc->translationUnit() && cppDoc->translationUnit()->isParsed(), return {});
    Utils::Text::Position pos;
    cppDoc->translationUnit()->getTokenPosition(symbol->sourceLocation(), &pos.line, &pos.column);
    --pos.column;
    return commentsForDeclaration(nameFromSymbol(symbol), pos, textDoc, cppDoc);
}

QList<CommentRange> commentsForDeclaration(const QString &symbolName,
                                           const Utils::Text::Position &pos,
                                           const QTextDocument &textDoc,
                                           const Document::Ptr &cppDoc)
{
    if (symbolName.isEmpty())
        return {};

    // Find the symbol declaration's AST node.
    // We stop at the last declaration node that precedes the symbol, except:
    //  - For parameter declarations, we just continue, because we are interested in the function.
    //  - If the declaration node is preceded directly by another one, we choose that one instead,
    //    because with nested declarations we want the outer one (e.g. templates).
    const QList<AST *> astPath = ASTPath(cppDoc)(pos.line, pos.column + 1);
    if (astPath.isEmpty())
        return {};
    const AST *declAst = nullptr;
    bool isParameter = false;
    for (auto it = std::next(std::rbegin(astPath)); it != std::rend(astPath); ++it) {
        AST * const node = *it;
        if (node->asParameterDeclaration()) {
            isParameter = true;
            continue;
        }
        if (node->asDeclaration()) {
            declAst = node;
            continue;
        }
        if (declAst)
            break;
    }
    if (!declAst)
        return {};

    return commentsForDeclaration(declAst, symbolName, textDoc, cppDoc, isParameter);
}

QList<CommentRange> commentsForDeclaration(const Symbol *symbol, const AST *decl,
                                           const QTextDocument &textDoc,
                                           const Document::Ptr &cppDoc)
{
    return commentsForDeclaration(decl, nameFromSymbol(symbol), textDoc, cppDoc,
                                  symbol->asArgument());
}

} // namespace CPlusPlus
