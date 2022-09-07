// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "dockerdevice.h"

#include "dockerapi.h"
#include "dockerconstants.h"
#include "dockerdevicewidget.h"
#include "dockertr.h"
#include "kitdetector.h"

#include <extensionsystem/pluginmanager.h>

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/idevicewidget.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/toolchainmanager.h>

#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtkitinformation.h>
#include <qtsupport/qtversionfactory.h>
#include <qtsupport/qtversionmanager.h>

#include <utils/algorithm.h>
#include <utils/basetreeview.h>
#include <utils/deviceshell.h>
#include <utils/environment.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/overridecursor.h>
#include <utils/pathlisteditor.h>
#include <utils/port.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/sortfiltermodel.h>
#include <utils/temporaryfile.h>
#include <utils/treemodel.h>
#include <utils/utilsicons.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileSystemWatcher>
#include <QHeaderView>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QThread>
#include <QToolButton>


#include <numeric>

#ifdef Q_OS_UNIX
#include <unistd.h>
#include <sys/types.h>
#endif

using namespace Core;
using namespace ProjectExplorer;
using namespace QtSupport;
using namespace Utils;

namespace Docker::Internal {

const QString s_pidMarker = "__qtc$$qtc__";

static Q_LOGGING_CATEGORY(dockerDeviceLog, "qtc.docker.device", QtWarningMsg);
#define LOG(x) qCDebug(dockerDeviceLog) << this << x << '\n'

class ContainerShell : public Utils::DeviceShell
{
public:
    ContainerShell(DockerSettings *settings, const QString &containerId, const FilePath &devicePath)
        : m_settings(settings)
        , m_containerId(containerId)
        , m_devicePath(devicePath)
    {
    }

private:
    void setupShellProcess(QtcProcess *shellProcess) final
    {
        shellProcess->setCommand({m_settings->dockerBinaryPath.filePath(), {"container", "start", "-i", "-a", m_containerId}});
    }

    CommandLine createFallbackCommand(const CommandLine &cmdLine)
    {
        CommandLine result = cmdLine;
        result.setExecutable(cmdLine.executable().onDevice(m_devicePath));
        return result;
    }

private:
    DockerSettings *m_settings;
    QString m_containerId;
    FilePath m_devicePath;
};

class DockerDevicePrivate : public QObject
{
public:
    DockerDevicePrivate(DockerDevice *parent, DockerSettings *settings)
        : q(parent)
        , m_settings(settings)
    {}

    ~DockerDevicePrivate() { stopCurrentContainer(); }

    bool runInContainer(const CommandLine &cmd) const;
    bool runInShell(const CommandLine &cmd, const QByteArray &stdInData = {}) const;
    QByteArray outputForRunInShell(const CommandLine &cmd) const;

    void updateContainerAccess();

    bool createContainer();
    void startContainer();
    void stopCurrentContainer();
    void fetchSystemEnviroment();

    DockerDevice *q;
    DockerDeviceData m_data;
    DockerSettings *m_settings;

    // For local file access

    std::unique_ptr<ContainerShell> m_shell;

    QString m_container;

    Environment m_cachedEnviroment;

    bool m_useFind = true;  // prefer find over ls and hacks, but be able to use ls as fallback
};

class DockerProcessImpl : public Utils::ProcessInterface
{
public:
    DockerProcessImpl(DockerDevicePrivate *device);
    virtual ~DockerProcessImpl();

private:
    void start() override;
    qint64 write(const QByteArray &data) override;
    void sendControlSignal(ControlSignal controlSignal) override;

private:
    CommandLine fullLocalCommandLine(bool interactive);

private:
    DockerDevicePrivate *m_devicePrivate = nullptr;
    // Store the IDevice::ConstPtr in order to extend the lifetime of device for as long
    // as this object is alive.
    IDevice::ConstPtr m_device;

