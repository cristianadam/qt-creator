#pragma once

#include "../hostosinfo.h"
#include "../qtcassert.h"

#include <QString>
#include <QRegularExpression>
#include <QFileInfo>

namespace Utils {

static QString volumeName(const QString &path)
{
#if defined(Q_OS_WIN)
    IShellItem *item = nullptr;
    const QString native = QDir::toNativeSeparators(path);
    HRESULT hr = SHCreateItemFromParsingName(reinterpret_cast<const wchar_t *>(native.utf16()),
                                             nullptr, IID_IShellItem,
                                             reinterpret_cast<void **>(&item));
    if (FAILED(hr))
        return QString();
    LPWSTR name = nullptr;
    hr = item->GetDisplayName(SIGDN_NORMALDISPLAY, &name);
    if (FAILED(hr))
        return QString();
    QString result = QString::fromWCharArray(name);
    CoTaskMemFree(name);
    item->Release();
    return result;
#else
    Q_UNUSED(path)
    QTC_CHECK(false);
    return {};
#endif // Q_OS_WIN
}

/*
    \internal

    Returns \c true if node passes the name filters and should be visible.
 */

static QRegularExpression QRegularExpression_fromWildcard(QStringView pattern, Qt::CaseSensitivity cs)
{
    auto reOptions = cs == Qt::CaseSensitive ? QRegularExpression::NoPatternOption :
                                             QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern.toString()), reOptions);
}

static bool useFileSystemWatcher()
{
    return true;
}

void static doStat(QFileInfo &fi)
{
    Q_UNUSED(fi)
//            driveInfo.stat();
}

static QString translateDriveName(const QFileInfo &drive)
{
    QString driveName = drive.absoluteFilePath();
    if (HostOsInfo::isWindowsHost()) {
        if (driveName.startsWith(QLatin1Char('/'))) // UNC host
            return drive.fileName();
        if (driveName.endsWith(QLatin1Char('/')))
            driveName.chop(1);
    }
    return driveName;
}

// TODO: FIXME: Change to accept FilePath
static QString qtc_GetLongPathName(const QString &strShortPath)
{
#ifdef Q_OS_WIN32
    if (strShortPath.isEmpty()
        || strShortPath == QLatin1String(".") || strShortPath == QLatin1String(".."))
        return strShortPath;
    if (strShortPath.length() == 2 && strShortPath.endsWith(QLatin1Char(':')))
        return strShortPath.toUpper();
    const QString absPath = QDir(strShortPath).absolutePath();
    if (absPath.startsWith(QLatin1String("//"))
        || absPath.startsWith(QLatin1String("\\\\"))) // unc
        return QDir::fromNativeSeparators(absPath);
    if (absPath.startsWith(QLatin1Char('/')))
        return QString();
    const QString inputString = QLatin1String("\\\\?\\") + QDir::toNativeSeparators(absPath);
    QVarLengthArray<TCHAR, MAX_PATH> buffer(MAX_PATH);
    DWORD result = ::GetLongPathName((wchar_t*)inputString.utf16(),
                                     buffer.data(),
                                     buffer.size());
    if (result > DWORD(buffer.size())) {
        buffer.resize(result);
        result = ::GetLongPathName((wchar_t*)inputString.utf16(),
                                   buffer.data(),
                                   buffer.size());
    }
    if (result > 4) {
        QString longPath = QString::fromWCharArray(buffer.data() + 4); // ignoring prefix
        longPath[0] = longPath.at(0).toUpper(); // capital drive letters
        return QDir::fromNativeSeparators(longPath);
    } else {
        return QDir::fromNativeSeparators(strShortPath);
    }
#else
    return strShortPath;
#endif
}



}
