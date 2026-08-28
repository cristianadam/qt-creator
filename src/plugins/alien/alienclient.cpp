// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alienclient.h"

#include "aliensettings.h"

#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>

#include <languageclient/languageclientinterface.h>
#include <languageclient/languageclientsettings.h>

#include <texteditor/textdocument.h>

using namespace Core;
using namespace LanguageClient;
using namespace TextEditor;
using namespace Utils;

namespace Alien::Internal {

static LanguageFilter languageFilter(const VscodeManifest &manifest)
{
    LanguageFilter filter;
    for (const VscodeLanguage &language : manifest.languages) {
        for (const QString &extension : language.extensions) {
            // ".qml" -> "*.qml"
            filter.filePattern << '*' + extension;
        }
    }
    return filter;
}

static BaseClientInterface *clientInterface(const CommandLine &serverCommand,
                                            const FilePath &workingDirectory)
{
    auto interface = new StdIOClientInterface;
    interface->setCommandLine(serverCommand);
    if (!workingDirectory.isEmpty())
        interface->setWorkingDirectory(workingDirectory);
    return interface;
}

AlienClient::AlienClient(const QString &name,
                         const CommandLine &serverCommand,
                         const LanguageFilter &filter,
                         const FilePath &workingDirectory,
                         const QJsonObject &initializationOptions)
    : Client(clientInterface(serverCommand, workingDirectory))
{
    setName(name);
    setSupportedLanguage(filter);
    if (!initializationOptions.isEmpty())
        setInitializationOptions(initializationOptions);

    start();
    wireDocuments();
}

AlienClient::AlienClient(const VscodeManifest &manifest, const CommandLine &serverCommand)
    : AlienClient(manifest.displayName.isEmpty() ? manifest.qualifiedId() : manifest.displayName,
                  serverCommand, languageFilter(manifest))
{
    m_manifest = manifest;
}

void AlienClient::wireDocuments()
{
    auto openDoc = [this](IDocument *document) {
        if (auto textDocument = qobject_cast<TextDocument *>(document))
            openDocument(textDocument);
    };
    connect(EditorManager::instance(), &EditorManager::documentOpened, this, openDoc);
    connect(EditorManager::instance(), &EditorManager::documentClosed, this,
            [this](IDocument *document) {
                if (auto textDocument = qobject_cast<TextDocument *>(document))
                    closeDocument(textDocument);
            });
    for (IDocument *document : DocumentModel::openedDocuments())
        openDoc(document);
}

std::optional<CommandLine> resolveServerCommand(const VscodeManifest &manifest)
{
    if (!manifest.hasLanguageServer())
        return std::nullopt;

    if (settings().assumeMainIsStdioServer()) {
        const FilePath node = settings().nodeJsPath();
        const FilePath main = manifest.mainPath();
        if (node.isExecutableFile() && main.exists())
            return CommandLine{node, {main.toFSPathString(), "--stdio"}};
    }

    // General case: needs the extension host to run activate() and observe the
    // ServerOptions the extension hands to vscode-languageclient.
    return std::nullopt;
}

} // namespace Alien::Internal