    QtcProcess m_process;
    qint64 m_remotePID = -1;
    bool m_hasReceivedFirstOutput = false;
};

CommandLine DockerProcessImpl::fullLocalCommandLine(bool interactive)
{
    QStringList args;

    if (!m_setup.m_workingDirectory.isEmpty()) {
        QTC_CHECK(DeviceManager::deviceForPath(m_setup.m_workingDirectory) == m_device);
        args.append({"cd", m_setup.m_workingDirectory.path()});
        args.append("&&");
    }

    args.append({"echo", s_pidMarker, "&&"});

    const Environment &env = m_setup.m_environment;
    for (auto it = env.constBegin(); it != env.constEnd(); ++it)
        args.append(env.key(it) + "='" + env.expandedValueForKey(env.key(it)) + '\'');

    args.append("exec");
    args.append({m_setup.m_commandLine.executable().path(), m_setup.m_commandLine.arguments()});

    CommandLine shCmd("/bin/sh", {"-c", args.join(" ")});
    return m_devicePrivate->q->withDockerExecCmd(shCmd, interactive);
}

DockerProcessImpl::DockerProcessImpl(DockerDevicePrivate *device)
    : m_devicePrivate(device)
    , m_device(device->q->sharedFromThis())
    , m_process(this)
{
    connect(&m_process, &QtcProcess::started, this, [this] {
        qCDebug(dockerDeviceLog) << "Process started:" << m_process.commandLine();
    });

    connect(&m_process, &QtcProcess::readyReadStandardOutput, this, [this] {
        if (!m_hasReceivedFirstOutput) {
            QByteArray output = m_process.readAllStandardOutput();
            qsizetype idx = output.indexOf('\n');
            QByteArray firstLine = output.left(idx);
            QByteArray rest = output.mid(idx + 1);
            qCDebug(dockerDeviceLog)
                << "Process first line received:" << m_process.commandLine() << firstLine;
            if (firstLine.startsWith("__qtc")) {
                bool ok = false;
                m_remotePID = firstLine.mid(5, firstLine.size() - 5 - 5).toLongLong(&ok);

                if (ok)
                    emit started(m_remotePID);

                if (rest.size() > 0)
                    emit readyRead(rest, {});

                m_hasReceivedFirstOutput = true;
                return;
            }
        }
        emit readyRead(m_process.readAllStandardOutput(), {});
    });

    connect(&m_process, &QtcProcess::readyReadStandardError, this, [this] {
        emit readyRead({}, m_process.readAllStandardError());
    });

    connect(&m_process, &QtcProcess::done, this, [this] {
        qCDebug(dockerDeviceLog) << "Process exited:" << m_process.commandLine()
                                 << "with code:" << m_process.resultData().m_exitCode;
        emit done(m_process.resultData());
    });
}

DockerProcessImpl::~DockerProcessImpl()
{
    if (m_process.state() == QProcess::Running)
        sendControlSignal(ControlSignal::Kill);
}

void DockerProcessImpl::start()
{
    m_process.setProcessImpl(m_setup.m_processImpl);
    m_process.setProcessMode(m_setup.m_processMode);
    m_process.setTerminalMode(m_setup.m_terminalMode);
    m_process.setReaperTimeout(m_setup.m_reaperTimeout);
    m_process.setWriteData(m_setup.m_writeData);
    m_process.setProcessChannelMode(m_setup.m_processChannelMode);
    m_process.setExtraData(m_setup.m_extraData);
    m_process.setStandardInputFile(m_setup.m_standardInputFile);
    m_process.setAbortOnMetaChars(m_setup.m_abortOnMetaChars);
    if (m_setup.m_lowPriority)
        m_process.setLowPriority();

    m_process.setCommand(fullLocalCommandLine(m_setup.m_processMode == ProcessMode::Writer));
    m_process.start();
}

qint64 DockerProcessImpl::write(const QByteArray &data)
{
    return m_process.writeRaw(data);
}

void DockerProcessImpl::sendControlSignal(ControlSignal controlSignal)
{
    int signal = controlSignalToInt(controlSignal);
    m_devicePrivate->runInShell(
        {"kill", {QString("-%1").arg(signal), QString("%2").arg(m_remotePID)}});
}

IDeviceWidget *DockerDevice::createWidget()
{
    return new DockerDeviceWidget(sharedFromThis());
}

Tasks DockerDevice::validate() const
{
    Tasks result;
    if (d->m_data.mounts.isEmpty()) {
        result << Task(Task::Error,
                       Tr::tr("The docker device has not set up shared directories."
                              "This will not work for building."),
                       {}, -1, {});
    }
    return result;
}

// DockerDeviceData

QString DockerDeviceData::repoAndTag() const
{
    if (repo == "<none>")
        return imageId;

    if (tag == "<none>")
        return repo;

    return repo + ':' + tag;
}

// DockerDevice

DockerDevice::DockerDevice(DockerSettings *settings, const DockerDeviceData &data)
    : d(new DockerDevicePrivate(this, settings))
{
    d->m_data = data;

    setDisplayType(Tr::tr("Docker"));
    setOsType(OsTypeOtherUnix);
    setDefaultDisplayName(Tr::tr("Docker Image"));;
    setDisplayName(Tr::tr("Docker Image \"%1\" (%2)").arg(data.repoAndTag()).arg(data.imageId));
    setAllowEmptyCommand(true);

    setOpenTerminal([this, settings](const Environment &env, const FilePath &workingDir) {
        Q_UNUSED(env); // TODO: That's the runnable's environment in general. Use it via -e below.
        updateContainerAccess();
        if (d->m_container.isEmpty()) {
            MessageManager::writeDisrupting(Tr::tr("Error starting remote shell. No container."));
            return;
        }

        QtcProcess *proc = new QtcProcess(d);
        proc->setTerminalMode(TerminalMode::On);

        QObject::connect(proc, &QtcProcess::done, [proc] {
            if (proc->error() != QProcess::UnknownError && MessageManager::instance()) {
                MessageManager::writeDisrupting(
                    Tr::tr("Error starting remote shell: %1").arg(proc->errorString()));
            }
            proc->deleteLater();
        });

        const QString wd = workingDir.isEmpty() ? "/" : workingDir.path();
        proc->setCommand({settings->dockerBinaryPath.filePath(), {"exec", "-it", "-w", wd, d->m_container, "/bin/sh"}});
        proc->setEnvironment(Environment::systemEnvironment()); // The host system env. Intentional.
        proc->start();
    });

    addDeviceAction({Tr::tr("Open Shell in Container"), [](const IDevice::Ptr &device, QWidget *) {
                         device->openTerminal(device->systemEnvironment(), FilePath());
    }});
}

DockerDevice::~DockerDevice()
{
    delete d;
}

void DockerDevice::shutdown()
{
    d->stopCurrentContainer();
    d->m_settings = nullptr;
}

const DockerDeviceData &DockerDevice::data() const
{
    return d->m_data;
}

DockerDeviceData &DockerDevice::data()
{
    return d->m_data;
}

void DockerDevice::updateContainerAccess() const
{
    d->updateContainerAccess();
}

void DockerDevicePrivate::stopCurrentContainer()
{
    if (!m_settings || m_container.isEmpty() || !DockerApi::isDockerDaemonAvailable(false).value_or(false))
        return;

    m_shell.reset();

    QtcProcess proc;
    proc.setCommand({m_settings->dockerBinaryPath.filePath(), {"container", "stop", m_container}});

    m_container.clear();

    proc.runBlocking();
}

static QString getLocalIPv4Address()
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (auto &a : addresses) {
        if (a.isInSubnet(QHostAddress("192.168.0.0"), 16))
            return a.toString();
        if (a.isInSubnet(QHostAddress("10.0.0.0"), 8))
            return a.toString();
        if (a.isInSubnet(QHostAddress("172.16.0.0"), 12))
            return a.toString();
    }
    return QString();
}

