// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "diffeditorwidgetcontroller.h"
#include "selectabletexteditorwidget.h" // TODO: we need DiffSelections here only

#include <QFutureWatcher>
#include <QTextCharFormat>
#include <QWidget>

namespace Core { class IContext; }

namespace TextEditor {
class FontSettings;
class TextEditorWidget;
}

QT_BEGIN_NAMESPACE
class QMenu;
class QSplitter;
QT_END_NAMESPACE

namespace DiffEditor {

class FileData;

namespace Internal {

class DiffEditorDocument;
class SideDiffEditorWidget;
class SideBySideDiffOutput;

class SideDiffData
{
public:
    static SideBySideDiffOutput diffOutput(QFutureInterface<void> &fi, int progressMin,
                                           int progressMax, const DiffEditorInput &input);

    // block number, visual line number.
    QMap<int, int> m_lineNumbers;
    // block number, fileInfo. Set for file lines only.
    QMap<int, DiffFileInfo> m_fileInfo;
    // block number, skipped lines and context info. Set for chunk lines only.
    QMap<int, QPair<int, QString>> m_skippedLines;
    // start block number, block count of a chunk, chunk index inside a file.
    QMap<int, QPair<int, int>> m_chunkInfo;
    // block number, separator. Set for file, chunk or span line.
    QMap<int, bool> m_separators;

    int m_lineNumberDigits = 1;

    bool isFileLine(int blockNumber) const { return m_fileInfo.contains(blockNumber); }
    bool isChunkLine(int blockNumber) const { return m_skippedLines.contains(blockNumber); }
    int blockNumberForFileIndex(int fileIndex) const;
    int fileIndexForBlockNumber(int blockNumber) const;
    int chunkIndexForBlockNumber(int blockNumber) const;
    int chunkRowForBlockNumber(int blockNumber) const;
    int chunkRowsCountForBlockNumber(int blockNumber) const;

private:
    void setLineNumber(int blockNumber, int lineNumber);
    void setFileInfo(int blockNumber, const DiffFileInfo &fileInfo);
    void setSkippedLines(int blockNumber, int skippedLines, const QString &contextInfo = {}) {
        m_skippedLines[blockNumber] = {skippedLines, contextInfo};
        setSeparator(blockNumber, true);
    }
    void setChunkIndex(int startBlockNumber, int blockCount, int chunkIndex);
    void setSeparator(int blockNumber, bool separator) { m_separators[blockNumber] = separator; }
};

class SideDiffOutput
{
public:
    SideDiffData diffData;
    QString diffText;
    DiffSelections selections;
};

class SideBySideDiffOutput
{
public:
    std::array<SideDiffOutput, SideCount> side{};
    // 'foldingIndent' is populated with <block number> and folding indentation
    // value where 1 indicates start of new file and 2 indicates a diff chunk.
    // Remaining lines (diff contents) are assigned 3.
    QHash<int, int> foldingIndent;
};


class SideBySideDiffEditorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SideBySideDiffEditorWidget(QWidget *parent = nullptr);

    TextEditor::TextEditorWidget *leftEditorWidget() const;
    TextEditor::TextEditorWidget *rightEditorWidget() const;

    void setDocument(DiffEditorDocument *document);
    DiffEditorDocument *diffDocument() const;

    void setDiff(const QList<FileData> &diffFileList);
    void setCurrentDiffFileIndex(int diffFileIndex);

    void setHorizontalSync(bool sync);

    void saveState();
    void restoreState();

    void clear(const QString &message = {});

signals:
    void currentDiffFileIndexChanged(int index);

private:
    void setFontSettings(const TextEditor::FontSettings &fontSettings);
    void slotLeftJumpToOriginalFileRequested(int diffFileIndex,
                                             int lineNumber, int columnNumber);
    void slotRightJumpToOriginalFileRequested(int diffFileIndex,
                                              int lineNumber, int columnNumber);
    void slotLeftContextMenuRequested(QMenu *menu, int fileIndex,
                                      int chunkIndex, const ChunkSelection &selection);
    void slotRightContextMenuRequested(QMenu *menu, int fileIndex,
                                       int chunkIndex, const ChunkSelection &selection);
    void leftVSliderChanged();
    void rightVSliderChanged();
    void leftHSliderChanged();
    void rightHSliderChanged();
    void leftCursorPositionChanged();
    void rightCursorPositionChanged();
    void syncHorizontalScrollBarPolicy();
    void handlePositionChange(SideDiffEditorWidget *source, SideDiffEditorWidget *dest);
    void syncCursor(SideDiffEditorWidget *source, SideDiffEditorWidget *dest);

    void showDiff();

    SideDiffEditorWidget *m_leftEditor = nullptr;
    SideDiffEditorWidget *m_rightEditor = nullptr;
    QSplitter *m_splitter = nullptr;

    DiffEditorWidgetController m_controller;

    bool m_horizontalSync = false;
};

} // namespace Internal
} // namespace DiffEditor
