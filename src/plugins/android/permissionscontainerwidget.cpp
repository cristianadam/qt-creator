// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "permissionscontainerwidget.h"
#include "androidtr.h"
#include "androidmanifestutils.h"

#include <cmakeprojectmanager/3rdparty/cmake/cmListFileCache.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildsystem.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>
#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtkitaspect.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <cmakeprojectmanager/cmakeprojectconstants.h>

#include <QAbstractListModel>
#include <QApplication>
#include <QCheckBox>
#include <QVersionNumber>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>


using namespace ProjectExplorer;
using namespace Utils;

namespace Android::Internal {

constexpr QLatin1StringView qtAddAndroidPermission("qt_add_android_permission");

struct ParsedPermission {
    QString name;
    QMap<QString, QString> attributes;
};

static std::optional<ParsedPermission> parsePermissionCall(const cmListFileFunction &func)
{
    const std::vector<cmListFileArgument> &args = func.Arguments();
    ParsedPermission result;
    bool inAttributes = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i].Value == "NAME" && i + 1 < args.size()) {
            result.name = QString::fromStdString(args[++i].Value);
        } else if (args[i].Value == "ATTRIBUTES") {
            inAttributes = true;
        } else if (inAttributes && i + 1 < args.size()) {
            result.attributes.insert(QString::fromStdString(args[i].Value),
                                     QString::fromStdString(args[i + 1].Value));
            ++i;
        }
    }
    if (result.name.isEmpty())
        return std::nullopt;
    return result;
}

static std::optional<cmListFile> parseCMakeFile(const Utils::FilePath &path,
                                                QString &outContent)
{
    QFile file(path.toFSPathString());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    outContent = QTextStream(&file).readAll();
    cmListFile result;
    std::string error;
    if (!result.ParseString(outContent.toStdString(), path.fileName().toStdString(), error))
        return std::nullopt;
    return result;
}

static bool isPermissionCallForTarget(const cmListFileFunction &func, const std::string &target,
                                      const std::string &permission = {})
{
    if (func.LowerCaseName() != qtAddAndroidPermission.data())
        return false;
    const auto &args = func.Arguments();
    if (args.empty() || args[0].Value != target)
        return false;
    if (permission.empty())
        return true;
    for (size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i].Value == "NAME" && args[i + 1].Value == permission)
            return true;
    }
    return false;
}

static QMap<QString, QMap<QString, QString>> parseCMakePermissions(
    const Utils::FilePath &path, const QString &targetName)
{
    QMap<QString, QMap<QString, QString>> permissions;
    QString content;
    const std::optional<cmListFile> cmakeFile = parseCMakeFile(path, content);
    if (!cmakeFile)
        return permissions;

    const std::string target = targetName.toStdString();
    for (const cmListFileFunction &func : cmakeFile->Functions) {
        if (!isPermissionCallForTarget(func, target))
            continue;
        const std::optional<ParsedPermission> parsed = parsePermissionCall(func);
        if (!parsed)
            continue;
        auto existingIt = permissions.find(parsed->name);
        if (existingIt == permissions.end()) {
            permissions.insert(parsed->name, parsed->attributes);
        } else if (existingIt.value().isEmpty() && !parsed->attributes.isEmpty()) {
            existingIt.value().insert(parsed->attributes);
        }
    }
    return permissions;
}

static QMap<QString, QMap<QString, QString>> mergePermissions(
    const QMap<QString, QMap<QString, QString>> &primary,
    const QMap<QString, QMap<QString, QString>> &secondary)
{
    QMap<QString, QMap<QString, QString>> result = primary;
    for (auto it = secondary.cbegin(); it != secondary.cend(); ++it) {
        auto existingIt = result.find(it.key());
        if (existingIt == result.end()) {
            result.insert(it.key(), it.value());
        } else if (existingIt.value().isEmpty() && !it.value().isEmpty()) {
            existingIt.value() = it.value();
        }
    }
    return result;
}

static bool removeAllPermissionCalls(const Utils::FilePath &path, const QString &targetName)
{
    QString content;
    const std::optional<cmListFile> cmakeFile = parseCMakeFile(path, content);
    if (!cmakeFile)
        return false;

    const std::string target = targetName.toStdString();
    QList<std::pair<long, long>> ranges; // 1-based, inclusive
    for (const cmListFileFunction &func : cmakeFile->Functions) {
        if (isPermissionCallForTarget(func, target))
            ranges.append({func.Line(), func.LineEnd()});
    }
    if (ranges.isEmpty())
        return true;

    QStringList lines = content.split('\n');
    for (auto it = ranges.crbegin(); it != ranges.crend(); ++it)
        lines.remove(static_cast<qsizetype>(it->first) - 1,
                     static_cast<qsizetype>(it->second - it->first) + 1);

    Utils::FileSaver saver(path, QIODevice::Text);
    saver.write(lines.join('\n').toUtf8());
    return (bool)saver.finalize();
}

static QString quotedCMakeArgument(const QString &value)
{
    static const QRegularExpression unsafe(R"([\s()#"\\;])");
    if (!value.isEmpty() && !value.contains(unsafe))
        return value;
    QString escaped = value;
    escaped.replace('\\', "\\\\");
    escaped.replace('"', "\\\"");
    return '"' + escaped + '"';
}

