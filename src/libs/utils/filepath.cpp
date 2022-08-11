/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "filepath.h"

#include "algorithm.h"
#include "environment.h"
#include "fileutils.h"
#include "hostosinfo.h"
#include "qtcassert.h"

#include <QtGlobal>
#include <QDateTime>
#include <QDebug>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUrl>
#include <QStringView>

#ifdef Q_OS_WIN
#ifdef QTCREATOR_PCH_H
#define CALLBACK WINAPI
#endif
#include <qt_windows.h>
#include <shlobj.h>
#endif

#ifdef Q_OS_OSX
#include "fileutils_mac.h"
#endif

namespace Utils {

static DeviceFileHooks s_deviceHooks;
static bool removeRecursivelyLocal(const FilePath &filePath, QString *error);

/*! \class Utils::FilePath

    \brief The FilePath class is an abstraction for handles to objects
    in a (possibly remote) file system, similar to a URL or, in the local
    case, a path to a file or directory.

    Ideally, all of \QC code should use FilePath for this purpose,
    but for historic reasons there are still large parts using QString.

    FilePaths are internally stored as triple of strings, with one
    part ("scheme") identifying an access method, a second part ("host")
    a file system (e.g. a host) and a third part ("path") identifying
    a (potential) object on the systems.

    FilePath follows the Unix paradigm of "everything is a file":
    There is no conceptional difference between FilePaths referring
    to plain files or directories.

    A FilePath is implicitly associated with an operating system via its
    host part. The path part of a FilePath is internally stored
    with forward slashes, independent of the associated OS.

    The path parts of FilePaths associated with Windows (and macOS,
    unless selected otherwise in the settings) are compared case-insensitively
    to each other.
    Note that comparisons for equivalence generally need out-of-band
    knowledge, as there may be multiple FilePath representations for
    the same file (e.g. different access methods may lead to the same
    file).

    There are several conversions between FilePath and other string-like
    representations:

    \list

    \li FilePath::fromUserInput()

        Convert string-like data from sources originating outside of
        \QC, e.g. from human input in GUI controls, from environment
        variables and from command-line parameters to \QC.

        The input can contain both slashes and backslashes and will
        be parsed and normalized.

    \li FilePath::nativePath()

        Converts the FilePath to the slash convention of the associated
        OS and drops the scheme and host parts.

        This is useful to interact with the facilities of the associated
        OS, e.g. when passing this FilePath as an argument to a command
        executed on the associated OS.

        \note The FilePath passed as executable to a CommandLine is typically
        not touched by user code. QtcProcess will use it to determine
        the remote system and apply the necessary conversions internally.

    \li FilePath::toUserOutput()

        Converts the FilePath to the slash convention of the associated
        OS and retains the scheme and host parts.

        This is rarely useful for remote paths as there is practically
        no consumer of this style.

    \li FilePath::displayName()

        Converts the FilePath to the slash convention of the associated
        OS and adds the scheme and host as a " on <device>" suffix.

        This is useful for static user-facing output in he GUI

    \li FilePath::fromVariant(), FilePath::toVariant()

        These are used to interface QVariant-based API, e.g.
        settings or item model (internal) data.

    \li FilePath::fromString(), FilePath::toString()

        These are used for internal interfaces to code areas that
        still use QString based file paths.

    \endlist

    Conversion of string-like data should always happen at the outer boundary
    of \QC code, using \c fromUserInput() for in-bound communication,
    and depending on the medium \c nativePath() or \c displayName() for out-bound
    communication.

    Communication with QVariant based Qt API should use \c fromVariant() and
    \c toVariant().

    Uses of \c fromString() and \c toString() should be phased out by transforming
    code from QString based file path to FilePath. An exception here are
    fragments of paths of a FilePath that are later used with \c pathAppended()
    or similar which should be kept as QString.
*/

FilePath::FilePath()
{
}

/// Constructs a FilePath from \a info
FilePath FilePath::fromFileInfo(const QFileInfo &info)
{
    return FilePath::fromString(info.absoluteFilePath());
}

/// \returns a QFileInfo
QFileInfo FilePath::toFileInfo() const
{
    QTC_ASSERT(!needsDevice(), return QFileInfo());
    return QFileInfo(cleanPath().path());
}

FilePath FilePath::fromUrl(const QUrl &url)
{
    FilePath fn;
    fn.m_scheme = url.scheme();
    fn.m_host = url.host();
    fn.setRootAndPath(url.path(), HostOsInfo::hostOs());
    return fn;
}

FilePath FilePath::currentWorkingPath()
{
    return FilePath::fromString(QDir::currentPath());
}

FilePath FilePath::rootPath()
{
    return FilePath::fromString(QDir::rootPath());
}

static QString hostEncoded(QString host)
{
    host.replace('%', "%25");
    host.replace('/', "%2f");
    return host;
}

/// \returns a QString for passing on to QString based APIs
QString FilePath::toString() const
{
    if (m_scheme.isEmpty())
        return m_root + m_path;

    if (!m_root.isEmpty())
        return specialPath(SpecialPathComponent::RootPath) + "/" + m_scheme + "/" + hostEncoded(m_host) + m_root + m_path;
    return specialPath(SpecialPathComponent::RootPath) + "/" + m_scheme + "/" + hostEncoded(m_host) + "/./" + m_path;
}

QString FilePath::toFSPathString() const
{
    // TODO ...
    return toString();
}

QUrl FilePath::toUrl() const
{
    QUrl url;
    url.setScheme(m_scheme);
    url.setHost(m_host);
    url.setPath(m_root + m_path);
    return url;
}

/// \returns a QString to display to the user, including the device prefix
/// Converts the separators to the native format of the system
/// this path belongs to.
QString FilePath::toUserOutput() const
{
    if (needsDevice()) {
        if (!m_root.isEmpty())
            return m_scheme + "://" + hostEncoded(m_host) + path();
        return m_scheme + "://" + hostEncoded(m_host) + "/./" + path();
    }

    QString tmp = toString();
    if (osType() == OsTypeWindows)
        tmp.replace('/', '\\');
    return tmp;
}

/// \returns a QString to pass to target system native commands, without the device prefix.
/// Converts the separators to the native format of the system
/// this path belongs to.
QString FilePath::nativePath() const
{
    QString data = path();
    if (osType() == OsTypeWindows)
        data.replace('/', '\\');
    return data;
}

QString FilePath::fileName() const
{
    const QChar slash = QLatin1Char('/');
    return m_path.mid(m_path.lastIndexOf(slash) + 1);
}

QString FilePath::fileNameWithPathComponents(int pathComponents) const
{
    QString fullPath = path();

    if (pathComponents < 0)
        return fullPath;
    const QChar slash = QLatin1Char('/');
    int i = fullPath.lastIndexOf(slash);
    if (pathComponents == 0 || i == -1)
        return fullPath.mid(i + 1);
    int component = i + 1;
    // skip adjacent slashes
    while (i > 0 && fullPath.at(--i) == slash)
        ;
    while (i >= 0 && --pathComponents >= 0) {
        i = fullPath.lastIndexOf(slash, i);
        component = i + 1;
        while (i > 0 && fullPath.at(--i) == slash)
            ;
    }

    if (i > 0 && fullPath.lastIndexOf(slash, i) != -1)
        return fullPath.mid(component);

    // If there are no more slashes before the found one, return the entire string
    return displayName();
}

/// \returns the base name of the file without the path.
///
/// The base name consists of all characters in the file up to
/// (but not including) the first '.' character.

QString FilePath::baseName() const
{
    const QString &name = fileName();
    return name.left(name.indexOf('.'));
}

/// \returns the complete base name of the file without the path.
///
/// The complete base name consists of all characters in the file up to
/// (but not including) the last '.' character. In case of ".ui.qml"
/// it will be treated as one suffix.

QString FilePath::completeBaseName() const
{
    const QString &name = fileName();
    if (name.endsWith(".ui.qml"))
        return name.left(name.length() - QString(".ui.qml").length());
    return name.left(name.lastIndexOf('.'));
}

/// \returns the suffix (extension) of the file.
///
/// The suffix consists of all characters in the file after
/// (but not including) the last '.'. In case of ".ui.qml" it will
/// be treated as one suffix.

QString FilePath::suffix() const
{
    const QString &name = fileName();
    if (name.endsWith(".ui.qml"))
        return "ui.qml";
    const int index = name.lastIndexOf('.');
    if (index >= 0)
        return name.mid(index + 1);
    return {};
}

/// \returns the complete suffix (extension) of the file.
///
/// The complete suffix consists of all characters in the file after
/// (but not including) the first '.'.

QString FilePath::completeSuffix() const
{
    const QString &name = fileName();
    const int index = name.indexOf('.');
    if (index >= 0)
        return name.mid(index + 1);
    return {};
}

void FilePath::setScheme(const QString &scheme)
{
    QTC_CHECK(!scheme.contains('/'));
    m_scheme = scheme;
}

void FilePath::setHost(const QString &host)
{
    m_host = host;
}


/// \returns a bool indicating whether a file with this
/// FilePath exists.
bool FilePath::exists() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.exists, return false);
        return s_deviceHooks.exists(*this);
    }
    return !isEmpty() && QFileInfo::exists(path());
}

