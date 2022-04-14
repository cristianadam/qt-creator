/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWidgets module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "filesystemmodel.h"

#include "extendedinformation.h"
#include "filesystemmodel_p.h"
#include "filesystemmodelutils.h"

#include "../filepath.h"
#include "../hostosinfo.h"
#include "../qtcassert.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QMimeData>
#include <QMutex>
#include <QPair>
#include <QStack>
#include <QCollator>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QTimerEvent>
#include <QUrl>
#include <QVarLengthArray>
#include <QWaitCondition>

#include <vector>
#include <algorithm>

#ifdef Q_OS_WIN
#ifdef QTCREATOR_PCH_H
 #define CALLBACK WINAPI
#endif
#  include <qt_windows.h>
#  include <shlobj.h>
#else
#  include <unistd.h>
#  include <sys/types.h>
#endif

namespace Utils {

/*!
    \fn void FileSystemModel::rootPathChanged(const QString &newPath);

    This signal is emitted whenever the root path has been changed to a \a newPath.
*/

/*!
    \fn void FileSystemModel::fileRenamed(const QString &path, const QString &oldName, const QString &newName)

    This signal is emitted whenever a file with the \a oldName is successfully
    renamed to \a newName.  The file is located in in the directory \a path.
*/

/*!
    \since 4.7
    \fn void FileSystemModel::directoryLoaded(const QString &path)

    This signal is emitted when the gatherer thread has finished to load the \a path.

*/

/*!
    \fn bool FileSystemModel::remove(const QModelIndex &index)

    Removes the model item \a index from the file system model and \b{deletes the
    corresponding file from the file system}, returning true if successful. If the
    item cannot be removed, false is returned.

    \warning This function deletes files from the file system; it does \b{not}
    move them to a location where they can be recovered.

    \sa rmdir()
*/

QFileInfo FileSystemModel::fileInfo(const QModelIndex &index) const
{
    return d->node(index)->fileInfo();
}

bool FileSystemModel::remove(const QModelIndex &aindex)
{
    const FilePath path = d->filePath(aindex);

#if QT_CONFIG(filesystemwatcher) && defined(Q_OS_WIN)
    // QTBUG-65683: Remove file system watchers prior to deletion to prevent
    // failure due to locked files on Windows.
    const FilePaths watchedPaths = d->unwatchPathsAt(aindex);
#endif // filesystemwatcher && Q_OS_WIN

    const bool success = (path.isFile() || path.isSymLink())
            ? path.removeFile() : path.removeRecursively();

#if QT_CONFIG(filesystemwatcher) && defined(Q_OS_WIN)
    if (!success)
        d->watchPaths(watchedPaths);
#endif // filesystemwatcher && Q_OS_WIN
    return success;
}

/*!
  Constructs a file system model with the given \a parent.
*/
FileSystemModel::FileSystemModel(QObject *parent)
    : QAbstractItemModel(parent),
    d(new FileSystemModelPrivate(this))
{
}

/*!
  Destroys this file system model.
*/
FileSystemModel::~FileSystemModel()
{
    delete d;
}

QModelIndex FileSystemModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || row >= rowCount(parent) || column >= columnCount(parent))
        return QModelIndex();

    // get the parent node
    FileSystemNode *parentNode = (d->indexValid(parent) ? d->node(parent) :
                                                   const_cast<FileSystemNode*>(&d->root));
    Q_ASSERT(parentNode);

    // now get the internal pointer for the index
    const int i = d->translateVisibleLocation(parentNode, row);
    if (i >= parentNode->visibleChildren.size())
        return QModelIndex();
    const PathKey childName = parentNode->visibleChildren.at(i);
    const FileSystemNode *indexNode = parentNode->children.value(childName);
    Q_ASSERT(indexNode);

    return createIndex(row, column, const_cast<FileSystemNode*>(indexNode));
}

