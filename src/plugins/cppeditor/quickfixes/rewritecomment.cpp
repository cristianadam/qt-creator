// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "rewritecomment.h"

#include "../cppeditortr.h"
#include "../cppeditorwidget.h"
#include "../cppmodelmanager.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#include <cplusplus/ASTPath.h>
#include <cplusplus/Overview.h>
#include <cplusplus/declarationcomments.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"
#endif
#include <projectexplorer/editorconfiguration.h>
#include <texteditor/tabsettings.h>
#include <texteditor/textdocument.h>
#include <utils/algorithm.h>

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

using namespace CPlusPlus;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

// A comment somebody is standing on: where it stands, and which of the four
// ways it is written. All this fix needs of a front end -- it rewrites the
// text between those two places, and the way it was written decides into
// what.
class WrittenComment
{
public:
    CommentRange range;
    CommentStyle style = CommentStyle::CStyle;
};

class ConvertCommentStyleOp : public CppQuickFixOperation
{
public:
    ConvertCommentStyleOp(const CppQuickFixInterface &interface,
                          const QList<CommentRange> &comments, CommentStyle style)
        : CppQuickFixOperation(interface),
        m_comments(comments),
        m_wasCxxStyle(style == CommentStyle::CppStyle
                      || style == CommentStyle::CppStyleDoxygen),
        m_isDoxygen(style == CommentStyle::CStyleDoxygen
                    || style == CommentStyle::CppStyleDoxygen)
    {
        setDescription(m_wasCxxStyle ? Tr::tr("Convert Comment to C-Style")
                                     : Tr::tr("Convert Comment to C++-Style"));
    }

private:
    // Turns every line of a C-style comment into a C++-style comment and vice versa.
    // For C++ -> C, we use one /* */ comment block per line. However, doxygen
    // requires a single comment, so there we just replace the prefix with whitespace and
    // add the start and end comment in extra lines.
    // For cosmetic reasons, we offer some convenience functionality:
    //   - Turn /***** ... into ////// ... and vice versa
    //   - With C -> C++, remove leading asterisks.
    //   - With C -> C++, remove the first and last line of a block if they have no content
    //     other than the comment start and end characters.
    //   - With C++ -> C, try to align the end comment characters.
    // These are obviously heuristics; we do not guarantee perfect results for everybody.
    // We also don't second-guess the users's selection: E.g. if there is an empty
    // line between the tokens, then it's not the same doxygen comment, but we merge
    // it anyway in C++ to C mode.
    void perform() override
    {
        const QString newCommentStart = getNewCommentStart();
        ChangeSet changeSet;
        int endCommentColumn = -1;
        const QChar oldFillChar = m_wasCxxStyle ? '/' : '*';
        const QChar newFillChar = m_wasCxxStyle ? '*' : '/';

        for (const CommentRange &comment : m_comments) {
            const int startPos = comment.start;
            const int endPos = comment.end;

            if (m_wasCxxStyle && m_isDoxygen) {
                // Replace "///" characters with whitespace (to keep alignment).
                // The insertion of "/*" and "*/" is done once after the loop.
                changeSet.replace(startPos, startPos + 3, "   ");
                continue;
            }

            const QTextBlock firstBlock = textDocument()->findBlock(startPos);
            const QTextBlock lastBlock = textDocument()->findBlock(endPos);
            for (QTextBlock block = firstBlock; block.isValid() && block.position() <= endPos;
                 block = block.next()) {
                const QString &blockText = block.text();
                const int firstColumn = block == firstBlock ? startPos - block.position() : 0;
                const int endColumn = block == lastBlock ? endPos - block.position()
                                                         : block.length();

                // Returns true if the current line looks like "/********/" or "//////////",
                // as is often the case at the start and end of comment blocks.
                const auto fillChecker = [&] {
                    if (m_isDoxygen)
                        return false;
                    QString textToCheck = blockText;
                    if (block == firstBlock)
                        textToCheck.remove(0, 1);
                    if (block == lastBlock)
                        textToCheck.chop(block.length() - endColumn);
                    return Utils::allOf(textToCheck, [oldFillChar](const QChar &c)
                                        { return c == oldFillChar || c == ' ';
                                        }) && textToCheck.count(oldFillChar) > 2;
                };

                // Returns the index of the first character of actual comment content,
                // as opposed to visual stuff like slashes, stars or whitespace.
                const auto indexOfActualContent = [&] {
                    const int offset = block == firstBlock ? firstColumn + newCommentStart.size()
                                                           : firstColumn;

                    for (int i = offset, lastFillChar = -1; i < blockText.size(); ++i) {
                        if (blockText.at(i) == oldFillChar) {
                            lastFillChar = i;
                            continue;
                        }
                        if (!blockText.at(i).isSpace())
                            return lastFillChar + 1;
                    }
                    return -1;
                };

                if (fillChecker()) {
                    const QString replacement = QString(endColumn - 1 - firstColumn, newFillChar);
                    changeSet.replace(block.position() + firstColumn,
                                      block.position() + endColumn - 1,
                                      replacement);
                    if (m_wasCxxStyle) {
                        changeSet.replace(block.position() + firstColumn,
                                          block.position() + firstColumn + 1, "/");
                        changeSet.insert(block.position() + endColumn - 1, "*");
                        endCommentColumn = endColumn - 1;
                    }
                    continue;
                }

                // Remove leading noise or even the entire block, if applicable.
                const bool blockIsRemovable = (block == firstBlock || block == lastBlock)
                                              && firstBlock != lastBlock;
                const auto removeBlock = [&] {
                    changeSet.remove(block.position() + firstColumn, block.position() + endColumn);
                };
                const int contentIndex = indexOfActualContent();
                int removed = 0;
                if (contentIndex == -1) {
                    if (blockIsRemovable) {
                        removeBlock();
                        continue;
                    } else if (!m_wasCxxStyle) {
                        changeSet.replace(block.position() + firstColumn,
                                          block.position() + endColumn - 1, newCommentStart);
                        continue;
                    }
                } else if (block == lastBlock && contentIndex == endColumn - 1) {
                    if (blockIsRemovable) {
                        removeBlock();
                        break;
                    }
                } else {
                    changeSet.remove(block.position() + firstColumn,
                                     block.position() + firstColumn + contentIndex);
                    removed = contentIndex;
                }

                if (block == firstBlock) {
                    changeSet.replace(startPos, startPos + newCommentStart.size(),
                                      newCommentStart);
                } else {
                    // If the line starts with enough whitespace, replace it with the
                    // comment start characters, so we don't move the content to the right
                    // unnecessarily. Otherwise, insert the comment start characters.
                    if (blockText.startsWith(QString(newCommentStart.size() + removed + 1, ' '))) {
                        changeSet.replace(block.position(),
                                          block.position() + newCommentStart.size(),
                                          newCommentStart);
                    } else {
                        changeSet.insert(block.position(), newCommentStart);
                    }
                }

                if (block == lastBlock) {
                    if (m_wasCxxStyle) {
                        // This is for proper alignment of the end comment character.
                        if (endCommentColumn != -1) {
                            const int endCommentPos = block.position() + endCommentColumn;
                            if (endPos < endCommentPos)
                                changeSet.insert(endPos, QString(endCommentPos - endPos - 1, ' '));
                        }
                        changeSet.insert(endPos, " */");
                    } else {
                        changeSet.remove(endPos - 2, endPos);
                    }
                }
            }
        }

        if (m_wasCxxStyle && m_isDoxygen) {
            changeSet.insert(m_comments.first().start, "/*!\n");
            changeSet.insert(m_comments.last().end, "\n*/");
        }

        changeSet.apply(textDocument());
    }

