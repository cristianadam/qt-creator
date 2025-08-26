// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmllsclientsettings.h"
#include "qmljseditorconstants.h"
#include "qmljseditortr.h"
#include "qmllsclient.h"

#include <utils/mimeconstants.h>
#include <utils/qtcsettings.h>

#include <coreplugin/messagemanager.h>

#include <languageclient/languageclientinterface.h>
#include <languageclient/languageclientmanager.h>
#include <languageclient/languageclientsettings.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>

#include <qmljs/qmljsmodelmanagerinterface.h>

#include <qtsupport/qtkitaspect.h>
#include <qtsupport/qtversionmanager.h>

#include <QCheckBox>
#include <QRadioButton>

using namespace LanguageClient;
using namespace QtSupport;
using namespace Utils;
using namespace ProjectExplorer;

namespace QmlJSEditor {

constexpr char useLatestQmllsKey[] = "useLatestQmlls";
constexpr char executableSelectionKey[] = "executableSelection";
constexpr char disableBuiltinCodemodelKey[] = "disableBuiltinCodemodel";
constexpr char generateQmllsIniFilesKey[] = "generateQmllsIniFiles";
constexpr char ignoreMinimumQmllsVersionKey[] = "ignoreMinimumQmllsVersion";
constexpr char useQmllsSemanticHighlightingKey[] = "enableQmllsSemanticHighlighting";
constexpr char executableKey[] = "executable";

QmllsClientSettings *qmllsSettings()
{
    BaseSettings *qmllsSettings
        = Utils::findOrDefault(LanguageClientManager::currentSettings(), [](BaseSettings *setting) {
              return setting->m_settingsTypeId == Constants::QMLLS_CLIENT_SETTINGS_ID;
          });
    return static_cast<QmllsClientSettings *>(qmllsSettings);
}

static const QStringList &supportedMimeTypes()
{
    using namespace Utils::Constants;
    static const QStringList mimeTypes
        = {QML_MIMETYPE,
           QMLUI_MIMETYPE,
           QMLPROJECT_MIMETYPE,
           QMLTYPES_MIMETYPE,
           JS_MIMETYPE};
    return mimeTypes;
}

QmllsClientSettings::QmllsClientSettings()
{
    name.setValue(Constants::QMLLS_NAME);

    m_languageFilter.mimeTypes = supportedMimeTypes();

    m_settingsTypeId = Constants::QMLLS_CLIENT_SETTINGS_ID;
    m_startBehavior = RequiresProject;
    initializationOptions.setValue("{\"qtCreatorHighlighting\": true}");
    enabled.setValue(false); // disabled by default
}

static std::pair<FilePath, QVersionNumber> evaluateLatestQmlls()
{
    if (!QtVersionManager::isLoaded())
        return {};

    const QtVersions versions = QtVersionManager::versions();
    FilePath latestQmlls;
    QVersionNumber latestVersion;
    int latestUniqueId = std::numeric_limits<int>::min();

    for (QtVersion *qtVersion : versions) {
        if (!qtVersion->qmakeFilePath().isLocal())
            continue;

        const QVersionNumber version = qtVersion->qtVersion();
        const int uniqueId = qtVersion->uniqueId();

        // note: break ties between qt kits of same versions by selecting the qt kit with the highest id
        if (std::tie(version, uniqueId) < std::tie(latestVersion, latestUniqueId))
            continue;

        const FilePath qmlls
            = QmlJS::ModelManagerInterface::qmllsForBinPath(qtVersion->hostBinPath(), version);
        if (!qmlls.isExecutableFile())
            continue;

        latestVersion = version;
        latestQmlls = qmlls;
        latestUniqueId = uniqueId;
    }
    return std::make_pair(latestQmlls, latestVersion);
}

static QVersionNumber mapStandaloneVersions(const QVersionNumber &standaloneVersion)
{
    // standalone qmlls 0.2 supports the same command line arguments as qmlls 6.10
    if (standaloneVersion >= QVersionNumber(0, 2)) {
        return QVersionNumber(6, 10);
    }
    // fallback
    return QVersionNumber(6, 9);
}

static std::pair<FilePath, QVersionNumber> evaluateOverridenQmlls()
{
    if (!qmllsSettings()->m_executable.exists()) {
        Core::MessageManager::writeFlashing(
                    Tr::tr("Custom qmlls executable \"%1\" does not exist and was disabled.")
                    .arg(qmllsSettings()->m_executable.path()));
        return {};
    }
    Process qmlls;
    qmlls.setCommand({qmllsSettings()->m_executable, {"--version"}});
    qmlls.start();
    qmlls.waitForFinished();
    if (qmlls.exitStatus() != QProcess::NormalExit || qmlls.exitCode() != EXIT_SUCCESS) {
        Core::MessageManager::writeFlashing(
                    Tr::tr(
                        "Custom qmlls executable \"%1\" exited abnormally and was disabled. Note that "
                        "qmlls versions < 6.10 are not supported this way. The custom executable output "
                        "was:\n %2")
                    .arg(qmllsSettings()->m_executable.path(), qmlls.readAllStandardError()));
        return {};
    }

    const QString output = qmlls.readAllStandardOutput();

    if (!output.contains("qmlls")) {
        Core::MessageManager::writeFlashing(
                    Tr::tr(
                        "Custom qmlls executable \"%1\" does not seem to be a qmlls executable and was "
                        "disabled")
                    .arg(qmllsSettings()->m_executable.path()));
        return {};
    }

    const bool isStandaloneQmlls = output.contains("(standalone)");
    const std::size_t versionBegin = isStandaloneQmlls
            ? std::char_traits<char>::length("qmlls (standalone) ")
            : std::char_traits<char>::length("qmlls ");

    std::pair<FilePath, QVersionNumber> result{
        qmllsSettings()->m_executable,
        QVersionNumber::fromString(QStringView(output).sliced(versionBegin)),
    };
    if (isStandaloneQmlls)
        result.second = mapStandaloneVersions(result.second);
    return result;
}

static std::pair<FilePath, QVersionNumber> evaluateQmlls(const QtVersion *qtVersion)
{
    switch (qmllsSettings()->m_executableSelection) {
    case QmllsClientSettings::FromQtKit:
        return std::make_pair(
            QmlJS::ModelManagerInterface::qmllsForBinPath(
                qtVersion->hostBinPath(), qtVersion->qtVersion()),
            qtVersion->qtVersion());
    case QmllsClientSettings::FromLatestQtKit:
        return evaluateLatestQmlls();
    case QmllsClientSettings::FromUser:
        return evaluateOverridenQmlls();
    }
    Q_UNREACHABLE_RETURN({});
}

static CommandLine commandLineForQmlls(BuildConfiguration *bc)
{
    const QtVersion *qtVersion = QtKitAspect::qtVersion(bc->kit());
    QTC_ASSERT(qtVersion, return {});

    auto [executable, version] = evaluateQmlls(qtVersion);

    CommandLine result{executable, {}};

    const QString buildDirectory = bc->buildDirectory().path();
    if (!buildDirectory.isEmpty())
        result.addArgs({"-b", buildDirectory});

    // qmlls 6.8 and later require the import path
    if (version >= QVersionNumber(6, 8, 0)) {
        result.addArgs({"-I", qtVersion->qmlPath().path()});

        // add custom import paths that the embedded codemodel uses too
        const QmlJS::ModelManagerInterface::ProjectInfo projectInfo
            = QmlJS::ModelManagerInterface::instance()->projectInfo(bc->project());
        for (QmlJS::PathAndLanguage path : projectInfo.importPaths) {
            if (path.language() == QmlJS::Dialect::Qml)
                result.addArgs({"-I", path.path().path()});
        }

        // work around QTBUG-132263 for qmlls 6.8.2
        if (!buildDirectory.isEmpty())
            result.addArgs({"-I", buildDirectory});
    }

    // qmlls 6.8.1 and later require the documentation path
    if (version >= QVersionNumber(6, 8, 1))
        result.addArgs({"-d", qtVersion->docsPath().path()});

    return result;
}

bool QmllsClientSettings::isValidOnBuildConfiguration(BuildConfiguration *bc) const
{
    if (!BaseSettings::isValidOnBuildConfiguration(bc))
        return false;

    if (!bc || !QtVersionManager::isLoaded())
        return false;

    const QtVersion *qtVersion = QtKitAspect::qtVersion(bc->kit());
    if (!qtVersion) {
        Core::MessageManager::writeSilently(
            Tr::tr("Current kit does not have a valid Qt version, disabling QML Language Server."));
        return false;
    }

    const auto &[filePath, version] = evaluateQmlls(qtVersion);

    if (filePath.isEmpty())
        return false;
    if (!m_ignoreMinimumQmllsVersion && version < QmllsClientSettings::mininumQmllsVersion)
        return false;

    return true;
}

class QmllsClientInterface : public StdIOClientInterface
{
public:
    FilePath qmllsFilePath() const { return m_cmd.executable(); }
};

BaseClientInterface *QmllsClientSettings::createInterface(BuildConfiguration *bc) const
{
    auto interface = new QmllsClientInterface;
    interface->setCommandLine(commandLineForQmlls(bc));
    return interface;
}

Client *QmllsClientSettings::createClient(BaseClientInterface *interface) const
{
    auto qmllsInterface = static_cast<QmllsClientInterface *>(interface);
    auto client = new QmllsClient(qmllsInterface);
    const QString display = QString("%1 (%2)").arg(
        name(), qmllsInterface->qmllsFilePath().toUserOutput());
    client->setName(display);
    return client;
}

class QmllsClientSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QmllsClientSettingsWidget(
        const QmllsClientSettings *settings, QWidget *parent = nullptr);