/// \returns a bool indicating whether a path is writable.
bool FilePath::isWritableDir() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.isWritableDir, return false);
        return s_deviceHooks.isWritableDir(*this);
    }
    const QFileInfo fi{path()};
    return exists() && fi.isDir() && fi.isWritable();
}

bool FilePath::isWritableFile() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.isWritableFile, return false);
        return s_deviceHooks.isWritableFile(*this);
    }
    const QFileInfo fi{path()};
    return fi.isWritable() && !fi.isDir();
}

bool FilePath::ensureWritableDir() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.ensureWritableDir, return false);
        return s_deviceHooks.ensureWritableDir(*this);
    }
    const QFileInfo fi{path()};
    if (fi.isDir() && fi.isWritable())
        return true;
    return QDir().mkpath(path());
}

bool FilePath::ensureExistingFile() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.ensureExistingFile, return false);
        return s_deviceHooks.ensureExistingFile(*this);
    }
    QFile f(path());
    if (f.exists())
        return true;
    f.open(QFile::WriteOnly);
    f.close();
    return f.exists();
}

bool FilePath::isExecutableFile() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.isExecutableFile, return false);
        return s_deviceHooks.isExecutableFile(*this);
    }
    const QFileInfo fi{path()};
    return fi.isExecutable() && !fi.isDir();
}

bool FilePath::isReadableFile() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.isReadableFile, return false);
        return s_deviceHooks.isReadableFile(*this);
    }
    const QFileInfo fi{path()};
    return fi.isReadable() && !fi.isDir();
}

bool FilePath::isReadableDir() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.isReadableDir, return false);
        return s_deviceHooks.isReadableDir(*this);
    }
    const QFileInfo fi{path()};
    return fi.isReadable() && fi.isDir();
}

bool FilePath::isFile() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.isFile, return false);
        return s_deviceHooks.isFile(*this);
    }
    const QFileInfo fi{path()};
    return fi.isFile();
}

bool FilePath::isDir() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.isDir, return false);
        return s_deviceHooks.isDir(*this);
    }
    const QFileInfo fi{path()};
    return fi.isDir();
}

bool FilePath::createDir() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.createDir, return false);
        return s_deviceHooks.createDir(*this);
    }
    QDir dir(path());
    return dir.mkpath(dir.absolutePath());
}