bool DockerDevicePrivate::createContainer()
{
    if (!m_settings)
        return false;

    const QString display = HostOsInfo::isLinuxHost() ? QString(":0")
                                                      : QString(getLocalIPv4Address() + ":0.0");
    CommandLine dockerCreate{m_settings->dockerBinaryPath.filePath(),
                             {"create",
                              "-i",
                              "--rm",
                              "-e",
                              QString("DISPLAY=%1").arg(display),
                              "-e",
                              "XAUTHORITY=/.Xauthority",
                              "--net",
                              "host"}};

#ifdef Q_OS_UNIX
    // no getuid() and getgid() on Windows.
    if (m_data.useLocalUidGid)
        dockerCreate.addArgs({"-u", QString("%1:%2").arg(getuid()).arg(getgid())});
#endif

    for (QString mount : qAsConst(m_data.mounts)) {
        if (mount.isEmpty())
            continue;
        mount = q->mapToDevicePath(FilePath::fromUserInput(mount));
        dockerCreate.addArgs({"-v", mount + ':' + mount});
    }
    FilePath dumperPath = FilePath::fromString("/tmp/qtcreator/debugger");
    dockerCreate.addArgs({"-v", q->debugDumperPath().toUserOutput() + ':' + dumperPath.path()});
    q->setDebugDumperPath(dumperPath);

    dockerCreate.addArgs({"--entrypoint", "/bin/sh", m_data.repoAndTag()});

    qCDebug(dockerDeviceLog) << "RUNNING: " << dockerCreate.toUserOutput();
    QtcProcess createProcess;
    createProcess.setCommand(dockerCreate);
    createProcess.runBlocking();

    if (createProcess.result() != ProcessResult::FinishedWithSuccess) {
        qCWarning(dockerDeviceLog) << "Failed creating docker container:";
        qCWarning(dockerDeviceLog) << "Exit Code:" << createProcess.exitCode();
        qCWarning(dockerDeviceLog) << createProcess.allOutput();
        return false;
    }

    m_container = createProcess.cleanedStdOut().trimmed();
    if (m_container.isEmpty())
        return false;

    LOG("ContainerId: " << m_container);
    return true;
}

void DockerDevicePrivate::startContainer()
{
    if (!createContainer())
        return;

    m_shell = std::make_unique<ContainerShell>(m_settings,
                                               m_container,
                                               FilePath::fromString(
                                                   QString("device://%1/")
                                                       .arg(this->q->id().toString())));

    connect(m_shell.get(), &DeviceShell::done, this, [this](const ProcessResultData &resultData) {
        if (resultData.m_error != QProcess::UnknownError)
            return;

        qCWarning(dockerDeviceLog) << "Container shell encountered error:" << resultData.m_error;
        m_shell.reset();

        DockerApi::recheckDockerDaemon();
        MessageManager::writeFlashing(Tr::tr("Docker daemon appears to be not running. "
                                             "Verify daemon is up and running and reset the "
                                             "docker daemon on the docker device settings page "
                                             "or restart Qt Creator."));
    });

    if (!m_shell->start()) {
        qCWarning(dockerDeviceLog) << "Container shell failed to start";
    }
}

