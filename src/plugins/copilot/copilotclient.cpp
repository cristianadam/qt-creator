// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "copilotclient.h"
#include "documentwatcher.h"

#include <languageclient/languageclientinterface.h>
#include <languageclient/languageclientmanager.h>
#include <languageclient/languageclientsettings.h>

#include <coreplugin/editormanager/editormanager.h>

namespace CoPilot::Internal {

static LanguageClient::BaseClientInterface *clientInterface()
{
    Utils::CommandLine cmd{"node",
                           {"/Users/mtillmanns/projects/qt/copilot.vim/copilot/dist/agent.js"}};

    const auto interface = new LanguageClient::StdIOClientInterface;
    interface->setCommandLine(cmd);
    return interface;
}

CoPilotClient::CoPilotClient()
    : LanguageClient::Client(clientInterface())
{
    setName("CoPilot");
    LanguageClient::LanguageFilter langFilter;

    langFilter.filePattern = {"*"};

    setSupportedLanguage(langFilter);
    start();

    connect(Core::EditorManager::instance(),
            &Core::EditorManager::documentOpened,
            this,
            [this](Core::IDocument *document) {
                TextEditor::TextDocument *textDocument = qobject_cast<TextEditor::TextDocument *>(
                    document);
                if (!textDocument)
                    return;

                openDocument(textDocument);

                m_documentWatchers.emplace(textDocument->filePath(),
                                           std::make_unique<DocumentWatcher>(this, textDocument));
            });

    connect(Core::EditorManager::instance(),
            &Core::EditorManager::documentClosed,
            this,
            [this](Core::IDocument *document) {
                auto textDocument = qobject_cast<TextEditor::TextDocument *>(document);
                if (!textDocument)
                    return;

                closeDocument(textDocument);
                m_documentWatchers.erase(textDocument->filePath());
            });
}

} // namespace CoPilot::Internal