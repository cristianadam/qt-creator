// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alienhover.h"

#include "extensionhost.h"

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <QTextBlock>
#include <QTextDocument>

using namespace TextEditor;
using namespace Utils;

namespace Alien::Internal {

void AlienHoverHandler::identifyMatch(TextEditorWidget *widget, int pos, ReportPriority report)
{
    const FilePath filePath = widget->textDocument()->filePath();
    const QTextBlock block = widget->document()->findBlock(pos);
    const int line = block.blockNumber();
    const int character = pos - block.position();

    m_host->requestHover(filePath, line, character, [this, report](const QString &contents) {
        if (!contents.isEmpty())
            setToolTip(contents, Qt::MarkdownText);
        report(contents.isEmpty() ? Priority_None : Priority_Tooltip);
    });
}

} // namespace Alien::Internal
