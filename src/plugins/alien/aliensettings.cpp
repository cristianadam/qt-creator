// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aliensettings.h"

#include "alienconstants.h"
#include "alientr.h"
#include "extensionhost.h"
#include "extensionregistry.h"
#include "vscodemanifest.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <extensionmanager/extensionmanagerconstants.h>

#include <utils/fancylineedit.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QSpinBox>
#include <QScreen>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QTextDocument>
#include <QStandardItemModel>
#include <QTreeView>

using namespace Utils;

namespace Alien::Internal {

// The values an extension's settings have right now: what its manifest
// declares, under whatever has been written over it.
static QJsonObject currentValues(const VscodeManifest &manifest)
{
    QJsonObject values = manifest.configurationDefaults;
    const FilePaths overrides = {settings().extensionsDir() / "settings.json",
                                 ExtensionHost::writtenConfigurationFile()};
    for (const FilePath &file : overrides) {
        if (const Result<QByteArray> contents = file.fileContents()) {
            const QJsonObject written = QJsonDocument::fromJson(*contents).object();
            for (auto it = written.begin(); it != written.end(); ++it) {
                if (values.contains(it.key()))
                    values.insert(it.key(), it.value());
            }
        }
    }
    return values;
}

// A description an extension wrote as Markdown, shown as what it says rather
// than as its source.
static QString markdownToHtml(const QString &markdown)
{
    QTextDocument document;
    document.setMarkdown(markdown);
    return document.toHtml();
}

// An editor for one setting, chosen by what the manifest says it holds. A type
// with no obvious editor - an array of objects, say - is edited as the JSON it
// is, which is still better than not being able to reach it at all.
static QWidget *editorFor(const VscodeSetting &setting, const QJsonValue &value,
                          const std::function<void(const QJsonValue &)> &onChanged)
{
    if (!setting.enumValues.isEmpty()) {
        auto combo = new QComboBox;
        // The values need not be strings, and need not be what the user is
        // shown: the choice is by position, and the value goes back as it came.
        const QJsonArray values = setting.enumRawValues;
        for (int i = 0; i < setting.enumValues.size(); ++i) {
            combo->addItem(i < setting.enumItemLabels.size() ? setting.enumItemLabels.at(i)
                                                             : setting.enumValues.at(i));
        }
        for (int i = 0; i < values.size() && i < combo->count(); ++i) {
            if (values.at(i) == value)
                combo->setCurrentIndex(i);
        }
        for (int i = 0; i < setting.enumDescriptions.size() && i < combo->count(); ++i)
            combo->setItemData(i, setting.enumDescriptions.at(i), Qt::ToolTipRole);
        QObject::connect(combo, &QComboBox::activated, combo, [values, onChanged](int index) {
            if (index >= 0 && index < values.size())
                onChanged(values.at(index));
        });
        return combo;
    }
    if (setting.type == "boolean") {
        auto box = new QCheckBox;
        box->setChecked(value.toBool());
        QObject::connect(box, &QCheckBox::toggled, box, [onChanged](bool checked) {
            onChanged(checked);
        });
        return box;
    }
    if (setting.type == "integer") {
        auto spin = new QSpinBox;
        spin->setRange(setting.minimum ? int(*setting.minimum)
                                       : std::numeric_limits<int>::min(),
                       setting.maximum ? int(*setting.maximum)
                                       : std::numeric_limits<int>::max());
        spin->setValue(value.toInt());
        QObject::connect(spin, &QSpinBox::valueChanged, spin, [onChanged](int number) {
            onChanged(number);
        });
        return spin;
    }
    // A number is not an integer: an opacity of 0.55 is not 0.
    if (setting.type == "number") {
        auto spin = new QDoubleSpinBox;
        spin->setDecimals(3);
        spin->setRange(setting.minimum.value_or(std::numeric_limits<int>::min()),
                       setting.maximum.value_or(std::numeric_limits<int>::max()));
        spin->setSingleStep(0.1);
        spin->setValue(value.toDouble());
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, spin, [onChanged](double number) {
            onChanged(number);
        });
        return spin;
    }
    auto edit = new FancyLineEdit;
    const bool isText = setting.type == "string" || setting.type.isEmpty();
    edit->setText(isText ? value.toString()
                         : QString::fromUtf8(QJsonDocument(QJsonArray{value})
                                                 .toJson(QJsonDocument::Compact)
                                                 .mid(1)
                                                 .chopped(1)));
    QObject::connect(edit, &FancyLineEdit::textEdited, edit, [edit, isText, onChanged] {
        if (isText) {
            onChanged(edit->text());
            return;
        }
        // Parsed as the JSON it claims to be; unparsable text is left alone
        // rather than stored as a string that means something else.
        const QJsonDocument document
            = QJsonDocument::fromJson("[" + edit->text().toUtf8() + "]");
        if (!document.isNull())
            onChanged(document.array().first());
    });
    return edit;
}