QModelIndex FileSystemModel::sibling(int row, int column, const QModelIndex &idx) const
{
    if (row == idx.row() && column < FileSystemModelPrivate::NumColumns) {
        // cheap sibling operation: just adjust the column:
        return createIndex(row, column, idx.internalPointer());
    } else {
        // for anything else: call the default implementation
        // (this could probably be optimized, too):
        return QAbstractItemModel::sibling(row, column, idx);
    }
}

/*!
    \overload

    Returns the model item index for the given \a path and \a column.
*/
QModelIndex FileSystemModel::index(const FilePath &path, int column) const
{
    FileSystemNode *node = d->node(path, false);
    return d->index(node, column);
}

void FileSystemModel::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == d->fetchingTimer.timerId()) {
        d->fetchingTimer.stop();
        if (useFileSystemWatcher()) {
            for (int i = 0; i < d->toFetch.count(); ++i) {
                const FileSystemNode *node = d->toFetch.at(i).node;
                if (!node->hasInformation()) {
                    d->fileInfoGatherer.fetchExtendedInformation(QList<FilePath>{d->toFetch.at(i).path});
                } else {
                    // qDebug("yah!, you saved a little gerbil soul");
                }
            }
        }
        d->toFetch.clear();
    }
}

/*!
    Returns \c true if the model item \a index represents a directory;
    otherwise returns \c false.
*/
bool FileSystemModel::isDir(const QModelIndex &index) const
{
    // This function is for public usage only because it could create a file info
    if (!index.isValid())
        return true;
    FileSystemNode *n = d->node(index);
    if (n->hasInformation())
        return n->isDir();
    return fileInfo(index).isDir();
}

/*!
    Returns the size in bytes of \a index. If the file does not exist, 0 is returned.
  */
qint64 FileSystemModel::size(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;
    return d->node(index)->size();
}

/*!
    Returns the type of file \a index such as "Directory" or "JPEG file".
  */
QString FileSystemModel::type(const QModelIndex &index) const
{
    if (!index.isValid())
        return QString();
    return d->node(index)->type();
}

/*!
    Returns the date and time when \a index was last modified.
 */
QDateTime FileSystemModel::lastModified(const QModelIndex &index) const
{
    if (!index.isValid())
        return QDateTime();
    return d->node(index)->lastModified();
}

QModelIndex FileSystemModel::parent(const QModelIndex &index) const
{
    if (!d->indexValid(index))
        return QModelIndex();

    FileSystemNode *indexNode = d->node(index);
    Q_ASSERT(indexNode != nullptr);
    FileSystemNode *parentNode = indexNode->parent;
    if (parentNode == nullptr || parentNode == &d->root)
        return QModelIndex();

    // get the parent's row
    FileSystemNode *grandParentNode = parentNode->parent;
    Q_ASSERT(grandParentNode->children.contains(parentNode->fileName));
    int visualRow = d->translateVisibleLocation(grandParentNode,
                                                grandParentNode->visibleLocation(
                                                    grandParentNode->children.value(parentNode->fileName)->fileName));
    if (visualRow == -1)
        return QModelIndex();
    return createIndex(visualRow, 0, parentNode);
}


bool FileSystemModel::hasChildren(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return false;

    if (!parent.isValid()) // drives
        return true;

    const FileSystemNode *indexNode = d->node(parent);
    Q_ASSERT(indexNode);
    return (indexNode->isDir());
}

bool FileSystemModel::canFetchMore(const QModelIndex &parent) const
{
    if (!d->setRootPath)
        return false;
    const FileSystemNode *indexNode = d->node(parent);
    return (!indexNode->populatedChildren);
}

void FileSystemModel::fetchMore(const QModelIndex &parent)
{
    if (!d->setRootPath)
        return;
    FileSystemNode *indexNode = d->node(parent);
    if (indexNode->populatedChildren)
        return;
    indexNode->populatedChildren = true;
    if (useFileSystemWatcher())
        d->fileInfoGatherer.list(filePath(parent));
}

int FileSystemModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        return d->root.visibleChildren.count();

    const FileSystemNode *parentNode = d->node(parent);
    return parentNode->visibleChildren.count();
}