static QString buildPermissionCall(const QString &target, const QString &permission,
                                   const QMap<QString, QString> &attributes)
{
    QString call = QString("%1(%2 NAME %3")
                    .arg(qtAddAndroidPermission, target, quotedCMakeArgument(permission));
    if (!attributes.isEmpty()) {
        call += "\n    ATTRIBUTES";
        for (auto it = attributes.cbegin(); it != attributes.cend(); ++it)
            call += QString("\n        %1 %2").arg(quotedCMakeArgument(it.key()),
                                                   quotedCMakeArgument(it.value()));
    }
    call += "\n)";
    return call;
}

class PermissionsModel : public QAbstractListModel
{
public:
    PermissionsModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    void setPermissions(const QMap<QString, QMap<QString, QString>> &permissions)
    {
        beginResetModel();
        m_permissions = permissions;
        endResetModel();
    }

    const QMap<QString, QMap<QString, QString>> &permissions() const { return m_permissions; }


    QModelIndex addPermission(const QString &permission)
    {
        if (!m_permissions.contains(permission)) {
            const int row = std::distance(m_permissions.begin(),
                                          m_permissions.lowerBound(permission));
            beginInsertRows(QModelIndex(), row, row);
            m_permissions.insert(permission, {});
            endInsertRows();
            return index(row, 0);
        }
        return QModelIndex();
    }

    void removePermission(int row)
    {
        if (row >= 0 && row < m_permissions.size()) {
            beginRemoveRows(QModelIndex(), row, row);
            auto it = std::next(m_permissions.begin(), row);
            m_permissions.erase(it);
            endRemoveRows();
        }
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 2;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            if (section == 0)
                return Tr::tr("Permission");
            if (section == 1)
                return Tr::tr("Attributes");
        }
        return QVariant();
    }

    void setAttributes(const QString &permission, const QMap<QString, QString> &attributes)
    {
        auto it = m_permissions.find(permission);
        if (it != m_permissions.end() && it.value() != attributes) {
            it.value() = attributes;
            const int row = std::distance(m_permissions.begin(), it);
            emit dataChanged(index(row, 1), index(row, 1));
        }
    }

    void removeAttribute(const QString &permission, const QString &attrName)
    {
        auto it = m_permissions.find(permission);
        if (it != m_permissions.end()) {
            if (it.value().remove(attrName) > 0) {
                const int row = std::distance(m_permissions.begin(), it);
                emit dataChanged(index(row, 1), index(row, 1));
            }
        }
    }

    QVariant data(const QModelIndex &idx, int role) const override
    {
        if (!idx.isValid() || idx.row() >= m_permissions.size())
            return QVariant();

        auto it = std::next(m_permissions.cbegin(), idx.row());

        switch (role) {
        case Qt::DisplayRole: {
            switch (idx.column()) {
            case 0:
                return it.key();
            case 1: {
                if (it.value().isEmpty())
                    return Tr::tr("No attributes");

                QStringList parts;
                for (auto ait = it.value().cbegin(); ait != it.value().cend(); ++ait)
                    parts.append(ait.key() + ": " + ait.value());
                return parts.join(", ");
            }
            default:
                return QVariant();
            }
        }
        case Qt::ForegroundRole: {
            if (idx.column() == 1 && it.value().isEmpty()) {
                return QApplication::palette().color(QPalette::PlaceholderText);
            }
            return QVariant();
        }
        default:
            return QVariant();
        }
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_permissions.size();
    }

private:
    QMap<QString, QMap<QString, QString>> m_permissions; // key = permission, value = <attrName: Value>
};

PermissionsContainerWidget::PermissionsContainerWidget(QWidget *parent)
    : QWidget(parent)
{
}

bool PermissionsContainerWidget::hasPermissionsInManifest(Utils::FilePath &manifestPath)
{
    if (!m_textEditorWidget || !m_textEditorWidget->textDocument())
        return false;

    auto dataResult = AndroidManifestParser::readManifest(manifestPath);
    if (!dataResult)
        return false;

    return !dataResult->permissions.isEmpty();
}

bool PermissionsContainerWidget::isCMakePermissionsSupported()
{
    if (!m_textEditorWidget || !m_textEditorWidget->textDocument())
        return false;

    Utils::FilePath manifestPath = m_textEditorWidget->textDocument()->filePath();
    if (manifestPath.isEmpty())
        return false;

    if (hasPermissionsInManifest(manifestPath))
        return false;
    Project *project = ProjectManager::projectForFile(manifestPath);
    if (!project)
        return false;

    const QList<Target *> targets = project->targets();

    QVersionNumber lowestTargetQtVersion;
    if (targets.isEmpty()) {
        lowestTargetQtVersion = QVersionNumber(6, 9);
    } else {
        for (Target *target : targets) {
            QtSupport::QtVersion *qtVersion = QtSupport::QtKitAspect::qtVersion(target->kit());
            if (qtVersion) {
                QVersionNumber ver = qtVersion->qtVersion();
                if (lowestTargetQtVersion.isNull() || ver < lowestTargetQtVersion)
                    lowestTargetQtVersion = ver;
            }
        }
    }

    if (lowestTargetQtVersion.isNull())
        lowestTargetQtVersion = QVersionNumber(6, 9);

    if (lowestTargetQtVersion < QVersionNumber(6, 9))
        return false;

    return true;
}

