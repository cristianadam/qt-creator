#include "global.h"

Q_LOGGING_CATEGORY(LogSystem, "am.system")

QString toAbsoluteFilePath(const QString &path, const QString &baseDir)
{
    return path.startsWith(qSL("qrc:")) ? path.mid(3) : QDir(baseDir).absoluteFilePath(path);
}