    bool useLatestQmlls() const;
    bool disableBuiltinCodemodel() const;
    bool generateQmllsIniFiles() const;
    bool ignoreMinimumQmllsVersion() const;
    bool useQmllsSemanticHighlighting() const;
    bool overrideExecutable() const;
    Utils::FilePath executable() const;
    QmllsClientSettings::ExecutableSelection executableSelection() const;

private:
    QCheckBox *m_disableBuiltinCodemodel;
    QCheckBox *m_generateQmllsIniFiles;
    QCheckBox *m_ignoreMinimumQmllsVersion;
    QCheckBox *m_useQmllsSemanticHighlighting;

    QRadioButton *m_useDefaultQmlls;
    QRadioButton *m_useLatestQmlls;
    QRadioButton *m_overrideExecutable;

    Utils::PathChooser *m_executable;
};

QWidget *QmllsClientSettings::createSettingsWidget(QWidget *parent) const
{
    return new QmllsClientSettingsWidget(this, parent);
}

QmllsClientSettings::ExecutableSelection QmllsClientSettingsWidget::executableSelection() const
{
    if (m_useDefaultQmlls->isChecked())
        return QmllsClientSettings::FromQtKit;
    if (m_useLatestQmlls->isChecked())
        return QmllsClientSettings::FromLatestQtKit;
    if (m_overrideExecutable->isChecked())
        return QmllsClientSettings::FromUser;

    QTC_ASSERT(false, return QmllsClientSettings::FromQtKit);
}