static const Utils::Key suppressCMakeWarning{"Android.SuppressCMakePermissionsWarning"};

void PermissionsContainerWidget::showCMakePermissionsConsentDialog()
{
    if (!m_CMakePermissionsCheckBox->isChecked())
        return;

    Project *project = m_textEditorWidget && m_textEditorWidget->textDocument()
                           ? ProjectManager::projectForFile(m_textEditorWidget->textDocument()->filePath())
                           : nullptr;

    if (project && project->namedSettings(suppressCMakeWarning).toBool())
        return;

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(Tr::tr("CMake Permission Management"));
    msgBox.setText(Tr::tr("Permissions will be managed via qt_add_android_permission() calls in "
                          "CMakeLists.txt instead of AndroidManifest.xml. "
                          "This requires Qt 6.9 or newer."));
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);

    QCheckBox *dontAskCheckBox = new QCheckBox(Tr::tr("Don't ask again"), &msgBox);
    msgBox.setCheckBox(dontAskCheckBox);

    if (msgBox.exec() == QMessageBox::Cancel) {
        m_CMakePermissionsCheckBox->setCheckState(Qt::Unchecked);
        return;
    }

    if (project && dontAskCheckBox->isChecked())
        project->setNamedSettings(suppressCMakeWarning, true);
}

void PermissionsContainerWidget::onCMakePermissionsCheckBoxChanged()
{
    if (m_CMakePermissionsCheckBox->isChecked()) {
        m_CMakePermissionsCheckBox->blockSignals(true);
        showCMakePermissionsConsentDialog();
        m_CMakePermissionsCheckBox->blockSignals(false);
        if (!m_CMakePermissionsCheckBox->isChecked())
            return;
        if (const Utils::Result<> result = migratePermissionsManifestToCMake()) {
            loadPermissionsFromCMake();
         } else {
            revertCMakePermissionsCheckBox(Qt::Unchecked, result.error());
            loadPermissionsFromManifest();
        }
    } else {
        if (const Utils::Result<> result = migratePermissionsCMakeToManifest())
            loadPermissionsFromManifest();
        else
            revertCMakePermissionsCheckBox(Qt::Checked, result.error());
    }
}

void PermissionsContainerWidget::revertCMakePermissionsCheckBox(Qt::CheckState state,
                                                              const QString &error)
{
    const QSignalBlocker blocker(m_CMakePermissionsCheckBox);
    m_CMakePermissionsCheckBox->setCheckState(state);
    QMessageBox::warning(this, Tr::tr("CMake Permission Management"),
                         Tr::tr("Cannot migrate permissions: %1").arg(error));
}

void PermissionsContainerWidget::updateCMakePermissionsCheckBoxState()
{
    bool checked = false;
    if (resolveCMakeProjectInfo()
        && !parseCMakePermissions(m_CMakeFilePath, m_CMakeTargetName).isEmpty()) {
        checked = true; // CMake already manages permissions
    } else {
        Utils::FilePath manifestPath = m_textEditorWidget->textDocument()->filePath();
        checked = !hasPermissionsInManifest(manifestPath) && isCMakePermissionsSupported();
    }

    const QSignalBlocker blocker(m_CMakePermissionsCheckBox);
    m_CMakePermissionsCheckBox->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}

