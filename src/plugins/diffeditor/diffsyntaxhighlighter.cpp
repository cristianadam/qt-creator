// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "diffsyntaxhighlighter.h"

#include <texteditor/textdocumentlayout.h>

#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/State>

using namespace TextEditor;

namespace DiffEditor::Internal {

DiffSyntaxHighlighter::DiffSyntaxHighlighter(const QList<FileRegion> &regions,
                                             const ContentPredicate &isContent)
    : m_regions(regions)
    , m_isContent(isContent)
{}

const DiffSyntaxHighlighter::FileRegion *DiffSyntaxHighlighter::regionForBlock(int blockNumber) const
{
    const FileRegion *result = nullptr;
    for (const FileRegion &region : m_regions) {
        if (region.startBlock > blockNumber)
            break;
        result = &region;
    }
    return result;
}

void DiffSyntaxHighlighter::highlightBlock(const QString &text)
{
    const QTextBlock block = currentBlock();
    const int blockNumber = block.blockNumber();
    const FileRegion *region = regionForBlock(blockNumber);

    if (!region || !region->definition.isValid() || !m_isContent(blockNumber)) {
        formatSpaces(text);
        return;
    }

    setDefinition(region->definition);

    // Only carry the syntax state from the previous line when it is contiguous
    // source of the same file. Across a file boundary or a skipped-lines gap the
    // state (e.g. an open block comment) does not apply, so we start fresh.
    KSyntaxHighlighting::State previousState;
    const int previousNumber = blockNumber - 1;
    if (m_isContent(previousNumber) && regionForBlock(previousNumber) == region)
        previousState = TextBlockUserData::syntaxState(block.previous());

    const KSyntaxHighlighting::State oldState = TextBlockUserData::syntaxState(block);
    const KSyntaxHighlighting::State state = highlightLine(text, previousState);
    if (oldState != state) {
        TextBlockUserData::setSyntaxState(block, state);
        // Toggles the LSB of the block state to force a rehighlight of the next block.
        setCurrentBlockState(currentBlockState() ^ 1);
    }

    formatSpaces(text);
}

void DiffSyntaxHighlighter::applyFolding(int, int, KSyntaxHighlighting::FoldingRegion)
{
}

} // namespace DiffEditor::Internal