    QString getNewCommentStart() const
    {
        if (m_wasCxxStyle) {
            if (m_isDoxygen)
                return "/*!";
            return "/*";
        }
        if (m_isDoxygen)
            return "//!";
        return "//";
    }

    const QList<CommentRange> m_comments;
    const bool m_wasCxxStyle;
    const bool m_isDoxygen;
};

// The function the cursor is on: the name it is declared under, where that
// name stands, and whether what stands there is its definition. All this fix
// needs to know about the code -- its documentation is found by the name and
// the place, and which way the documentation moves by which of the two sides
// the cursor is on.
class WrittenFunction
{
public:
    QString name;                       // without the scopes in front of it
    Utils::Text::Position namePosition;  // line one-based, column zero-based
    bool isDefinition = false;
};

// Where the outermost declaration written directly around \a loc begins,
// which is where documentation moved to it goes -- above a template rather
// than between the template and the function it declares.
std::optional<int> builtinDeclarationStartIn(const CppRefactoringFilePtr &targetFile,
                                             const Link &loc)
{
    const Document::Ptr &targetCppDoc = targetFile->cppDocument();
    const QList<AST *> targetAstPath = ASTPath(targetCppDoc)(loc.target.line,
                                                             loc.target.column + 1);
    if (targetAstPath.isEmpty())
        return std::nullopt;
    const AST *targetDeclAst = nullptr;
    for (auto it = std::next(std::rbegin(targetAstPath)); it != std::rend(targetAstPath); ++it) {
        AST * const node = *it;
        if (node->asDeclaration()) {
            targetDeclAst = node;
            continue;
        }
        if (targetDeclAst)
            break;
    }
    if (!targetDeclAst)
        return std::nullopt;
    return targetCppDoc->translationUnit()->getTokenPositionInDocument(
        targetDeclAst->firstToken(), targetFile->document());
}

