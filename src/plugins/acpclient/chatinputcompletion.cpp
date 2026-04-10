// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "chatinputcompletion.h"

#include <texteditor/codeassist/assistinterface.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/asyncprocessor.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/texteditor.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <utils/algorithm.h>

#include <QRegularExpression>
#include <QSet>

using namespace TextEditor;

namespace AcpClient::Internal {

class ChatInputCompletionProcessor final : public AsyncProcessor
{
public:
    explicit ChatInputCompletionProcessor(QString editorText, QStringList fileNames)
        : m_editorText(std::move(editorText))
        , m_fileNames(std::move(fileNames))
    {}

    IAssistProposal *performAsync() override
    {
        int pos = interface()->position();
        QChar chr;
        do {
            chr = interface()->characterAt(--pos);
        } while (chr.isLetterOrNumber() || chr == '_');
        ++pos;

        const int length = interface()->position() - pos;
        if (length < 2)
            return nullptr;

        static const QRegularExpression wordRE("([\\p{L}_][\\p{L}0-9_]{2,})");
        QSet<QString> seen;
        QList<AssistProposalItemInterface *> items;

        auto it = wordRE.globalMatch(m_editorText);
        while (it.hasNext()) {
            if (isCanceled())
                return nullptr;
            const QString word = it.next().captured();
            if (Utils::insert(seen, word)) {
                auto *item = new AssistProposalItem;
                item->setText(word);
                items.append(item);
            }
        }

        for (const QString &fileName : std::as_const(m_fileNames)) {
            if (isCanceled())
                return nullptr;
            if (Utils::insert(seen, fileName)) {
                auto *item = new AssistProposalItem;
                item->setText(fileName);
                items.append(item);
            }
        }

        return new GenericProposal(pos, items);
    }

private:
    QString m_editorText;
    QStringList m_fileNames;
};

ChatInputCompletionProvider::ChatInputCompletionProvider(QObject *parent)
    : CompletionAssistProvider(parent)
{}

IAssistProcessor *ChatInputCompletionProvider::createProcessor(const AssistInterface *) const
{
    QString editorText;
    if (auto *widget = TextEditorWidget::currentTextEditorWidget())
        editorText = widget->toPlainText();

    QStringList fileNames;
    if (const auto *project = ProjectExplorer::ProjectManager::startupProject()) {
        const Utils::FilePaths files = project->files(ProjectExplorer::Project::SourceFiles);
        fileNames.reserve(files.size());
        for (const Utils::FilePath &file : files)
            fileNames.append(file.fileName());
    }

    return new ChatInputCompletionProcessor(std::move(editorText), std::move(fileNames));
}

} // namespace AcpClient::Internal
