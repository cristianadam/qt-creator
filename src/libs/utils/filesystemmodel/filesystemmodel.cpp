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

#include <utils/runextensions.h>

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
#include <thread>

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

FileSystemModel::FileSystemModel(QObject *parent)
{

}

FileSystemModel::~FileSystemModel()
{
}

QModelIndex FileSystemModel::index(int row, int column, const QModelIndex &parent) const
{
    QTC_ASSERT(column == 0, return {});
    QTC_ASSERT(row >= 0, return {});

    if (!parent.isValid()) { //(parent.constInternalPointer() == nullptr) {
        QTC_ASSERT(row < m_knownRoots.size(), return {});
        return createIndex(row, column, &m_knownRoots.at(row));
    }

    const FilePaths& list = getFilePathMapEntry(parent);
    QTC_ASSERT(row < list.length(), return {});

    return createIndex(row, column, &list.at(row));
}

QModelIndex FileSystemModel::index(const FilePath &path, int column) const
{
    QTC_ASSERT(column == 0, return {});

    FilePath actualPath = path;
    if (path.isEmpty())
        actualPath = FilePath::fromString(QDir(".").absolutePath());

    const FilePath parentPath = actualPath.parentDir();
    if (parentPath.isEmpty()) {
        qsizetype idx = m_knownRoots.indexOf(actualPath);
        return createIndex(idx, 0, &m_knownRoots.at(idx));
    }

    const FilePaths& list = getFilePathMapEntry(parentPath);

    const qsizetype idx = list.indexOf(actualPath);
    QTC_ASSERT(idx != -1, return {});

    return createIndex(idx, 0, &list.at(idx));
}

QModelIndex FileSystemModel::parent(const QModelIndex &child) const
{
    QTC_ASSERT(child.isValid() && child.constInternalPointer(), return {});

    const FilePath& parentPath = filePathFromIndex(child).parentDir();
    if(parentPath.isEmpty())
        return {}; //createIndex(0, 0, nullptr);
    return index(parentPath, 0);
}

QModelIndex FileSystemModel::sibling(int row, int column, const QModelIndex &idx) const
{
    QTC_ASSERT(idx.isValid(), return {});
    QTC_ASSERT(row >= 0, return {});
    QTC_ASSERT(column == 0, return {});

    const FilePath parentPath = filePathFromIndex(idx).parentDir();

    if(parentPath.isEmpty()) {
        QTC_ASSERT(row < m_knownRoots.length(), return {});
        return createIndex(row, column, &m_knownRoots.at(row));
    }

    return index(row, column, index(parentPath));
}

bool FileSystemModel::hasChildren(const QModelIndex &parent) const
{
    //QTC_ASSERT(parent.isValid(), return false);
    if (!parent.isValid())
        return m_knownRoots.length() > 0;

    //if (parent.constInternalPointer() == nullptr && parent.row() == 0 && parent.column() == 0)
    //    return m_knownRoots.length() > 0;

    return !getFilePathMapEntry(parent).isEmpty(); // hasPathMapEntry(parent);
}

bool FileSystemModel::canFetchMore(const QModelIndex &parent) const
{
    //QTC_ASSERT(parent.isValid(), return false);

    if (!parent.isValid())//(parent.constInternalPointer() == nullptr)
        return false;

    return !hasPathMapEntry(parent);
}

void FileSystemModel::fetchMore(const QModelIndex &parent)
{
    if (parent.isValid())
        return;

    // if (parent.constInternalPointer() == nullptr)
    //     return;

    getFilePathMapEntryOrCreate(parent);
}

int FileSystemModel::rowCount(const QModelIndex &parent) const
{
    // QTC_ASSERT(parent.isValid(), return 0);

    if (!parent.isValid())//(parent.constInternalPointer() == nullptr)
        return m_knownRoots.length(); // TODO: Replace "length" with "size" everywhere!

    const FilePaths& list = getFilePathMapEntry(parent);

    return list.size();
}

int FileSystemModel::columnCount(const QModelIndex &parent) const
{
    //QTC_ASSERT(parent.isValid(), return 0);
    return 1;
}

QVariant FileSystemModel::myComputer(int role) const
{
    return "";
}