bool QmllsClientSettings::applyFromSettingsWidget(QWidget *widget)
{
    bool changed = BaseSettings::applyFromSettingsWidget(widget);

    QmllsClientSettingsWidget *qmllsWidget = qobject_cast<QmllsClientSettingsWidget *>(widget);
    if (!qmllsWidget)
        return changed;

    if (m_executableSelection != qmllsWidget->executableSelection()) {
        m_executableSelection = qmllsWidget->executableSelection();
        changed = true;
    }

    if (m_disableBuiltinCodemodel != qmllsWidget->disableBuiltinCodemodel()) {
        m_disableBuiltinCodemodel = qmllsWidget->disableBuiltinCodemodel();
        changed = true;
    }

    if (m_generateQmllsIniFiles != qmllsWidget->generateQmllsIniFiles()) {
        m_generateQmllsIniFiles = qmllsWidget->generateQmllsIniFiles();
        changed = true;
    }

    if (m_ignoreMinimumQmllsVersion != qmllsWidget->ignoreMinimumQmllsVersion()) {
        m_ignoreMinimumQmllsVersion = qmllsWidget->ignoreMinimumQmllsVersion();
        changed = true;
    }

    if (m_useQmllsSemanticHighlighting != qmllsWidget->useQmllsSemanticHighlighting()) {
        m_useQmllsSemanticHighlighting = qmllsWidget->useQmllsSemanticHighlighting();
        changed = true;
    }

    if (m_executable != qmllsWidget->executable()) {
        m_executable = qmllsWidget->executable();
        changed = true;
    }

    return changed;
}

void QmllsClientSettings::toMap(Store &map) const
{
    BaseSettings::toMap(map);

    map.insert(executableSelectionKey, static_cast<int>(m_executableSelection));
    map.insert(disableBuiltinCodemodelKey, m_disableBuiltinCodemodel);
    map.insert(generateQmllsIniFilesKey, m_generateQmllsIniFiles);
    map.insert(ignoreMinimumQmllsVersionKey, m_ignoreMinimumQmllsVersion);
    map.insert(useQmllsSemanticHighlightingKey, m_useQmllsSemanticHighlighting);
    map.insert(executableKey, m_executable.toSettings());
}