int FileSystemModel::columnCount(const QModelIndex &parent) const
{
    return (parent.column() > 0) ? 0 : FileSystemModelPrivate::NumColumns;
}

/*!
    Returns the data stored under the given \a role for the item "My Computer".

    \sa Qt::ItemDataRole
 */
QVariant FileSystemModel::myComputer(int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        return FileSystemModelPrivate::myComputer();
    case Qt::DecorationRole:
        if (useFileSystemWatcher())
            return d->fileInfoGatherer.iconProvider()->icon(QFileIconProvider::Computer);
    }
    return QVariant();
}

QVariant FileSystemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this)
        return QVariant();

    switch (role) {
    case Qt::EditRole:
        if (index.column() == 0)
            return d->name(index);
        Q_FALLTHROUGH();
    case Qt::DisplayRole:
        switch (index.column()) {
        case 0: return d->displayName(index);
        case 1: return d->size(index);
        case 2: return d->type(index);
        case 3: return d->time(index);
        default:
            qWarning("data: invalid display value column %d", index.column());
            break;
        }
        break;
    case FilePathRole:
        return filePath(index).toVariant();
    case FileNameRole:
        return d->name(index);
    case Qt::DecorationRole:
        if (index.column() == 0) {
            QIcon icon = d->icon(index);
            if (useFileSystemWatcher() && icon.isNull()) {
                if (d->node(index)->isDir())
                    icon = d->fileInfoGatherer.iconProvider()->icon(QFileIconProvider::Folder);
                else
                    icon = d->fileInfoGatherer.iconProvider()->icon(QFileIconProvider::File);
            }
            return icon;
        }
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == 1)
            return QVariant(Qt::AlignTrailing | Qt::AlignVCenter);
        break;
    case FilePermissions:
        int p = permissions(index);
        return p;
    }

    return QVariant();
}

bool FileSystemModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    /*if (!idx.isValid()
        || idx.column() != 0
        || role != Qt::EditRole
        || (flags(idx) & Qt::ItemIsEditable) == 0) {
        return false;
    }

    QString newName = value.toString();
    QString oldName = idx.data().toString();
    if (newName == oldName)
        return true;

    const FilePath parentPath = filePath(parent(idx));

    if (newName.isEmpty() || QDir::toNativeSeparators(newName).contains(QDir::separator()))
        return false;

    QStringList watchedPaths;
    if (useFileSystemWatcher() && HostOsInfo::isWindowsHost()) {
        // FIXME: Probably no more relevant
        // QTBUG-65683: Remove file system watchers prior to renaming to prevent
        // failure due to locked files on Windows.
         watchedPaths = d->unwatchPathsAt(idx);
    }
    if (!QDir(parentPath).rename(oldName, newName)) {
        if (useFileSystemWatcher() && HostOsInfo::isWindowsHost())
            d->watchPaths(watchedPaths);
        return false;
    } else {
        /*
            *After re-naming something we don't want the selection to change*
            - can't remove rows and later insert
            - can't quickly remove and insert
            - index pointer can't change because treeview doesn't use persistent index's

            - if this get any more complicated think of changing it to just
              use layoutChanged
         *

        FileSystemNode *indexNode = d->node(idx);
        FileSystemNode *parentNode = indexNode->parent;
        int visibleLocation = parentNode->visibleLocation(parentNode->children.value(indexNode->fileName)->fileName);

        const PathKey newNameKey{newName, indexNode->caseSensitivity()};
        const PathKey oldNameKey{oldName, indexNode->caseSensitivity()};
        parentNode->visibleChildren.removeAt(visibleLocation);
        QScopedPointer<FileSystemNode> nodeToRename(parentNode->children.take(oldNameKey));
        nodeToRename->fileName = newNameKey;
        nodeToRename->parent = parentNode;
        if (useFileSystemWatcher())
            nodeToRename->populate(d->fileInfoGatherer.getInfo(QFileInfo(parentPath, newName)));
        nodeToRename->isVisible = true;
        parentNode->children[newNameKey] = nodeToRename.take();
        parentNode->visibleChildren.insert(visibleLocation, newNameKey);

        d->delayedSort();
        emit fileRenamed(parentPath, oldName, newName);
    }
*/
    // FIXME: Implement setData
    return false;
}

