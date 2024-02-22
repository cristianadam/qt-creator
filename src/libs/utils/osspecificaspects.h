// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QString>

#include <algorithm>

#define QTC_WIN_EXE_SUFFIX ".exe"

namespace Utils {

// Add more as needed.
enum OsType { OsTypeWindows, OsTypeLinux, OsTypeMac, OsTypeOtherUnix, OsTypeOther };

enum OsArch { OsArchUnknown, OsArchX86, OsArchAMD64, OsArchItanium, OsArchArm, OsArchArm64 };

inline QString osTypeToString(OsType osType)
{
    switch (osType) {
    case OsTypeWindows:
        return "Windows";
    case OsTypeLinux:
        return "Linux";
    case OsTypeMac:
        return "Mac";
    case OsTypeOtherUnix:
        return "Other Unix";
    case OsTypeOther:
    default:
        return "Other";
    }
}

inline OsType osTypeFromString(QString string)
{
    string = string.toLower();
    if (string == "windows")
        return OsTypeWindows;
    if (string == "linux")
        return OsTypeLinux;
    if (string == "mac")
        return OsTypeMac;
    if (string == "darwin")
        return OsTypeMac;
    if (string == "other unix")
        return OsTypeOtherUnix;
    return OsTypeOther;
}

namespace OsSpecificAspects {

inline QString withExecutableSuffix(OsType osType, const QString &executable)
{
    QString finalName = executable;
    if (osType == OsTypeWindows && !finalName.endsWith(QTC_WIN_EXE_SUFFIX))
        finalName += QLatin1String(QTC_WIN_EXE_SUFFIX);
    return finalName;
}

constexpr Qt::CaseSensitivity fileNameCaseSensitivity(OsType osType)
{
    return osType == OsTypeWindows || osType == OsTypeMac ? Qt::CaseInsensitive : Qt::CaseSensitive;
}

constexpr Qt::CaseSensitivity envVarCaseSensitivity(OsType osType)
{
    return fileNameCaseSensitivity(osType);
}

constexpr QChar pathListSeparator(OsType osType)
{
    return QLatin1Char(osType == OsTypeWindows ? ';' : ':');
}

constexpr Qt::KeyboardModifier controlModifier(OsType osType)
{
    return osType == OsTypeMac ? Qt::MetaModifier : Qt::ControlModifier;
}

inline QString pathWithNativeSeparators(OsType osType, const QString &pathName)
{
    if (osType == OsTypeWindows) {
        const int pos = pathName.indexOf('/');
        if (pos >= 0) {
            QString n = pathName;
            std::replace(std::begin(n) + pos, std::end(n), '/', '\\');
            return n;
        }
    } else {
        const int pos = pathName.indexOf('\\');
        if (pos >= 0) {
            QString n = pathName;
            std::replace(std::begin(n) + pos, std::end(n), '\\', '/');
            return n;
        }
    }
    return pathName;
}

} // namespace OsSpecificAspects
} // namespace Utils
