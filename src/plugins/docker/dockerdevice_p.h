// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "containershell.h"
#include "dockerapi.h"
#include "dockerconstants.h"
#include "dockertr.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <utils/algorithm.h>
#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>

#ifdef Q_OS_UNIX
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Docker::Internal {

static QString getLocalIPv4Address();

template<typename TFunc>
bool inThread(QObject *obj, TFunc func)
{
    if (QThread::currentThread() != obj->thread()) {
        QMetaObject::invokeMethod(obj, func, Qt::BlockingQueuedConnection);
        return true;
    }
    return false;
}

class DockerDevicePrivate : public QObject
{
public:
    DockerDevicePrivate(DockerDevice *parent, DockerSettings *settings, DockerDeviceData data)
        : q(parent)
        , m_settings(settings)
        , m_data(std::move(data))
    {}

    ~DockerDevicePrivate() { stopCurrentContainer(); }

    bool runInShell(const Utils::CommandLine &cmd, const QByteArray &stdInData = {});
    QByteArray outputForRunInShell(const Utils::CommandLine &cmd);

    void updateContainerAccess();
    void changeMounts(QStringList newMounts);
    bool ensureReachable(const Utils::FilePath &other);
    void shutdown();

    DockerDevice *const q;

    QString containerId() { return m_container; }
    DockerDeviceData data() { return m_data; }
    void setData(DockerDeviceData data) { m_data = std::move(data); }
    DockerSettings *settings() { return m_settings; }

    QString repoAndTag() const { return m_data.repoAndTag(); }
    QString dockerImageId() const { return m_data.imageId; }

    bool useFind() const { return m_useFind; }
    void setUseFind(bool useFind) { m_useFind = useFind; }

    Utils::Environment environment();

private:
    bool createContainer();
    void startContainer();
    void stopCurrentContainer();
    void fetchSystemEnviroment();

    bool addTemporaryMount(const Utils::FilePath &path, const Utils::FilePath &containerPath);

    DockerSettings *m_settings;
    DockerDeviceData m_data;

    struct TemporaryMountInfo {
        Utils::FilePath path;
        Utils::FilePath containerPath;
    };

    QList<TemporaryMountInfo> m_temporaryMounts;

    std::unique_ptr<ContainerShell> m_shell;
    QString m_container;

    Utils::Environment m_cachedEnviroment;
    bool m_useFind = true; // prefer find over ls and hacks, but be able to use ls as fallback
    bool m_isShutdown = false;
};

bool DockerDevicePrivate::addTemporaryMount(const Utils::FilePath &path, const Utils::FilePath &containerPath)
{
    if (!Utils::anyOf(m_temporaryMounts, [path, containerPath](const TemporaryMountInfo &info) {
            return info.containerPath == containerPath;
        })) {
        qCDebug(dockerDeviceLog) << "Adding temporary mount:" << path;
        m_temporaryMounts.append({path, containerPath});
        stopCurrentContainer(); // Force re-start with new mounts.
        return true;
    }
    return false;
}

Utils::Environment DockerDevicePrivate::environment()
{
    Utils::Environment result;
    if (inThread(this, [this, &result]() { result = environment(); }))
        return result;

    if (!m_cachedEnviroment.isValid())
        fetchSystemEnviroment();

    QTC_CHECK(m_cachedEnviroment.isValid());
    return m_cachedEnviroment;
}

void DockerDevicePrivate::shutdown()
{
    if (inThread(this, [this] { shutdown(); }))
        return;

    m_isShutdown = true;
    m_settings = nullptr;
    stopCurrentContainer();
}

void DockerDevicePrivate::stopCurrentContainer()
{
    if (!m_settings || m_container.isEmpty()
        || !DockerApi::isDockerDaemonAvailable(false).value_or(false))
        return;

    m_shell.reset();

    Utils::QtcProcess proc;
    proc.setCommand({m_settings->dockerBinaryPath.filePath(), {"container", "stop", m_container}});
    m_container.clear();

    proc.runBlocking();

    m_cachedEnviroment.clear();
}

bool DockerDevicePrivate::createContainer()
{
    if (!m_settings)
        return false;

    const QString display = Utils::HostOsInfo::isLinuxHost()
                                ? QString(":0")
                                : QString(getLocalIPv4Address() + ":0.0");
    Utils::CommandLine dockerCreate{m_settings->dockerBinaryPath.filePath(),
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
        mount = q->mapToDevicePath(Utils::FilePath::fromUserInput(mount));
        dockerCreate.addArgs({"-v", mount + ':' + mount});
    }

    Utils::FilePath dumperPath = Utils::FilePath::fromString("/tmp/qtcreator/debugger");
    addTemporaryMount(Core::ICore::resourcePath("debugger/"), dumperPath);
    q->setDebugDumperPath(dumperPath);

    for (const auto &[path, containerPath] : qAsConst(m_temporaryMounts)) {
        if (path.isEmpty())
            continue;
        dockerCreate.addArgs({"-v", path.toString() + ':' + containerPath.toString()});
    }

    dockerCreate.addArgs({m_data.repoAndTag()});

    qCDebug(dockerDeviceLog) << "RUNNING: " << dockerCreate.toUserOutput();
    Utils::QtcProcess createProcess;
    createProcess.setCommand(dockerCreate);
    createProcess.runBlocking();

    if (createProcess.result() != Utils::ProcessResult::FinishedWithSuccess) {
        qCWarning(dockerDeviceLog) << "Failed creating docker container:";
        qCWarning(dockerDeviceLog) << "Exit Code:" << createProcess.exitCode();
        qCWarning(dockerDeviceLog) << createProcess.allOutput();
        return false;
    }

    m_container = createProcess.cleanedStdOut().trimmed();
    if (m_container.isEmpty())
        return false;

    qCDebug(dockerDeviceLog) << "ContainerId:" << m_container;
    return true;
}

void DockerDevicePrivate::startContainer()
{
    if (!createContainer())
        return;

    m_shell = std::make_unique<ContainerShell>(m_settings,
                                               m_container,
                                               Utils::FilePath::fromString(
                                                   QString("device://%1/")
                                                       .arg(this->q->id().toString())));

    connect(m_shell.get(),
            &Utils::DeviceShell::done,
            this,
            [this](const Utils::ProcessResultData &resultData) {
                if (resultData.m_error != QProcess::UnknownError
                    || resultData.m_exitStatus == QProcess::NormalExit)
                    return;

                qCWarning(dockerDeviceLog)
                    << "Container shell encountered error:" << resultData.m_error;
                m_shell.reset();

                DockerApi::recheckDockerDaemon();
                Core::MessageManager::writeFlashing(
                    Tr::tr("Docker daemon appears to be not running. "
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
    if (inThread(this, [this]() { updateContainerAccess(); }))
        return;

    if (m_isShutdown)
        return;

    if (DockerApi::isDockerDaemonAvailable(false).value_or(false) == false)
        return;

    if (m_shell)
        return;

    startContainer();
}

void DockerDevicePrivate::fetchSystemEnviroment()
{
    updateContainerAccess();

    if (m_shell && m_shell->state() == Utils::DeviceShell::State::Succeeded) {
        const QByteArray output = outputForRunInShell({"env", {}});
        const QString out = QString::fromUtf8(output.data(), output.size());
        m_cachedEnviroment = Utils::Environment(out.split('\n', Qt::SkipEmptyParts), q->osType());
        return;
    }

    Utils::QtcProcess proc;
    proc.setCommand(q->withDockerExecCmd({"env", {}}));
    proc.start();
    proc.waitForFinished();
    const QString remoteOutput = proc.cleanedStdOut();

    m_cachedEnviroment = Utils::Environment(remoteOutput.split('\n', Qt::SkipEmptyParts),
                                            q->osType());

    const QString remoteError = proc.cleanedStdErr();
    if (!remoteError.isEmpty())
        qCWarning(dockerDeviceLog) << "Cannot read container environment:", qPrintable(remoteError);
}

void DockerDevicePrivate::changeMounts(QStringList newMounts)
{
    if (inThread(this, [this, &newMounts]() { changeMounts(std::move(newMounts)); }))
        return;

    newMounts.removeDuplicates();
    if (m_data.mounts != newMounts) {
        m_data.mounts = newMounts;
        stopCurrentContainer(); // Force re-start with new mounts.
    }
}

bool DockerDevicePrivate::ensureReachable(const Utils::FilePath &other)
{
    bool result = false;
    if (inThread(this, [this, &other, &result]() { result = ensureReachable(other); }))
        return result;

    for (const auto &mount : m_data.mounts) {
        const Utils::FilePath fMount = Utils::FilePath::fromString(mount);
        if (other.isChildOf(fMount))
            return true;
    }

    for (const auto &[path, containerPath] : m_temporaryMounts) {
        if (path.path() != containerPath.path())
            continue;

        if (other.isChildOf(path))
            return true;
    }

    addTemporaryMount(other, other);
    return true;
}

bool DockerDevicePrivate::runInShell(const Utils::CommandLine &cmd, const QByteArray &stdInData)
{
    updateContainerAccess();
    QTC_ASSERT(m_shell, return false);
    return m_shell->runInShell(cmd, stdInData);
}

QByteArray DockerDevicePrivate::outputForRunInShell(const Utils::CommandLine &cmd)
{
    updateContainerAccess();
    QTC_ASSERT(m_shell.get(), return {});
    return m_shell->outputForRunInShell(cmd).stdOut;
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

} // namespace Docker::Internal
