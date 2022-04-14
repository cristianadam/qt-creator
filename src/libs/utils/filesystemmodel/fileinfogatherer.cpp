#include "fileinfogatherer.h"
#include "filesystemmodelutils.h"

#include "../filepath.h"

#include <QDir>
#include <QDirIterator>


namespace Utils {

/*!
    Creates thread
*/
FileInfoGatherer::FileInfoGatherer(QObject *parent)
    : QThread(parent)
    , m_iconProvider(&defaultProvider)
{
    start(LowPriority);
}

/*!
    Destroys thread
*/
FileInfoGatherer::~FileInfoGatherer()
{
    abort.storeRelaxed(true);
    QMutexLocker locker(&mutex);
    condition.wakeAll();
    locker.unlock();
    wait();
}

void FileInfoGatherer::setResolveSymlinks(bool enable)
{
    m_resolveSymlinks = enable;
}

void FileInfoGatherer::driveAdded()
{
    fetchExtendedInformation(QList<FilePath>{FilePath::fromString("")});
}

void FileInfoGatherer::driveRemoved()
{
    QStringList drives;
    const QFileInfoList driveInfoList = QDir::drives();
    for (const QFileInfo &fi : driveInfoList)
        drives.append(translateDriveName(fi));
    newListOfFiles(QString(), drives);
}

bool FileInfoGatherer::resolveSymlinks() const
{
    return HostOsInfo::isWindowsHost() && m_resolveSymlinks;
}

void FileInfoGatherer::setIconProvider(QFileIconProvider *provider)
{
    m_iconProvider = provider;
}

QFileIconProvider *FileInfoGatherer::iconProvider() const
{
    return m_iconProvider;
}

/*!
    Fetch extended information for all \a files in \a path

    \sa updateFile(), update(), resolvedName()
*/
void FileInfoGatherer::fetchExtendedInformation(const FilePaths &paths)
{
    QMutexLocker locker(&mutex);

    // See if we already have this dir/file in our queue
    for(const FilePath& path : paths) {
        if (!path.isEmpty()) {
            this->gatherPaths.push(path.toString());
            this->gatherFiles.push({path.toString()});
        }
    }
    condition.wakeAll();

    watchPaths(paths);

    /*
    int loc = this->path.lastIndexOf(path);
    while (loc > 0)  {
        if (this->files.at(loc) == files) {
            return;
        }
        loc = this->path.lastIndexOf(path, loc - 1);
    }
    this->path.push(path);
    this->files.push(files);
    condition.wakeAll();

    if (useFileSystemWatcher()) {
        if (files.isEmpty()
                && !path.isEmpty()
                && !path.startsWith(QLatin1String("//")) /*don't watch UNC path*) {
            if (!watchedDirectories().contains(path))
                watchPaths(QStringList(path));
        }
    }
    */
}

/*!
    Fetch extended information for all \a filePath

    \sa fetchExtendedInformation()
*/
void FileInfoGatherer::updateFile(const FilePath &filePath)
{
    //QString dir = filePath.mid(0, filePath.lastIndexOf(QLatin1Char('/')));
    //QString fileName = filePath.mid(dir.length() + 1);
    fetchExtendedInformation(FilePaths{filePath});
}

FilePaths FileInfoGatherer::watchedFiles() const
{
    FilePaths paths;
    if (useFileSystemWatcher() && m_watcher) {
        const QStringList files = m_watcher->files();
        for(const QString& file : files) {
            paths.append(FilePath::fromString(file));
        }
    }
    return paths;
}

FilePaths FileInfoGatherer::watchedDirectories() const
{
    FilePaths paths;
    if (useFileSystemWatcher() && m_watcher) {
        const QStringList directories = m_watcher->directories();
        for(const QString& directorie : directories) {
            paths.append(FilePath::fromString(directorie));
        }
    }
    return paths;
}

void FileInfoGatherer::createWatcher()
{
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& dirPath) { this->list(FilePath::fromString(dirPath)); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& filePath) { this->updateFile(FilePath::fromString(filePath)); });
    if (HostOsInfo::isWindowsHost()) {
        const QVariant listener = m_watcher->property("_q_driveListener");
        if (listener.canConvert<QObject *>()) {
            if (QObject *driveListener = listener.value<QObject *>()) {
                connect(driveListener, SIGNAL(driveAdded()), this, SLOT(driveAdded()));
                connect(driveListener, SIGNAL(driveRemoved()), this, SLOT(driveRemoved()));
            }
        }
    }
}

void FileInfoGatherer::watchPaths(const FilePaths &paths)
{
    if (useFileSystemWatcher() && m_watching) {
        if (m_watcher == nullptr)
            createWatcher();

        QStringList sPaths;
        for(const FilePath& path : paths) {
            if (!path.needsDevice())
                sPaths.append(path.toString());
        }
        if (!sPaths.isEmpty())
            m_watcher->addPaths(sPaths);
    }
}

void FileInfoGatherer::unwatchPaths(const FilePaths &paths)
{
    QStringList sPaths;
    for(const FilePath& path : paths) {
        if (!path.needsDevice())
            sPaths.append(path.toString());
    }
    if (useFileSystemWatcher() && m_watcher && !paths.isEmpty())
        m_watcher->removePaths(sPaths);
}

bool FileInfoGatherer::isWatching() const
{
    bool result = false;
    QMutexLocker locker(&mutex);
    result = m_watching;
    return result;
}

void FileInfoGatherer::setWatching(bool v)
{
    QMutexLocker locker(&mutex);
    if (v != m_watching) {
        if (!v) {
            delete m_watcher;
            m_watcher = nullptr;
        }
        m_watching = v;
    }
}

