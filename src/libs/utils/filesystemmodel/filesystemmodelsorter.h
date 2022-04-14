#pragma once

#include "filesystemnode.h"

#include "../hostosinfo.h"

#include <QCollator>

namespace Utils {
/*
    \internal
    Helper functor used by sort()
*/
class QFileSystemModelSorter
{
public:
    inline QFileSystemModelSorter(int column) : sortColumn(column)
    {
        naturalCompare.setNumericMode(true);
        naturalCompare.setCaseSensitivity(Qt::CaseInsensitive);
    }

    bool compareNodes(const FileSystemNode *l, const FileSystemNode *r) const
    {
        switch (sortColumn) {
        case 0: {
            if (!HostOsInfo::isMacHost()) {
                // place directories before files
                bool left = l->isDir();
                bool right = r->isDir();
                if (left ^ right)
                    return left;
            }
            return naturalCompare.compare(l->fileName.data, r->fileName.data) < 0;
        }
        case 1:
        {
            // Directories go first
            bool left = l->isDir();
            bool right = r->isDir();
            if (left ^ right)
                return left;

            qint64 sizeDifference = l->size() - r->size();
            if (sizeDifference == 0)
                return naturalCompare.compare(l->fileName.data, r->fileName.data) < 0;

            return sizeDifference < 0;
        }
        case 2:
        {
            int compare = naturalCompare.compare(l->type(), r->type());
            if (compare == 0)
                return naturalCompare.compare(l->fileName.data, r->fileName.data) < 0;

            return compare < 0;
        }
        case 3:
        {
            if (l->lastModified() == r->lastModified())
                return naturalCompare.compare(l->fileName.data, r->fileName.data) < 0;

            return l->lastModified() < r->lastModified();
        }
        }
        Q_ASSERT(false);
        return false;
    }

    bool operator()(const FileSystemNode *l, const FileSystemNode *r) const
    {
        return compareNodes(l, r);
    }

private:
    QCollator naturalCompare;
    int sortColumn;
};

}
