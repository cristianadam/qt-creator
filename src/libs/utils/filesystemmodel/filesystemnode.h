#pragma once

#include "extendedinformation.h"
#include "pathkey.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QIcon>
#include <QString>

namespace Utils {

class FileSystemNode
{
public:
    Q_DISABLE_COPY_MOVE(FileSystemNode)

    explicit FileSystemNode(const PathKey &filename = {}, FileSystemNode *p = nullptr)
        : fileName(filename), parent(p)
    {}

    ~FileSystemNode() {
        qDeleteAll(children);
        delete info;
    }

    PathKey fileName;
    QString volumeName; // Windows only

    inline qint64 size() const { if (info && !info->isDir()) return info->size(); return 0; }
    inline QString type() const { if (info) return info->displayType; return QLatin1String(""); }
    inline QDateTime lastModified() const { if (info) return info->lastModified(); return QDateTime(); }
    inline QFile::Permissions permissions() const { if (info) return info->permissions(); return { }; }
    inline bool isReadable() const { return ((permissions() & QFile::ReadUser) != 0); }
    inline bool isWritable() const { return ((permissions() & QFile::WriteUser) != 0); }
    inline bool isExecutable() const { return ((permissions() & QFile::ExeUser) != 0); }
    inline bool isDir() const {
        if (info)
            return info->isDir();
        if (children.count() > 0)
            return true;
        return false;
    }
    inline QFileInfo fileInfo() const { if (info) return info->fileInfo(); return QFileInfo(); }
    inline bool isFile() const { if (info) return info->isFile(); return true; }
    inline bool isSystem() const { if (info) return info->isSystem(); return true; }
    inline bool isHidden() const { if (info) return info->isHidden(); return false; }
    inline bool isSymLink(bool ignoreNtfsSymLinks = false) const { return info && info->isSymLink(ignoreNtfsSymLinks); }
    inline bool caseSensitive() const { return fileName.caseSensitivity == Qt::CaseSensitive; }
    inline Qt::CaseSensitivity caseSensitivity() const { return fileName.caseSensitivity; }
    inline QIcon icon() const { if (info) return info->icon; return QIcon(); }

    friend bool operator<(const FileSystemNode &a, const FileSystemNode &b) {
        return a.fileName < b.fileName;
    }
    friend bool operator>(const FileSystemNode &a, const FileSystemNode &b) {
        return a.fileName > b.fileName;
    }
    friend bool operator==(const FileSystemNode &a, const FileSystemNode &b) {
        return a.fileName == b.fileName;
    }

    friend bool operator!=(const FileSystemNode &a, const ExtendedInformation &fileInfo) {
        return !(a == fileInfo);
    }
    friend bool operator==(const FileSystemNode &a, const ExtendedInformation &fileInfo) {
        return a.info && (*a.info == fileInfo);
    }

    inline bool hasInformation() const { return info != nullptr; }

    void populate(const ExtendedInformation &fileInfo) {
        if (!info)
            info = new ExtendedInformation();
        (*info) = fileInfo;
    }

    // children shouldn't normally be accessed directly, use node()
    inline int visibleLocation(const PathKey &childName) {
        return visibleChildren.indexOf(childName);
    }
    void updateIcon(QFileIconProvider *iconProvider, const QString &path) {
        if (info)
            info->icon = iconProvider->icon(QFileInfo(path));
        for (FileSystemNode *child : qAsConst(children)) {
            //On windows the root (My computer) has no path so we don't want to add a / for nothing (e.g. /C:/)
            if (!path.isEmpty()) {
                if (path.endsWith(QLatin1Char('/')))
                    child->updateIcon(iconProvider, path + child->fileName.data);
                else
                    child->updateIcon(iconProvider, path + QLatin1Char('/') + child->fileName.data);
            } else
                child->updateIcon(iconProvider, child->fileName.data);
        }
    }

    void retranslateStrings(QFileIconProvider *iconProvider, const QString &path) {
        if (info)
            info->displayType = iconProvider->type(QFileInfo(path));
        for (FileSystemNode *child : qAsConst(children)) {
            //On windows the root (My computer) has no path so we don't want to add a / for nothing (e.g. /C:/)
            if (!path.isEmpty()) {
                if (path.endsWith(QLatin1Char('/')))
                    child->retranslateStrings(iconProvider, path + child->fileName.data);
                else
                    child->retranslateStrings(iconProvider, path + QLatin1Char('/') + child->fileName.data);
            } else
                child->retranslateStrings(iconProvider, child->fileName.data);
        }
    }

    QHash<PathKey, FileSystemNode *> children;
    QList<PathKey> visibleChildren;
    ExtendedInformation *info = nullptr;
    FileSystemNode *parent;
    int dirtyChildrenIndex = -1;
    bool populatedChildren = false;
    bool isVisible = false;
};

}
