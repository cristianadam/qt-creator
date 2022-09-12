// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "dockerdevice.h"

#include "dockerdevice_p.h"
#include "dockerprocessimpl_p.h"

#include "dockerapi.h"
#include "dockerconstants.h"
#include "dockerdevicewidget.h"
#include "dockertr.h"
#include "kitdetector.h"

#include <projectexplorer/task.h>

#include <utils/fileutils.h>
#include <utils/port.h>

#include <QRegularExpression>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

Q_LOGGING_CATEGORY(dockerDeviceLog, "qtc.docker.device", QtWarningMsg);

namespace Docker::Internal {

DockerDevice::DockerDevice(DockerSettings *settings, const DockerDeviceData &data)
    : d(new DockerDevicePrivate(this, settings, data))
{
    m_privateThread.setObjectName("DockerDevicePrivateThread (" + data.repoAndTag() + ")");
    d->moveToThread(&m_privateThread);
    m_privateThread.start();

    setDisplayType(Tr::tr("Docker"));
    setOsType(OsTypeOtherUnix);
    setDefaultDisplayName(Tr::tr("Docker Image"));

    setDisplayName(Tr::tr("Docker Image \"%1\" (%2)").arg(data.repoAndTag()).arg(data.imageId));
    setAllowEmptyCommand(true);

    setOpenTerminal([this, settings](const Environment &env, const FilePath &workingDir) {
        Q_UNUSED(env); // TODO: That's the runnable's environment in general. Use it via -e below.
        updateContainerAccess();
        if (d->containerId().isEmpty()) {
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
        proc->setCommand({settings->dockerBinaryPath.filePath(),
                          {"exec", "-it", "-w", wd, d->containerId(), "/bin/sh"}});
        proc->setEnvironment(Environment::systemEnvironment()); // The host system env. Intentional.
        proc->start();
    });

    addDeviceAction({Tr::tr("Open Shell in Container"), [](const IDevice::Ptr &device, QWidget *) {
                         device->openTerminal(device->systemEnvironment(), FilePath());
                     }});
}

DockerDevice::~DockerDevice()
{
    m_privateThread.quit();
    m_privateThread.wait();
    delete d;
}

void DockerDevice::shutdown()
{
    d->shutdown();
}

const DockerDeviceData DockerDevice::data() const
{
    return d->data();
}

DockerDeviceData DockerDevice::data()
{
    return d->data();
}

void DockerDevice::updateContainerAccess() const
{
    d->updateContainerAccess();
}

void DockerDevice::setMounts(const QStringList &mounts) const
{
    d->changeMounts(mounts);
}

CommandLine DockerDevice::withDockerExecCmd(const Utils::CommandLine &cmd, bool interactive) const
{
    if (!d->settings())
        return {};

    QStringList args;

    args << "exec";
    if (interactive)
        args << "-i";
    args << d->containerId();

    CommandLine dcmd{d->settings()->dockerBinaryPath.filePath(), args};
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
    DockerDeviceData data;

    data.repo = map.value(DockerDeviceDataRepoKey).toString();
    data.tag = map.value(DockerDeviceDataTagKey).toString();
    data.imageId = map.value(DockerDeviceDataImageIdKey).toString();
    data.size = map.value(DockerDeviceDataSizeKey).toString();
    data.useLocalUidGid = map.value(DockerDeviceUseOutsideUser, HostOsInfo::isLinuxHost())
                                   .toBool();
    data.mounts = map.value(DockerDeviceMappedPaths).toStringList();
    d->setData(data);
}

QVariantMap DockerDevice::toMap() const
{
    QVariantMap map = ProjectExplorer::IDevice::toMap();
    DockerDeviceData data = d->data();

    map.insert(DockerDeviceDataRepoKey, data.repo);
    map.insert(DockerDeviceDataTagKey, data.tag);
    map.insert(DockerDeviceDataImageIdKey, data.imageId);
    map.insert(DockerDeviceDataSizeKey, data.size);
    map.insert(DockerDeviceUseOutsideUser, data.useLocalUidGid);
    map.insert(DockerDeviceMappedPaths, data.mounts);
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
    return {[this](QAbstractSocket::NetworkLayerProtocol protocol) -> CommandLine {
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

            &Port::parseFromSedOutput};
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

    return FilePath::fromParts(Constants::DOCKER_DEVICE_SCHEME,
                               d->repoAndTag(),
                               pathOnDevice.path());

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
    return FilePath::fromParts(Constants::DOCKER_DEVICE_SCHEME, d->repoAndTag(), u"/");
}

bool DockerDevice::handlesFile(const FilePath &filePath) const
{
    if (filePath.scheme() == u"device" && filePath.host() == id().toString())
        return true;

    if (filePath.scheme() == Constants::DOCKER_DEVICE_SCHEME && filePath.host() == d->dockerImageId())
        return true;

    if (filePath.scheme() == Constants::DOCKER_DEVICE_SCHEME
        && filePath.host() == QString(d->repoAndTag()))
        return true;

    return false;
}

bool DockerDevice::isExecutableFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"test", {"-x", path}});
}

bool DockerDevice::isReadableFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"test", {"-r", path, "-a", "-f", path}});
}

bool DockerDevice::isWritableFile(const Utils::FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"test", {"-w", path, "-a", "-f", path}});
}