QVariant FileSystemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    switch (role) {
    case Qt::DecorationRole:
        if (section == 0) {
            // ### TODO oh man this is ugly and doesn't even work all the way!
            // it is still 2 pixels off
            QImage pixmap(16, 1, QImage::Format_ARGB32_Premultiplied);
            pixmap.fill(Qt::transparent);
            return pixmap;
        }
        break;
    case Qt::TextAlignmentRole:
        return Qt::AlignLeft;
    }

    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractItemModel::headerData(section, orientation, role);

    QString returnValue;
    switch (section) {
    case 0: returnValue = tr("Name");
            break;
    case 1: returnValue = tr("Size");
            break;
    case 2: returnValue = HostOsInfo::isMacHost()
                    ? tr("Kind", "Match OS X Finder")
                    :tr("Type", "All other platforms");
           break;
    // Windows   - Type
    // OS X      - Kind
    // Konqueror - File Type
    // Nautilus  - Type
    case 3: returnValue = tr("Date Modified");
            break;
    default: return QVariant();
    }
    return returnValue;
}

Qt::ItemFlags FileSystemModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flags = QAbstractItemModel::flags(index);
    if (!index.isValid())
        return flags;

    FileSystemNode *indexNode = d->node(index);
    if (d->nameFilterDisables && !d->passNameFilters(indexNode)) {
        flags &= ~Qt::ItemIsEnabled;
        // ### TODO you shouldn't be able to set this as the current item, task 119433
        return flags;
    }

    flags |= Qt::ItemIsDragEnabled;
    if (d->readOnly)
        return flags;
    if ((index.column() == 0) && indexNode->permissions() & QFile::WriteUser) {
        flags |= Qt::ItemIsEditable;
        if (indexNode->isDir())
            flags |= Qt::ItemIsDropEnabled;
        else
            flags |= Qt::ItemNeverHasChildren;
    }
    return flags;
}


void FileSystemModel::sort(int column, Qt::SortOrder order)
{
    if (d->sortOrder == order && d->sortColumn == column && !d->forceSort)
        return;

    emit layoutAboutToBeChanged();
    QModelIndexList oldList = persistentIndexList();
    QList<QPair<FileSystemNode *, int>> oldNodes;
    const int nodeCount = oldList.count();
    oldNodes.reserve(nodeCount);
    for (int i = 0; i < nodeCount; ++i) {
        const QModelIndex &oldNode = oldList.at(i);
        QPair<FileSystemNode*, int> pair(d->node(oldNode), oldNode.column());
        oldNodes.append(pair);
    }

    if (!(d->sortColumn == column && d->sortOrder != order && !d->forceSort)) {
        //we sort only from where we are, don't need to sort all the model
        d->sortChildren(column, index(rootPath()));
        d->sortColumn = column;
        d->forceSort = false;
    }
    d->sortOrder = order;

    QModelIndexList newList;
    const int numOldNodes = oldNodes.size();
    newList.reserve(numOldNodes);
    for (int i = 0; i < numOldNodes; ++i) {
        const QPair<FileSystemNode*, int> &oldNode = oldNodes.at(i);
        newList.append(d->index(oldNode.first, oldNode.second));
    }
    changePersistentIndexList(oldList, newList);
    emit layoutChanged();
}

/*!
    Returns a list of MIME types that can be used to describe a list of items
    in the model.
*/
QStringList FileSystemModel::mimeTypes() const
{
    return QStringList(QLatin1String("text/uri-list"));
}

