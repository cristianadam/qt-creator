#pragma once

#include <QString>
#include <QFileInfo>

namespace Utils {

class FileSystemModelPrivate;
class FileSystemModel;

class FileSystemModelSlots : public QObject
{
public:
    explicit FileSystemModelSlots(FileSystemModelPrivate *owner,
                                  FileSystemModel *q_owner)
        : owner(owner), q_owner(q_owner)
    {}

    void _q_directoryChanged(const QString &directory, const QStringList &list);
    void _q_performDelayedSort();
    void _q_fileSystemChanged(const QString &path,
                              const QList<QPair<QString, QFileInfo>> &);
    void _q_resolvedName(const QString &fileName, const QString &resolvedName);


    void directoryLoaded(const QString &path);

    FileSystemModelPrivate *owner;
    FileSystemModel *q_owner;
};

}