bool PermissionsContainerWidget::initialize(TextEditor::TextEditorWidget *textEditorWidget)
{
    if (!textEditorWidget)
        return false;

    m_textEditorWidget = textEditorWidget;

    auto mainLayout = new QVBoxLayout(this);
    auto permissionsGroupBox = new QGroupBox(this);
    permissionsGroupBox->setTitle(Android::Tr::tr("Permissions"));
    auto layout = new QGridLayout(permissionsGroupBox);

    m_defaultPermissonsCheckBox = new QCheckBox(this);
    m_defaultPermissonsCheckBox->setText(
        Android::Tr::tr("Include default permissions for Qt modules."));
    layout->addWidget(m_defaultPermissonsCheckBox, 0, 0);

    m_defaultFeaturesCheckBox = new QCheckBox(this);
    m_defaultFeaturesCheckBox->setText(Android::Tr::tr("Include default features for Qt modules."));
    layout->addWidget(m_defaultFeaturesCheckBox, 1, 0);

    m_CMakePermissionsCheckBox = new QCheckBox(this);
    m_CMakePermissionsCheckBox->setText(Android::Tr::tr("Manage permissions through CMake."));
    m_CMakePermissionsCheckBox->setToolTip(
        Tr::tr("Permissions will be added as qt_add_android_permission() calls in CMakeLists.txt "
               "instead of AndroidManifest.xml. This requires Qt 6.9 or newer."));
    updateCMakePermissionsCheckBoxState();
    layout->addWidget(m_CMakePermissionsCheckBox, 2, 0);

    m_permissionsComboBox = new QComboBox(permissionsGroupBox);
    m_permissionsComboBox->insertItems(0, {
              "android.permission.ACCESS_CHECKIN_PROPERTIES",
              "android.permission.ACCESS_COARSE_LOCATION",
              "android.permission.ACCESS_FINE_LOCATION",
              "android.permission.ACCESS_LOCATION_EXTRA_COMMANDS",
              "android.permission.ACCESS_MOCK_LOCATION",
              "android.permission.ACCESS_NETWORK_STATE",
              "android.permission.ACCESS_SURFACE_FLINGER",
              "android.permission.ACCESS_WIFI_STATE",
              "android.permission.ACCOUNT_MANAGER",
              "com.android.voicemail.permission.ADD_VOICEMAIL",
              "android.permission.AUTHENTICATE_ACCOUNTS",
              "android.permission.BATTERY_STATS",
              "android.permission.BIND_ACCESSIBILITY_SERVICE",
              "android.permission.BIND_APPWIDGET",
              "android.permission.BIND_DEVICE_ADMIN",
              "android.permission.BIND_INPUT_METHOD",
              "android.permission.BIND_REMOTEVIEWS",
              "android.permission.BIND_TEXT_SERVICE",
              "android.permission.BIND_VPN_SERVICE",
              "android.permission.BIND_WALLPAPER",
              "android.permission.BLUETOOTH",
              "android.permission.BLUETOOTH_ADMIN",
              "android.permission.BRICK",
              "android.permission.BROADCAST_PACKAGE_REMOVED",
              "android.permission.BROADCAST_SMS",
              "android.permission.BROADCAST_STICKY",
              "android.permission.BROADCAST_WAP_PUSH",
              "android.permission.CALL_PHONE",
              "android.permission.CALL_PRIVILEGED",
              "android.permission.CAMERA",
              "android.permission.CHANGE_COMPONENT_ENABLED_STATE",
              "android.permission.CHANGE_CONFIGURATION",
              "android.permission.CHANGE_NETWORK_STATE",
              "android.permission.CHANGE_WIFI_MULTICAST_STATE",
              "android.permission.CHANGE_WIFI_STATE",
              "android.permission.CLEAR_APP_CACHE",
              "android.permission.CLEAR_APP_USER_DATA",
              "android.permission.CONTROL_LOCATION_UPDATES",
              "android.permission.DELETE_CACHE_FILES",
              "android.permission.DELETE_PACKAGES",
              "android.permission.DEVICE_POWER",
              "android.permission.DIAGNOSTIC",
              "android.permission.DISABLE_KEYGUARD",
              "android.permission.DUMP",
              "android.permission.EXPAND_STATUS_BAR",
              "android.permission.FACTORY_TEST",
              "android.permission.FLASHLIGHT",
              "android.permission.FORCE_BACK",
              "android.permission.GET_ACCOUNTS",
              "android.permission.GET_PACKAGE_SIZE",
              "android.permission.GET_TASKS",
              "android.permission.GLOBAL_SEARCH",
              "android.permission.HARDWARE_TEST",
              "android.permission.INJECT_EVENTS",
              "android.permission.INSTALL_LOCATION_PROVIDER",
              "android.permission.INSTALL_PACKAGES",
              "android.permission.INTERNAL_SYSTEM_WINDOW",
              "android.permission.INTERNET",
              "android.permission.KILL_BACKGROUND_PROCESSES",
              "android.permission.MANAGE_ACCOUNTS",
              "android.permission.MANAGE_APP_TOKENS",
              "android.permission.MASTER_CLEAR",
              "android.permission.MODIFY_AUDIO_SETTINGS",
              "android.permission.MODIFY_PHONE_STATE",
              "android.permission.MOUNT_FORMAT_FILESYSTEMS",
              "android.permission.MOUNT_UNMOUNT_FILESYSTEMS",
              "android.permission.NFC",
              "android.permission.PERSISTENT_ACTIVITY",
              "android.permission.PROCESS_OUTGOING_CALLS",
              "android.permission.READ_CALENDAR",
              "android.permission.READ_CALL_LOG",
              "android.permission.READ_CONTACTS",
              "android.permission.READ_EXTERNAL_STORAGE",
              "android.permission.READ_FRAME_BUFFER",
              "com.android.browser.permission.READ_HISTORY_BOOKMARKS",
              "android.permission.READ_INPUT_STATE",
              "android.permission.READ_LOGS",
              "android.permission.READ_PHONE_STATE",
              "android.permission.READ_PROFILE",
              "android.permission.READ_SMS",
              "android.permission.READ_SOCIAL_STREAM",
              "android.permission.READ_SYNC_SETTINGS",
              "android.permission.READ_SYNC_STATS",
              "android.permission.READ_USER_DICTIONARY",
              "android.permission.REBOOT",
              "android.permission.RECEIVE_BOOT_COMPLETED",
              "android.permission.RECEIVE_MMS",
              "android.permission.RECEIVE_SMS",
              "android.permission.RECEIVE_WAP_PUSH",
              "android.permission.RECORD_AUDIO",
              "android.permission.REORDER_TASKS",
              "android.permission.RESTART_PACKAGES",
              "android.permission.SEND_SMS",
              "android.permission.SET_ACTIVITY_WATCHER",
              "com.android.alarm.permission.SET_ALARM",
              "android.permission.SET_ALWAYS_FINISH",
              "android.permission.SET_ANIMATION_SCALE",
              "android.permission.SET_DEBUG_APP",
              "android.permission.SET_ORIENTATION",
              "android.permission.SET_POINTER_SPEED",
              "android.permission.SET_PREFERRED_APPLICATIONS",
              "android.permission.SET_PROCESS_LIMIT",
              "android.permission.SET_TIME",
              "android.permission.SET_TIME_ZONE",
              "android.permission.SET_WALLPAPER",
              "android.permission.SET_WALLPAPER_HINTS",
              "android.permission.SIGNAL_PERSISTENT_PROCESSES",
              "android.permission.STATUS_BAR",
              "android.permission.SUBSCRIBED_FEEDS_READ",
              "android.permission.SUBSCRIBED_FEEDS_WRITE",
              "android.permission.SYSTEM_ALERT_WINDOW",
              "android.permission.UPDATE_DEVICE_STATS",
              "android.permission.USE_CREDENTIALS",
              "android.permission.USE_SIP",
              "android.permission.VIBRATE",
              "android.permission.WAKE_LOCK",
              "android.permission.WRITE_APN_SETTINGS",
              "android.permission.WRITE_CALENDAR",
              "android.permission.WRITE_CALL_LOG",
              "android.permission.WRITE_CONTACTS",
              "android.permission.WRITE_EXTERNAL_STORAGE",
              "android.permission.WRITE_GSERVICES",
              "com.android.browser.permission.WRITE_HISTORY_BOOKMARKS",
              "android.permission.WRITE_PROFILE",
              "android.permission.WRITE_SECURE_SETTINGS",
              "android.permission.WRITE_SETTINGS",
              "android.permission.WRITE_SMS",
              "android.permission.WRITE_SOCIAL_STREAM",
              "android.permission.WRITE_SYNC_SETTINGS",
              "android.permission.WRITE_USER_DICTIONARY",
          });
    m_permissionsComboBox->setEditable(true);
    layout->addWidget(m_permissionsComboBox, 3, 0);

    m_addPermissionButton = new QPushButton(permissionsGroupBox);
    m_addPermissionButton->setText(Android::Tr::tr("Add"));
    layout->addWidget(m_addPermissionButton, 3, 1);

    m_permissionsModel = new PermissionsModel(this);

    m_permissionsListView = new QTreeView(permissionsGroupBox);
    m_permissionsListView->setRootIsDecorated(false);
    m_permissionsListView->setModel(m_permissionsModel);
    m_permissionsListView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(m_permissionsListView, 4, 0, 3, 1);

    m_editAttributesButton = new QPushButton(permissionsGroupBox);
    m_editAttributesButton->setText(Android::Tr::tr("Edit Attributes"));
    layout->addWidget(m_editAttributesButton, 4, 1);

    m_removePermissionButton = new QPushButton(permissionsGroupBox);
    m_removePermissionButton->setText(Android::Tr::tr("Remove"));
    layout->addWidget(m_removePermissionButton, 5, 1);

    permissionsGroupBox->setLayout(layout);
    mainLayout->addWidget(permissionsGroupBox);
    setLayout(mainLayout);

    if (m_CMakePermissionsCheckBox->isChecked())
        loadPermissionsFromCMake();
    else
        loadPermissionsFromManifest();

    updateAddRemovePermissionButtons();

    connect(m_defaultPermissonsCheckBox, &QCheckBox::stateChanged,
            this, &PermissionsContainerWidget::defaultPermissionOrFeatureCheckBoxClicked);
    connect(m_defaultFeaturesCheckBox, &QCheckBox::stateChanged,
            this, &PermissionsContainerWidget::defaultPermissionOrFeatureCheckBoxClicked);
    connect(m_CMakePermissionsCheckBox, &QCheckBox::stateChanged,
            this, &PermissionsContainerWidget::onCMakePermissionsCheckBoxChanged);
    connect(m_addPermissionButton, &QAbstractButton::clicked,
            this, &PermissionsContainerWidget::addPermission);
    connect(m_removePermissionButton, &QAbstractButton::clicked,
            this, &PermissionsContainerWidget::removePermission);
    connect(m_editAttributesButton, &QAbstractButton::clicked,
            this, &PermissionsContainerWidget::editAttributes);
    connect(m_permissionsComboBox, &QComboBox::currentTextChanged,
            this, &PermissionsContainerWidget::updateAddRemovePermissionButtons);
    connect(m_permissionsListView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &PermissionsContainerWidget::updateAddRemovePermissionButtons);
    connect(m_permissionsModel, &QAbstractItemModel::modelReset,
            this, &PermissionsContainerWidget::updateAddRemovePermissionButtons);
    connect(m_permissionsListView, &QTreeView::doubleClicked,
            this, &PermissionsContainerWidget::editAttributes);

    return true;
}