/*!
    Returns an object that contains a serialized description of the specified
    \a indexes. The format used to describe the items corresponding to the
    indexes is obtained from the mimeTypes() function.

    If the list of indexes is empty, \nullptr is returned rather than a
    serialized empty list.
*/
QMimeData *FileSystemModel::mimeData(const QModelIndexList &indexes) const
{
    // TODO: Implement
    /*QList<QUrl> urls;
    QList<QModelIndex>::const_iterator it = indexes.begin();
    for (; it != indexes.end(); ++it)
        if ((*it).column() == 0)
            urls << QUrl::fromLocalFile(filePath(*it));
    QMimeData *data = new QMimeData();
    data->setUrls(urls);
    return data;
    */
    return nullptr;
}

/*!
    Handles the \a data supplied by a drag and drop operation that ended with
    the given \a action over the row in the model specified by the \a row and
    \a column and by the \a parent index. Returns true if the operation was
    successful.

    \sa supportedDropActions()
*/
bool FileSystemModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                             int row, int column, const QModelIndex &parent)
{
    /*Q_UNUSED(row);
    Q_UNUSED(column);
    if (!parent.isValid() || isReadOnly())
        return false;

    bool success = true;
    QString to = filePath(parent) + QDir::separator();

    QList<QUrl> urls = data->urls();
    QList<QUrl>::const_iterator it = urls.constBegin();

    switch (action) {
    case Qt::CopyAction:
        for (; it != urls.constEnd(); ++it) {
            QString path = (*it).toLocalFile();
            success = QFile::copy(path, to + QFileInfo(path).fileName()) && success;
        }
        break;
    case Qt::LinkAction:
        for (; it != urls.constEnd(); ++it) {
            QString path = (*it).toLocalFile();
            success = QFile::link(path, to + QFileInfo(path).fileName()) && success;
        }
        break;
    case Qt::MoveAction:
        for (; it != urls.constEnd(); ++it) {
            QString path = (*it).toLocalFile();
            success = QFile::rename(path, to + QFileInfo(path).fileName()) && success;
        }
        break;
    default:
        return false;
    }

    return success;
    */
    // TODO: Implement
    return false;
}

Qt::DropActions FileSystemModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
}

QHash<int, QByteArray> FileSystemModel::roleNames() const
{
    auto ret = QAbstractItemModel::roleNames();
    ret.insert(FileSystemModel::FileIconRole,
               QByteArrayLiteral("fileIcon")); // == Qt::decoration
    ret.insert(FileSystemModel::FilePathRole, QByteArrayLiteral("filePath"));
    ret.insert(FileSystemModel::FileNameRole, QByteArrayLiteral("fileName"));
    ret.insert(FileSystemModel::FilePermissions, QByteArrayLiteral("filePermissions"));
    return ret;
}

/*!
    \enum FileSystemModel::Option
    \since 5.14

    \value DontWatchForChanges Do not add file watchers to the paths.
    This reduces overhead when using the model for simple tasks
    like line edit completion.

    \value DontResolveSymlinks Don't resolve symlinks in the file
    system model. By default, symlinks are resolved.

    \value DontUseCustomDirectoryIcons Always use the default directory icon.
    Some platforms allow the user to set a different icon. Custom icon lookup
    causes a big performance impact over network or removable drives.
    This sets the QFileIconProvider::DontUseCustomDirectoryIcons
    option in the icon provider accordingly.

    \sa resolveSymlinks
*/

/*!
    \since 5.14
    Sets the given \a option to be enabled if \a on is true; otherwise,
    clears the given \a option.

    Options should be set before changing properties.

    \sa options, testOption()
*/
void FileSystemModel::setOption(Option option, bool on)
{
    FileSystemModel::Options previousOptions = options();
    setOptions(previousOptions.setFlag(option, on));
}

/*!
    \since 5.14

    Returns \c true if the given \a option is enabled; otherwise, returns
    false.

    \sa options, setOption()
*/
bool FileSystemModel::testOption(Option option) const
{
    return options().testFlag(option);
}