FilePaths FilePath::dirEntries(const FileFilter &filter, QDir::SortFlags sort) const
{
    FilePaths result;

    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.iterateDirectory, return {});
        const auto callBack = [&result](const FilePath &path) { result.append(path); return true; };
        s_deviceHooks.iterateDirectory(*this, callBack, filter);
    } else {
        QDirIterator dit(path(), filter.nameFilters, filter.fileFilters, filter.iteratorFlags);
        while (dit.hasNext())
            result.append(FilePath::fromString(dit.next()));
    }

    // FIXME: Not all flags supported here.

    const QDir::SortFlags sortBy = (sort & QDir::SortByMask);
    if (sortBy == QDir::Name) {
        Utils::sort(result);
    } else if (sortBy == QDir::Time) {
        Utils::sort(result, [](const FilePath &path1, const FilePath &path2) {
            return path1.lastModified() < path2.lastModified();
        });
    }

    if (sort & QDir::Reversed)
        std::reverse(result.begin(), result.end());

    return result;
}

FilePaths FilePath::dirEntries(QDir::Filters filters) const
{
    return dirEntries(FileFilter({}, filters));
}

// This runs \a callBack on each directory entry matching all \a filters and
// either of the specified \a nameFilters.
// An empty \nameFilters list matches every name.

void FilePath::iterateDirectory(const std::function<bool(const FilePath &item)> &callBack,
                                const FileFilter &filter) const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.iterateDirectory, return);
        s_deviceHooks.iterateDirectory(*this, callBack, filter);
        return;
    }

    QDirIterator it(path(), filter.nameFilters, filter.fileFilters, filter.iteratorFlags);
    while (it.hasNext()) {
        if (!callBack(FilePath::fromString(it.next())))
            return;
    }
}

void FilePath::iterateDirectories(const FilePaths &dirs,
                                  const std::function<bool(const FilePath &)> &callBack,
                                  const FileFilter &filter)
{
    for (const FilePath &dir : dirs)
        dir.iterateDirectory(callBack, filter);
}

QByteArray FilePath::fileContents(qint64 maxSize, qint64 offset) const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.fileContents, return {});
        return s_deviceHooks.fileContents(*this, maxSize, offset);
    }

    const QString path = toString();
    QFile f(path);
    if (!f.exists())
        return {};

    if (!f.open(QFile::ReadOnly))
        return {};

    if (offset != 0)
        f.seek(offset);

    if (maxSize != -1)
        return f.read(maxSize);

    return f.readAll();
}

void FilePath::asyncFileContents(const Continuation<const QByteArray &> &cont,
                                 qint64 maxSize, qint64 offset) const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.asyncFileContents, return);
        return s_deviceHooks.asyncFileContents(cont, *this, maxSize, offset);
    }

    cont(fileContents(maxSize, offset));
}

bool FilePath::writeFileContents(const QByteArray &data) const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.writeFileContents, return {});
        return s_deviceHooks.writeFileContents(*this, data);
    }

    QFile file(path());
    QTC_ASSERT(file.open(QFile::WriteOnly | QFile::Truncate), return false);
    qint64 res = file.write(data);
    return res == data.size();
}

void FilePath::asyncWriteFileContents(const Continuation<bool> &cont, const QByteArray &data) const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.asyncWriteFileContents, return);
        s_deviceHooks.asyncWriteFileContents(cont, *this, data);
        return;
    }

    cont(writeFileContents(data));
}

bool FilePath::needsDevice() const
{
    return !m_scheme.isEmpty();
}

/// \returns an empty FilePath if this is not a symbolic linl
FilePath FilePath::symLinkTarget() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.symLinkTarget, return {});
        return s_deviceHooks.symLinkTarget(*this);
    }
    const QFileInfo info(path());
    if (!info.isSymLink())
        return {};
    return FilePath::fromString(info.symLinkTarget());
}

QString FilePath::mapToDevicePath() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.mapToDevicePath, return path());
        return s_deviceHooks.mapToDevicePath(*this);
    }
    return path();
}

FilePath FilePath::withExecutableSuffix() const
{
    FilePath res = *this;
    res.setPath(OsSpecificAspects::withExecutableSuffix(osType(), path()));
    return res;
}

/// Find the parent directory of a given directory.

/// Returns an empty FilePath if the current directory is already
/// a root level directory.

/// \returns \a FilePath with the last segment removed.
FilePath FilePath::parentDir() const
{
    const QString basePath = path();
    if (basePath.isEmpty())
        return FilePath();

    // TODO: Replace usage of QDir !!
    const QDir base(basePath);
    if (base.isRoot())
        return FilePath();

    const QString path = basePath + QLatin1String("/..");
    const QString parent = doCleanPath(path);
    QTC_ASSERT(parent != path, return FilePath());

    FilePath result = *this;
    result.setPath(parent);
    return result;
}

FilePath FilePath::absolutePath() const
{
    const FilePath parentPath = isAbsolutePath() ? parentDir() : FilePath::currentWorkingPath().resolvePath(*this).parentDir();
    return parentPath.isEmpty() ? *this : parentPath;
}

FilePath FilePath::absoluteFilePath() const
{
    if (isAbsolutePath())
        return *this;

    return FilePath::currentWorkingPath().resolvePath(*this);
}

QString FilePath::specialPath(SpecialPathComponent component)
{
    switch (component) {
    case SpecialPathComponent::RootName:
        return QLatin1String("__qtc_devices__");
    case SpecialPathComponent::RootPath:
        return (QDir::rootPath() + "__qtc_devices__");
    case SpecialPathComponent::DeviceRootName:
        return QLatin1String("device");
    case SpecialPathComponent::DeviceRootPath:
        return QDir::rootPath() + "__qtc_devices__/device";
    }

    QTC_ASSERT(false, return {});
}

FilePath FilePath::specialFilePath(SpecialPathComponent component)
{
    return FilePath::fromString(specialPath(component));
}

FilePath FilePath::normalizedPathName() const
{
    FilePath result = *this;
    if (!needsDevice()) // FIXME: Assumes no remote Windows and Mac for now.
        result.m_path = FileUtils::normalizedPathName(result.m_path);
    return result;
}