void DockerDevicePrivate::updateContainerAccess()
{
    if (DockerApi::isDockerDaemonAvailable(false).value_or(false) == false)
        return;

    if (m_shell)
        return;

     startContainer();
}

void DockerDevice::setMounts(const QStringList &mounts) const
{
    d->m_data.mounts = mounts;
    d->stopCurrentContainer(); // Force re-start with new mounts.
}

CommandLine DockerDevice::withDockerExecCmd(const Utils::CommandLine &cmd, bool interactive) const
{
    if (!d->m_settings)
        return {};

    QStringList args;

    args << "exec";
    if (interactive)
        args << "-i";
    args << d->m_container;

    CommandLine dcmd{d->m_settings->dockerBinaryPath.filePath(), args};
    dcmd.addCommandLineAsArgs(cmd);
    return dcmd;
}

const char DockerDeviceDataImageIdKey[] = "DockerDeviceDataImageId";
const char DockerDeviceDataRepoKey[] = "DockerDeviceDataRepo";
const char DockerDeviceDataTagKey[] = "DockerDeviceDataTag";
const char DockerDeviceDataSizeKey[] = "DockerDeviceDataSize";
const char DockerDeviceUseOutsideUser[] = "DockerDeviceUseUidGid";
const char DockerDeviceMappedPaths[] = "DockerDeviceMappedPaths";

void DockerDevice::fromMap(const QVariantMap &map)
{
    ProjectExplorer::IDevice::fromMap(map);
    d->m_data.repo = map.value(DockerDeviceDataRepoKey).toString();
    d->m_data.tag = map.value(DockerDeviceDataTagKey).toString();
    d->m_data.imageId = map.value(DockerDeviceDataImageIdKey).toString();
    d->m_data.size = map.value(DockerDeviceDataSizeKey).toString();
    d->m_data.useLocalUidGid = map.value(DockerDeviceUseOutsideUser,
                                         HostOsInfo::isLinuxHost()).toBool();
    d->m_data.mounts = map.value(DockerDeviceMappedPaths).toStringList();
}

QVariantMap DockerDevice::toMap() const
{
    QVariantMap map = ProjectExplorer::IDevice::toMap();
    map.insert(DockerDeviceDataRepoKey, d->m_data.repo);
    map.insert(DockerDeviceDataTagKey, d->m_data.tag);
    map.insert(DockerDeviceDataImageIdKey, d->m_data.imageId);
    map.insert(DockerDeviceDataSizeKey, d->m_data.size);
    map.insert(DockerDeviceUseOutsideUser, d->m_data.useLocalUidGid);
    map.insert(DockerDeviceMappedPaths, d->m_data.mounts);
    return map;
}

ProcessInterface *DockerDevice::createProcessInterface() const
{
    return new DockerProcessImpl(d);
}

bool DockerDevice::canAutoDetectPorts() const
{
    return true;
}

PortsGatheringMethod DockerDevice::portsGatheringMethod() const
{
    return {
        [this](QAbstractSocket::NetworkLayerProtocol protocol) -> CommandLine {
            // We might encounter the situation that protocol is given IPv6
            // but the consumer of the free port information decides to open
            // an IPv4(only) port. As a result the next IPv6 scan will
            // report the port again as open (in IPv6 namespace), while the
            // same port in IPv4 namespace might still be blocked, and
            // re-use of this port fails.
            // GDBserver behaves exactly like this.

            Q_UNUSED(protocol)

            // /proc/net/tcp* covers /proc/net/tcp and /proc/net/tcp6
            return {filePath("sed"),
                    "-e 's/.*: [[:xdigit:]]*:\\([[:xdigit:]]\\{4\\}\\).*/\\1/g' /proc/net/tcp*",
                    CommandLine::Raw};
        },

        &Port::parseFromSedOutput
    };
};

DeviceProcessList *DockerDevice::createProcessListModel(QObject *) const
{
    return nullptr;
}

DeviceTester *DockerDevice::createDeviceTester() const
{
    return nullptr;
}

DeviceProcessSignalOperation::Ptr DockerDevice::signalOperation() const
{
    return DeviceProcessSignalOperation::Ptr();
}

DeviceEnvironmentFetcher::Ptr DockerDevice::environmentFetcher() const
{
    return DeviceEnvironmentFetcher::Ptr();
}

FilePath DockerDevice::mapToGlobalPath(const FilePath &pathOnDevice) const
{
    if (pathOnDevice.needsDevice()) {
        // Already correct form, only sanity check it's ours...
        QTC_CHECK(handlesFile(pathOnDevice));
        return pathOnDevice;
    }

    return FilePath::fromParts(Constants::DOCKER_DEVICE_SCHEME, d->m_data.repoAndTag(), pathOnDevice.path());

// The following would work, but gives no hint on repo and tag
//   result.setScheme("docker");
//    result.setHost(d->m_data.imageId);

// The following would work, but gives no hint on repo, tag and imageid
//    result.setScheme("device");
//    result.setHost(id().toString());
}