/*!
    \property FileSystemModel::options
    \brief the various options that affect the model
    \since 5.14

    By default, all options are disabled.

    Options should be set before changing properties.

    \sa setOption(), testOption()
*/
void FileSystemModel::setOptions(Options options)
{
    const Options changed = (options ^ FileSystemModel::options());

    if (changed.testFlag(DontResolveSymlinks))
        setResolveSymlinks(!options.testFlag(DontResolveSymlinks));

    if (useFileSystemWatcher() && changed.testFlag(DontWatchForChanges))
        d->fileInfoGatherer.setWatching(!options.testFlag(DontWatchForChanges));

    if (changed.testFlag(DontUseCustomDirectoryIcons)) {
        if (auto provider = iconProvider()) {
            QFileIconProvider::Options providerOptions = provider->options();
            providerOptions.setFlag(QFileIconProvider::DontUseCustomDirectoryIcons,
                                    options.testFlag(FileSystemModel::DontUseCustomDirectoryIcons));
            provider->setOptions(providerOptions);
        } else {
            qWarning("Setting FileSystemModel::DontUseCustomDirectoryIcons has no effect when no provider is used");
        }
    }
}

FileSystemModel::Options FileSystemModel::options() const
{
    FileSystemModel::Options result;
    result.setFlag(DontResolveSymlinks, !resolveSymlinks());
    if (useFileSystemWatcher())
        result.setFlag(DontWatchForChanges, !d->fileInfoGatherer.isWatching());
    else
        result.setFlag(DontWatchForChanges);
    if (auto provider = iconProvider()) {
        result.setFlag(DontUseCustomDirectoryIcons,
                       provider->options().testFlag(QFileIconProvider::DontUseCustomDirectoryIcons));
    }
    return result;
}

/*!
    Returns the path of the item stored in the model under the
    \a index given.
*/
FilePath FileSystemModel::filePath(const QModelIndex &index) const
{
    return d->filePath(index);
    /*
    QString fullPath = d->filePath(index);
    FileSystemNode *dirNode = d->node(index);

    //TODO: FIXME: Support Sym links ?
    if (dirNode->isSymLink()
        && d->fileInfoGatherer.resolveSymlinks()
        && d->resolvedSymLinks.contains(fullPath)
        && dirNode->isDir()) {
        QFileInfo fullPathInfo(dirNode->fileInfo());
        if (!dirNode->hasInformation())
            fullPathInfo = QFileInfo(fullPath);
        QString canonicalPath = fullPathInfo.canonicalFilePath();
        auto *canonicalNode = d->node(fullPathInfo.canonicalFilePath(), false);
        QFileInfo resolvedInfo = canonicalNode->fileInfo();
        if (!canonicalNode->hasInformation())
            resolvedInfo = QFileInfo(canonicalPath);
        if (resolvedInfo.exists())
            return resolvedInfo.filePath();
    }
    return fullPath;
    */
}


/*!
    Create a directory with the \a name in the \a parent model index.
*/
QModelIndex FileSystemModel::mkdir(const QModelIndex &parent, const QString &name)
{
    if (!parent.isValid())
        return parent;
    // TODO: Implement
    /*
    QDir dir(filePath(parent));
    if (!dir.mkdir(name))
        return QModelIndex();
    FileSystemNode *parentNode = d->node(parent);
    PathKey nameKey(name, parentNode->caseSensitivity());
    d->addNode(parentNode, nameKey, QFileInfo());
    Q_ASSERT(parentNode->children.contains(nameKey));
    FileSystemNode *node = parentNode->children[nameKey];
    if (useFileSystemWatcher())
        node->populate(d->fileInfoGatherer.getInfo(QFileInfo(dir.absolutePath() + QDir::separator() + name)));
    d->addVisibleFiles(parentNode, {PathKey(name, parentNode->caseSensitivity())});
    return d->index(node);
    */
}

/*!
    Returns the complete OR-ed together combination of QFile::Permission for the \a index.
 */
QFile::Permissions FileSystemModel::permissions(const QModelIndex &index) const
{
    return d->node(index)->permissions();
}