QString FilePath::displayName(const QString &args) const
{
    QString deviceName;
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.deviceDisplayName, return path());
        deviceName = s_deviceHooks.deviceDisplayName(*this);
    }

    const QString fullPath = path();

    if (args.isEmpty()) {
        if (deviceName.isEmpty())
            return fullPath;

        return QCoreApplication::translate("Utils::FileUtils", "%1 on %2", "File on device")
                .arg(fullPath, deviceName);
    }

    if (deviceName.isEmpty())
        return fullPath + ' ' + args;

    return QCoreApplication::translate("Utils::FileUtils", "%1 %2 on %3", "File and args on device")
            .arg(fullPath, args, deviceName);
}

/*!
   \fn FilePath FilePath::fromString(const QString &filepath)

   Constructs a FilePath from \a filepath

   \a filepath is not checked for validity. It can be given in the following forms:

   \list
   \li  /some/absolute/local/path
   \li  some/relative/path
   \li  scheme://host/absolute/path
   \li  scheme://host/./relative/path    \note the ./ is verbatim part of the path
   \endlist

   Some decoding happens when parsing the \a filepath
   A sequence %25 present in the host part is replaced by % in the host name,
   a sequence %2f present in the host part is replaced by / in the host name.

   The path part might consist of several parts separated by /, independent
   of the platform or file system.

   To create FilePath objects from strings possibly containing backslashes as
   path separator, use \c fromUserInput.

   \sa toString, fromUserInput
*/
FilePath FilePath::fromString(const QString &filepath, OsType osType)
{
    FilePath fn;
    fn.setFromString(filepath, osType);
    return fn;
}

