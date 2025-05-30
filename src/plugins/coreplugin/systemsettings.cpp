// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "systemsettings.h"

#include "coreconstants.h"
#include "coreplugintr.h"
#include "editormanager/editormanager_p.h"
#include "dialogs/ioptionspage.h"
#include "fileutils.h"
#include "icore.h"
#include "iversioncontrol.h" // sic!
#include "vcsmanager.h"

#ifdef ENABLE_CRASHREPORTING
#include "coreplugin.h"
#endif

#include <utils/algorithm.h>
#include <utils/appinfo.h>
#include <utils/checkablemessagebox.h>
#include <utils/elidinglabel.h>
#include <utils/environment.h>
#include <utils/environmentdialog.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/terminalcommand.h>
#include <utils/unixutils.h>

#include <QComboBox>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>

using namespace Utils;
using namespace Layouting;

namespace Core::Internal {

#ifdef CRASHREPORTING_USES_CRASHPAD
// TODO: move to somewhere in Utils
static QString formatSize(qint64 size)
{
    QStringList units{Tr::tr("Bytes"), Tr::tr("KiB"), Tr::tr("MiB"), Tr::tr("GiB"), Tr::tr("TiB")};
    double outputSize = size;
    int i;
    for (i = 0; i < units.size() - 1; ++i) {
        if (outputSize < 1024)
            break;
        outputSize /= 1024;
    }
    return i == 0 ? QString("%0 %1").arg(outputSize).arg(units[i]) // Bytes
                  : QString("%0 %1").arg(outputSize, 0, 'f', 2).arg(units[i]); // KB, MB, GB, TB
}
#endif // CRASHREPORTING_USES_CRASHPAD

SystemSettings &systemSettings()
{
    static SystemSettings theSettings;
    return theSettings;
}

SystemSettings::SystemSettings()
    : m_startupSystemEnvironment(Environment::systemEnvironment())
{
    const EnvironmentItems changes = EnvironmentItem::fromStringList(
        ICore::settings()->value(kEnvironmentChanges).toStringList());
    setEnvironmentChanges(changes);

    setAutoApply(false);

    patchCommand.setSettingsKey("General/PatchCommand");
    patchCommand.setDefaultValue("patch");
    patchCommand.setExpectedKind(PathChooser::ExistingCommand);
    patchCommand.setHistoryCompleter("General.PatchCommand.History");
    patchCommand.setLabelText(Tr::tr("Patch command:"));
    patchCommand.setToolTip(Tr::tr("Command used for reverting diff chunks."));

    autoSaveModifiedFiles.setSettingsKey("EditorManager/AutoSaveEnabled");
    autoSaveModifiedFiles.setDefaultValue(true);
    autoSaveModifiedFiles.setLabelText(Tr::tr("Auto-save modified files"));
    autoSaveModifiedFiles.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    autoSaveModifiedFiles.setToolTip(
        Tr::tr("Automatically creates temporary copies of modified files. "
               "If %1 is restarted after a crash or power failure, it asks whether to "
               "recover the auto-saved content.")
            .arg(QGuiApplication::applicationDisplayName()));

    autoSaveInterval.setSettingsKey("EditorManager/AutoSaveInterval");
    autoSaveInterval.setSuffix(Tr::tr("min"));
    autoSaveInterval.setRange(1, 1000000);
    autoSaveInterval.setDefaultValue(5);
    autoSaveInterval.setLabelText(Tr::tr("Interval:"));

    autoSaveAfterRefactoring.setSettingsKey("EditorManager/AutoSaveAfterRefactoring");
    autoSaveAfterRefactoring.setDefaultValue(true);
    autoSaveAfterRefactoring.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    autoSaveAfterRefactoring.setLabelText(Tr::tr("Auto-save files after refactoring"));
    autoSaveAfterRefactoring.setToolTip(
        Tr::tr("Automatically saves all open files affected by a refactoring operation,\n"
               "provided they were unmodified before the refactoring."));

    autoSuspendEnabled.setSettingsKey("EditorManager/AutoSuspendEnabled");
    autoSuspendEnabled.setDefaultValue(true);
    autoSuspendEnabled.setLabelText(Tr::tr("Auto-suspend unmodified files"));
    autoSuspendEnabled.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    autoSuspendEnabled.setToolTip(
        Tr::tr("Automatically free resources of old documents that are not visible and not "
               "modified. They stay visible in the list of open documents."));

    autoSuspendMinDocumentCount.setSettingsKey("EditorManager/AutoSuspendMinDocuments");
    autoSuspendMinDocumentCount.setRange(1, 500);
    autoSuspendMinDocumentCount.setDefaultValue(10);
    autoSuspendMinDocumentCount.setLabelText(Tr::tr("Files to keep open:"));
    autoSuspendMinDocumentCount.setToolTip(
        Tr::tr("Minimum number of open documents that should be kept in memory. Increasing this "
           "number will lead to greater resource usage when not manually closing documents."));

    warnBeforeOpeningBigFiles.setSettingsKey("EditorManager/WarnBeforeOpeningBigTextFiles");
    warnBeforeOpeningBigFiles.setDefaultValue(true);
    warnBeforeOpeningBigFiles.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    warnBeforeOpeningBigFiles.setLabelText(Tr::tr("Warn before opening text files greater than"));

    bigFileSizeLimitInMB.setSettingsKey("EditorManager/BigTextFileSizeLimitInMB");
    bigFileSizeLimitInMB.setSuffix(Tr::tr("MB"));
    bigFileSizeLimitInMB.setRange(1, 500);
    bigFileSizeLimitInMB.setDefaultValue(5);

    maxRecentFiles.setSettingsKey("EditorManager/MaxRecentFiles");
    maxRecentFiles.setRange(1, 99);
    maxRecentFiles.setDefaultValue(8);

    reloadSetting.setSettingsKey("EditorManager/ReloadBehavior");
    reloadSetting.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    reloadSetting.addOption(Tr::tr("Always Ask"));
    reloadSetting.addOption(Tr::tr("Reload All Unchanged Editors"));
    reloadSetting.addOption(Tr::tr("Ignore Modifications"));
    reloadSetting.setDefaultValue(IDocument::ReloadUnmodified);
    reloadSetting.setLabelText(Tr::tr("When files are externally modified:"));

    askBeforeExit.setSettingsKey("AskBeforeExit");
    askBeforeExit.setLabelText(Tr::tr("Ask for confirmation before exiting"));
    askBeforeExit.setLabelPlacement(BoolAspect::LabelPlacement::Compact);

#ifdef ENABLE_CRASHREPORTING
    enableCrashReporting.setSettingsKey("CrashReportingEnabled");
    enableCrashReporting.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    enableCrashReporting.setLabelText(Tr::tr("Enable crash reporting"));
    enableCrashReporting.setToolTip(
        "<p>"
        + Tr::tr("Allow crashes to be automatically reported. Collected reports are "
                 "used for the sole purpose of fixing bugs.")
        + "</p><p>"
        + Tr::tr("Crash reports are saved in \"%1\".").arg(appInfo().crashReports.toUserOutput()));
#endif // ENABLE_CRASHREPORTING
    readSettings();

    autoSaveInterval.setEnabler(&autoSaveModifiedFiles);
    autoSuspendMinDocumentCount.setEnabler(&autoSuspendEnabled);
    bigFileSizeLimitInMB.setEnabler(&warnBeforeOpeningBigFiles);

    autoSaveModifiedFiles.addOnChanged(this, &EditorManagerPrivate::updateAutoSave);
    autoSaveInterval.addOnChanged(this, &EditorManagerPrivate::updateAutoSave);
}

class SystemSettingsWidget : public IOptionsPageWidget
{
public:
    SystemSettingsWidget()
        : m_fileSystemCaseSensitivityChooser(HostOsInfo::isMacHost() ? new QComboBox : nullptr)
        , m_externalFileBrowserEdit(new QLineEdit)
        , m_terminalComboBox(new QComboBox)
        , m_terminalOpenArgs(new QLineEdit)
        , m_terminalExecuteArgs(new QLineEdit)
        , m_environmentChangesLabel(new Utils::ElidingLabel)
#ifdef CRASHREPORTING_USES_CRASHPAD
        , m_crashReportsMenuButton(new QPushButton(Tr::tr("Manage"), this))
        , m_crashReportsSizeText(new QLabel(this))
#endif