void PermissionsContainerWidget::addPermission()
{
    const QString permission = m_permissionsComboBox->currentText().trimmed();
    if (permission.isEmpty())
        return;

    if (m_CMakePermissionsCheckBox->isChecked()) {
        if (m_CMakeFilePath.isEmpty() && !resolveCMakeProjectInfo())
            return;
        if (m_permissionsModel->addPermission(permission).isValid())
            addCMakePermission(permission);
    } else {
        m_permissionsModel->addPermission(permission);
        updateManifestPermissions();
    }

    emit permissionsModified();
}

void PermissionsContainerWidget::editAttributes()
{
    const QModelIndex index = m_permissionsListView->currentIndex();
    if (!index.isValid())
        return;

    auto it = std::next(m_permissionsModel->permissions().cbegin(), index.row());
    const QString permission = it.key();
    QMap<QString, QString> attributes = it.value();

    QDialog dialog(this);
    QVBoxLayout *dialogLayout = new QVBoxLayout(&dialog);

    QLabel *permissionLabel = new QLabel(Tr::tr("Permission: %1").arg("<b>" + permission + "</b>"), &dialog);
    dialogLayout->addWidget(permissionLabel);

    QListWidget *attrListWidget = new QListWidget(&dialog);
    auto refreshAttrList = [&]() {
        attrListWidget->clear();
        for (auto ait = attributes.cbegin(); ait != attributes.cend(); ++ait)
            attrListWidget->addItem(ait.key() + " = " + ait.value());
    };
    refreshAttrList();
    dialogLayout->addWidget(attrListWidget);

    QPushButton *removeAttrButton = new QPushButton(Tr::tr("Remove Selected"), &dialog);
    removeAttrButton->setEnabled(false);
    connect(attrListWidget, &QListWidget::itemSelectionChanged, &dialog, [&]() {
        const bool hasSelection = attrListWidget->currentItem() != nullptr;
        removeAttrButton->setEnabled(hasSelection);
    });
    connect(removeAttrButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = attrListWidget->currentRow();
        if (row < 0)
            return;
        auto attIt = std::next(attributes.begin(), row);
        attributes.erase(attIt);
        refreshAttrList();
    });
    dialogLayout->addWidget(removeAttrButton);

    QFrame *line = new QFrame(&dialog);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    dialogLayout->addWidget(line);

    QFormLayout *addRowLayout = new QFormLayout;
    QComboBox *attributeCombo = new QComboBox(&dialog);
    attributeCombo->addItems({QStringLiteral("android:minSdkVersion"),
                              QStringLiteral("android:maxSdkVersion"),
                              QStringLiteral("android:usesPermissionFlags")});
    attributeCombo->setEditable(true);
    addRowLayout->addRow(Tr::tr("Attribute:"), attributeCombo);

    QLineEdit *valueEdit = new QLineEdit(&dialog);
    addRowLayout->addRow(Tr::tr("Value:"), valueEdit);
    dialogLayout->addLayout(addRowLayout);

    QPushButton *addRowButton = new QPushButton(Tr::tr("Add"), &dialog);
    connect(addRowButton, &QPushButton::clicked, &dialog, [&]() {
        const QString name = attributeCombo->currentText().trimmed();
        const QString val = valueEdit->text().trimmed();
        if (name.isEmpty() || val.isEmpty())
            return;
        attributes.insert(name, val);
        refreshAttrList();
        valueEdit->clear();
    });

    dialogLayout->addWidget(addRowButton);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString name = attributeCombo->currentText().trimmed();
        const QString val = valueEdit->text().trimmed();
        if (!name.isEmpty() && !val.isEmpty())
            attributes.insert(name, val);
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialogLayout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    if (m_CMakePermissionsCheckBox->isChecked()) {
        if (m_CMakeFilePath.isEmpty() && !resolveCMakeProjectInfo())
            return;
        if (!updateCMakePermission(permission, attributes))
            return;
    } else if (m_textEditorWidget && m_textEditorWidget->textDocument()) {
        const Utils::FilePath manifestPath = m_textEditorWidget->textDocument()->filePath();
        if (manifestPath.exists())
            if (!updateManifestPermissionAttributes(manifestPath, permission, attributes))
                return;
    }

    m_permissionsModel->setAttributes(permission, attributes);

    emit permissionsModified();

}