bool isWindowsDriveLetter(QChar ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

using RootAndPath = QPair<QStringView, QStringView>;

optional<RootAndPath> windowsRootAndPath(const QStringView path)
{
    const QChar slash('/');
    QStringView workPath = path;
    if (workPath.size() < 2)
        return {};
    if (workPath.startsWith(slash))
        workPath = workPath.mid(1);

    if (workPath.startsWith(slash)) {
        // Its a UNC path ( //server/share/path )
        workPath = workPath.mid(1);
        const auto firstSlash = workPath.indexOf(slash);

        // If the first slash is not found, we either received "//" or "//server"
        // If we only received "//" we return an empty root name, otherwise
        // we return "//server" as root name.
        if (firstSlash == -1)
            return RootAndPath{path.length() > 2 ? path : workPath, {}};

        workPath = workPath.mid(firstSlash + 1);

        const auto secondSlash = workPath.indexOf(slash);
        // If the second slash is not found, we either received "//server/" or "//server/share".
        // In the first case we return "//server" as root name, in the second case we return "//server/share" as root name.
        if (secondSlash == -1)
            return RootAndPath{path.mid(0, workPath.length() > 0 ? path.length() : (firstSlash + 2)),
                               {}};

        workPath = workPath.mid(secondSlash + 1);

        return RootAndPath{path.mid(0, firstSlash + secondSlash + 3), workPath};
    }

    if (workPath[1] == ':' && isWindowsDriveLetter(workPath[0]))
        return RootAndPath{workPath.mid(0, 2), workPath.mid(3)};
    return {};
}

void FilePath::setRootAndPath(QStringView path, OsType osType)
{
    optional<RootAndPath> windowsDriveAndPath = osType == OsType::OsTypeWindows ? windowsRootAndPath(path) : nullopt;

    if (path.startsWith(QLatin1String("/./"))) {
        m_root = "";
        path = path.mid(3);
        m_path = path.toString();
    } else if (windowsDriveAndPath ) {
        if (windowsDriveAndPath->first.isEmpty())
            m_root.clear();
        else
            m_root = windowsDriveAndPath->first.toString() + '/';

        m_path = windowsDriveAndPath->second.toString();
    } else if (path.startsWith('/')) {
        m_root = "/";
        m_path = path.mid(1).toString();
    } else if (path.startsWith(':')) {
        if (path.length() > 1 && path[1] == '/') {
            m_root = ":/";
            m_path = path.mid(2).toString();
        } else {
            m_root = ":";
            m_path = path.mid(1).toString();
        }
    } else {
        m_root.clear();
        m_path = path.toString();
    }
}

void FilePath::setFromString(const QString &filename, OsType osType)
{
    static const QLatin1String qtcDevSlash("__qtc_devices__/");
    static const QLatin1String colonSlashSlash("://");
    static const QString rootPath = QDir::rootPath();

    const QChar slash('/');
    const QStringView fileNameView(filename);

    if (fileNameView.startsWith(rootPath, Qt::CaseInsensitive)) { // Absolute path ...
        const QStringView withoutRootPath = fileNameView.mid(rootPath.size());
        if (withoutRootPath.startsWith(qtcDevSlash)) { // Starts with "/__qtc_devices__/" ...
            const QStringView withoutQtcDeviceRoot = withoutRootPath.mid(qtcDevSlash.size());

            const auto firstSlash = withoutQtcDeviceRoot.indexOf(slash);

            if (firstSlash != -1) {
                m_scheme = withoutQtcDeviceRoot.left(firstSlash).toString();
                const auto secondSlash = withoutQtcDeviceRoot.indexOf(slash, firstSlash + 1);
                m_host = withoutQtcDeviceRoot.mid(firstSlash + 1, secondSlash - firstSlash - 1)
                             .toString();
                if (secondSlash != -1) {
                    QStringView path = withoutQtcDeviceRoot.mid(secondSlash);
                    setRootAndPath(path, osType);
                    return;
                }

                m_path.clear();
                m_root = slash;
                return;
            }

            m_scheme.clear();
            m_host.clear();
            m_path = filename;
            return;
        }
    }

    const auto firstSlash = filename.indexOf(slash);
    const auto schemeEnd = filename.indexOf(colonSlashSlash);
    if (schemeEnd != -1 && schemeEnd < firstSlash) {
        // This is a pseudo Url, we can't use QUrl here sadly.
        m_scheme = filename.left(schemeEnd);
        const auto hostEnd = filename.indexOf(slash, schemeEnd + 3);
        m_host = filename.mid(schemeEnd + 3, hostEnd - schemeEnd - 3);
        setRootAndPath(QStringView(filename).mid(hostEnd), osType);
        return;
    }

    setRootAndPath(filename, osType);
    return;
}

/// Constructs a FilePath from \a filePath. The \a defaultExtension is appended
/// to \a filename if that does not have an extension already.
/// \a filePath is not checked for validity.
FilePath FilePath::fromStringWithExtension(const QString &filepath, const QString &defaultExtension)
{
    if (filepath.isEmpty() || defaultExtension.isEmpty())
        return FilePath::fromString(filepath);

    FilePath rc = FilePath::fromString(filepath);
    // Add extension unless user specified something else
    const QChar dot = QLatin1Char('.');
    if (!rc.fileName().contains(dot)) {
        if (!defaultExtension.startsWith(dot))
            rc = rc.stringAppended(dot);
        rc = rc.stringAppended(defaultExtension);
    }
    return rc;
}

/// Constructs a FilePath from \a filePath
/// \a filePath is only passed through QDir::fromNativeSeparators
FilePath FilePath::fromUserInput(const QString &filePath)
{
    QString clean = doCleanPath(filePath);
    if (clean.startsWith(QLatin1String("~/")))
        return FileUtils::homePath().pathAppended(clean.mid(2));
    return FilePath::fromString(clean);
}

/// Constructs a FilePath from \a filePath, which is encoded as UTF-8.
/// \a filePath is not checked for validity.
FilePath FilePath::fromUtf8(const char *filename, int filenameSize)
{
    return FilePath::fromString(QString::fromUtf8(filename, filenameSize));
}

FilePath FilePath::fromVariant(const QVariant &variant)
{
    if (variant.type() == QVariant::Url)
        return FilePath::fromUrl(variant.toUrl());
    return FilePath::fromString(variant.toString());
}

QVariant FilePath::toVariant() const
{
    return toString();
}

bool FilePath::operator==(const FilePath &other) const
{
    return QString::compare(m_root, other.m_root, caseSensitivity()) == 0
        && QString::compare(m_path, other.m_path, caseSensitivity()) == 0
        && m_host == other.m_host
        && m_scheme == other.m_scheme;
}

bool FilePath::operator!=(const FilePath &other) const
{
    return !(*this == other);
}

bool FilePath::operator<(const FilePath &other) const
{
    const int rootCmp = QString::compare(m_root, other.m_root, caseSensitivity());
    const int cmp = rootCmp == 0 ? QString::compare(m_path, other.m_path, caseSensitivity()) : rootCmp;
    if (cmp != 0)
        return cmp < 0;
    if (m_host != other.m_host)
        return m_host < other.m_host;
    return m_scheme < other.m_scheme;
}

bool FilePath::operator<=(const FilePath &other) const
{
    return !(other < *this);
}

bool FilePath::operator>(const FilePath &other) const
{
    return other < *this;
}

bool FilePath::operator>=(const FilePath &other) const
{
    return !(*this < other);
}

FilePath FilePath::operator+(const QString &s) const
{
    return pathAppended(s);
}

/// \returns whether FilePath is a child of \a s
bool FilePath::isChildOf(const FilePath &s) const
{
    if (s.isEmpty())
        return false;
    if (s.m_root != m_root)
        return false;
    if (!m_path.startsWith(s.m_path, caseSensitivity()))
        return false;
    if (m_path.size() <= s.m_path.size())
        return false;
    // s is root, '/' was already tested in startsWith
    if (s.m_path.endsWith(QLatin1Char('/')))
        return true;
    // s is a directory, next character should be '/' (/tmpdir is NOT a child of /tmp)
    return s.m_path.isEmpty() || m_path.at(s.m_path.size()) == QLatin1Char('/');
}

/// \returns whether path() startsWith \a s
bool FilePath::startsWith(const QString &s) const
{
    return path().startsWith(s, caseSensitivity());
}

/*!
* \param s The string to check for at the end of the path.
* \returns whether FilePath endsWith \a s
*/
bool FilePath::endsWith(const QString &s) const
{
    return path().endsWith(s, caseSensitivity());
}

/*!
* \brief Checks whether the FilePath starts with a drive letter.
* Defaults to \c false if it is a non-Windows host or represents a path on device
* \returns whether FilePath starts with a drive letter
*/
bool FilePath::startsWithDriveLetter() const
{
    if (needsDevice() || !HostOsInfo::isWindowsHost())
        return false;
    return m_root.length() >= 2 && m_root.at(0).isLetter() && m_root.at(1) == ':';
}

/*!
* \brief Relative path from \a parent to this.
* Returns a empty FilePath if this is not a child of \p parent.
* That is, this never returns a path starting with "../"
* \param parent The Parent to calculate the relative path to.
* \returns The relative path of this to \p parent if this is a child of \p parent.
*/
FilePath FilePath::relativeChildPath(const FilePath &parent) const
{
    FilePath res;
    if (isChildOf(parent)) {
        res.m_scheme = m_scheme;
        res.m_host = m_host;
        res.m_path = m_path.mid(parent.m_path.size());
        if (res.m_path.startsWith('/'))
            res.m_path = res.m_path.mid(1);
    }
    return res;
}

/// \returns the relativePath of FilePath to given \a anchor.
/// Both, FilePath and anchor may be files or directories.
/// Example usage:
///
/// \code
///     FilePath filePath("/foo/b/ar/file.txt");
///     FilePath relativePath = filePath.relativePath("/foo/c");
///     qDebug() << relativePath
/// \endcode
///
/// The debug output will be "../b/ar/file.txt".
///
FilePath FilePath::relativePath(const FilePath &anchor) const
{
    QTC_ASSERT(!needsDevice(), return *this);

    const QFileInfo fileInfo(toString());
    QString absolutePath;
    QString filename;
    if (fileInfo.isFile()) {
        absolutePath = fileInfo.absolutePath();
        filename = fileInfo.fileName();
    } else if (fileInfo.isDir()) {
        absolutePath = fileInfo.absoluteFilePath();
    } else {
        return {};
    }
    const QFileInfo anchorInfo(anchor.toString());
    QString absoluteAnchorPath;
    if (anchorInfo.isFile())
        absoluteAnchorPath = anchorInfo.absolutePath();
    else if (anchorInfo.isDir())
        absoluteAnchorPath = anchorInfo.absoluteFilePath();
    else
        return {};
    QString relativeFilePath = calcRelativePath(absolutePath, absoluteAnchorPath);
    if (!filename.isEmpty()) {
        if (relativeFilePath == ".")
            relativeFilePath.clear();
        if (!relativeFilePath.isEmpty())
            relativeFilePath += '/';
        relativeFilePath += filename;
    }
    return FilePath::fromString(relativeFilePath);
}

/// \returns the relativePath of \a absolutePath to given \a absoluteAnchorPath.
/// Both paths must be an absolute path to a directory. Example usage:
///
/// \code
///     qDebug() << FilePath::calcRelativePath("/foo/b/ar", "/foo/c");
/// \endcode
///
/// The debug output will be "../b/ar".
///
/// \see FilePath::relativePath
///
QString FilePath::calcRelativePath(const QString &absolutePath, const QString &absoluteAnchorPath)
{
    if (absolutePath.isEmpty() || absoluteAnchorPath.isEmpty())
        return QString();
    // TODO using split() instead of parsing the strings by char index is slow
    // and needs more memory (but the easiest implementation for now)
    const QStringList splits1 = absolutePath.split('/');
    const QStringList splits2 = absoluteAnchorPath.split('/');
    int i = 0;
    while (i < splits1.count() && i < splits2.count() && splits1.at(i) == splits2.at(i))
        ++i;
    QString relativePath;
    int j = i;
    bool addslash = false;
    while (j < splits2.count()) {
        if (!splits2.at(j).isEmpty()) {
            if (addslash)
                relativePath += '/';
            relativePath += "..";
            addslash = true;
        }
        ++j;
    }
    while (i < splits1.count()) {
        if (!splits1.at(i).isEmpty()) {
            if (addslash)
                relativePath += '/';
            relativePath += splits1.at(i);
            addslash = true;
        }
        ++i;
    }
    if (relativePath.isEmpty())
        return QString(".");
    return relativePath;
}

/*!
 * \brief Returns a path corresponding to the current object on the
 * same device as \a deviceTemplate. The FilePath needs to be local.
 *
 * Example usage:
 * \code
 *     localDir = FilePath("/tmp/workingdir");
 *     executable = FilePath::fromUrl("docker://123/bin/ls")
 *     realDir = localDir.onDevice(executable)
 *     assert(realDir == FilePath::fromUrl("docker://123/tmp/workingdir"))
 * \endcode
 *
 * \param deviceTemplate A path from which the host and scheme is taken.
 *
 * \returns A path on the same device as \a deviceTemplate.
*/
FilePath FilePath::onDevice(const FilePath &deviceTemplate) const
{
    const bool sameDevice = m_scheme == deviceTemplate.m_scheme && m_host == deviceTemplate.m_host;
    if (sameDevice)
        return *this;
    // TODO: converting paths between different non local devices is still unsupported
    QTC_CHECK(!needsDevice());
    FilePath res;
    res.m_scheme = deviceTemplate.m_scheme;
    res.m_host = deviceTemplate.m_host;
    res.m_root = m_root;
    res.m_path = m_path;
    res.setRootAndPath(res.mapToDevicePath(), res.osType());
    return res;
}

/*!
    Returns a FilePath with local path \a newPath on the same device
    as the current object.

    Example usage:
    \code
        devicePath = FilePath("docker://123/tmp");
        newPath = devicePath.withNewPath("/bin/ls");
        assert(realDir == FilePath::fromUrl("docker://123/bin/ls"))
    \endcode
*/
FilePath FilePath::withNewPath(const QString &newPath) const
{
    FilePath res;
    res.setRootAndPath(newPath, osType());
    res.m_host = m_host;
    res.m_scheme = m_scheme;
    return res;
}

/*!
    Searched a binary corresponding to this object in the PATH of
    the device implied by this object's scheme and host.

    Example usage:
    \code
        binary = FilePath::fromUrl("docker://123/./make);
        fullPath = binary.searchInDirectories(binary.deviceEnvironment().path());
        assert(fullPath == FilePath::fromUrl("docker://123/usr/bin/make"))
    \endcode
*/
FilePath FilePath::searchInDirectories(const FilePaths &dirs) const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.searchInPath, return {});
        return s_deviceHooks.searchInPath(*this, dirs);
    }
    return Environment::systemEnvironment().searchInDirectories(path(), dirs);
}

