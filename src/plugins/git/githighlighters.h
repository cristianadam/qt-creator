// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/syntaxhighlighter.h>

#include <QRegularExpression>

namespace Git {
namespace Internal {

enum Format {
    Format_Comment,
    Format_Change,
    Format_Description,
    Format_Pick,
    Format_Reword,
    Format_Edit,
    Format_Squash,
    Format_Fixup,
    Format_Exec,
    Format_Break,
    Format_Drop,
    Format_Label,
    Format_Reset,
    Format_Merge,
    Format_Count
};

// Highlighter for git submit messages. Make the first line bold, indicates
// comments as such (retrieving the format from the text editor) and marks up
// keywords (words in front of a colon as in 'Task: <bla>').
class GitSubmitHighlighter : public TextEditor::SyntaxHighlighter
{
public:
    explicit GitSubmitHighlighter(QTextEdit *parent = nullptr);
    void highlightBlock(const QString &text) override;

private:
    enum State { None = -1, Header, Other };
    const QRegularExpression m_keywordPattern;
    QChar m_hashChar;
};

// Highlighter for interactive rebase todo. Indicates comments as such
// (retrieving the format from the text editor) and marks up keywords
class GitRebaseHighlighter : public TextEditor::SyntaxHighlighter
{
public:
    explicit GitRebaseHighlighter(QTextDocument *parent = nullptr);
    void highlightBlock(const QString &text) override;

private:
    class RebaseAction
    {
    public:
        QRegularExpression exp;
        Format formatCategory;
        RebaseAction(const QString &regexp, const Format formatCategory);
    };
    const QChar m_hashChar;
    const QRegularExpression m_changeNumberPattern;
    QList<RebaseAction> m_actions;
};

} // namespace Internal
} // namespace Git
