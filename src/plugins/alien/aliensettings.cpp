// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aliensettings.h"

#include "alienconstants.h"
#include "alientr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>

using namespace Utils;

namespace Alien::Internal {

AlienSettings &settings()
{
    static AlienSettings theSettings;
    return theSettings;
}

AlienSettings::AlienSettings()
{
    setAutoApply(false);

    enable.setSettingsKey("Alien.Enable");
    enable.setLabelText(Tr::tr("Enable VS Code extension support"));
    enable.setToolTip(Tr::tr("Discover VS Code extensions and surface their "
                             "language servers in the editor. Experimental."));
    enable.setDefaultValue(false);

    nodeJsPath.setSettingsKey("Alien.NodeJsPath");
    nodeJsPath.setExpectedKind(PathChooserKind::ExistingCommand);
    nodeJsPath.setDefaultPathValue(FilePath("node").searchInPath());
    nodeJsPath.setLabelText(Tr::tr("Node.js path:"));
    nodeJsPath.setToolTip(Tr::tr("Path to the node.js executable used to run "
                                 "extension code."));

    extensionsDir.setSettingsKey("Alien.ExtensionsDir");
    extensionsDir.setExpectedKind(PathChooserKind::ExistingDirectory);
    extensionsDir.setDefaultPathValue(
        FilePath::fromUserInput("~/.qtcreator-vscode-extensions"));
    extensionsDir.setLabelText(Tr::tr("Extensions directory:"));
    extensionsDir.setToolTip(Tr::tr("Directory scanned for installed VS Code "
                                    "extensions. Each extension lives in its own "
                                    "subfolder containing a package.json file, "
                                    "mirroring the ~/.vscode/extensions layout."));

    assumeMainIsStdioServer.setSettingsKey("Alien.AssumeMainIsStdioServer");
    assumeMainIsStdioServer.setLabelText(
        Tr::tr("Run extension entry point as a stdio language server"));
    assumeMainIsStdioServer.setToolTip(
        Tr::tr("Experimental stopgap until the extension host is implemented. "
               "Launches \"node <main> --stdio\" for extensions that contribute "
               "a language. Only works if the entry point is itself a language "
               "server."));
    assumeMainIsStdioServer.setDefaultValue(false);

    setLayouter([this] {
        using namespace Layouting;
        return Column {
            enable,
            Form {
                nodeJsPath, br,
                extensionsDir, br,
                assumeMainIsStdioServer, br,
            },
            st,
        };
    });

    readSettings();
}

class AlienSettingsPage final : public Core::IOptionsPage
{
public:
    AlienSettingsPage()
    {
        setId(Constants::SETTINGS_ID);
        setDisplayName(Tr::tr("VS Code Extensions"));
        setCategory(Core::Constants::SETTINGS_CATEGORY_AI);
        setSettingsProvider([] { return &settings(); });
    }
};

void setupAlienSettings()
{
    static AlienSettingsPage theSettingsPage;
}

} // namespace Alien::Internal