FilePath FilePath::searchInPath(const FilePaths &additionalDirs, PathAmending amending) const
{
    FilePaths directories = deviceEnvironment().path();
    if (!additionalDirs.isEmpty()) {
        if (amending == AppendToPath)
            directories.append(additionalDirs);
        else
            directories = additionalDirs + directories;
    }
    return searchInDirectories(directories);
}

Environment FilePath::deviceEnvironment() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.environment, return {});
        return s_deviceHooks.environment(*this);
    }
    return Environment::systemEnvironment();
}

QString FilePath::formatFilePaths(const FilePaths &files, const QString &separator)
{
    const QStringList nativeFiles = transform(files, &FilePath::toUserOutput);
    return nativeFiles.join(separator);
}

void FilePath::removeDuplicates(FilePaths &files)
{
    // FIXME: Improve.
    // FIXME: This drops the osType information, which is not correct.
    QStringList list = transform<QStringList>(files, &FilePath::toString);
    list.removeDuplicates();
    files = FileUtils::toFilePathList(list);
}

void FilePath::sort(FilePaths &files)
{
    // FIXME: Improve.
    // FIXME: This drops the osType information, which is not correct.
    QStringList list = transform<QStringList>(files, &FilePath::toString);
    list.sort();
    files = FileUtils::toFilePathList(list);
}