void PermissionsContainerWidget::removePermission()
{
    const QModelIndex index = m_permissionsListView->currentIndex();
    if (!index.isValid())
        return;

    const QString permission = index.siblingAtColumn(0).data().toString();

    if (m_CMakePermissionsCheckBox->isChecked()) {
        if (m_CMakeFilePath.isEmpty() && !resolveCMakeProjectInfo())
            return;
        if (removeCMakePermission(permission))
            m_permissionsModel->removePermission(index.row());
    } else {
        m_permissionsModel->removePermission(index.row());
        updateManifestPermissions();
    }

    emit permissionsModified();
}

void PermissionsContainerWidget::updateAddRemovePermissionButtons()
{
    const bool cmakeReady = !m_CMakePermissionsCheckBox->isChecked() || !m_CMakeFilePath.isEmpty();
    m_addPermissionButton->setEnabled(cmakeReady && !m_permissionsComboBox->currentText().trimmed().isEmpty());
    m_removePermissionButton->setEnabled(cmakeReady && m_permissionsListView->currentIndex().isValid());
    const bool hasSelection = m_permissionsListView->selectionModel()
                              && m_permissionsListView->selectionModel()->hasSelection();
    m_editAttributesButton->setEnabled(hasSelection);
}

void PermissionsContainerWidget::defaultPermissionOrFeatureCheckBoxClicked()
{
    updateManifestPermissions();
    emit permissionsModified();
}