/*!
    Sets the directory that is being watched by the model to \a newPath by
    installing a \l{QFileSystemWatcher}{file system watcher} on it. Any
    changes to files and directories within this directory will be
    reflected in the model.

    If the path is changed, the rootPathChanged() signal will be emitted.

    \note This function does not change the structure of the model or
    modify the data available to views. In other words, the "root" of
    the model is \e not changed to include only files and directories
    within the directory specified by \a newPath in the file system.
  */
QModelIndex FileSystemModel::setRootPath(const FilePath &newPath)
{
    //QString longNewPath = qtc_GetLongPathName(newPath);
    //we remove .. and . from the given path if exist
    // TODO: FIXME ? Use FilePath to clean path ??
    //if (!newPath.isEmpty())
    //    longNewPath = QDir::cleanPath(longNewPath);

    d->setRootPath = true;

    //user don't ask for the root path ("") but the conversion failed
    //if (!newPath.isEmpty() && longNewPath.isEmpty())
    //    return d->index(rootPath());

    if (d->rootDir == newPath)
        return d->index(rootPath());

    auto node = d->node(newPath);
    QFileInfo newPathInfo;
    if (node && node->hasInformation())
        newPathInfo = node->fileInfo();
    else
        newPathInfo = QFileInfo(newPath.toString());

    bool showDrives = (newPath.isEmpty() || newPath.toString() == FileSystemModelPrivate::myComputer());
    if (!showDrives && !newPathInfo.exists())
        return d->index(rootPath());

    //We remove the watcher on the previous path
    if (!rootPath().isEmpty() && rootPath().toString() != QLatin1String(".")) {
        //This remove the watcher for the old rootPath
        if (useFileSystemWatcher())
            d->fileInfoGatherer.removePath(rootPath());
        //This line "marks" the node as dirty, so the next fetchMore
        //call on the path will ask the gatherer to install a watcher again
        //But it doesn't re-fetch everything
        d->node(rootPath())->populatedChildren = false;
    }

    // We have a new valid root path
    d->rootDir = newPath;
    QModelIndex newRootIndex;
    if (showDrives) {
        // otherwise dir will become '.'
        d->rootDir.setPath(QLatin1String(""));
    } else {
        newRootIndex = d->index(d->rootDir);
    }
    fetchMore(newRootIndex);
    emit rootPathChanged(d->rootDir.toString());
    d->forceSort = true;
    d->delayedSort();
    return newRootIndex;
}

/*!
    The currently set root path

    \sa rootDirectory()
*/
FilePath FileSystemModel::rootPath() const
{
    return d->rootDir;
}

/*!
    The currently set directory

    \sa rootPath()
*/
QDir FileSystemModel::rootDirectory() const
{
    // FIXME: Get rid of rootDirectory / don't return QDir
    if (d->rootDir.needsDevice()) {
        return QDir();
    }
    QDir dir(d->rootDir.toDir());
    dir.setNameFilters(nameFilters());
    dir.setFilter(filter());
    return dir;
}

/*!
    Sets the \a provider of file icons for the directory model.
*/
void FileSystemModel::setIconProvider(QFileIconProvider *provider)
{
    if (useFileSystemWatcher())
        d->fileInfoGatherer.setIconProvider(provider);
    d->root.updateIcon(provider, QString());
}

/*!
    Returns the file icon provider for this directory model.
*/
QFileIconProvider *FileSystemModel::iconProvider() const
{
    if (useFileSystemWatcher()) {
        return d->fileInfoGatherer.iconProvider();
    }
    return nullptr;
}

/*!
    Sets the directory model's filter to that specified by \a filters.

    Note that the filter you set should always include the QDir::AllDirs enum value,
    otherwise FileSystemModel won't be able to read the directory structure.

    \sa QDir::Filters
*/
void FileSystemModel::setFilter(QDir::Filters filters)
{
    if (d->filters == filters)
        return;
    const bool changingCaseSensitivity =
        filters.testFlag(QDir::CaseSensitive) != d->filters.testFlag(QDir::CaseSensitive);
    d->filters = filters;
    if (changingCaseSensitivity)
        d->rebuildNameFilterRegexps();
    d->forceSort = true;
    d->delayedSort();
}