void join(QString &left, const QString &right)
{
    QStringView r(right);
    if (r.startsWith('/'))
        r = r.mid(1);

    if (left.isEmpty() || left.endsWith('/'))
        left += r;
    else
        left += '/' + r;
}

FilePath FilePath::pathAppended(const QString &path) const
{
    if (path.isEmpty())
        return *this;

    FilePath other = FilePath::fromString(path);

    if (isEmpty()) {
        return other;
    }
    FilePath fn = *this;
    join(fn.m_path, other.path());
    return fn;
}

FilePath FilePath::stringAppended(const QString &str) const
{
    FilePath fn = *this;
    fn.m_path.append(str);
    return fn;
}

size_t FilePath::hash(uint seed) const
{
    if (HostOsInfo::fileNameCaseSensitivity() == Qt::CaseInsensitive)
        return qHash(path().toCaseFolded(), seed);
    return qHash(path(), seed);
}

QDateTime FilePath::lastModified() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.lastModified, return {});
        return s_deviceHooks.lastModified(*this);
    }
    return toFileInfo().lastModified();
}

QFile::Permissions FilePath::permissions() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.permissions, return {});
        return s_deviceHooks.permissions(*this);
    }
    return toFileInfo().permissions();
}

bool FilePath::setPermissions(QFile::Permissions permissions) const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.setPermissions, return false);
        return s_deviceHooks.setPermissions(*this, permissions);
    }
    return QFile(path()).setPermissions(permissions);
}

OsType FilePath::osType() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.osType, return OsType::OsTypeLinux);
        return s_deviceHooks.osType(*this);
    }
    return HostOsInfo::hostOs();
}

bool FilePath::removeFile() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.removeFile, return false);
        return s_deviceHooks.removeFile(*this);
    }
    return QFile::remove(path());
}

/*!
  Removes the directory this filePath refers too and its subdirectories recursively.

  \note The \a error parameter is optional.

  Returns whether the operation succeeded.
*/
bool FilePath::removeRecursively(QString *error) const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.removeRecursively, return false);
        return s_deviceHooks.removeRecursively(*this);
    }
    return removeRecursivelyLocal(*this, error);
}

bool FilePath::copyFile(const FilePath &target) const
{
    if (host() != target.host()) {
        // FIXME: This does not scale.
        const QByteArray ba = fileContents();
        return target.writeFileContents(ba);
    }
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.copyFile, return false);
        return s_deviceHooks.copyFile(*this, target);
    }
    return QFile::copy(path(), target.path());
}

void FilePath::asyncCopyFile(const std::function<void(bool)> &cont, const FilePath &target) const
{
    if (host() != target.host()) {
        asyncFileContents([cont, target](const QByteArray &ba) {
            target.asyncWriteFileContents(cont, ba);
        });
    } else if (needsDevice()) {
        s_deviceHooks.asyncCopyFile(cont, *this, target);
    } else {
        cont(copyFile(target));
    }
}

bool FilePath::renameFile(const FilePath &target) const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.renameFile, return false);
        return s_deviceHooks.renameFile(*this, target);
    }
    return QFile::rename(path(), target.path());
}

qint64 FilePath::fileSize() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.fileSize, return false);
        return s_deviceHooks.fileSize(*this);
    }
    return QFileInfo(path()).size();
}

qint64 FilePath::bytesAvailable() const
{
    if (needsDevice()) {
        QTC_ASSERT(s_deviceHooks.bytesAvailable, return false);
        return s_deviceHooks.bytesAvailable(*this);
    }
    return QStorageInfo(path()).bytesAvailable();
}

static bool removeRecursivelyLocal(const FilePath &filePath, QString *error)
{
    QTC_ASSERT(!filePath.needsDevice(), return false);
    QFileInfo fileInfo = filePath.toFileInfo();
    if (!fileInfo.exists() && !fileInfo.isSymLink())
        return true;

    QFile::setPermissions(fileInfo.absoluteFilePath(), fileInfo.permissions() | QFile::WriteUser);

    if (fileInfo.isDir()) {
        QDir dir(fileInfo.absoluteFilePath());
        dir.setPath(dir.canonicalPath());
        if (dir.isRoot()) {
            if (error) {
                *error = QCoreApplication::translate("Utils::FileUtils",
                    "Refusing to remove root directory.");
            }
            return false;
        }
        if (dir.path() == QDir::home().canonicalPath()) {
            if (error) {
                *error = QCoreApplication::translate("Utils::FileUtils",
                    "Refusing to remove your home directory.");
            }
            return false;
        }

        const QStringList fileNames = dir.entryList(
                    QDir::Files | QDir::Hidden | QDir::System | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &fileName : fileNames) {
            if (!removeRecursivelyLocal(filePath / fileName, error))
                return false;
        }
        if (!QDir::root().rmdir(dir.path())) {
            if (error) {
                *error = QCoreApplication::translate("Utils::FileUtils", "Failed to remove directory \"%1\".")
                        .arg(filePath.toUserOutput());
            }
            return false;
        }
    } else {
        if (!QFile::remove(filePath.toString())) {
            if (error) {
                *error = QCoreApplication::translate("Utils::FileUtils", "Failed to remove file \"%1\".")
                        .arg(filePath.toUserOutput());
            }
            return false;
        }
    }
    return true;
}