ExtensionSettingsDialog::ExtensionSettingsDialog(const VscodeManifest &manifest,
                                                 QWidget *parent)
    : QDialog(parent)
    , m_values(currentValues(manifest))
{
    setObjectName("extensionSettingsDialog");
    setWindowTitle(manifest.displayName.isEmpty() ? manifest.qualifiedId()
                                                  : manifest.displayName);

    using namespace Layouting;
    auto content = new QWidget;
    Form form;
    QString shownSection;
    for (const VscodeSetting &setting : manifest.configurationSettings) {
        // An extension groups its settings under headings of its own; without
        // them a long list is one undifferentiated wall.
        if (!setting.section.isEmpty() && setting.section != shownSection) {
            shownSection = setting.section;
            auto heading = new QLabel(shownSection);
            heading->setObjectName("extensionSettingSection." + shownSection);
            QFont font = heading->font();
            font.setBold(true);
            heading->setFont(font);
            form.addItems({heading, br});
        }
        auto label = new QLabel(setting.key);
        // A description written as Markdown is shown as such, not as its source.
        label->setToolTip(setting.descriptionIsMarkdown
                              ? markdownToHtml(setting.description)
                              : setting.description);
        if (!setting.deprecationMessage.isEmpty()) {
            QFont font = label->font();
            font.setStrikeOut(true);
            label->setFont(font);
            label->setToolTip(setting.deprecationMessage);
        }
        QWidget *editor = editorFor(setting, m_values.value(setting.key),
                                    [this, key = setting.key](const QJsonValue &value) {
                                        m_edited.insert(key, value);
                                    });
        editor->setObjectName("extensionSetting." + setting.key);
        m_editors.insert(setting.key, editor);
        editor->setToolTip(label->toolTip());
        form.addItems({label, editor, br});
    }

    form.attachTo(content);

    // An extension may declare a great many settings - some declare hundreds -
    // so the form scrolls rather than growing a dialog taller than the screen.
    m_scrollArea = new QScrollArea;
    QScrollArea *scrollArea = m_scrollArea;
    scrollArea->setWidget(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    Column {
        scrollArea,
        buttons,
    }.attachTo(this);

    const QRect available = screen() ? screen()->availableGeometry() : QRect();
    if (!available.isEmpty()) {
        resize(sizeHint().width(),
               std::min(content->sizeHint().height() + buttons->sizeHint().height(),
                        available.height() * 2 / 3));
    }
}

void ExtensionSettingsDialog::focusSetting(const QString &key)
{
    QWidget *editor = m_editors.value(key);
    if (!editor)
        return;
    if (m_scrollArea)
        m_scrollArea->ensureWidgetVisible(editor);
    editor->setFocus();
}

// Which extension declares this setting. The key is the one an extension asks
// for, "qdocPreview.roots"; its own identifier is accepted too, so both of the
// things a caller might name are understood.
QString extensionOwning(const QString &query)
{
    if (query.isEmpty())
        return {};
    for (const VscodeManifest &manifest : ExtensionRegistry::scan(settings().extensionsDir())) {
        if (manifest.qualifiedId().compare(query, Qt::CaseInsensitive) == 0)
            return manifest.qualifiedId();
        for (const VscodeSetting &setting : manifest.configurationSettings) {
            if (setting.key.compare(query, Qt::CaseInsensitive) == 0)
                return manifest.qualifiedId();
        }
    }
    return {};
}

// The settings of one extension, opened on the setting that was asked for.
static void showExtensionSettings(const QString &extensionId, const QString &settingKey,
                                  QWidget *parent)
{
    for (const VscodeManifest &manifest : ExtensionRegistry::scan(settings().extensionsDir())) {
        if (manifest.qualifiedId() != extensionId)
            continue;
        if (manifest.configurationSettings.isEmpty())
            return;
        ExtensionSettingsDialog dialog(manifest, parent);
        if (!settingKey.isEmpty())
            dialog.focusSetting(settingKey);
        if (dialog.exec() != QDialog::Accepted || dialog.editedValues().isEmpty())
            return;
        const Result<> written = runningExtensionHost()
                                     ? runningExtensionHost()->writeConfiguration(
                                           dialog.editedValues())
                                     : ExtensionHost::writeConfigurationFile(dialog.editedValues());
        if (!written)
            Core::MessageManager::writeFlashing(written.error());
        return;
    }
}

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

    // A double click opens what the extension itself can be told, which is
    // otherwise only reachable by writing its settings file by hand.
    connect(view, &QAbstractItemView::doubleClicked, view,
            [view](const QModelIndex &index) {
                showExtensionSettings(index.siblingAtColumn(0).data(Qt::UserRole).toString(),
                                      {}, view);
            });

    // Somebody asked for a particular setting - an extension calling the
    // command that opens preferences names one - so the list is narrowed to
    // whoever owns it and its dialog opens on that setting.
    if (const Id preselected = Core::preselectedOptionsPageItem(Constants::SETTINGS_ID);
        preselected.isValid()) {
        const QString query = preselected.toString();
        const QString owner = extensionOwning(query);
        if (!owner.isEmpty()) {
            filter->setText(owner);
            QMetaObject::invokeMethod(view, [view, owner, query] {
                showExtensionSettings(owner, query, view);
            }, Qt::QueuedConnection);
        }
    }

    // The aspect machinery does the rest: this marks the page dirty, which arms
    // Apply and Discard, and feeds guiToVolatileValue() below.
    connect(m_model, &QStandardItemModel::itemChanged, this, [this] { handleGuiChanged(); });

    volatileValueToGui();

    auto container = createSubWidget<QWidget>();
    using namespace Layouting;
    Column {
        Tr::tr("Select the extensions to run. Extensions are found "
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
    enable.setLabelText(Tr::tr("Enable VSIX extension support"));
    enable.setToolTip(Tr::tr("Discover VSIX extensions - extensions written for Visual "
                             "Studio Code - and surface their language servers in "
                             "the editor. Experimental."));
    enable.setDefaultValue(false);

    legalNoticeAccepted.setSettingsKey("LegalNoticeAccepted");
    legalNoticeAccepted.setDefaultValue(false);

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
    // Qt Creator's own folder, which is what the installer writes to. An
    // editor's extensions are that editor's to run: some are licensed for one
    // only, so pointing this at another's installation is the user's decision
    // to make and not the default.
    extensionsDir.setDefaultPathValue(Core::ICore::userResourcePath("alien/extensions"));
    extensionsDir.setLabelText(Tr::tr("Extensions directory:"));
    extensionsDir.setToolTip(Tr::tr("Directory scanned for installed extensions. "
                                    "Each extension lives in its own subfolder "
                                    "containing a package.json file, which is what "
                                    "installing a .vsix leaves behind. Per-extension "
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
        setDisplayName(Tr::tr("VSIX Extensions"));
        // Upstream turned the extension mode into a settings category, so
        // this belongs there rather than in one of its own.
        setCategory(ExtensionManager::Constants::EXTENSIONMANAGER_SETTINGSPAGE_CATEGORY);
        setSettingsProvider([] { return &settings(); });
    }
};

std::unique_ptr<Core::IOptionsPage> setupAlienSettings()
{
    // Registered here as well as by the ExtensionManager plugin, with the same
    // id and name: registering a category twice is harmless, and it saves a
    // plugin dependency taken purely to have somewhere to put this page.
    Core::IOptionsPage::registerCategory(
        ExtensionManager::Constants::EXTENSIONMANAGER_SETTINGSPAGE_CATEGORY,
        Tr::tr("Extensions"),
        ":/extensionmanager/images/settingscategory_extensionmanager.png");

    return std::make_unique<AlienSettingsPage>();
}

} // namespace Alien::Internal