void PermissionsContainerWidget::updateManifestPermissions()
{
    Utils::FilePath manifestPath;
    manifestPath = m_textEditorWidget->textDocument()->filePath();

    if (manifestPath.isEmpty() || !manifestPath.exists())
        return;

    bool includeDefaultPermissions = m_defaultPermissonsCheckBox->isChecked();
    bool includeDefaultFeatures = m_defaultFeaturesCheckBox->isChecked();

    Android::Internal::updateManifestPermissions(
        manifestPath,
        m_permissionsModel->permissions().keys(),
        includeDefaultPermissions,
        includeDefaultFeatures);

    if (m_textEditorWidget && m_textEditorWidget->textDocument())
        m_textEditorWidget->textDocument()->reload();
}

void PermissionsContainerWidget::refresh()
{
    if (!m_textEditorWidget)
        return;
    updateCMakePermissionsCheckBoxState();
    if (m_CMakePermissionsCheckBox && m_CMakePermissionsCheckBox->isChecked())
        loadPermissionsFromCMake();
    else
        loadPermissionsFromManifest();
}

void PermissionsContainerWidget::loadPermissionsFromManifest()
{
    if (!m_textEditorWidget || !m_textEditorWidget->textDocument())
        return;

    Utils::FilePath manifestPath = m_textEditorWidget->textDocument()->filePath();
    if (!manifestPath.exists())
        return;

    auto dataResult = AndroidManifestParser::readManifest(manifestPath);
    if (!dataResult)
        return;

    const AndroidManifestParser::ManifestData &data = *dataResult;

    m_permissionsModel->setPermissions(data.permissions);
    m_defaultPermissonsCheckBox->setChecked(data.hasDefaultPermissionsComment);
    m_defaultFeaturesCheckBox->setChecked(data.hasDefaultFeaturesComment);
}

void PermissionsContainerWidget::addCMakePermission(const QString &permission,
                                                    const QMap<QString, QString> &attributes)
{
    QString content;
    const auto cmakeFile = parseCMakeFile(m_CMakeFilePath, content);
    if (!cmakeFile)
        return;

    const std::string targetName = m_CMakeTargetName.toStdString();
    const QString newCall = buildPermissionCall(m_CMakeTargetName, permission, attributes);

    long lastLineEnd = -1;
    for (const cmListFileFunction &func : cmakeFile->Functions) {
        if (isPermissionCallForTarget(func, targetName))
            lastLineEnd = func.LineEnd();
    }

    QStringList lines = content.split('\n');
    if (lastLineEnd != -1) {
        lines.insert(static_cast<qsizetype>(lastLineEnd), newCall);
    } else {
        if (!lines.isEmpty() && lines.last().isEmpty())
            lines.last() = newCall;
        else
            lines.append(newCall);
        lines.append(QString());
    }

    Utils::FileSaver saver(m_CMakeFilePath, QIODevice::Text);
    saver.write(lines.join('\n').toUtf8());
    saver.finalize();
}

bool PermissionsContainerWidget::resolveCMakeProjectInfo()
{
    if (!m_textEditorWidget || !m_textEditorWidget->textDocument())
        return false;

    const Utils::FilePath manifestPath = m_textEditorWidget->textDocument()->filePath();
    Project *project = ProjectManager::projectForFile(manifestPath);
    if (!project)
        return false;

    Target *target = project->activeTarget();
    if (!target || !target->activeBuildConfiguration())
        return false;

    BuildSystem *buildSystem = target->activeBuildConfiguration()->buildSystem();
    if (!buildSystem)
        return false;

    if (project->type() != Utils::Id(CMakeProjectManager::Constants::CMAKE_PROJECT_ID))
        return false;

    const QList<BuildTargetInfo> appTargets = buildSystem->applicationTargets();

    const BuildTargetInfo *matched = nullptr;
    for (const BuildTargetInfo &ti : appTargets) {
        if (!ti.projectFilePath.isEmpty()
            && manifestPath.parentDir().isChildOf(ti.projectFilePath.parentDir())) {
            matched = &ti;
            break;
        }
    }

    if (!matched)
        return false;

    m_CMakeTargetName = matched->buildKey;
    m_CMakeFilePath = matched->projectFilePath.isEmpty() ? buildSystem->projectFilePath()
                                                         : matched->projectFilePath;

    if (m_CMakeTargetName.isEmpty() || m_CMakeFilePath.isEmpty())
        return false;

    return true;
}

void PermissionsContainerWidget::loadPermissionsFromCMake()
{
    if (!resolveCMakeProjectInfo())
        return;

    QString content;
    const auto cmakeFile = parseCMakeFile(m_CMakeFilePath, content);
    if (!cmakeFile)
        return;

    const std::string targetName = m_CMakeTargetName.toStdString();
    QMap<QString, QMap<QString, QString>> permissions;

    for (const cmListFileFunction &func : cmakeFile->Functions) {
        if (!isPermissionCallForTarget(func, targetName))
            continue;
        auto result = parsePermissionCall(func);
        if (result) {
            permissions.insert(result->name, result->attributes);
        }
    }
    m_permissionsModel->setPermissions(permissions);
}