QVariant FileSystemModel::data(const QModelIndex &index, int role) const
{
    QTC_ASSERT(index.isValid(), return {});

    const FilePath& path = filePathFromIndex(index);

    switch(role) {
    case Qt::DisplayRole: {
        return path.fileName();
    }
    case Qt::DecorationRole:
    {
        if (index.column() == 0 && iconProvider()) {
            QIcon icon;
            if (!path.needsDevice())
               icon = iconProvider()->icon(path.toFileInfo());

            if (icon.isNull()) {
                if (isDir(index))
                    icon = iconProvider()->icon(QAbstractFileIconProvider::Folder);
                else
                    icon = iconProvider()->icon(QAbstractFileIconProvider::File);
            }
            return icon;
        }
        break;
    }
    case FilePathRole:
        return path.absoluteFilePath().toString();
    case FileNameRole:
        return path.fileName();
    case FilePermissions:
        return path.permissions().toInt();
    }

    return QVariant();
}

bool FileSystemModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    return false;
}

QVariant FileSystemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    return {};
}

Qt::ItemFlags FileSystemModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flags = QAbstractItemModel::flags(index);

    return flags;
}

void FileSystemModel::sort(int column, Qt::SortOrder order)
{

}

QStringList FileSystemModel::mimeTypes() const
{
    return {};
}

QMimeData *FileSystemModel::mimeData(const QModelIndexList &indexes) const
{
    return {};
}

bool FileSystemModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    return false;
}

Qt::DropActions FileSystemModel::supportedDropActions() const
{
    return Qt::IgnoreAction;
}

QHash<int, QByteArray> FileSystemModel::roleNames() const
{
    return {};
}

QModelIndex FileSystemModel::setRootPath(FilePath path)
{
    if (path.isEmpty())
        path = FilePath(".").absoluteFilePath();

    if (!path.isAbsolutePath())
        path = path.absolutePath();

    m_rootFilePath = path;
    rootPathChanged(path.toString());

    const FilePath parentPath = path.parentDir();

    if (!m_knownRoots.contains(path.rootDir())) {
        // TODO: QSortModel crashes here on startup, why ??
        beginInsertRows({}, m_knownRoots.length(), m_knownRoots.length());
        m_knownRoots.append(path.rootDir());
        endInsertRows();
    }

    if (parentPath.isEmpty()) {
        qsizetype idx = m_knownRoots.indexOf(path);
        QTC_ASSERT(idx >= 0, return {});

        return createIndex(idx, 0, &m_knownRoots.at(idx));
    }

    return index(path, 0);
}

FilePath FileSystemModel::rootPath() const
{
    return m_rootFilePath;
}

void FileSystemModel::setIconProvider(QFileIconProvider *provider)
{
    m_iconProvider = provider;
}

QFileIconProvider *FileSystemModel::iconProvider() const
{
    return m_iconProvider;
}

void FileSystemModel::setFilter(QDir::Filters filters)
{
    m_dirFilters = filters;
    beginResetModel();
    m_filePathMap.clear();
    endResetModel();
}

QDir::Filters FileSystemModel::filter() const
{
    return m_dirFilters;
}

void FileSystemModel::setResolveSymlinks(bool enable)
{

}

bool FileSystemModel::resolveSymlinks() const
{
    return false;
}

void FileSystemModel::setReadOnly(bool enable)
{

}

bool FileSystemModel::isReadOnly() const
{
    return true;
}

void FileSystemModel::setNameFilterDisables(bool enable)
{

}

bool FileSystemModel::nameFilterDisables() const
{
    return false;
}

void FileSystemModel::setNameFilters(const QStringList &filters)
{

}

QStringList FileSystemModel::nameFilters() const
{
    return {};
}

void FileSystemModel::setOption(Option option, bool on)
{

}

bool FileSystemModel::testOption(Option option) const
{

}

void FileSystemModel::setOptions(Options options)
{

}

FileSystemModel::Options FileSystemModel::options() const
{
    return {};
}

FilePath FileSystemModel::filePath(const QModelIndex &index) const
{
    QTC_ASSERT(index.isValid(), return {});
    return filePathFromIndex(index);
}

bool FileSystemModel::isDir(const QModelIndex &index) const
{
    QTC_ASSERT(index.isValid(), return {});

    return const_cast<FileSystemModel*>(this)->getCachedInfo(filePathFromIndex(index)).isDir;

    //return filePathFromIndex(index).isDir();
}

qint64 FileSystemModel::size(const QModelIndex &index) const
{
    QTC_ASSERT(index.isValid(), return {});
    return filePathFromIndex(index).fileSize();
}

QString FileSystemModel::type(const QModelIndex &index) const
{
    QTC_ASSERT(index.isValid(), return {});
    if (isDir(index))
        return "Directory";
    else
        return "File";
    return "";
}

