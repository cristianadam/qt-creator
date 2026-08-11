// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aliensettings.h"

#include "alienconstants.h"
#include "alientr.h"
#include "extensionregistry.h"
#include "vscodemanifest.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <extensionmanager/extensionmanagerconstants.h>

#include <utils/fancylineedit.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>

#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>

using namespace Utils;

namespace Alien::Internal {

AlienSettings &settings()
{
    static AlienSettings theSettings;
    return theSettings;
}

EnabledExtensionsAspect::EnabledExtensionsAspect(AspectContainer *container)
    : TypedAspect(container)
{
    setSettingsKey("EnabledExtensions");
}

QStringList EnabledExtensionsAspect::ids() const
{
    QStringList result;
    for (const QString &line : value().split('\n')) {
        const QString id = line.trimmed();
        if (!id.isEmpty())
            result.append(id);
    }
    return result;
}

bool EnabledExtensionsAspect::isEnabled(const QString &id) const
{
    return ids().contains(id);
}

void EnabledExtensionsAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    m_model = new QStandardItemModel;
    m_model->setHorizontalHeaderLabels(
        {Tr::tr("Extension"), Tr::tr("Identifier"), Tr::tr("Version")});

    for (const VscodeManifest &manifest : ExtensionRegistry::scan(settings().extensionsDir())) {
        auto name = new QStandardItem(
            manifest.displayName.isEmpty() ? manifest.name : manifest.displayName);
        name->setCheckable(true);
        name->setData(manifest.qualifiedId(), Qt::UserRole);
        name->setEditable(false);
        auto id = new QStandardItem(manifest.qualifiedId());
        id->setEditable(false);
        auto version = new QStandardItem(manifest.version);
        version->setEditable(false);
        m_model->appendRow({name, id, version});
    }

    m_proxy = new QSortFilterProxyModel(m_model);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1); // name, identifier and version alike

    auto view = new QTreeView;
    view->setObjectName("enabledExtensionsView");
    view->setModel(m_proxy);
    m_model->setParent(view);
    view->setRootIsDecorated(false);
    view->setUniformRowHeights(true);
    view->setSelectionMode(QAbstractItemView::NoSelection);
    view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    view->header()->setStretchLastSection(true);

    auto filter = new FancyLineEdit;
    filter->setObjectName("extensionsFilterEdit");
    filter->setFiltering(true);
    filter->setPlaceholderText(Tr::tr("Filter Extensions"));
    connect(filter, &FancyLineEdit::textChanged, m_proxy, [this](const QString &text) {
        m_proxy->setFilterFixedString(text);
    });

    // The aspect machinery does the rest: this marks the page dirty, which arms
    // Apply and Discard, and feeds guiToVolatileValue() below.
    connect(m_model, &QStandardItemModel::itemChanged, this, [this] { handleGuiChanged(); });

    volatileValueToGui();

    auto container = createSubWidget<QWidget>();
    using namespace Layouting;
    Column {
        Tr::tr("Select the VS Code extensions to run. Extensions are found "
               "automatically but stay inactive until enabled here."),
        filter,
        view,
        noMargin,
    }.attachTo(container);
    parent.addItem(container);
}

bool EnabledExtensionsAspect::guiToVolatileValue()
{
    if (!m_model)
        return false;
    QStringList enabled;
    for (int row = 0, n = m_model->rowCount(); row < n; ++row) {
        const QStandardItem *item = m_model->item(row, 0);
        if (item->checkState() == Qt::Checked)
            enabled << item->data(Qt::UserRole).toString();
    }
    const QString joined = enabled.join('\n');
    if (joined == m_volatileValue)
        return false;
    m_volatileValue = joined;
    return true;
}

void EnabledExtensionsAspect::volatileValueToGui()
{
    if (!m_model)
        return;
    const QStringList enabled = m_volatileValue.split('\n', Qt::SkipEmptyParts);
    QSignalBlocker blocker(m_model); // setting states is not a user edit
    for (int row = 0, n = m_model->rowCount(); row < n; ++row) {
        QStandardItem *item = m_model->item(row, 0);
        const QString id = item->data(Qt::UserRole).toString();
        item->setCheckState(enabled.contains(id) ? Qt::Checked : Qt::Unchecked);
    }
}

AlienSettings::AlienSettings()
{
    setAutoApply(false);
    setSettingsGroup("Alien");

    enable.setSettingsKey("Enable");
    enable.setLabelText(Tr::tr("Enable VS Code extension support"));
    enable.setToolTip(Tr::tr("Discover VS Code extensions and surface their "
                             "language servers in the editor. Experimental."));
    enable.setDefaultValue(false);

    nodeJsPath.setSettingsKey("NodeJsPath");
    nodeJsPath.setExpectedKind(PathChooserKind::ExistingCommand);
    // The host runs wherever node is, so this may point at a device.
    nodeJsPath.setAllowPathFromDevice(true);
    nodeJsPath.setDefaultPathValue(FilePath("node").searchInPath());
    nodeJsPath.setLabelText(Tr::tr("Node.js path:"));
    nodeJsPath.setToolTip(Tr::tr("Path to the node.js executable used to run "
                                 "extension code."));

    extensionsDir.setSettingsKey("ExtensionsDir");
    extensionsDir.setExpectedKind(PathChooserKind::ExistingDirectory);
    extensionsDir.setAllowPathFromDevice(true);
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

    assumeMainIsStdioServer.setSettingsKey("AssumeMainIsStdioServer");
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
            enabledExtensions,
        };
    });

    readSettings();
}

// One page for everything: general options and the extension list, the latter
// contributed by EnabledExtensionsAspect.
class AlienSettingsPage final : public Core::IOptionsPage
{
public:
    AlienSettingsPage()
    {
        setId(Constants::SETTINGS_ID);
        setDisplayName(Tr::tr("VS Code"));
        // Upstream turned the extension mode into a settings category, so
        // this belongs there rather than in one of its own.
        setCategory(ExtensionManager::Constants::EXTENSIONMANAGER_SETTINGSPAGE_CATEGORY);
        setSettingsProvider([] { return &settings(); });
    }
};

void setupAlienSettings()
{
    // Registered here as well as by the ExtensionManager plugin, with the same
    // id and name: registering a category twice is harmless, and it saves a
    // plugin dependency taken purely to have somewhere to put this page.
    Core::IOptionsPage::registerCategory(
        ExtensionManager::Constants::EXTENSIONMANAGER_SETTINGSPAGE_CATEGORY,
        Tr::tr("Extensions"),
        ":/extensionmanager/images/settingscategory_extensionmanager.png");

    static AlienSettingsPage theSettingsPage;
}

} // namespace Alien::Internal