bool PermissionsContainerWidget::removeCMakePermission(const QString &permission)
{
    QString content;
    const auto cmakeFile = parseCMakeFile(m_CMakeFilePath, content);
    if (!cmakeFile)
        return false;

    const std::string targetName = m_CMakeTargetName.toStdString();
    const std::string permName = permission.toStdString();

    long lineStart = -1, lineEnd = -1;
    for (const cmListFileFunction &func : cmakeFile->Functions) {
        if (isPermissionCallForTarget(func, targetName, permName)) {
            lineStart = func.Line();
            lineEnd = func.LineEnd();
            break;
        }
    }

    if (lineStart == -1)
        return false;

    QStringList lines = content.split('\n');
    lines.remove(static_cast<qsizetype>(lineStart) - 1,
                 static_cast<qsizetype>(lineEnd - lineStart) + 1);

    Utils::FileSaver saver(m_CMakeFilePath, QIODevice::Text);
    saver.write(lines.join('\n').toUtf8());
    return (bool)saver.finalize();
}

Utils::Result<> PermissionsContainerWidget::migratePermissionsManifestToCMake()
{
    if (!resolveCMakeProjectInfo())
        return Utils::ResultError(Tr::tr("No Cmake target found for the Android manifest."));

    if (!m_textEditorWidget || !m_textEditorWidget->textDocument())
        return Utils::ResultError(Tr::tr("No manifest document available."));

    const Utils::FilePath manifestPath = m_textEditorWidget->textDocument()->filePath();
    const auto manifestData = AndroidManifestParser::readManifest(manifestPath);
    if (!manifestData)
        return Utils::ResultError(manifestData.error());

    const auto cmakeFile = CMakeProjectManager::parseCMakeFile(m_CMakeFilePath);
    if(!cmakeFile)
        return Utils::ResultError(cmakeFile.error());

    const QMap<QString, QMap<QString, QString>> merged = mergePermissions(
        manifestData->permissions,
        parseCMakePermissions(m_CMakeFilePath, m_CMakeTargetName));

    if (!removeAllPermissionCalls(m_CMakeFilePath, m_CMakeTargetName))
        return Utils::ResultError(Tr::tr("Cannot update \"%1\".")
                                  .arg(m_CMakeFilePath.toUserOutput()));

    for (auto it = merged.cbegin(); it != merged.cend(); ++it)
        addCMakePermission(it.key(), it.value());

    m_permissionsModel->setPermissions({});
    updateManifestPermissions();
    return Utils::ResultOk;
}

Utils::Result<> PermissionsContainerWidget::migratePermissionsCMakeToManifest()
{
    if (!resolveCMakeProjectInfo())
        return Utils::ResultError(Tr::tr("No CMake target found for the Android manifest."));

    if (!m_textEditorWidget || !m_textEditorWidget->textDocument())
        return Utils::ResultError(Tr::tr("No manifest document available."));

    const Utils::FilePath manifestPath = m_textEditorWidget->textDocument()->filePath();
    const auto manifestData = AndroidManifestParser::readManifest(manifestPath);
    if (!manifestData)
        return Utils::ResultError(manifestData.error());

    const auto cmakeFile = CMakeProjectManager::parseCMakeFile(m_CMakeFilePath);
    if (!cmakeFile)
        return Utils::ResultError(cmakeFile.error());

    const QMap<QString, QMap<QString, QString>> permissions = mergePermissions(
        manifestData->permissions,
        parseCMakePermissions(m_CMakeFilePath, m_CMakeTargetName));

    if (!removeAllPermissionCalls(m_CMakeFilePath, m_CMakeTargetName))
        return Utils::ResultError(Tr::tr("Cannot update \"%1\".")
                                  .arg(m_CMakeFilePath.toUserOutput()));

    m_permissionsModel->setPermissions(permissions);
    updateManifestPermissions();

    for (auto it = permissions.cbegin(); it != permissions.cend(); ++it) {
        if (!it.value().isEmpty())
            updateManifestPermissionAttributes(manifestPath, it.key(), it.value());
    }
    return Utils::ResultOk;
}

bool PermissionsContainerWidget::updateCMakePermission(const QString &permission,
                                                       const QMap<QString, QString> &attributes)
{
    if (m_CMakeFilePath.isEmpty())
        return false;

    QString content;
    const auto cmakeFile = parseCMakeFile(m_CMakeFilePath, content);
    if (!cmakeFile)
        return false;

    const std::string targetName = m_CMakeTargetName.toStdString();
    const std::string permName = permission.toStdString();

    long lineStart = -1, lineEnd = -1;
    for (const cmListFileFunction &func : cmakeFile->Functions) {
        if (isPermissionCallForTarget(func, targetName, permName)) {
            const std::optional<ParsedPermission> current = parsePermissionCall(func);
            if (current && current->attributes == attributes)
                return true; // No change needed
            lineStart = func.Line();
            lineEnd = func.LineEnd();
            break;
        }
    }

    if (lineStart == -1)
        return false;

    QStringList lines = content.split('\n');
    lines.remove(static_cast<qsizetype>(lineStart) - 1,
                 static_cast<qsizetype>(lineEnd - lineStart) + 1);
    lines.insert(static_cast<qsizetype>(lineStart) - 1,
                  buildPermissionCall(m_CMakeTargetName, permission, attributes));

    Utils::FileSaver saver(m_CMakeFilePath, QIODevice::Text);
    saver.write(lines.join('\n').toUtf8());
    return (bool)saver.finalize();
}

} // namespace Android::Internal