    {
        SystemSettings &s = systemSettings();

        m_terminalExecuteArgs->setToolTip(
            Tr::tr("Command line arguments used for \"Run in terminal\"."));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sizePolicy.setHorizontalStretch(5);
        m_environmentChangesLabel->setSizePolicy(sizePolicy);
        QSizePolicy termSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        termSizePolicy.setHorizontalStretch(3);
        m_terminalComboBox->setSizePolicy(termSizePolicy);
        m_terminalComboBox->setMinimumSize(QSize(100, 0));
        m_terminalComboBox->setEditable(true);
        m_terminalOpenArgs->setToolTip(
            Tr::tr("Command line arguments used for \"%1\".").arg(FileUtils::msgTerminalHereAction()));

        auto resetFileBrowserButton = new QPushButton(Tr::tr("Reset"));
        resetFileBrowserButton->setToolTip(Tr::tr("Reset to default."));
        auto helpExternalFileBrowserButton = new QToolButton;
        helpExternalFileBrowserButton->setText(Tr::tr("?"));
#ifdef ENABLE_CRASHREPORTING
        auto helpCrashReportingButton = new QToolButton(this);
        helpCrashReportingButton->setText(Tr::tr("?"));
        connect(helpCrashReportingButton, &QAbstractButton::clicked, this, [this] {
            showHelpDialog(Tr::tr("Crash Reporting"), CorePlugin::msgCrashpadInformation());
        });
#endif
        auto resetTerminalButton = new QPushButton(Tr::tr("Reset"));
        resetTerminalButton->setToolTip(Tr::tr("Reset to default.", "Terminal"));
        auto environmentButton = new QPushButton(Tr::tr("Change..."));
        environmentButton->setSizePolicy(QSizePolicy::Fixed,
                                         environmentButton->sizePolicy().verticalPolicy());

        Grid grid;
        grid.addRow({Tr::tr("Environment:"),
                     Span(3, m_environmentChangesLabel), environmentButton});
        if (HostOsInfo::isAnyUnixHost()) {
            grid.addRow({Tr::tr("Terminal:"),
                         m_terminalComboBox,
                         m_terminalOpenArgs,
                         m_terminalExecuteArgs,
                         resetTerminalButton});
        }
        if (HostOsInfo::isAnyUnixHost() && !HostOsInfo::isMacHost()) {
            grid.addRow({Tr::tr("External file browser:"),
                         Span(3, m_externalFileBrowserEdit),
                         resetFileBrowserButton,
                         helpExternalFileBrowserButton});
        }
        grid.addRow({Span(4, s.patchCommand)});
        if (HostOsInfo::isMacHost()) {
            auto fileSystemCaseSensitivityLabel = new QLabel(Tr::tr("File system case sensitivity:"));
            fileSystemCaseSensitivityLabel->setToolTip(
                Tr::tr("Influences how file names are matched to decide if they are the same."));
            grid.addRow({fileSystemCaseSensitivityLabel,
                         m_fileSystemCaseSensitivityChooser});
        }
        grid.addRow({s.reloadSetting});
        grid.addRow({s.autoSaveModifiedFiles, Row{s.autoSaveInterval, st}});
        grid.addRow({s.autoSuspendEnabled, Row{s.autoSuspendMinDocumentCount, st}});
        grid.addRow({s.autoSaveAfterRefactoring});
        grid.addRow({s.warnBeforeOpeningBigFiles, Row{s.bigFileSizeLimitInMB, st}});
        grid.addRow({Tr::tr("Maximum number of entries in \"Recent Files\":"),
                    Row{s.maxRecentFiles, st}});
        grid.addRow({s.askBeforeExit});
#ifdef ENABLE_CRASHREPORTING
        Row crashDetails;
#ifdef CRASHREPORTING_USES_CRASHPAD
        m_crashReportsMenuButton->setToolTip(s.enableCrashReporting.toolTip());
        m_crashReportsSizeText->setToolTip(s.enableCrashReporting.toolTip());
        auto crashReportsMenu = new QMenu(m_crashReportsMenuButton);
        m_crashReportsMenuButton->setMenu(crashReportsMenu);
        crashDetails.addItems({m_crashReportsMenuButton, m_crashReportsSizeText});
#endif // CRASHREPORTING_USES_CRASHPAD
        crashDetails.addItem(helpCrashReportingButton);
        if (qtcEnvironmentVariableIsSet("QTC_SHOW_CRASHBUTTON")) {
            auto crashButton = new QPushButton("CRASH!!!");
            connect(crashButton, &QPushButton::clicked, [] {
                // do a real crash
                volatile int *a = reinterpret_cast<volatile int *>(NULL);
                *a = 1;
            });
            crashDetails.addItem(crashButton);
        }
        crashDetails.addItem(st);
        grid.addRow({s.enableCrashReporting, crashDetails});

#endif

        Column {
            Group {
                title(Tr::tr("System")),
                Column { grid, st }
            }
        }.attachTo(this);

        if (HostOsInfo::isAnyUnixHost()) {
            const QVector<TerminalCommand> availableTerminals
                = TerminalCommand::availableTerminalEmulators();
            for (const TerminalCommand &term : availableTerminals)
                m_terminalComboBox->addItem(term.command.toUserOutput(), QVariant::fromValue(term));
            updateTerminalUi(TerminalCommand::terminalEmulator());
            connect(m_terminalComboBox, &QComboBox::currentIndexChanged, this, [this](int index) {
                updateTerminalUi(m_terminalComboBox->itemData(index).value<TerminalCommand>());
            });
        }

        if (HostOsInfo::isAnyUnixHost() && !HostOsInfo::isMacHost()) {
            m_externalFileBrowserEdit->setText(UnixUtils::fileBrowser(ICore::settings()));
        }

#if defined(ENABLE_CRASHREPORTING) && defined(CRASHREPORTING_USES_CRASHPAD)
        const FilePaths reportsPaths
            = {ICore::crashReportsPath() / "completed",
               ICore::crashReportsPath() / "reports",
               ICore::crashReportsPath() / "attachments",
               ICore::crashReportsPath() / "pending",
               ICore::crashReportsPath() / "new"};

        auto openLocationAction = new QAction(Tr::tr("Go to Crash Reports"));
        connect(openLocationAction, &QAction::triggered, this, [reportsPaths] {
            const FilePath path = reportsPaths.first().parentDir();
            if (!QDesktopServices::openUrl(path.toUrl())) {
                qWarning() << "Failed to open path:" << path;
            }
        });
        crashReportsMenu->addAction(openLocationAction);

        auto clearAction = new QAction(Tr::tr("Clear Crash Reports"));
        crashReportsMenu->addAction(clearAction);

        const auto updateClearCrashWidgets = [this, reportsPaths] {
            qint64 size = 0;
            FilePath::iterateDirectories(
                reportsPaths,
                [&size](const FilePath &item) {
                    size += item.fileSize();
                    return IterationPolicy::Continue;
                },
                FileFilter({}, QDir::Files, QDirIterator::Subdirectories));
            m_crashReportsMenuButton->setEnabled(size > 0);
            m_crashReportsSizeText->setText(formatSize(size));
        };
        updateClearCrashWidgets();
        connect(
            clearAction,
            &QAction::triggered,
            this,
            [updateClearCrashWidgets, reportsPaths] {
                FilePath::iterateDirectories(
                    reportsPaths,
                    [](const FilePath &item) {
                        item.removeRecursively();
                        return IterationPolicy::Continue;
                    },
                    FileFilter({}, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot));
                updateClearCrashWidgets();
            });
#endif // ENABLE_CRASHREPORTING && CRASHREPORTING_USES_CRASHPAD

        if (HostOsInfo::isAnyUnixHost()) {
            connect(resetTerminalButton,
                    &QAbstractButton::clicked,
                    this,
                    &SystemSettingsWidget::resetTerminal);
            if (!HostOsInfo::isMacHost()) {
                connect(resetFileBrowserButton,
                        &QAbstractButton::clicked,
                        this,
                        &SystemSettingsWidget::resetFileBrowser);
                connect(helpExternalFileBrowserButton,
                        &QAbstractButton::clicked,
                        this,
                        &SystemSettingsWidget::showHelpForFileBrowser);
            }
        }

        if (HostOsInfo::isMacHost()) {
            Qt::CaseSensitivity defaultSensitivity
                    = OsSpecificAspects::fileNameCaseSensitivity(HostOsInfo::hostOs());
            if (defaultSensitivity == Qt::CaseSensitive) {
                m_fileSystemCaseSensitivityChooser->addItem(Tr::tr("Case Sensitive (Default)"),
                                                            Qt::CaseSensitive);
            } else {
                m_fileSystemCaseSensitivityChooser->addItem(Tr::tr("Case Sensitive"), Qt::CaseSensitive);
            }
            if (defaultSensitivity == Qt::CaseInsensitive) {
                m_fileSystemCaseSensitivityChooser->addItem(Tr::tr("Case Insensitive (Default)"),
                                                            Qt::CaseInsensitive);
            } else {
                m_fileSystemCaseSensitivityChooser->addItem(Tr::tr("Case Insensitive"),
                                                            Qt::CaseInsensitive);
            }
            const Qt::CaseSensitivity sensitivity = EditorManagerPrivate::readFileSystemSensitivity(
                ICore::settings());
            if (sensitivity == Qt::CaseSensitive)
                m_fileSystemCaseSensitivityChooser->setCurrentIndex(0);
            else
                m_fileSystemCaseSensitivityChooser->setCurrentIndex(1);
        }

        updatePath();

        m_environmentChangesLabel->setElideMode(Qt::ElideRight);
        m_environmentChanges = systemSettings().environmentChanges();
        updateEnvironmentChangesLabel();
        connect(environmentButton, &QPushButton::clicked, this, [this, environmentButton] {
            std::optional<EnvironmentItems> changes
                = runEnvironmentItemsDialog(environmentButton, m_environmentChanges);
            if (!changes)
                return;
            m_environmentChanges = *changes;
            updateEnvironmentChangesLabel();
            updatePath();
        });

        connect(VcsManager::instance(), &VcsManager::configurationChanged,
                this, &SystemSettingsWidget::updatePath);

        setOnCancel([] { systemSettings().cancel(); });
    }

private:
    void apply() final;