void QmllsClientSettings::fromMap(const Store &map)
{
    BaseSettings::fromMap(map);
    m_languageFilter.mimeTypes = supportedMimeTypes();

    // port from previous settings
    if (map.contains(useLatestQmllsKey))
        m_executableSelection = map[useLatestQmllsKey].toBool() ? FromLatestQtKit : FromQtKit;

    // don't overwrite ported settings if the new key is not in use yet.
    if (map.contains(executableSelectionKey))
        m_executableSelection = static_cast<ExecutableSelection>(
            map[executableSelectionKey].toInt());

    m_disableBuiltinCodemodel = map[disableBuiltinCodemodelKey].toBool();
    m_generateQmllsIniFiles = map[generateQmllsIniFilesKey].toBool();
    m_ignoreMinimumQmllsVersion = map[ignoreMinimumQmllsVersionKey].toBool();
    m_useQmllsSemanticHighlighting = map[useQmllsSemanticHighlightingKey].toBool();
    m_executable = Utils::FilePath::fromSettings(map[executableKey]);
}

bool QmllsClientSettings::isEnabledOnProjectFile(const Utils::FilePath &file) const
{
    Project *project = ProjectManager::projectForFile(file);
    return isEnabledOnProject(project);
}

bool QmllsClientSettings::useQmllsWithBuiltinCodemodelOnProject(Project *project,
                                                                const FilePath &file) const
{
    if (m_disableBuiltinCodemodel)
        return false;

    // disableBuitinCodemodel only makes sense when qmlls is enabled
    return project && isEnabledOnProject(project) && project->isKnownFile(file);
}

// first time initialization: port old settings from the QmlJsEditingSettings AspectContainer
static void portFromOldSettings(QmllsClientSettings* qmllsClientSettings)
{
    QtcSettings *settings = &Utils::userSettings();

    const Key baseKey = Key{QmlJSEditor::Constants::SETTINGS_CATEGORY_QML} + "/";

    auto portSetting = [&settings](const Key &key, bool *output) {
        if (settings->contains(key))
            *output = settings->value(key).toBool();
    };

    constexpr char USE_QMLLS[] = "QmlJSEditor.UseQmlls";
    constexpr char USE_LATEST_QMLLS[] = "QmlJSEditor.UseLatestQmlls";
    constexpr char DISABLE_BUILTIN_CODEMODEL[] = "QmlJSEditor.DisableBuiltinCodemodel";
    constexpr char GENERATE_QMLLS_INI_FILES[] = "QmlJSEditor.GenerateQmllsIniFiles";
    constexpr char IGNORE_MINIMUM_QMLLS_VERSION[] = "QmlJSEditor.IgnoreMinimumQmllsVersion";
    constexpr char USE_QMLLS_SEMANTIC_HIGHLIGHTING[]
        = "QmlJSEditor.EnableQmllsSemanticHighlighting";

    if (settings->contains(baseKey + USE_QMLLS))
        qmllsClientSettings->enabled.setValue(settings->value(baseKey + USE_QMLLS).toBool());

    if (settings->contains(baseKey + USE_LATEST_QMLLS))
        qmllsClientSettings->m_executableSelection = QmllsClientSettings::FromLatestQtKit;
    portSetting(baseKey + DISABLE_BUILTIN_CODEMODEL, &qmllsClientSettings->m_disableBuiltinCodemodel);
    portSetting(baseKey + GENERATE_QMLLS_INI_FILES, &qmllsClientSettings->m_generateQmllsIniFiles);
    portSetting(baseKey + IGNORE_MINIMUM_QMLLS_VERSION, &qmllsClientSettings->m_ignoreMinimumQmllsVersion);
    portSetting(baseKey + USE_QMLLS_SEMANTIC_HIGHLIGHTING, &qmllsClientSettings->m_useQmllsSemanticHighlighting);
}

void registerQmllsSettings()
{
    const ClientType type{Constants::QMLLS_CLIENT_SETTINGS_ID,
                          Constants::QMLLS_NAME,
                          []() { return new QmllsClientSettings; },
                          false};

    LanguageClientSettings::registerClientType(type);
}

