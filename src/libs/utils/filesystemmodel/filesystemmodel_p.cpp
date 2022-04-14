#include "filesystemmodel_p.h"

#include "filesystemmodelutils.h"
#include "filesystemmodelsorter.h"

namespace Utils {

/*!
    \internal

    Return the FileSystemNode that goes to index.
  */
FileSystemNode *FileSystemModelPrivate::node(const QModelIndex &index) const
{
    if (!index.isValid())
        return const_cast<FileSystemNode*>(&root);
    FileSystemNode *indexNode = static_cast<FileSystemNode*>(index.internalPointer());
    Q_ASSERT(indexNode);
    return indexNode;
}

/*!
    \internal

    Given a path return the matching FileSystemNode or &root if invalid
*/
FileSystemNode *FileSystemModelPrivate::node(const FilePath &path, bool fetch) const
{
    if (path.isEmpty() || path.toString() == myComputer() || path.toString().startsWith(QLatin1Char(':')))
        return const_cast<FileSystemNode*>(&root);

    if (path == "/") {
        qDebug() << "ROOOOT";
    }

    // Construct the nodes up to the new root path if they need to be built
    QString absolutePath;
    QString longPath = qtc_GetLongPathName(path.toString());
    if (longPath == rootDir.path())
        absolutePath = rootDir.toString();
    else
        absolutePath = FilePath::fromString(longPath).absoluteFilePath().toString();

    // ### TODO can we use bool QAbstractFileEngine::caseSensitive() const?
    QStringList pathElements = absolutePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if ((pathElements.isEmpty())
#if !defined(Q_OS_WIN)
        && QDir::fromNativeSeparators(longPath) != QLatin1String("/")
#endif
    ) {
        return const_cast<FileSystemNode*>(&root);
    }


    QModelIndex index = QModelIndex(); // start with "My Computer"
    QString elementPath;
    QChar separator = QLatin1Char('/');
    QString trailingSeparator;
    if (HostOsInfo::isWindowsHost()) {
        if (absolutePath.startsWith(QLatin1String("//"))) { // UNC path
            QString host = QLatin1String("\\\\") + pathElements.constFirst();
            if (absolutePath == QDir::fromNativeSeparators(host))
                absolutePath.append(QLatin1Char('/'));
            if (longPath.endsWith(QLatin1Char('/')) && !absolutePath.endsWith(QLatin1Char('/')))
                absolutePath.append(QLatin1Char('/'));
            if (absolutePath.endsWith(QLatin1Char('/')))
                trailingSeparator = QLatin1String("\\");
            int r = 0;
            auto rootNode = const_cast<FileSystemNode*>(&root);
            auto it = root.children.constFind(PathKey(host, rootNode->caseSensitivity()));
            if (it != root.children.cend()) {
                host = it.key().data; // Normalize case for lookup in visibleLocation()
            } else {
                const PathKey hostKey{host, rootNode->caseSensitivity()};
                if (pathElements.count() == 1 && !absolutePath.endsWith(QLatin1Char('/')))
                    return rootNode;
                QFileInfo info(host);
                if (!info.exists())
                    return rootNode;
                FileSystemModelPrivate *p = const_cast<FileSystemModelPrivate*>(this);
                p->addNode(rootNode, hostKey, info);
                p->addVisibleFiles(rootNode, {hostKey});
            }
            const PathKey hostKey{host, rootNode->caseSensitivity()};
            r = rootNode->visibleLocation(hostKey);
            r = translateVisibleLocation(rootNode, r);
            index = q->index(r, 0, QModelIndex());
            pathElements.pop_front();
            separator = QLatin1Char('\\');
            elementPath = host;
            elementPath.append(separator);
        } else {
            if (!pathElements.at(0).contains(QLatin1Char(':'))) {
                QString rootPath = QDir(longPath).rootPath();
                pathElements.prepend(rootPath);
            }
            if (pathElements.at(0).endsWith(QLatin1Char('/')))
                pathElements[0].chop(1);
        }
    } else {
        // add the "/" item, since it is a valid path element on Unix
        if (absolutePath[0] == QLatin1Char('/'))
            pathElements.prepend(QLatin1String("/"));
    }

    FileSystemNode *parent = node(index);

    for (int i = 0; i < pathElements.count(); ++i) {
        QString element = pathElements.at(i);
        if (i != 0)
            elementPath.append(separator);
        elementPath.append(element);
        if (i == pathElements.count() - 1)
            elementPath.append(trailingSeparator);

        if (HostOsInfo::isWindowsHost()) {
            // On Windows, "filename    " and "filename" are equivalent and
            // "filename  .  " and "filename" are equivalent
            // "filename......." and "filename" are equivalent Task #133928
            // whereas "filename  .txt" is still "filename  .txt"
            // If after stripping the characters there is nothing left then we
            // just return the parent directory as it is assumed that the path
            // is referring to the parent
            while (element.endsWith(QLatin1Char('.')) || element.endsWith(QLatin1Char(' ')))
                element.chop(1);
            // Only filenames that can't possibly exist will be end up being empty
            if (element.isEmpty())
                return parent;
        }
        const PathKey elementKey(element, parent->caseSensitivity());
        bool alreadyExisted = parent->children.contains(elementKey);

        // we couldn't find the path element, we create a new node since we
        // _know_ that the path is valid
        if (alreadyExisted) {
            if (parent->children.count() == 0
                    || parent->children.value(elementKey)->fileName != elementKey)
                alreadyExisted = false;
        }

        FileSystemNode *node;
        if (!alreadyExisted) {
            // Someone might call ::index("file://cookie/monster/doesn't/like/veggies"),
            // a path that doesn't exists, I.E. don't blindly create directories.
            QFileInfo info(elementPath);
            if (!info.exists())
                return const_cast<FileSystemNode*>(&root);
            FileSystemModelPrivate *p = const_cast<FileSystemModelPrivate*>(this);
            node = p->addNode(parent, PathKey(element, parent->caseSensitivity()), info);
            if (useFileSystemWatcher())
                node->populate(fileInfoGatherer.getInfo(FilePath::fromString(elementPath)));
        } else {
            node = parent->children.value(elementKey);
        }

        Q_ASSERT(node);
        if (!node->isVisible) {
            // It has been filtered out
            if (alreadyExisted && node->hasInformation() && !fetch)
                return const_cast<FileSystemNode*>(&root);

            FileSystemModelPrivate *p = const_cast<FileSystemModelPrivate*>(this);
            p->addVisibleFiles(parent, {elementKey});
            if (!p->bypassFilters.contains(node))
                p->bypassFilters[node] = 1;
            FilePath path = q->filePath(this->index(parent));
            if (!node->hasInformation() && fetch) {
                Fetching f = { std::move(path), node };
                p->toFetch.append(std::move(f));
                p->fetchingTimer.start(0, const_cast<FileSystemModel*>(q));
            }
        }
        parent = node;
    }

    return parent;
}

/*
    \internal

    return the index for node
*/
QModelIndex FileSystemModelPrivate::index(const FileSystemNode *node, int column) const
{
    FileSystemNode *parentNode = (node ? node->parent : nullptr);
    if (node == &root || !parentNode)
        return QModelIndex();

    // get the parent's row
    Q_ASSERT(node);
    if (!node->isVisible)
        return QModelIndex();

    int visualRow = translateVisibleLocation(parentNode, parentNode->visibleLocation(node->fileName));
    return q->createIndex(visualRow, column, const_cast<FileSystemNode*>(node));
}

QString FileSystemModelPrivate::size(const QModelIndex &index) const
{
    if (!index.isValid())
        return QString();
    const FileSystemNode *n = node(index);
    if (n->isDir()) {
        if (HostOsInfo::isMacHost())
            return QLatin1String("--");
        else
            return QLatin1String("");
    // Windows   - ""
    // OS X      - "--"
    // Konqueror - "4 KB"
    // Nautilus  - "9 items" (the number of children)
    }
    return size(n->size());
}


QString FileSystemModelPrivate::size(qint64 bytes)
{
    return QLocale::system().formattedDataSize(bytes);
}

QString FileSystemModelPrivate::time(const QModelIndex &index) const
{
    if (!index.isValid())
        return QString();
    return QLocale::system().toString(node(index)->lastModified(), QLocale::ShortFormat);
}

QString FileSystemModelPrivate::type(const QModelIndex &index) const
{
    if (!index.isValid())
        return QString();
    return node(index)->type();
}

QString FileSystemModelPrivate::name(const QModelIndex &index) const
{
    if (!index.isValid())
        return QString();
    FileSystemNode *dirNode = node(index);

    // FIXME: TODO: Support symlinks
    /*if (fileInfoGatherer.resolveSymlinks() &&
            !resolvedSymLinks.isEmpty() && dirNode->isSymLink(/* ignoreNtfsSymLinks = * true)) {
        FilePath path = filePath(index);
        return resolvedSymLinks.value(path, dirNode->fileName.data);
    }*/
    return dirNode->info->path().fileName();
}

QString FileSystemModelPrivate::displayName(const QModelIndex &index) const
{
    if (HostOsInfo::isWindowsHost()) {
        FileSystemNode *dirNode = node(index);
        if (!dirNode->volumeName.isEmpty())
            return dirNode->volumeName;
    }
    return name(index);
}

QIcon FileSystemModelPrivate::icon(const QModelIndex &index) const
{
    if (!index.isValid())
        return QIcon();
    return node(index)->icon();
}

void FileSystemModelPrivate::_q_performDelayedSort()
{
    q->sort(sortColumn, sortOrder);
}



/*
    \internal

    Sort all of the children of parent
*/
void FileSystemModelPrivate::sortChildren(int column, const QModelIndex &parent)
{
    FileSystemNode *indexNode = node(parent);
    if (indexNode->children.count() == 0)
        return;

    QList<FileSystemNode *> values;

    for (auto iterator = indexNode->children.constBegin(), cend = indexNode->children.constEnd(); iterator != cend; ++iterator) {
        if (filtersAcceptsNode(iterator.value())) {
            values.append(iterator.value());
        } else {
            iterator.value()->isVisible = false;
        }
    }
    QFileSystemModelSorter ms(column);
    std::sort(values.begin(), values.end(), ms);
    // First update the new visible list
    indexNode->visibleChildren.clear();
    //No more dirty item we reset our internal dirty index
    indexNode->dirtyChildrenIndex = -1;
    const int numValues = values.count();
    indexNode->visibleChildren.reserve(numValues);
    for (int i = 0; i < numValues; ++i) {
        indexNode->visibleChildren.append(values.at(i)->fileName);
        values.at(i)->isVisible = true;
    }

    if (!disableRecursiveSort) {
        for (int i = 0; i < q->rowCount(parent); ++i) {
            const QModelIndex childIndex = q->index(i, 0, parent);
            FileSystemNode *indexNode = node(childIndex);
            //Only do a recursive sort on visible nodes
            if (indexNode->isVisible)
                sortChildren(column, childIndex);
        }
    }
}

FilePath FileSystemModelPrivate::filePath(const QModelIndex &index) const
{
    if (!index.isValid())
        return FilePath();
    Q_ASSERT(index.model() == q);

    QStringList path;
    QModelIndex idx = index;
    while (idx.isValid()) {
        FileSystemNode *dirNode = node(idx);
        if (dirNode)
            path.prepend(dirNode->fileName.data);
        idx = idx.parent();
    }
    return FilePath::fromString(path.join('/'));
    /*QString fullPath = QDir::fromNativeSeparators(path.join(QDir::separator()));
    if (!HostOsInfo::isWindowsHost()) {
        if ((fullPath.length() > 2) && fullPath[0] == QLatin1Char('/') && fullPath[1] == QLatin1Char('/'))
            fullPath = fullPath.mid(1);
    } else {
        if (fullPath.length() == 2 && fullPath.endsWith(QLatin1Char(':')))
            fullPath.append(QLatin1Char('/'));
    }

    return fullPath;*/
}


/*!
     \internal

    Performed quick listing and see if any files have been added or removed,
    then fetch more information on visible files.
 */
void FileSystemModelPrivate::_q_directoryChanged(const QString &directory, const QStringList &files)
{
    FileSystemNode *parentNode = node(FilePath::fromString(directory), false);
    if (parentNode->children.count() == 0)
        return;
    QStringList toRemove;
    QStringList newFiles = files;
    std::sort(newFiles.begin(), newFiles.end());
    for (auto i = parentNode->children.constBegin(), cend = parentNode->children.constEnd(); i != cend; ++i) {
        QStringList::iterator iterator = std::lower_bound(newFiles.begin(), newFiles.end(), i.value()->fileName.data);
        if ((iterator == newFiles.end()) || (i.value()->fileName < PathKey(*iterator, parentNode->caseSensitivity())))
            toRemove.append(i.value()->fileName.data);
    }
    for (int i = 0 ; i < toRemove.count() ; ++i )
        removeNode(parentNode, PathKey(toRemove[i], parentNode->caseSensitivity()));
}


/*!
    \internal

    Adds a new file to the children of parentNode

    *WARNING* this will change the count of children
*/
FileSystemNode* FileSystemModelPrivate::addNode(FileSystemNode *parentNode, const PathKey &fileName, const QFileInfo& info)
{
    // In the common case, itemLocation == count() so check there first
    FileSystemNode *node = new FileSystemNode(fileName, parentNode);
    if (useFileSystemWatcher())
        node->populate(ExtendedInformation(info));

    // The parentNode is "" so we are listing the drives
    if (HostOsInfo::isWindowsHost() && parentNode->fileName.data.isEmpty())
        node->volumeName = volumeName(fileName.data);
    Q_ASSERT(!parentNode->children.contains(fileName));
    parentNode->children.insert(fileName, node);
    return node;
}

/*!
    \internal

    File at parentNode->children(itemLocation) has been removed, remove from the lists
    and emit signals if necessary

    *WARNING* this will change the count of children and could change visibleChildren
 */
void FileSystemModelPrivate::removeNode(FileSystemNode *parentNode, const PathKey &name)
{
    QModelIndex parent = index(parentNode);
    bool indexHidden = isHiddenByFilter(parentNode, parent);

    int vLocation = parentNode->visibleLocation(name);
    if (vLocation >= 0 && !indexHidden)
        q->beginRemoveRows(parent, translateVisibleLocation(parentNode, vLocation),
                                       translateVisibleLocation(parentNode, vLocation));
    FileSystemNode * node = parentNode->children.take(name);
    delete node;
    // cleanup sort files after removing rather then re-sorting which is O(n)
    if (vLocation >= 0)
        parentNode->visibleChildren.removeAt(vLocation);
    if (vLocation >= 0 && !indexHidden)
        q->endRemoveRows();
}

/*!
    \internal

    File at parentNode->children(itemLocation) was not visible before, but now should be
    and emit signals if necessary.

    *WARNING* this will change the visible count
 */
void FileSystemModelPrivate::addVisibleFiles(FileSystemNode *parentNode, const PathKeys &newFiles)
{
    QModelIndex parent = index(parentNode);
    bool indexHidden = isHiddenByFilter(parentNode, parent);
    if (!indexHidden) {
        q->beginInsertRows(parent, parentNode->visibleChildren.count() , parentNode->visibleChildren.count() + newFiles.count() - 1);
    }

    if (parentNode->dirtyChildrenIndex == -1)
        parentNode->dirtyChildrenIndex = parentNode->visibleChildren.count();

    for (const PathKey &newFile : newFiles) {
        parentNode->visibleChildren.append(newFile);
        FileSystemNode *node = parentNode->children.value(newFile);
        QTC_ASSERT(node, continue);
        node->isVisible = true;
    }
    if (!indexHidden)
      q->endInsertRows();
}

/*!
    \internal

    File was visible before, but now should NOT be

    *WARNING* this will change the visible count
 */
void FileSystemModelPrivate::removeVisibleFile(FileSystemNode *parentNode, int vLocation)
{
    if (vLocation == -1)
        return;
    QModelIndex parent = index(parentNode);
    bool indexHidden = isHiddenByFilter(parentNode, parent);
    if (!indexHidden)
        q->beginRemoveRows(parent, translateVisibleLocation(parentNode, vLocation),
                                       translateVisibleLocation(parentNode, vLocation));
    parentNode->children.value(parentNode->visibleChildren.at(vLocation))->isVisible = false;
    parentNode->visibleChildren.removeAt(vLocation);
    if (!indexHidden)
        q->endRemoveRows();
}

/*!
    \internal

    The thread has received new information about files,
    update and emit dataChanged if it has actually changed.
 */
void FileSystemModelPrivate::_q_fileSystemChanged(const QString &path,
                                                   const QList<QPair<QString, QFileInfo>> &updates)
{
    QTC_CHECK(useFileSystemWatcher());

    PathKeys rowsToUpdate;
    PathKeys newFiles;
    FileSystemNode *parentNode = node(FilePath::fromString(path), false);
    QModelIndex parentIndex = index(parentNode);
    for (const auto &update : updates) {
        PathKey fileName{update.first, parentNode->caseSensitivity()};
        Q_ASSERT(!fileName.data.isEmpty());
        ExtendedInformation info = fileInfoGatherer.getInfo(FilePath::fromFileInfo(update.second));
        bool previouslyHere = parentNode->children.contains(fileName);
        if (!previouslyHere) {
            addNode(parentNode, fileName, info.fileInfo());
        }
        FileSystemNode * node = parentNode->children.value(fileName);
        if (node->fileName != fileName)
            continue;
        const bool isCaseSensitive = parentNode->caseSensitive();
        if (isCaseSensitive) {
            Q_ASSERT(node->fileName == fileName);
        } else {
            node->fileName = fileName;
        }

        if (*node != info ) {
            node->populate(info);
            bypassFilters.remove(node);
            // brand new information.
            if (filtersAcceptsNode(node)) {
                if (!node->isVisible) {
                    newFiles.append(fileName);
                } else {
                    rowsToUpdate.append(fileName);
                }
            } else {
                if (node->isVisible) {
                    int visibleLocation = parentNode->visibleLocation(fileName);
                    removeVisibleFile(parentNode, visibleLocation);
                } else {
                    // The file is not visible, don't do anything
                }
            }
        }
    }

    // bundle up all of the changed signals into as few as possible.
    std::sort(rowsToUpdate.begin(), rowsToUpdate.end());
    PathKey min;
    PathKey max;
    for (const PathKey &value : qAsConst(rowsToUpdate)) {
        //##TODO is there a way to bundle signals with QString as the content of the list?
        /*if (min.isEmpty()) {
            min = value;
            if (i != rowsToUpdate.count() - 1)
                continue;
        }
        if (i != rowsToUpdate.count() - 1) {
            if ((value == min + 1 && max.isEmpty()) || value == max + 1) {
                max = value;
                continue;
            }
        }*/
        max = value;
        min = value;
        int visibleMin = parentNode->visibleLocation(min);
        int visibleMax = parentNode->visibleLocation(max);
        if (visibleMin >= 0
            && visibleMin < parentNode->visibleChildren.count()
            && parentNode->visibleChildren.at(visibleMin) == min
            && visibleMax >= 0) {
            QModelIndex bottom = q->index(translateVisibleLocation(parentNode, visibleMin), 0, parentIndex);
            QModelIndex top = q->index(translateVisibleLocation(parentNode, visibleMax), 3, parentIndex);
            emit q->dataChanged(bottom, top);
        }

        /*min = QString();
        max = QString();*/
    }

    if (newFiles.count() > 0) {
        addVisibleFiles(parentNode, newFiles);
    }

    if (newFiles.count() > 0 || (sortColumn != 0 && rowsToUpdate.count() > 0)) {
        forceSort = true;
        delayedSort();
    }
}

void FileSystemModelPrivate::_q_resolvedName(const QString &fileName, const QString &resolvedName)
{
    resolvedSymLinks[fileName] = resolvedName;
}

// Remove file system watchers at/below the index and return a list of previously
// watched files. This should be called prior to operations like rename/remove
// which might fail due to watchers on platforms like Windows. The watchers
// should be restored on failure.
FilePaths FileSystemModelPrivate::unwatchPathsAt(const QModelIndex &index)
{
    QTC_CHECK(HostOsInfo::isWindowsHost());
    QTC_CHECK(useFileSystemWatcher());
    const FileSystemNode *indexNode = node(index);
    if (indexNode == nullptr)
        return {};
    const Qt::CaseSensitivity caseSensitivity = indexNode->caseSensitive()
        ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const FilePath path = FilePath::fromFileInfo(indexNode->fileInfo());

    FilePaths result;
    const auto filter = [path, caseSensitivity] (const FilePath &watchedPath)
    {
        /*const int pathSize = path.fileSize();
        if (pathSize == watchedPath.fileSize()) {
            return QString(path.toString()).compare(watchedPath.toString(), caseSensitivity) == 0;
        } else if (watchedPath.fileSize() > pathSize) {
            return watchedPath.at(pathSize) == QLatin1Char('/')
                && watchedPath.startsWith(path, caseSensitivity);
        }*/
        return path == watchedPath;
    };

    const FilePaths &watchedFiles = fileInfoGatherer.watchedFiles();
    std::copy_if(watchedFiles.cbegin(), watchedFiles.cend(),
                 std::back_inserter(result), filter);

    const FilePaths &watchedDirectories = fileInfoGatherer.watchedDirectories();
    std::copy_if(watchedDirectories.cbegin(), watchedDirectories.cend(),
                 std::back_inserter(result), filter);

    fileInfoGatherer.unwatchPaths(result);
    return result;
}

void FileSystemModelPrivate::init()
{
    delayedSortTimer.setSingleShot(true);

    qRegisterMetaType<QList<QPair<QString, QFileInfo>>>();
    if (useFileSystemWatcher()) {
        QObject::connect(&fileInfoGatherer, &FileInfoGatherer::newListOfFiles,
                   &mySlots, &FileSystemModelSlots::_q_directoryChanged);
        QObject::connect(&fileInfoGatherer, &FileInfoGatherer::updates,
                   &mySlots, &FileSystemModelSlots::_q_fileSystemChanged);
        QObject::connect(&fileInfoGatherer, &FileInfoGatherer::nameResolved,
                   &mySlots, &FileSystemModelSlots::_q_resolvedName);
        q->connect(&fileInfoGatherer, SIGNAL(directoryLoaded(QString)),
                   q, SIGNAL(directoryLoaded(QString)));
    }
    QObject::connect(&delayedSortTimer, &QTimer::timeout,
                     &mySlots, &FileSystemModelSlots::_q_performDelayedSort,
                     Qt::QueuedConnection);
}

/*!
    \internal

    Returns \c false if node doesn't pass the filters otherwise true

    QDir::Modified is not supported
    QDir::Drives is not supported
*/
bool FileSystemModelPrivate::filtersAcceptsNode(const FileSystemNode *node) const
{
    // always accept drives
    if (node->parent == &root || bypassFilters.contains(node))
        return true;

    // If we don't know anything yet don't accept it
    if (!node->hasInformation())
        return false;

    const bool filterPermissions = ((filters & QDir::PermissionMask)
                                   && (filters & QDir::PermissionMask) != QDir::PermissionMask);
    const bool hideDirs          = !(filters & (QDir::Dirs | QDir::AllDirs));
    const bool hideFiles         = !(filters & QDir::Files);
    const bool hideReadable      = !(!filterPermissions || (filters & QDir::Readable));
    const bool hideWritable      = !(!filterPermissions || (filters & QDir::Writable));
    const bool hideExecutable    = !(!filterPermissions || (filters & QDir::Executable));
    const bool hideHidden        = !(filters & QDir::Hidden);
    const bool hideSystem        = !(filters & QDir::System);
    const bool hideSymlinks      = (filters & QDir::NoSymLinks);
    const bool hideDot           = (filters & QDir::NoDot);
    const bool hideDotDot        = (filters & QDir::NoDotDot);

    // Note that we match the behavior of entryList and not QFileInfo on this.
    bool isDot    = (node->fileName.data == ".");
    bool isDotDot = (node->fileName.data == "..");
    if (   (hideHidden && !(isDot || isDotDot) && node->isHidden())
        || (hideSystem && node->isSystem())
        || (hideDirs && node->isDir())
        || (hideFiles && node->isFile())
        || (hideSymlinks && node->isSymLink())
        || (hideReadable && node->isReadable())
        || (hideWritable && node->isWritable())
        || (hideExecutable && node->isExecutable())
        || (hideDot && isDot)
        || (hideDotDot && isDotDot))
        return false;

    return nameFilterDisables || passNameFilters(node);
}


bool FileSystemModelPrivate::passNameFilters(const FileSystemNode *node) const
{
    if (nameFilters.isEmpty())
        return true;

    // Check the name regularexpression filters
    if (!(node->isDir() && (filters & QDir::AllDirs))) {
        const auto matchesNodeFileName = [node](const QRegularExpression &re)
        {
            return node->fileName.data.contains(re);
        };
        return std::any_of(nameFiltersRegexps.begin(),
                           nameFiltersRegexps.end(),
                           matchesNodeFileName);
    }
    return true;
}

void FileSystemModelPrivate::rebuildNameFilterRegexps()
{
    nameFiltersRegexps.clear();
    nameFiltersRegexps.reserve(nameFilters.size());
    const auto cs = (filters & QDir::CaseSensitive) ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const auto convertWildcardToRegexp = [cs](const QString &nameFilter)
    {
        return QRegularExpression_fromWildcard(nameFilter, cs);
    };
    std::transform(nameFilters.constBegin(),
                   nameFilters.constEnd(),
                   std::back_inserter(nameFiltersRegexps),
                   convertWildcardToRegexp);
}

}