QDateTime FileSystemModel::lastModified(const QModelIndex &index) const
{
    QTC_ASSERT(index.isValid(), return {});
    return filePathFromIndex(index).lastModified();
}

QModelIndex FileSystemModel::mkdir(const QModelIndex &parent, const QString &name)
{
    return {};
}

bool FileSystemModel::rmdir(const QModelIndex &index)
{
    return false;
}

QString FileSystemModel::fileName(const QModelIndex &index) const
{
    QTC_ASSERT(index.isValid(), return {});
    return filePathFromIndex(index).fileName();
}

QIcon FileSystemModel::fileIcon(const QModelIndex &aindex) const
{
    return {};
}

QFileDevice::Permissions FileSystemModel::permissions(const QModelIndex &index) const
{
    QTC_ASSERT(index.isValid(), return {});
    return filePathFromIndex(index).permissions();
}

QFileInfo FileSystemModel::fileInfo(const QModelIndex &index) const
{
    QTC_ASSERT(index.isValid(), return {});
    return filePathFromIndex(index).toFileInfo();
}

bool FileSystemModel::remove(const QModelIndex &index)
{
    return false;
}

void FileSystemModel::timerEvent(QTimerEvent *event)
{

}

bool FileSystemModel::event(QEvent *event)
{
    return false;
}

const FilePaths &FileSystemModel::getFilePathMapEntryOrCreate(const QModelIndex &index)
{
    static QList<FilePath> emptyList;
    if (!index.isValid())
        return emptyList;

    return getFilePathMapEntryOrCreate(filePathFromIndex(index));
}

const FilePaths &FileSystemModel::getFilePathMapEntryOrCreate(const FilePath &parentPath)
{
    static QList<FilePath> emptyList;

    if (parentPath.isEmpty())
        return emptyList;

    auto it = m_filePathMap.find(parentPath);
    if (it == m_filePathMap.end()) {
        if (parentPath.isDir()) {
            const auto newList = parentPath.dirEntries(m_dirFilters);
            it = m_filePathMap.insert(parentPath, newList);
        }
    }

    if (it == m_filePathMap.end())
        return emptyList;

    this->directoryLoaded(parentPath.toString());

    return it.value();
}

bool FileSystemModel::hasPathMapEntry(const QModelIndex &index) const
{
    return hasPathMapEntry(filePathFromIndex(index));
}

bool FileSystemModel::hasPathMapEntry(const FilePath &path) const
{
    return m_filePathMap.contains(path);
}

const FilePath &FileSystemModel::filePathFromIndex(const QModelIndex &index) const
{
    static FilePath emptyPath;

    if (!index.constInternalPointer()) {
        QTC_ASSERT(index.row() < m_knownRoots.size(), return emptyPath);
        return m_knownRoots.at(index.row());
    }

    return *(static_cast<const Utils::FilePath*>(index.constInternalPointer()));
}

const FileSystemModel::FilePathCache FileSystemModel::getCachedInfo(const FilePath &path)
{
    auto it = m_filePathCache.find(path);
    if (it != m_filePathCache.end()) {
        return *it;
    }

    FilePathCache cachedData;
    cachedData.isDir = path.isDir();

    m_filePathCache.insert(path, cachedData);

    return cachedData;
}

const FilePaths &FileSystemModel::getFilePathMapEntry(const FilePath& parentPath) const
{
    auto pThis = const_cast<FileSystemModel*>(this);
    return pThis->getFilePathMapEntryOrCreate(parentPath);

    /*static FilePaths emptyList;
    auto it = m_filePathMap.find(parentPath);

    QTC_ASSERT(it != m_filePathMap.end(), return emptyList);

    return it.value();
    */

    /*if (it == m_filePathMap.end()) {
        Utils::runAsync([this, parentPath]() {
            auto pthis = const_cast<FileSystemModel*>(this);
            std::unique_lock<QMutex> lk(m_filePathMapMutex);
            const auto newList = parentPath.dirEntries(m_dirFilters);
            pthis->beginInsertRows(
            m_filePathMap.insert(parentPath, newList);
        });
    }*/
}

const FilePaths &FileSystemModel::getFilePathMapEntry(const QModelIndex& index) const
{
    static FilePaths emptyList;
    QTC_ASSERT(index.isValid(), return emptyList);
    return getFilePathMapEntry(filePathFromIndex(index));
}

}

//#include "moc_filesystemmodel.cpp"
//#include "filesystemmodel.moc"