    void showHelpForFileBrowser();
    void resetFileBrowser();
    void resetTerminal();
    void updateTerminalUi(const Utils::TerminalCommand &term);
    void updatePath();
    void updateEnvironmentChangesLabel();
    void showHelpDialog(const QString &title, const QString &helpText);

    QComboBox *m_fileSystemCaseSensitivityChooser;
    QLineEdit *m_externalFileBrowserEdit;
    QComboBox *m_terminalComboBox;
    QLineEdit *m_terminalOpenArgs;
    QLineEdit *m_terminalExecuteArgs;
    Utils::ElidingLabel *m_environmentChangesLabel;
#ifdef CRASHREPORTING_USES_CRASHPAD
    QPushButton *m_crashReportsMenuButton;
    QLabel *m_crashReportsSizeText;
#endif
    QPointer<QMessageBox> m_dialog;
    EnvironmentItems m_environmentChanges;
};

void SystemSettingsWidget::apply()
{
    systemSettings().apply();
    systemSettings().writeSettings();

    QtcSettings *settings = ICore::settings();
    if (HostOsInfo::isAnyUnixHost()) {
        TerminalCommand::setTerminalEmulator({
            FilePath::fromUserInput(m_terminalComboBox->lineEdit()->text()),
            m_terminalOpenArgs->text(),
            m_terminalExecuteArgs->text()
        });
        if (!HostOsInfo::isMacHost()) {
            UnixUtils::setFileBrowser(settings, m_externalFileBrowserEdit->text());
        }
    }

    if (HostOsInfo::isMacHost()) {
        const Qt::CaseSensitivity sensitivity = EditorManagerPrivate::readFileSystemSensitivity(
            settings);
        const Qt::CaseSensitivity selectedSensitivity = Qt::CaseSensitivity(
            m_fileSystemCaseSensitivityChooser->currentData().toInt());
        if (selectedSensitivity != sensitivity) {
            EditorManagerPrivate::writeFileSystemSensitivity(settings, selectedSensitivity);
            ICore::askForRestart(
                Tr::tr("The file system case sensitivity change will take effect after restart."));
        }
    }

    systemSettings().setEnvironmentChanges(m_environmentChanges);
}

void SystemSettingsWidget::resetTerminal()
{
    if (HostOsInfo::isAnyUnixHost())
        m_terminalComboBox->setCurrentIndex(0);
}

void SystemSettingsWidget::updateTerminalUi(const TerminalCommand &term)
{
    m_terminalComboBox->lineEdit()->setText(term.command.toUserOutput());
    m_terminalOpenArgs->setText(term.openArgs);
    m_terminalExecuteArgs->setText(term.executeArgs);
}

void SystemSettingsWidget::resetFileBrowser()
{
    if (HostOsInfo::isAnyUnixHost() && !HostOsInfo::isMacHost())
        m_externalFileBrowserEdit->setText(UnixUtils::defaultFileBrowser());
}

void SystemSettingsWidget::updatePath()
{
    Environment env;
    env.appendToPath(VcsManager::additionalToolsPath());
    systemSettings().patchCommand.setEnvironment(env);
}

void SystemSettingsWidget::updateEnvironmentChangesLabel()
{
    const QString shortSummary = Utils::EnvironmentItem::toStringList(m_environmentChanges)
                                     .join("; ");
    m_environmentChangesLabel->setText(shortSummary.isEmpty() ? Tr::tr("No changes to apply.")
                                                              : shortSummary);
}

void SystemSettingsWidget::showHelpDialog(const QString &title, const QString &helpText)
{
    if (m_dialog) {
        if (m_dialog->windowTitle() != title)
            m_dialog->setText(helpText);

        if (m_dialog->text() != helpText)
            m_dialog->setText(helpText);

        m_dialog->show();
        ICore::raiseWindow(m_dialog);
        return;
    }
    auto mb = new QMessageBox(QMessageBox::Information, title, helpText, QMessageBox::Close, this);
    mb->setWindowModality(Qt::NonModal);
    m_dialog = mb;
    mb->show();
}

void SystemSettingsWidget::showHelpForFileBrowser()
{
    if (HostOsInfo::isAnyUnixHost() && !HostOsInfo::isMacHost())
        showHelpDialog(Tr::tr("Variables"), UnixUtils::fileBrowserHelpText());
}

EnvironmentItems SystemSettings::environmentChanges() const
{
    return m_environmentChanges;
}

void SystemSettings::setEnvironmentChanges(const EnvironmentItems &changes)
{
    if (m_environmentChanges == changes)
        return;
    m_environmentChanges = changes;
    Environment systemEnv = m_startupSystemEnvironment;
    systemEnv.modify(changes);
    Environment::setSystemEnvironment(systemEnv);
    ICore::settings()->setValueWithDefault(kEnvironmentChanges,
                                           EnvironmentItem::toStringList(changes));
    if (ICore::instance())
        emit ICore::instance()->systemEnvironmentChanged();
}

// SystemSettingsPage

class SystemSettingsPage final : public IOptionsPage
{
public:
    SystemSettingsPage()
    {
        setId(Constants::SETTINGS_ID_SYSTEM);
        setDisplayName(Tr::tr("System"));
        setCategory(Constants::SETTINGS_CATEGORY_CORE);
        setWidgetCreator([] { return new SystemSettingsWidget; });
    }
};

const SystemSettingsPage settingsPage;

} // Core::Internal