/*
    List all files in \a directoryPath

    \sa listed()
*/
void FileInfoGatherer::clear()
{
    QTC_CHECK(useFileSystemWatcher());
    QMutexLocker locker(&mutex);
    unwatchPaths(watchedFiles());
    unwatchPaths(watchedDirectories());
}

/*
    Remove a \a path from the watcher

    \sa listed()
*/
void FileInfoGatherer::removePath(const FilePath &path)
{
    QTC_CHECK(useFileSystemWatcher());
    QMutexLocker locker(&mutex);
    unwatchPaths(FilePaths{path});
}

/*
    List all files in \a directoryPath

    \sa listed()
*/
void FileInfoGatherer::list(const FilePath &directoryPath)
{
    fetchExtendedInformation(FilePaths{directoryPath});
}

/*
    Until aborted wait to fetch a directory or files
*/
void FileInfoGatherer::run()
{
    forever {
        QMutexLocker locker(&mutex);
        while (!abort.loadRelaxed() && gatherPaths.isEmpty())
            condition.wait(&mutex);
        if (abort.loadRelaxed())
            return;
        const QString thisPath = qAsConst(gatherPaths).front();
        gatherPaths.pop_front();
        const QStringList thisList = qAsConst(gatherFiles).front();
        gatherFiles.pop_front();
        locker.unlock();

        getFileInfos(thisPath, thisList);
    }
}

ExtendedInformation FileInfoGatherer::getInfo(const FilePath &path) const
{
    ExtendedInformation info(path);
    info.icon = !path.needsDevice() ? m_iconProvider->icon(path.toFileInfo()) : QIcon();
    info.displayType = !path.needsDevice() ? m_iconProvider->type(path.toFileInfo()) : "";
    if (useFileSystemWatcher()) {
        // ### Not ready to listen all modifications by default
        static const bool watchFiles = qEnvironmentVariableIsSet("QT_FILESYSTEMMODEL_WATCH_FILES");
        if (watchFiles) {
            if (!path.exists() && !path.isSymLink()) {
                const_cast<FileInfoGatherer *>(this)->unwatchPaths(FilePaths{path});
            } else {
                const_cast<FileInfoGatherer *>(this)->watchPaths(FilePaths{path});
            }
        }
    }

    /*
     * TODO: Implement
    if (HostOsInfo::isWindowsHost() && m_resolveSymlinks && info.isSymLink(/* ignoreNtfsSymLinks = * true)) {
        QFileInfo resolvedInfo(QFileInfo(path.symLinkTarget()).canonicalFilePath());
        if (resolvedInfo.exists()) {
            emit nameResolved(path.filePath(), resolvedInfo.fileName());
        }
    }
    */
    return info;
}

/*
    Get specific file info's, batch the files so update when we have 100
    items and every 200ms after that
 */
void FileInfoGatherer::getFileInfos(const QString &path, const QStringList &files)
{
    // List drives
    if (path.isEmpty()) {
        QFileInfoList infoList;
        if (files.isEmpty()) {
            infoList = QDir::drives();
        } else {
            infoList.reserve(files.count());
            for (const auto &file : files)
                infoList << QFileInfo(file);
        }
        QList<QPair<QString, QFileInfo>> updatedFiles;
        updatedFiles.reserve(infoList.count());
        for (int i = infoList.count() - 1; i >= 0; --i) {
            QFileInfo driveInfo = infoList.at(i);
            doStat(driveInfo);
            QString driveName = translateDriveName(driveInfo);
            updatedFiles.append(QPair<QString,QFileInfo>(driveName, driveInfo));
        }
        emit updates(path, updatedFiles);
        return;
    }

    QElapsedTimer base;
    base.start();
    QFileInfo fileInfo;
    bool firstTime = true;
    QList<QPair<QString, QFileInfo>> updatedFiles;
    QStringList filesToCheck = files;

    QStringList allFiles;
    if (files.isEmpty()) {
        QDirIterator dirIt(path, QDir::AllEntries | QDir::System | QDir::Hidden);
        while (!abort.loadRelaxed() && dirIt.hasNext()) {
            dirIt.next();
            fileInfo = dirIt.fileInfo();
            doStat(fileInfo);
            allFiles.append(fileInfo.fileName());
            fetch(fileInfo, base, firstTime, updatedFiles, path);
        }
    }
    if (!allFiles.isEmpty())
        emit newListOfFiles(path, allFiles);

    QStringList::const_iterator filesIt = filesToCheck.constBegin();
    while (!abort.loadRelaxed() && filesIt != filesToCheck.constEnd()) {
        fileInfo.setFile(path + QDir::separator() + *filesIt);
        ++filesIt;
        doStat(fileInfo);
        fetch(fileInfo, base, firstTime, updatedFiles, path);
    }
    if (!updatedFiles.isEmpty())
        emit updates(path, updatedFiles);
    emit directoryLoaded(path);
}

void FileInfoGatherer::fetch(const QFileInfo &fileInfo, QElapsedTimer &base, bool &firstTime,
                              QList<QPair<QString, QFileInfo>> &updatedFiles, const QString &path)
{
    updatedFiles.append(QPair<QString, QFileInfo>(fileInfo.fileName(), fileInfo));
    QElapsedTimer current;
    current.start();
    if ((firstTime && updatedFiles.count() > 100) || base.msecsTo(current) > 1000) {
        emit updates(path, updatedFiles);
        updatedFiles.clear();
        base = current;
        firstTime = false;
    }
}

}
