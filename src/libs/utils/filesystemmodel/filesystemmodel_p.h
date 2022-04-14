#pragma once

#include "filesystemmodel.h"
#include "filesystemnode.h"
#include "filesystemmodelslots.h"
#include "fileinfogatherer.h"
#include "pathkey.h"

#include <QDir>
#include <QModelIndex>
#include <QTimer>

namespace Utils {

class FileSystemModelPrivate
{
public:
    enum { NumColumns = 4 };

    FileSystemModelPrivate(FileSystemModel *q) : q(q), mySlots(this, q)
    {
        init();
    }

    inline bool indexValid(const QModelIndex &index) const {
         return (index.row() >= 0) && (index.column() >= 0) && (index.model() == q);
    }

    void init();
    // Return true if index which is owned by node is hidden by the filter.
    bool isHiddenByFilter(FileSystemNode *indexNode, const QModelIndex &index) const
    {
       return (indexNode != &root && !index.isValid());
    }
    FileSystemNode *node(const QModelIndex &index) const;
    FileSystemNode *node(const FilePath &path, bool fetch = true) const;
    inline QModelIndex index(const FilePath &path, int column = 0) { return index(node(path), column); }
    QModelIndex index(const FileSystemNode *node, int column = 0) const;
    bool filtersAcceptsNode(const FileSystemNode *node) const;
    bool passNameFilters(const FileSystemNode *node) const;
    void removeNode(FileSystemNode *parentNode, const PathKey &name);
    FileSystemNode *addNode(FileSystemNode *parentNode, const PathKey &fileName, const QFileInfo &info);
    void addVisibleFiles(FileSystemNode *parentNode, const PathKeys &newFiles);
    void removeVisibleFile(FileSystemNode *parentNode, int visibleLocation);
    void sortChildren(int column, const QModelIndex &parent);

    inline int translateVisibleLocation(FileSystemNode *parent, int row) const {
        if (sortOrder != Qt::AscendingOrder) {
            if (parent->dirtyChildrenIndex == -1)
                return parent->visibleChildren.count() - row - 1;

            if (row < parent->dirtyChildrenIndex)
                return parent->dirtyChildrenIndex - row - 1;
        }

        return row;
    }

    static QString myComputer()
    {
        // ### TODO We should query the system to find out what the string should be
        // XP == "My Computer",
        // Vista == "Computer",
        // OS X == "Computer" (sometime user generated) "Benjamin's PowerBook G4"
        if (HostOsInfo::isWindowsHost())
            return FileSystemModel::tr("My Computer");
        return FileSystemModel::tr("Computer");
    }

    inline void delayedSort() {
        if (!delayedSortTimer.isActive())
            delayedSortTimer.start(0);
    }

    QIcon icon(const QModelIndex &index) const;
    QString name(const QModelIndex &index) const;
    QString displayName(const QModelIndex &index) const;
    FilePath filePath(const QModelIndex &index) const;
    QString size(const QModelIndex &index) const;
    static QString size(qint64 bytes);
    QString type(const QModelIndex &index) const;
    QString time(const QModelIndex &index) const;

    void _q_directoryChanged(const QString &directory, const QStringList &list);
    void _q_performDelayedSort();
    void _q_fileSystemChanged(const QString &path, const QList<QPair<QString, QFileInfo>> &);
    void _q_resolvedName(const QString &fileName, const QString &resolvedName);

    FileSystemModel * const q;

    FilePath rootDir;
    //Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive; // FIXME: Set properly

    // Next two used on Windows only
    FilePaths unwatchPathsAt(const QModelIndex &);
    void watchPaths(const FilePaths &paths) { fileInfoGatherer.watchPaths(paths); }

    FileInfoGatherer fileInfoGatherer;
    FileSystemModelSlots mySlots;

    QTimer delayedSortTimer;
    QHash<const FileSystemNode*, bool> bypassFilters;
    QStringList nameFilters;
    std::vector<QRegularExpression> nameFiltersRegexps;
    void rebuildNameFilterRegexps();

    QHash<QString, QString> resolvedSymLinks;

    FileSystemNode root;

    struct Fetching {
        FilePath path;
        const FileSystemNode *node;
    };
    QList<Fetching> toFetch;

    QBasicTimer fetchingTimer;

    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs;
    int sortColumn = 0;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
    bool forceSort = true;
    bool readOnly = true;
    bool setRootPath = false;
    bool nameFilterDisables = true; // false on windows, true on mac and unix
    // This flag is an optimization for QFileDialog. It enables a sort which is
    // not recursive, meaning we sort only what we see.
    bool disableRecursiveSort = false;
};

}
