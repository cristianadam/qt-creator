// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/highlighter.h>
#include <texteditor/highlighterhelper.h>

#include <QList>

#include <functional>

namespace DiffEditor::Internal {

// A diff document concatenates several files, so the syntax definition is
// picked per block from the file that block belongs to. Folding is owned by
// the diff widget and left untouched here.
class DiffSyntaxHighlighter : public TextEditor::Highlighter
{
public:
    class FileRegion
    {
    public:
        int startBlock = 0; // first block belonging to the file
        TextEditor::HighlighterHelper::Definition definition;
    };

    // Tells whether a block carries actual source text (as opposed to a file,
    // chunk, separator or span line).
    using ContentPredicate = std::function<bool(int blockNumber)>;

    DiffSyntaxHighlighter(const QList<FileRegion> &regions, const ContentPredicate &isContent);

protected:
    void highlightBlock(const QString &text) override;
    void applyFolding(int offset, int length, KSyntaxHighlighting::FoldingRegion region) override;

private:
    const FileRegion *regionForBlock(int blockNumber) const;

    const QList<FileRegion> m_regions;
    const ContentPredicate m_isContent;
};

} // namespace DiffEditor::Internal
