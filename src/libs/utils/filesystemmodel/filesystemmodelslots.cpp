#include "filesystemmodelslots.h"

#include "filesystemmodel_p.h"

namespace Utils {
void FileSystemModelSlots::_q_directoryChanged(const QString &directory, const QStringList &list)
{
    owner->_q_directoryChanged(directory, list);
}

void FileSystemModelSlots::_q_performDelayedSort()
{
    owner->_q_performDelayedSort();
}

void FileSystemModelSlots::_q_fileSystemChanged(const QString &path,
                              const QList<QPair<QString, QFileInfo>> &list)
{
    owner->_q_fileSystemChanged(path, list);
}

void FileSystemModelSlots::_q_resolvedName(const QString &fileName, const QString &resolvedName)
{
    owner->_q_resolvedName(fileName, resolvedName);
}

void FileSystemModelSlots::directoryLoaded(const QString &path)
{
    q_owner->directoryLoaded(path);
}

}