QString DockerDevice::mapToDevicePath(const Utils::FilePath &globalPath) const
{
    // make sure to convert windows style paths to unix style paths with the file system case:
    // C:/dev/src -> /c/dev/src
    const FilePath normalized = FilePath::fromString(globalPath.path()).normalizedPathName();
    QString path = normalized.path();
    if (normalized.startsWithDriveLetter()) {
        const QChar lowerDriveLetter = path.at(0).toLower();
        path = '/' + lowerDriveLetter + path.mid(2); // strip C:
    }
    return path;
}

Utils::FilePath DockerDevice::rootPath() const
{
    return FilePath::fromParts(Constants::DOCKER_DEVICE_SCHEME,  d->m_data.repoAndTag(), u"/");
}

bool DockerDevice::handlesFile(const FilePath &filePath) const
{
    if (filePath.scheme() == u"device" && filePath.host() == id().toString())
        return true;

    if (filePath.scheme() == Constants::DOCKER_DEVICE_SCHEME
            && filePath.host() == d->m_data.imageId)
        return true;

    if (filePath.scheme() == Constants::DOCKER_DEVICE_SCHEME
            && filePath.host() == QString(d->m_data.repo + ':' + d->m_data.tag))
        return true;

    return false;
}

bool DockerDevice::isExecutableFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInShell({"test", {"-x", path}});
}

bool DockerDevice::isReadableFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInShell({"test", {"-r", path, "-a", "-f", path}});
}

bool DockerDevice::isWritableFile(const Utils::FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInShell({"test", {"-w", path, "-a", "-f", path}});
}

bool DockerDevice::isReadableDirectory(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInShell({"test", {"-r", path, "-a", "-d", path}});
}

bool DockerDevice::isWritableDirectory(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInShell({"test", {"-w", path, "-a", "-d", path}});
}

bool DockerDevice::isFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInShell({"test", {"-f", path}});
}

bool DockerDevice::isDirectory(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInShell({"test", {"-d", path}});
}

bool DockerDevice::createDirectory(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInContainer({"mkdir", {"-p", path}});
}

bool DockerDevice::exists(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInShell({"test", {"-e", path}});
}

bool DockerDevice::ensureExistingFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    const QString path = filePath.path();
    return d->runInShell({"touch", {path}});
}

bool DockerDevice::removeFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    updateContainerAccess();
    return d->runInContainer({"rm", {filePath.path()}});
}

bool DockerDevice::removeRecursively(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    QTC_ASSERT(filePath.path().startsWith('/'), return false);
    updateContainerAccess();

    const QString path = filePath.cleanPath().path();
    // We are expecting this only to be called in a context of build directories or similar.
    // Chicken out in some cases that _might_ be user code errors.
    QTC_ASSERT(path.startsWith('/'), return false);
    const int levelsNeeded = path.startsWith("/home/") ? 4 : 3;
    QTC_ASSERT(path.count('/') >= levelsNeeded, return false);

    return d->runInContainer({"rm", {"-rf", "--", path}});
}

bool DockerDevice::copyFile(const FilePath &filePath, const FilePath &target) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    QTC_ASSERT(handlesFile(target), return false);
    updateContainerAccess();
    return d->runInContainer({"cp", {filePath.path(), target.path()}});
}

bool DockerDevice::renameFile(const FilePath &filePath, const FilePath &target) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    QTC_ASSERT(handlesFile(target), return false);
    updateContainerAccess();
    return d->runInContainer({"mv", {filePath.path(), target.path()}});
}

QDateTime DockerDevice::lastModified(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return {});
    updateContainerAccess();
    const QByteArray output = d->outputForRunInShell({"stat", {"-L", "-c", "%Y", filePath.path()}});
    qint64 secs = output.toLongLong();
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(secs, Qt::UTC);
    return dt;
}

FilePath DockerDevice::symLinkTarget(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return {});
    updateContainerAccess();
    const QByteArray output = d->outputForRunInShell({"readlink", {"-n", "-e", filePath.path()}});
    const QString out = QString::fromUtf8(output.data(), output.size());
    return out.isEmpty() ? FilePath() : filePath.withNewPath(out);
}

qint64 DockerDevice::fileSize(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return -1);
    updateContainerAccess();
    const QByteArray output = d->outputForRunInShell({"stat", {"-L", "-c", "%s", filePath.path()}});
    return output.toLongLong();
}

QFileDevice::Permissions DockerDevice::permissions(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return {});
    updateContainerAccess();

    const QByteArray output = d->outputForRunInShell({"stat", {"-L", "-c", "%a", filePath.path()}});
    const uint bits = output.toUInt(nullptr, 8);
    QFileDevice::Permissions perm = {};
#define BIT(n, p) if (bits & (1<<n)) perm |= QFileDevice::p
    BIT(0, ExeOther);
    BIT(1, WriteOther);
    BIT(2, ReadOther);
    BIT(3, ExeGroup);
    BIT(4, WriteGroup);
    BIT(5, ReadGroup);
    BIT(6, ExeUser);
    BIT(7, WriteUser);
    BIT(8, ReadUser);