/*!
 * \brief Checks if this is newer than \p timeStamp
 * \param timeStamp The time stamp to compare with
 * \returns true if this is newer than \p timeStamp.
 *  If this is a directory, the function will recursively check all files and return
 *  true if one of them is newer than \a timeStamp. If this is a single file, true will
 *  be returned if the file is newer than \a timeStamp.
 *
 *  Returns whether at least one file in \a filePath has a newer date than
 *  \p timeStamp.
 */
bool FilePath::isNewerThan(const QDateTime &timeStamp) const
{
    if (!exists() || lastModified() >= timeStamp)
        return true;
    if (isDir()) {
        const FilePaths dirContents = dirEntries(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const FilePath &entry : dirContents) {
            if (entry.isNewerThan(timeStamp))
                return true;
        }
    }
    return false;
}

/*!
 * \brief Returns the caseSensitivity of the path.
 * \returns The caseSensitivity of the path.
 * This is currently only based on the Host OS.
 * For device paths, \c Qt::CaseSensitive is always returned.
 */
Qt::CaseSensitivity FilePath::caseSensitivity() const
{
    if (m_scheme.isEmpty())
        return HostOsInfo::fileNameCaseSensitivity();

    // FIXME: This could or possibly should the target device's file name case sensitivity
    // into account by diverting to IDevice. However, as this is expensive and we are
    // in time-critical path here, we go with "good enough" for now:
    // The first approximation is "most things are case-sensitive".
    return Qt::CaseSensitive;
}

/*!
*  \brief Recursively resolves symlinks if this is a symlink.
*  To resolve symlinks anywhere in the path, see canonicalPath.
*  Unlike QFileInfo::canonicalFilePath(), this function will still return the expected deepest
*  target file even if the symlink is dangling.
*
*  \note Maximum recursion depth == 16.
*
*  \returns the symlink target file path.
*/
FilePath FilePath::resolveSymlinks() const
{
    FilePath current = *this;
    int links = 16;
    while (links--) {
        const FilePath target = current.symLinkTarget();
        if (target.isEmpty())
            return current;
        current = target;
    }
    return current;
}

/*!
*  \brief Recursively resolves possibly present symlinks in this file name.
*  Unlike QFileInfo::canonicalFilePath(), this function will not return an empty
*  string if path doesn't exist.
*
*  \returns the canonical path.
*/
FilePath FilePath::canonicalPath() const
{
    if (needsDevice()) {
        // FIXME: Not a full solution, but it stays on the right device.
        return *this;
    }
    const QString result = toFileInfo().canonicalFilePath();
    if (result.isEmpty())
        return *this;
    return FilePath::fromString(result);
}

FilePath FilePath::operator/(const QString &str) const
{
    return pathAppended(str);
}

/*!
* \brief Clears all parts of the FilePath.
*/
void FilePath::clear()
{
    m_path.clear();
    m_root.clear();
    m_host.clear();
    m_scheme.clear();
}

/*!
* \brief Checks if the path() is empty.
* \returns true if the path() is empty.
* The Host and Scheme of the part are ignored.
*/
bool FilePath::isEmpty() const
{
    return m_root.isEmpty() && m_path.isEmpty();
}

/*!
* \brief Converts the path to a possibly shortened path with native separators.
* Like QDir::toNativeSeparators(), but use prefix '~' instead of $HOME on unix systems when an
* absolute path is given.
*
* \returns the possibly shortened path with native separators.
*/
QString FilePath::shortNativePath() const
{
    if (HostOsInfo::isAnyUnixHost()) {
        const FilePath home = FileUtils::homePath();
        if (isChildOf(home)) {
            return QLatin1Char('~') + QDir::separator()
                + QDir::toNativeSeparators(relativeChildPath(home).toString());
        }
    }
    return toUserOutput();
}

/*!
* \brief Checks whether the path is relative
* \returns true if the path is relative.
*/
bool FilePath::isRelativePath() const
{
    return m_root.isEmpty();
}

/*!
* \brief Appends the tail to this, if the tail is a relative path.
* \param tail The tail to append.
* \returns Returns tail if tail is absolute, otherwise this + tail.
*/
FilePath FilePath::resolvePath(const FilePath &tail) const
{
    if (!tail.m_root.isEmpty())
        return tail;
    return pathAppended(tail.m_path);
}

/*!
* \brief Appends the tail to this, if the tail is a relative path.
* \param tail The tail to append.
* \returns Returns tail if tail is absolute, otherwise this + tail.
*/
FilePath FilePath::resolvePath(const QString &tail) const
{
   FilePath tailPath = FilePath::fromString(doCleanPath(tail));
   return resolvePath(tailPath);
}

FilePath FilePath::cleanPath() const
{
    FilePath result = *this;
    result.setPath(doCleanPath(result.path()));
    return result;
}

QTextStream &operator<<(QTextStream &s, const FilePath &fn)
{
    return s << fn.toString();
}

FileFilter::FileFilter(const QStringList &nameFilters,
                               const QDir::Filters fileFilters,
                               const QDirIterator::IteratorFlags flags)
    : nameFilters(nameFilters),
      fileFilters(fileFilters),
      iteratorFlags(flags)
{
}

DeviceFileHooks &DeviceFileHooks::instance()
{
    return s_deviceHooks;
}

} // namespace Utils

std::hash<Utils::FilePath>::result_type
    std::hash<Utils::FilePath>::operator()(const std::hash<Utils::FilePath>::argument_type &fn) const
{
    if (fn.caseSensitivity() == Qt::CaseInsensitive)
        return hash<string>()(fn.toString().toCaseFolded().toStdString());
    return hash<string>()(fn.toString().toStdString());
}

QT_BEGIN_NAMESPACE
QDebug operator<<(QDebug dbg, const Utils::FilePath &c)
{
    return dbg << c.toString();
}

QT_END_NAMESPACE