std::optional<int> declarationStartIn(const CppRefactoringFilePtr &targetFile, const Link &loc)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<CxxFrontendFunctionDeclaration> found = cxxFrontendFunctionAt(
            CppModelManager::snapshot(), CppModelManager::workingCopy(), loc.targetFilePath,
            loc.target.line, loc.target.column + 1)) {
        if (!found->isValid())
            return std::nullopt;
        return targetFile->position(found->startLine, found->startColumn);
    }
#endif
    return builtinDeclarationStartIn(targetFile, loc);
}

class MoveFunctionCommentsOp : public CppQuickFixOperation
{
public:
    enum class Direction { ToDecl, ToDef };
    MoveFunctionCommentsOp(const CppQuickFixInterface &interface, int namePos,
                           const Link &name, const QList<CommentRange> &comments,
                           Direction direction)
        : CppQuickFixOperation(interface), m_namePos(namePos), m_name(name),
        m_comments(comments)
    {
        setDescription(direction == Direction::ToDecl
                           ? Tr::tr("Move Function Documentation to Declaration")
                           : Tr::tr("Move Function Documentation to Definition"));
    }

private:
    void perform() override
    {
        const CppRefactoringFilePtr file = currentFile();
        const auto textDoc = const_cast<QTextDocument *>(file->document());
        QTextCursor cursor(textDoc);
        cursor.setPosition(m_namePos);
        const CursorInEditor cursorInEditor(cursor, file->filePath(), editor(),
                                            editor()->textDocument());
        const auto callback = [symbolLoc = m_name, comments = m_comments, file]
            (const Link &link) {
                moveComments(file, link, symbolLoc, comments);
            };
        NonInteractiveFollowSymbolMarker niMarker;
        CppCodeModelSettings::setInteractiveFollowSymbol(false);
        CppModelManager::followSymbol(cursorInEditor, callback, true, false,
                                      FollowSymbolMode::Exact);
    }

    static void moveComments(
        const CppRefactoringFilePtr &sourceFile,
        const Link &targetLoc,
        const Link &symbolLoc,
        const QList<CommentRange> &comments)
    {
        if (!targetLoc.hasValidTarget() || targetLoc.hasSameLocation(symbolLoc))
            return;

        CppRefactoringChanges changes(CppModelManager::snapshot());
        const CppRefactoringFilePtr targetFile
            = targetLoc.targetFilePath == symbolLoc.targetFilePath
                  ? sourceFile
                  : changes.cppFile(targetLoc.targetFilePath);
        const std::optional<int> declarationStart = declarationStartIn(targetFile, targetLoc);
        if (!declarationStart)
            return;
        const int insertionPos = *declarationStart;
        const int sourceCommentStartPos = comments.first().start;
        const int sourceCommentEndPos = comments.last().end;

        // Manually adjust indentation, as both our built-in indenter and ClangFormat
        // are unreliable with regards to comment continuation lines.
        auto tabSettings = [](CppRefactoringFilePtr file) {
            if (auto editor = file->editor())
                return editor->textDocument()->tabSettings();
            return ProjectExplorer::actualTabSettings(file->filePath(), nullptr);
        };
        const TabSettingsData &sts = tabSettings(sourceFile);
        const TabSettingsData &tts = tabSettings(targetFile);
        const QTextBlock insertionBlock = targetFile->document()->findBlock(insertionPos);
        const int insertionColumn = tts.columnAt(insertionBlock.text(),
                                                 insertionPos - insertionBlock.position());
        const QTextBlock removalBlock = sourceFile->document()->findBlock(sourceCommentStartPos);
        const QTextBlock removalBlockEnd = sourceFile->document()->findBlock(sourceCommentEndPos);
        const int removalColumn = sts.columnAt(removalBlock.text(),
                                               sourceCommentStartPos - removalBlock.position());
        const int columnOffset = insertionColumn - removalColumn;
        QString functionDoc;
        if (columnOffset != 0) {
            for (QTextBlock block = removalBlock;
                 block.isValid() && block != removalBlockEnd.next();
                 block = block.next()) {
                QString text = block.text() + QChar::ParagraphSeparator;
                if (block == removalBlockEnd)
                    text = text.left(sourceCommentEndPos - block.position());
                if (block == removalBlock) {
                    text = text.mid(sourceCommentStartPos - block.position());
                } else {
                    int lineIndentColumn = sts.indentationColumn(text) + columnOffset;
                    text.replace(0,
                                 TabSettingsData::firstNonSpace(text),
                                 tts.indentationString(0, lineIndentColumn, 0));
                }
                functionDoc += text;
            }
        } else {
            functionDoc = sourceFile->textOf(sourceCommentStartPos, sourceCommentEndPos);
        }

        // Remove comment plus leading and trailing whitespace, including trailing newline.
        const auto removeAtSource = [&](ChangeSet &changeSet) {
            int removalPos = sourceCommentStartPos;
            const QChar newline(QChar::ParagraphSeparator);
            while (true) {
                const int prev = removalPos - 1;
                if (prev < 0)
                    break;
                const QChar prevChar = sourceFile->charAt(prev);
                if (!prevChar.isSpace() || prevChar == newline)
                    break;
                removalPos = prev;
            }
            int removalEndPos = sourceCommentEndPos;
            while (true) {
                if (removalEndPos == sourceFile->document()->characterCount())
                    break;
                const QChar nextChar = sourceFile->charAt(removalEndPos);
                if (!nextChar.isSpace())
                    break;
                ++removalEndPos;
                if (nextChar == newline)
                    break;
            }
            changeSet.remove(removalPos, removalEndPos);
        };

        ChangeSet targetChangeSet;
        targetChangeSet.insert(insertionPos, functionDoc);
        targetChangeSet.insert(insertionPos, "\n");
        targetChangeSet.insert(insertionPos, QString(insertionColumn, ' '));
        if (targetFile == sourceFile)
            removeAtSource(targetChangeSet);
        const bool targetFileSuccess = targetFile->apply(targetChangeSet);
        if (targetFile == sourceFile || !targetFileSuccess)
            return;
        ChangeSet sourceChangeSet;
        removeAtSource(sourceChangeSet);
        sourceFile->apply(sourceChangeSet);
    }