#undef BIT
    return perm;
}

bool DockerDevice::setPermissions(const FilePath &filePath, QFileDevice::Permissions permissions) const
{
    Q_UNUSED(permissions)
    QTC_ASSERT(handlesFile(filePath), return {});
    updateContainerAccess();
    QTC_CHECK(false); // FIXME: Implement.
    return false;
}

void DockerDevice::iterateWithFind(const FilePath &filePath,
                                   const std::function<bool(const Utils::FilePath &)> &callBack,
                                   const FileFilter &filter) const
{
    QTC_ASSERT(callBack, return);
    QTC_CHECK(filePath.isAbsolutePath());
    QStringList arguments{filePath.path()};

    const QDir::Filters filters = filter.fileFilters;
    if (filters & QDir::NoSymLinks)
        arguments.prepend("-H");
    else
        arguments.prepend("-L");

    arguments.append({"-mindepth", "1"});

    if (!filter.iteratorFlags.testFlag(QDirIterator::Subdirectories))
        arguments.append({"-maxdepth", "1"});

    QStringList filterOptions;

    if (!(filters & QDir::Hidden))
        filterOptions << "!" << "-name" << ".*";

    QStringList filterFilesAndDirs;
    if (filters & QDir::Dirs)
        filterFilesAndDirs << "-type" << "d";
    if (filters & QDir::Files) {
        if (!filterFilesAndDirs.isEmpty())
            filterFilesAndDirs << "-o";
        filterFilesAndDirs << "-type" << "f";
    }
    if (!filterFilesAndDirs.isEmpty())
        filterOptions << "(" << filterFilesAndDirs << ")";

    QStringList accessOptions;
    if (filters & QDir::Readable)
        accessOptions << "-readable";
    if (filters & QDir::Writable) {
        if (!accessOptions.isEmpty())
            accessOptions << "-o";
        accessOptions << "-writable";
    }
    if (filters & QDir::Executable) {
        if (!accessOptions.isEmpty())
            accessOptions << "-o";
        accessOptions << "-executable";
    }

    if (!accessOptions.isEmpty())
        filterOptions << "(" << accessOptions << ")";

    QTC_CHECK(filters ^ QDir::AllDirs);
    QTC_CHECK(filters ^ QDir::Drives);
    QTC_CHECK(filters ^ QDir::NoDot);
    QTC_CHECK(filters ^ QDir::NoDotDot);
    QTC_CHECK(filters ^ QDir::Hidden);
    QTC_CHECK(filters ^ QDir::System);

    const QString nameOption = (filters & QDir::CaseSensitive) ? QString{"-name"}
                                                               : QString{"-iname"};
    if (!filter.nameFilters.isEmpty()) {
        const QRegularExpression oneChar("\\[.*?\\]");
        bool addedFirst = false;
        for (const QString &current : filter.nameFilters) {
            if (current.indexOf(oneChar) != -1) {
                LOG("Skipped" << current << "due to presence of [] wildcard");
                continue;
            }

            if (addedFirst)
                filterOptions << "-o";
            filterOptions << nameOption << current;
            addedFirst = true;
        }
    }
    arguments << filterOptions;
    const QByteArray output = d->outputForRunInShell({"find", arguments});
    const QString out = QString::fromUtf8(output.data(), output.size());
    if (!output.isEmpty() && !out.startsWith(filePath.path())) { // missing find, unknown option
        LOG("Setting 'do not use find'" << out.left(out.indexOf('\n')));
        d->m_useFind = false;
        return;
    }

    const QStringList entries = out.split("\n", Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        if (entry.startsWith("find: "))
            continue;
        const FilePath fp = FilePath::fromString(entry);

        if (!callBack(fp.onDevice(filePath)))
            break;
    }
}

void DockerDevice::iterateDirectory(const FilePath &filePath,
                                    const std::function<bool(const FilePath &)> &callBack,
                                    const FileFilter &filter) const
{
    QTC_ASSERT(handlesFile(filePath), return);
    updateContainerAccess();

    if (d->m_useFind) {
        iterateWithFind(filePath, callBack, filter);
        // d->m_useFind will be set to false if 'find' is not found. In this
        // case fall back to 'ls' below.
        if (d->m_useFind)
            return;
    }

    // if we do not have find - use ls as fallback
    const QByteArray output = d->outputForRunInShell({"ls", {"-1", "-b", "--", filePath.path()}});
    const QStringList entries = QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts);
    FileUtils::iterateLsOutput(filePath, entries, filter, callBack);
}

QByteArray DockerDevice::fileContents(const FilePath &filePath, qint64 limit, qint64 offset) const
{
    QTC_ASSERT(handlesFile(filePath), return {});
    updateContainerAccess();

    QStringList args = {"if=" + filePath.path(), "status=none"};
    if (limit > 0 || offset > 0) {
        const qint64 gcd = std::gcd(limit, offset);
        args += {QString("bs=%1").arg(gcd),
                 QString("count=%1").arg(limit / gcd),
                 QString("seek=%1").arg(offset / gcd)};
    }

    QtcProcess proc;
    proc.setCommand(withDockerExecCmd({"dd", args}));
    proc.start();
    proc.waitForFinished();

    QByteArray output = proc.readAllStandardOutput();
    return output;
}