bool DockerDevice::isReadableDirectory(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"test", {"-r", path, "-a", "-d", path}});
}

bool DockerDevice::isWritableDirectory(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"test", {"-w", path, "-a", "-d", path}});
}

bool DockerDevice::isFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"test", {"-f", path}});
}

bool DockerDevice::isDirectory(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"test", {"-d", path}});
}

bool DockerDevice::createDirectory(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"mkdir", {"-p", path}});
}

bool DockerDevice::exists(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"test", {"-e", path}});
}

bool DockerDevice::ensureExistingFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    const QString path = filePath.path();
    return d->runInShell({"touch", {path}});
}

bool DockerDevice::removeFile(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    return d->runInShell({"rm", {filePath.path()}});
}

bool DockerDevice::removeRecursively(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    QTC_ASSERT(filePath.path().startsWith('/'), return false);

    const QString path = filePath.cleanPath().path();
    // We are expecting this only to be called in a context of build directories or similar.
    // Chicken out in some cases that _might_ be user code errors.
    QTC_ASSERT(path.startsWith('/'), return false);
    const int levelsNeeded = path.startsWith("/home/") ? 4 : 3;
    QTC_ASSERT(path.count('/') >= levelsNeeded, return false);

    return d->runInShell({"rm", {"-rf", "--", path}});
}

bool DockerDevice::copyFile(const FilePath &filePath, const FilePath &target) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    QTC_ASSERT(handlesFile(target), return false);
    return d->runInShell({"cp", {filePath.path(), target.path()}});
}

bool DockerDevice::renameFile(const FilePath &filePath, const FilePath &target) const
{
    QTC_ASSERT(handlesFile(filePath), return false);
    QTC_ASSERT(handlesFile(target), return false);
    return d->runInShell({"mv", {filePath.path(), target.path()}});
}

QDateTime DockerDevice::lastModified(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return {});
    const QByteArray output = d->outputForRunInShell({"stat", {"-L", "-c", "%Y", filePath.path()}});
    qint64 secs = output.toLongLong();
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(secs, Qt::UTC);
    return dt;
}

FilePath DockerDevice::symLinkTarget(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return {});
    const QByteArray output = d->outputForRunInShell({"readlink", {"-n", "-e", filePath.path()}});
    const QString out = QString::fromUtf8(output.data(), output.size());
    return out.isEmpty() ? FilePath() : filePath.withNewPath(out);
}

qint64 DockerDevice::fileSize(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return -1);
    const QByteArray output = d->outputForRunInShell({"stat", {"-L", "-c", "%s", filePath.path()}});
    return output.toLongLong();
}

QFileDevice::Permissions DockerDevice::permissions(const FilePath &filePath) const
{
    QTC_ASSERT(handlesFile(filePath), return {});

    const QByteArray output = d->outputForRunInShell({"stat", {"-L", "-c", "%a", filePath.path()}});
    const uint bits = output.toUInt(nullptr, 8);
    QFileDevice::Permissions perm = {};
#define BIT(n, p) \
    if (bits & (1 << n)) \
    perm |= QFileDevice::p
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

bool DockerDevice::setPermissions(const FilePath &filePath,
                                  QFileDevice::Permissions permissions) const
{
    Q_UNUSED(permissions)
    QTC_ASSERT(handlesFile(filePath), return {});
    QTC_CHECK(false); // FIXME: Implement.
    return false;
}

bool DockerDevice::ensureReachable(const Utils::FilePath &other) const
{
    if (other.needsDevice())
        return false;

    if (other.isDir())
        return d->ensureReachable(other);
    return d->ensureReachable(other.parentDir());
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
        filterOptions << "!"
                      << "-name"
                      << ".*";

    QStringList filterFilesAndDirs;
    if (filters & QDir::Dirs)
        filterFilesAndDirs << "-type"
                           << "d";
    if (filters & QDir::Files) {
        if (!filterFilesAndDirs.isEmpty())
            filterFilesAndDirs << "-o";
        filterFilesAndDirs << "-type"
                           << "f";
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
                qCDebug(dockerDeviceLog)
                    << "Skipped" << current << "due to presence of [] wildcard";
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
        qCDebug(dockerDeviceLog) << "Setting 'do not use find'" << out.left(out.indexOf('\n'));
        d->setUseFind(false);
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
    QTC_ASSERT(handlesFile(filePath), return );

    if (d->useFind()) {
        iterateWithFind(filePath, callBack, filter);
        // d->m_useFind will be set to false if 'find' is not found. In this
        // case fall back to 'ls' below.
        if (d->useFind())
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
    return d->runInShell({"dd", {"of=" + filePath.path()}}, data);
}

Environment DockerDevice::systemEnvironment() const
{
    return d->environment();
}

void DockerDevice::aboutToBeRemoved() const
{
    KitDetector detector(sharedFromThis());
    detector.undoAutoDetect(id().toString());
}

IDeviceWidget *DockerDevice::createWidget()
{
    return new DockerDeviceWidget(sharedFromThis());
}

Tasks DockerDevice::validate() const
{
    Tasks result;
    if (d->data().mounts.isEmpty()) {
        result << Task(Task::Error,
                       Tr::tr("The docker device has not set up shared directories."
                              "This will not work for building."),
                       {},
                       -1,
                       {});
    }
    return result;
}

} // namespace Docker::Internal