    const int m_namePos;
    const Link m_name;
    const QList<CommentRange> m_comments;
};

//! Converts C-style to C++-style comments and vice versa
// The comments the cursor covers, as the built-in front end's token stream
// has them. Empty where anything else is in there, which is what a run of
// tokens says outright.
QList<WrittenComment> builtinCommentsForCursor(const CppQuickFixInterface &interface)
{
    // If there's a selection, then it must entirely consist of comment tokens.
    // If there's no selection, the cursor must be on a comment.
    const QList<Token> cursorTokens = interface.currentFile()->tokensForCursor();
    if (cursorTokens.empty() || !cursorTokens.front().isComment())
        return {};

    TranslationUnit * const tu = interface.currentFile()->cppDocument()->translationUnit();
    const auto styleOf = [](const Token &token) {
        switch (token.kind()) {
        case T_CPP_COMMENT: return CommentStyle::CppStyle;
        case T_DOXY_COMMENT: return CommentStyle::CStyleDoxygen;
        case T_CPP_DOXY_COMMENT: return CommentStyle::CppStyleDoxygen;
        default: return CommentStyle::CStyle;
        }
    };

    QList<WrittenComment> comments;
    for (const Token &token : cursorTokens) {
        if (!token.isComment())
            return {};
        comments.append({{tu->getTokenPositionInDocument(token, interface.textDocument()),
                          tu->getTokenEndPositionInDocument(token, interface.textDocument())},
                         styleOf(token)});
    }
    return comments;
}

QList<WrittenComment> commentsForCursor(const CppQuickFixInterface &interface)
{
#ifdef QTC_WITH_CXX_FRONTEND
    const QTextCursor cursor = interface.currentFile()->cursor();
    if (const std::optional<QList<CxxFrontendComment>> found = cxxFrontendCommentsIn(
            interface.currentFile()->filePath(), *interface.textDocument(),
            cursor.selectionStart(), cursor.selectionEnd())) {
        return Utils::transform<QList<WrittenComment>>(*found,
                                                       [](const CxxFrontendComment &comment) {
                                                           return WrittenComment{comment.range,
                                                                                 comment.style};
                                                       });
    }
#endif
    return builtinCommentsForCursor(interface);
}

