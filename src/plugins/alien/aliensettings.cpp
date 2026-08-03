// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aliensettings.h"

#include "alienconstants.h"
#include "alientr.h"
#include "extensionregistry.h"
#include "vscodemanifest.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>

#include <QHeaderView>
#include <QStandardItemModel>
#include <QTreeView>

using namespace Utils;

namespace Alien::Internal {

AlienSettings &settings()
{
    static AlienSettings theSettings;
    return theSettings;
}

AlienExtensionSettings &extensionSettings()
{
    static AlienExtensionSettings theExtensionSettings;
    return theExtensionSettings;
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
    // Reuse the VS Code extensions folder so already-installed extensions are
    // picked up; new ones can be added with "Install VS Code Extension".
    extensionsDir.setDefaultPathValue(FilePath::fromUserInput("~/.vscode/extensions"));
    extensionsDir.setLabelText(Tr::tr("Extensions directory:"));
    extensionsDir.setToolTip(Tr::tr("Directory scanned for installed VS Code "
                                    "extensions. Each extension lives in its own "
                                    "subfolder containing a package.json file "
                                    "(the ~/.vscode/extensions layout). Per-extension "
                                    "settings can be overridden in a settings.json "
                                    "file in this directory."));

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

AlienExtensionSettings::AlienExtensionSettings()
{
    setAutoApply(false);
    disabledExtensions.setSettingsKey("Alien.DisabledExtensions");
    readSettings();
}

QStringList AlienExtensionSettings::disabledIds() const
{
    QStringList ids;
    const QStringList lines = disabledExtensions().split('\n');
    for (const QString &line : lines) {
        const QString id = line.trimmed();
        if (!id.isEmpty())
            ids.append(id);
    }
    return ids;
}

void AlienExtensionSettings::setDisabledIds(const QStringList &ids)
{
    disabledExtensions.setValue(ids.join('\n'));
}

bool AlienExtensionSettings::isEnabled(const QString &id) const
{
    return !disabledIds().contains(id);
}

void AlienExtensionSettings::save()
{
    writeSettings();
    emit changed();
}

class AlienSettingsPage final : public Core::IOptionsPage
{
public:
    AlienSettingsPage()
    {
        setId(Constants::SETTINGS_ID);
        setDisplayName(Tr::tr("General"));
        setCategory(Constants::SETTINGS_CATEGORY);
        setSettingsProvider([] { return &settings(); });
    }
};

// A checkable list of the discovered extensions; unchecked ones are stored as
// the disabled set and skipped on activation.
class AlienExtensionsWidget final : public Core::IOptionsPageWidget
{
public:
    AlienExtensionsWidget()
    {
        m_model = new QStandardItemModel(this);
        m_model->setHorizontalHeaderLabels(
            {Tr::tr("Extension"), Tr::tr("Identifier"), Tr::tr("Version")});

        const QList<VscodeManifest> manifests
            = ExtensionRegistry::scan(settings().extensionsDir());
        const QStringList disabledList = extensionSettings().disabledIds();
        const QSet<QString> disabled(disabledList.begin(), disabledList.end());
        for (const VscodeManifest &manifest : manifests) {
            auto name = new QStandardItem(
                manifest.displayName.isEmpty() ? manifest.name : manifest.displayName);
            name->setCheckable(true);
            name->setCheckState(
                disabled.contains(manifest.qualifiedId()) ? Qt::Unchecked : Qt::Checked);
            name->setData(manifest.qualifiedId(), Qt::UserRole);
            name->setEditable(false);
            auto id = new QStandardItem(manifest.qualifiedId());
            id->setEditable(false);
            auto version = new QStandardItem(manifest.version);
            version->setEditable(false);
            m_model->appendRow({name, id, version});
        }

        auto view = new QTreeView;
        view->setModel(m_model);
        view->setRootIsDecorated(false);
        view->setUniformRowHeights(true);
        view->setSelectionMode(QAbstractItemView::NoSelection);
        view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        view->header()->setStretchLastSection(true);

        using namespace Layouting;
        Column {
            Tr::tr("Enable the VS Code extensions to activate. Changes take "
                   "effect immediately."),
            view,
        }.attachTo(this);
    }

    void apply() final
    {
        QStringList disabled;
        for (int row = 0, n = m_model->rowCount(); row < n; ++row) {
            const QStandardItem *item = m_model->item(row, 0);
            if (item->checkState() != Qt::Checked)
                disabled << item->data(Qt::UserRole).toString();
        }
        extensionSettings().setDisabledIds(disabled);
        extensionSettings().save();
    }

private:
    QStandardItemModel *m_model = nullptr;
};

class AlienExtensionsPage final : public Core::IOptionsPage
{
public:
    AlienExtensionsPage()
    {
        setId(Constants::EXTENSIONS_SETTINGS_ID);
        setDisplayName(Tr::tr("Extensions"));
        setCategory(Constants::SETTINGS_CATEGORY);
        setWidgetCreator([] { return new AlienExtensionsWidget; });
    }
};

void setupAlienSettings()
{
    Core::IOptionsPage::registerCategory(
        Constants::SETTINGS_CATEGORY, Tr::tr("Alien"),
        FilePath::fromString(":/core/images/settingscategory_ai.png"));

    static AlienSettingsPage theSettingsPage;
    static AlienExtensionsPage theExtensionsPage;
}

} // namespace Alien::Internal
