// Copyright (C) 2016 Jan Dalheimer <jan@dalheimer.de>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cmakeindenter.h"

#include "cmakecommandkeywords.h"

#include <cmakelang/cmakeindentation.h>

#include <texteditor/tabsettings.h>

#include <QTextDocument>

#include <optional>

using namespace TextEditor;

namespace CMakeProjectManager::Internal {

class CMakeIndenter final : public TextEditor::TextIndenter
{
public:
    explicit CMakeIndenter(QTextDocument *doc)
        : TextEditor::TextIndenter(doc)
    {
        setElectricCharacters("()");
    }

    int indentFor(const QTextBlock &block,
                  const TabSettingsData &tabSettings,
                  int cursorPositionInEditor = -1) final;
    IndentationForBlock indentationForBlocks(const QList<QTextBlock> &blocks,
                                             const TabSettingsData &tabSettings,
                                             int cursorPositionInEditor = -1) final;
    void indent(const QTextCursor &cursor,
                const QChar &typedChar,
                const TabSettingsData &tabSettings,
                int cursorPositionInEditor = -1) final;
    void reindent(const QTextCursor &cursor,
                  const TabSettingsData &tabSettings,
                  int cursorPositionInEditor = -1) final;
    void invalidateCache() final;

private:
    int levelFor(const QTextBlock &block);
    void indentSelection(const QTextCursor &cursor,
                         const TabSettingsData &tabSettings,
                         int cursorPositionInEditor);

    CommandKeywords m_keywords;
    std::optional<CMakeLang::Indentation> m_indentation;
    int m_revision = -1;
};

int CMakeIndenter::levelFor(const QTextBlock &block)
{
    if (!m_indentation || m_revision != m_doc->revision()) {
        m_keywords.refresh();
        const QString source = m_doc->toPlainText();
        m_indentation.emplace(source,
                              [this](const QString &command, const QString &argument) {
                                  return m_keywords.contains(command, argument);
                              });
        m_revision = m_doc->revision();
    }
    return m_indentation->levelAt(block.blockNumber() + 1);
}

int CMakeIndenter::indentFor(const QTextBlock &block,
                             const TabSettingsData &tabSettings,
                             int /*cursorPositionInEditor*/)
{
    const int level = levelFor(block);
    if (level == CMakeLang::Indentation::Keep)
        return -1;
    return level * tabSettings.m_indentSize;
}

IndentationForBlock CMakeIndenter::indentationForBlocks(const QList<QTextBlock> &blocks,
                                                        const TabSettingsData &tabSettings,
                                                        int /*cursorPositionInEditor*/)
{
    IndentationForBlock result;
    for (const QTextBlock &block : blocks) {
        const int level = levelFor(block);
        result.insert(block.blockNumber(),
                      level == CMakeLang::Indentation::Keep
                          ? tabSettings.indentationColumn(block.text())
                          : level * tabSettings.m_indentSize);
    }
    return result;
}

// The indentation of a line follows from the tokens above it, not from the way
// those lines are laid out, so the whole selection is measured against the text
// as it stands before the first line of it moves.
void CMakeIndenter::indentSelection(const QTextCursor &cursor,
                                    const TabSettingsData &tabSettings,
                                    int cursorPositionInEditor)
{
    const QTextBlock end = m_doc->findBlock(cursor.selectionEnd()).next();

    QList<QPair<int, int>> indentations;
    for (QTextBlock block = m_doc->findBlock(cursor.selectionStart());
         block.isValid() && block != end;
         block = block.next()) {
        indentations.append({block.blockNumber(),
                             indentFor(block, tabSettings, cursorPositionInEditor)});
    }

    for (const auto &[number, indentation] : std::as_const(indentations)) {
        if (indentation >= 0)
            tabSettings.indentLine(m_doc->findBlockByNumber(number), indentation);
    }
}

void CMakeIndenter::indent(const QTextCursor &cursor,
                           const QChar &typedChar,
                           const TabSettingsData &tabSettings,
                           int cursorPositionInEditor)
{
    if (cursor.hasSelection())
        indentSelection(cursor, tabSettings, cursorPositionInEditor);
    else
        indentBlock(cursor.block(), typedChar, tabSettings, cursorPositionInEditor);
}

void CMakeIndenter::reindent(const QTextCursor &cursor,
                             const TabSettingsData &tabSettings,
                             int cursorPositionInEditor)
{
    indent(cursor, QChar::Null, tabSettings, cursorPositionInEditor);
}

void CMakeIndenter::invalidateCache()
{
    m_indentation.reset();
    m_revision = -1;
}

TextEditor::Indenter *createCMakeIndenter(QTextDocument *doc)
{
    return new CMakeIndenter(doc);
}

} // CMakeProjectManager::Internal