bool DockerDevice::writeFileContents(const FilePath &filePath, const QByteArray &data) const
{
    QTC_ASSERT(handlesFile(filePath), return {});
    updateContainerAccess();

    QTC_ASSERT(handlesFile(filePath), return {});
    return d->runInShell({"dd", {"of=" + filePath.path()}}, data);
}

Environment DockerDevice::systemEnvironment() const
{
    if (!d->m_cachedEnviroment.isValid())
        d->fetchSystemEnviroment();

    QTC_CHECK(d->m_cachedEnviroment.isValid());
    return d->m_cachedEnviroment;
}

void DockerDevice::aboutToBeRemoved() const
{
    KitDetector detector(sharedFromThis());
    detector.undoAutoDetect(id().toString());
}

void DockerDevicePrivate::fetchSystemEnviroment()
{
    if (m_shell && m_shell->state() == DeviceShell::State::Succeeded) {
        const QByteArray output = outputForRunInShell({"env", {}});
        const QString out = QString::fromUtf8(output.data(), output.size());
        m_cachedEnviroment = Environment(out.split('\n', Qt::SkipEmptyParts), q->osType());
        return;
    }

    QtcProcess proc;
    proc.setCommand(q->withDockerExecCmd({"env", {}}));
    proc.start();
    proc.waitForFinished();
    const QString remoteOutput = proc.cleanedStdOut();

    m_cachedEnviroment = Environment(remoteOutput.split('\n', Qt::SkipEmptyParts), q->osType());

    const QString remoteError = proc.cleanedStdErr();
    if (!remoteError.isEmpty())
        qWarning("Cannot read container environment: %s\n", qPrintable(remoteError));
}

bool DockerDevicePrivate::runInContainer(const CommandLine &cmd) const
{
    if (!m_settings || !DockerApi::isDockerDaemonAvailable(false).value_or(false))
        return false;

    CommandLine dcmd{m_settings->dockerBinaryPath.filePath(), {"exec", m_container}};
    dcmd.addCommandLineAsArgs(cmd);

    QtcProcess proc;
    proc.setCommand(dcmd);
    proc.setWorkingDirectory(FilePath::fromString(QDir::tempPath()));
    proc.start();
    proc.waitForFinished();

    LOG("Run sync:" << dcmd.toUserOutput() << " result: " << proc.exitCode());
    const int exitCode = proc.exitCode();
    return exitCode == 0;
}

bool DockerDevicePrivate::runInShell(const CommandLine &cmd, const QByteArray& stdInData) const
{
    QTC_ASSERT(m_shell, return false);
    return m_shell->runInShell(cmd, stdInData);
}

QByteArray DockerDevicePrivate::outputForRunInShell(const CommandLine &cmd) const
{
    QTC_ASSERT(m_shell.get(), return {});
    return m_shell->outputForRunInShell(cmd).stdOut;
}

// Factory

class DockerImageItem final : public TreeItem, public DockerDeviceData
{
public:
    DockerImageItem() {}

    QVariant data(int column, int role) const final
    {
        switch (column) {
        case 0:
            if (role == Qt::DisplayRole)
                return repo;
            break;
        case 1:
            if (role == Qt::DisplayRole)
                return tag;
            break;
        case 2:
            if (role == Qt::DisplayRole)
                return imageId;
            break;
        case 3:
            if (role == Qt::DisplayRole)
                return size;
            break;
        }

        return QVariant();
    }
};