void setupQmllsClient()
{
    if (!Utils::anyOf(LanguageClientManager::currentSettings(), [](const BaseSettings *settings) {
            return settings->m_settingsTypeId == Constants::QMLLS_CLIENT_SETTINGS_ID;
        })) {
        QmllsClientSettings *clientSettings = new QmllsClientSettings();
        portFromOldSettings(clientSettings);
        LanguageClientManager::registerClientSettings(clientSettings);
    }
}

QmllsClientSettingsWidget::QmllsClientSettingsWidget(
    const QmllsClientSettings *settings, QWidget *parent)
    : QWidget(parent)
    , m_disableBuiltinCodemodel(new QCheckBox(
          Tr::tr("Use advanced features (renaming, find usages, and so on) (experimental)"), this))
    , m_generateQmllsIniFiles(
          new QCheckBox(Tr::tr("Create .qmlls.ini files for new projects"), this))
    , m_ignoreMinimumQmllsVersion(new QCheckBox(
          Tr::tr("Allow versions below Qt %1")
              .arg(QmllsClientSettings::mininumQmllsVersion.toString()),
          this))
    , m_useQmllsSemanticHighlighting(
          new QCheckBox(Tr::tr("Enable semantic highlighting (experimental)"), this))
    , m_useDefaultQmlls(new QRadioButton(Tr::tr("Use qmlls from project Qt kit"), this))
    , m_useLatestQmlls(new QRadioButton(
          Tr::tr("Use qmlls from latest Qt kit (located at %1)")
              .arg(evaluateLatestQmlls().first.path()),
          this))
    , m_overrideExecutable(new QRadioButton(Tr::tr("Use custom qmlls executable:"), this))
    , m_executable(new Utils::PathChooser(this))
{
    m_useDefaultQmlls->setChecked(settings->m_executableSelection == QmllsClientSettings::FromQtKit);
    m_useLatestQmlls->setChecked(
        settings->m_executableSelection == QmllsClientSettings::FromLatestQtKit);
    m_overrideExecutable->setChecked(
        settings->m_executableSelection == QmllsClientSettings::FromUser);

    m_disableBuiltinCodemodel->setChecked(settings->m_disableBuiltinCodemodel);
    m_generateQmllsIniFiles->setChecked(settings->m_generateQmllsIniFiles);
    m_ignoreMinimumQmllsVersion->setChecked(settings->m_ignoreMinimumQmllsVersion);
    m_useQmllsSemanticHighlighting->setChecked(settings->m_useQmllsSemanticHighlighting);

    QObject::connect(
        m_overrideExecutable, &QCheckBox::toggled, m_executable, &PathChooser::setEnabled);
    m_executable->setFilePath(settings->m_executable);
    m_executable->setExpectedKind(Utils::PathChooser::File);
    m_executable->setEnabled(m_overrideExecutable->isChecked());

    using namespace Layouting;
    // clang-format off
    auto form = Column {
        Group {
            title(Tr::tr("Options")),
            Form {
                m_ignoreMinimumQmllsVersion, br,
                m_disableBuiltinCodemodel, br,
                m_useQmllsSemanticHighlighting, br,
                m_generateQmllsIniFiles, br,
            }
        },
        Group {
            title(Tr::tr("Executable selection for qmlls")),
            Column {
                Row { m_useDefaultQmlls },
                Row { m_useLatestQmlls },
                Row { m_overrideExecutable, m_executable },
            }
        },
    };
    // clang-format on

    form.attachTo(this);
}
bool QmllsClientSettingsWidget::useLatestQmlls() const
{
    return m_useLatestQmlls->isChecked();
}
bool QmllsClientSettingsWidget::disableBuiltinCodemodel() const
{
    return m_disableBuiltinCodemodel->isChecked();
}
bool QmllsClientSettingsWidget::generateQmllsIniFiles() const
{
    return m_generateQmllsIniFiles->isChecked();
}
bool QmllsClientSettingsWidget::ignoreMinimumQmllsVersion() const
{
    return m_ignoreMinimumQmllsVersion->isChecked();
}
bool QmllsClientSettingsWidget::useQmllsSemanticHighlighting() const
{
    return m_useQmllsSemanticHighlighting->isChecked();
}

bool QmllsClientSettingsWidget::overrideExecutable() const
{
    return m_overrideExecutable->isChecked();
}

Utils::FilePath QmllsClientSettingsWidget::executable() const
{
    return m_executable->filePath();
}

} // namespace QmlJSEditor

#include "qmllsclientsettings.moc"