/*!
    Returns the filter specified for the directory model.

    If a filter has not been set, the default filter is QDir::AllEntries |
    QDir::NoDotAndDotDot | QDir::AllDirs.

    \sa QDir::Filters
*/
QDir::Filters FileSystemModel::filter() const
{
    return d->filters;
}

/*!
    \property FileSystemModel::resolveSymlinks
    \brief Whether the directory model should resolve symbolic links

    This is only relevant on Windows.

    By default, this property is \c true.

    \sa FileSystemModel::Options
*/
void FileSystemModel::setResolveSymlinks(bool enable)
{
    if (useFileSystemWatcher()) {
        d->fileInfoGatherer.setResolveSymlinks(enable);
    }
}

bool FileSystemModel::resolveSymlinks() const
{
    if (useFileSystemWatcher()) {
        return d->fileInfoGatherer.resolveSymlinks();
    }
    return false;
}

/*!
    \property FileSystemModel::readOnly
    \brief Whether the directory model allows writing to the file system

    If this property is set to false, the directory model will allow renaming, copying
    and deleting of files and directories.

    This property is \c true by default
*/
void FileSystemModel::setReadOnly(bool enable)
{
    d->readOnly = enable;
}

bool FileSystemModel::isReadOnly() const
{
    return d->readOnly;
}

/*!
    \property FileSystemModel::nameFilterDisables
    \brief Whether files that don't pass the name filter are hidden or disabled

    This property is \c true by default
*/
void FileSystemModel::setNameFilterDisables(bool enable)
{
    if (d->nameFilterDisables == enable)
        return;
    d->nameFilterDisables = enable;
    d->forceSort = true;
    d->delayedSort();
}

bool FileSystemModel::nameFilterDisables() const
{
    return d->nameFilterDisables;
}

/*!
    Sets the name \a filters to apply against the existing files.
*/
void FileSystemModel::setNameFilters(const QStringList &filters)
{
    if (!d->bypassFilters.isEmpty()) {
        // update the bypass filter to only bypass the stuff that must be kept around
        d->bypassFilters.clear();
        // We guarantee that rootPath will stick around
        QPersistentModelIndex root(index(rootPath()));
        const QModelIndexList persistentList = persistentIndexList();
        for (const auto &persistentIndex : persistentList) {
            FileSystemNode *node = d->node(persistentIndex);
            while (node) {
                if (d->bypassFilters.contains(node))
                    break;
                if (node->isDir())
                    d->bypassFilters[node] = true;
                node = node->parent;
            }
        }
    }

    d->nameFilters = filters;
    d->rebuildNameFilterRegexps();
    d->forceSort = true;
    d->delayedSort();
}

/*!
    Returns a list of filters applied to the names in the model.
*/
QStringList FileSystemModel::nameFilters() const
{
    return d->nameFilters;
}

bool FileSystemModel::event(QEvent *event)
{
    if (useFileSystemWatcher() && event->type() == QEvent::LanguageChange) {
        d->root.retranslateStrings(d->fileInfoGatherer.iconProvider(), QString());
        return true;
    }
    return QAbstractItemModel::event(event);
}

bool FileSystemModel::rmdir(const QModelIndex &aindex)
{
    FilePath path = filePath(aindex);
    const bool success = path.removeRecursively();
    if (useFileSystemWatcher() && success) {
        d->fileInfoGatherer.removePath(path);
    }
    return success;
}

QString FileSystemModel::fileName(const QModelIndex &aindex) const
{
    return aindex.data(Qt::DisplayRole).toString();
}

QIcon FileSystemModel::fileIcon(const QModelIndex &aindex) const
{
    return qvariant_cast<QIcon>(aindex.data(Qt::DecorationRole));
}

}

//#include "moc_filesystemmodel.cpp"
//#include "filesystemmodel.moc"