class DockerDeviceSetupWizard final : public QDialog
{
public:
    DockerDeviceSetupWizard(DockerSettings *settings)
        : QDialog(ICore::dialogParent())
        , m_settings(settings)
    {
        setWindowTitle(Tr::tr("Docker Image Selection"));
        resize(800, 600);

        m_model.setHeader({"Repository", "Tag", "Image", "Size"});

        m_view = new TreeView;
        QCheckBox *showUnnamedContainers = new QCheckBox(Tr::tr("Show Unnamed Images"));
        QLabel *statusLabel = new QLabel();
        statusLabel->setText(Tr::tr("Loading ..."));
        statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        statusLabel->setAlignment(Qt::AlignCenter);

        m_proxyModel = new SortFilterModel(this);

        m_proxyModel->setFilterRowFunction(
            [showUnnamedContainers, this](int source_row, const QModelIndex &parent) {
                if (showUnnamedContainers->isChecked())
                    return true;

                return m_model.index(source_row, 0, parent).data(Qt::DisplayRole) != "<none>";
            });

        connect(showUnnamedContainers, &QCheckBox::toggled, this, [this]() {
            m_proxyModel->invalidate();
        });

        m_proxyModel->setSourceModel(&m_model);

        m_view->setModel(m_proxyModel);
        m_view->setEnabled(false);
        m_view->header()->setStretchLastSection(true);
        m_view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_view->setSelectionMode(QAbstractItemView::SingleSelection);
        m_view->setSortingEnabled(true);
        m_view->sortByColumn(0, Qt::AscendingOrder);
        m_view->setEnabled(false);

        connect(m_view, &QAbstractItemView::doubleClicked, this, &QDialog::accept);

        m_log = new QTextBrowser;
        m_log->setVisible(dockerDeviceLog().isDebugEnabled());

        const QString fail = QString{"Docker: "}
                             + QCoreApplication::translate("Debugger", "Process failed to start.");
        auto errorLabel = new Utils::InfoLabel(fail, Utils::InfoLabel::Error, this);
        errorLabel->setVisible(false);

        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

        using namespace Layouting;
        Column {
            Stack {
                statusLabel,
                m_view,
            },
            m_log,
            errorLabel,
            Row {
                showUnnamedContainers,
                m_buttons
            },
        }.attachTo(this);

        connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

        CommandLine cmd{m_settings->dockerBinaryPath.filePath(), {"images", "--format", "{{.ID}}\\t{{.Repository}}\\t{{.Tag}}\\t{{.Size}}"}};
        m_log->append(Tr::tr("Running \"%1\"\n").arg(cmd.toUserOutput()));

        m_process = new QtcProcess(this);
        m_process->setCommand(cmd);

        connect(m_process, &QtcProcess::readyReadStandardOutput, [this] {
            const QString out = QString::fromUtf8(m_process->readAllStandardOutput().trimmed());
            m_log->append(out);
            for (const QString &line : out.split('\n')) {
                const QStringList parts = line.trimmed().split('\t');
                if (parts.size() != 4) {
                    m_log->append(Tr::tr("Unexpected result: %1").arg(line) + '\n');
                    continue;
                }
                auto item = new DockerImageItem;
                item->imageId = parts.at(0);
                item->repo = parts.at(1);
                item->tag = parts.at(2);
                item->size = parts.at(3);
                m_model.rootItem()->appendChild(item);
            }
            m_log->append(Tr::tr("Done."));
        });

        connect(m_process, &Utils::QtcProcess::readyReadStandardError, this, [this] {
            const QString out = Tr::tr("Error: %1").arg(m_process->cleanedStdErr());
            m_log->append(Tr::tr("Error: %1").arg(out));
        });

        connect(m_process, &QtcProcess::done, errorLabel, [errorLabel, this, statusLabel] {
            delete statusLabel;
            if (m_process->result() == ProcessResult::FinishedWithSuccess)
                m_view->setEnabled(true);
            else
                errorLabel->setVisible(true);
        });

        connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, [this] {
            const QModelIndexList selectedRows = m_view->selectionModel()->selectedRows();
            QTC_ASSERT(selectedRows.size() == 1, return);
            m_buttons->button(QDialogButtonBox::Ok)->setEnabled(selectedRows.size() == 1);
        });

        m_process->start();
    }

    IDevice::Ptr device() const
    {
        const QModelIndexList selectedRows = m_view->selectionModel()->selectedRows();
        QTC_ASSERT(selectedRows.size() == 1, return {});
        DockerImageItem *item = m_model.itemForIndex(m_proxyModel->mapToSource(selectedRows.front()));
        QTC_ASSERT(item, return {});

        auto device = DockerDevice::create(m_settings, *item);
        device->setupId(IDevice::ManuallyAdded);
        device->setType(Constants::DOCKER_DEVICE_TYPE);
        device->setMachineType(IDevice::Hardware);

        return device;
    }

public:
    TreeModel<DockerImageItem> m_model;
    TreeView *m_view = nullptr;
    SortFilterModel *m_proxyModel = nullptr;
    QTextBrowser *m_log = nullptr;
    QDialogButtonBox *m_buttons;
    DockerSettings *m_settings;

    QtcProcess *m_process = nullptr;
    QString m_selectedId;
};

// Factory

DockerDeviceFactory::DockerDeviceFactory(DockerSettings *settings)
    : IDeviceFactory(Constants::DOCKER_DEVICE_TYPE)
{
    setDisplayName(Tr::tr("Docker Device"));
    setIcon(QIcon());
    setCreator([settings] {
        DockerDeviceSetupWizard wizard(settings);
        if (wizard.exec() != QDialog::Accepted)
            return IDevice::Ptr();
        return wizard.device();
    });
    setConstructionFunction([settings, this] {
        auto device = DockerDevice::create(settings, {});
        QMutexLocker lk(&m_deviceListMutex);
        m_existingDevices.push_back(device);
        return device;
    });
}

void DockerDeviceFactory::shutdownExistingDevices()
{
    QMutexLocker lk(&m_deviceListMutex);
    for (const auto &weakDevice : m_existingDevices) {
        if (QSharedPointer<DockerDevice> device = weakDevice.lock())
            device->shutdown();
    }
}

} // Docker::Internal