class ConvertCommentStyle : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface,
                 TextEditor::QuickFixOperations &result) override
    {
        const QList<WrittenComment> comments = commentsForCursor(interface);
        if (comments.isEmpty())
            return;

        // All comments must be written the same way, but we make an exception for
        // doxygen comments that start with "///", as these are often not intended to
        // be doxygen. For our purposes, we treat them as normal comments.
        const auto effectiveStyle = [&interface](const WrittenComment &comment) {
            if (comment.style != CommentStyle::CppStyleDoxygen)
                return comment.style;
            return interface.textAt(comment.range.start, 3) == "///"
                       ? CommentStyle::CppStyle
                       : CommentStyle::CppStyleDoxygen;
        };
        const CommentStyle style = effectiveStyle(comments.first());
        for (const WrittenComment &comment : comments) {
            if (effectiveStyle(comment) != style)
                return;
        }

        result << new ConvertCommentStyleOp(
            interface,
            Utils::transform<QList<CommentRange>>(comments, &WrittenComment::range),
            style);
    }
};

// The same, off the built-in syntax tree: the innermost function definition
// or function declaration the cursor is in.
std::optional<WrittenFunction> builtinFunctionAt(const CppQuickFixInterface &interface)
{
    const QList<AST *> &astPath = interface.path();
    if (astPath.isEmpty())
        return std::nullopt;
    const Symbol *symbol = nullptr;
    bool isDefinition = false;
    for (auto it = std::next(std::rbegin(astPath)); it != std::rend(astPath); ++it) {
        if (const auto func = (*it)->asFunctionDefinition()) {
            symbol = func->symbol;
            isDefinition = true;
            break;
        }
        const auto decl = (*it)->asSimpleDeclaration();
        if (!decl || !decl->declarator_list)
            continue;
        for (auto it = decl->declarator_list->begin();
             !symbol && it != decl->declarator_list->end(); ++it) {
            PostfixDeclaratorListAST * const funcDecls = (*it)->postfix_declarator_list;
            if (!funcDecls)
                continue;
            for (auto it = funcDecls->begin(); it != funcDecls->end(); ++it) {
                if (const auto func = (*it)->asFunctionDeclarator()) {
                    symbol = func->symbol;
                    isDefinition = false;
                    break;
                }
            }
        }
    }
    if (!symbol)
        return std::nullopt;

    TranslationUnit * const unit = interface.currentFile()->cppDocument()->translationUnit();
    Utils::Text::Position position;
    unit->getTokenPosition(symbol->sourceLocation(), &position.line, &position.column);
    --position.column;
    const QStringList parts = Overview().prettyName(symbol->name())
                                  .split("::", Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return std::nullopt;
    return WrittenFunction{parts.last(), position, isDefinition};
}

std::optional<WrittenFunction> functionAt(const CppQuickFixInterface &interface)
{
#ifdef QTC_WITH_CXX_FRONTEND
    const CppRefactoringFilePtr file = interface.currentFile();
    const QTextCursor cursor = file->cursor();
    const Utils::Text::Position at = Utils::Text::Position::fromPositionInDocument(
        file->document(), cursor.position());
    if (const std::optional<CxxFrontendFunctionDeclaration> found = cxxFrontendFunctionAt(
            CppModelManager::snapshot(), CppModelManager::workingCopy(), file->filePath(),
            at.line, at.column + 1)) {
        if (!found->isValid())
            return std::nullopt;
        const QString name = file->textOf(
            file->position(found->nameLine, found->nameColumn),
            file->position(found->nameEndLine, found->nameEndColumn));
        return WrittenFunction{name, {found->nameLine, found->nameColumn - 1},
                               found->isDefinition};
    }
#endif
    return builtinFunctionAt(interface);
}

//! Moves function documentation between declaration and implementation.
class MoveFunctionComments : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface,
                 TextEditor::QuickFixOperations &result) override
    {
        const std::optional<WrittenFunction> function = functionAt(interface);
        if (!function)
            return;

        const QList<CommentRange> comments = commentsForDeclaration(
            function->name, function->namePosition, *interface.textDocument(),
            interface.currentFile()->cppDocument());
        if (comments.isEmpty())
            return;

        const CppRefactoringFilePtr file = interface.currentFile();
        const int namePos = file->position(function->namePosition.line,
                                           function->namePosition.column + 1);
        const Link link(file->filePath(), function->namePosition.line,
                        function->namePosition.column);
        result << new MoveFunctionCommentsOp(
            interface, namePos, link, comments,
            function->isDefinition ? MoveFunctionCommentsOp::Direction::ToDecl
                                   : MoveFunctionCommentsOp::Direction::ToDef);
    }
};

#ifdef WITH_TESTS
class ConvertCommentStyleTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
class MoveFunctionCommentsTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerRewriteCommentQuickfixes()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(ConvertCommentStyle);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(MoveFunctionComments);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <rewritecomment.moc>
#endif
