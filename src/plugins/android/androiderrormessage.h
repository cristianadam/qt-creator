// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <QVariantList>
#include <QCoreApplication>

namespace Android {
namespace Internal {

class AndroidErrorMessage
{
    Q_DECLARE_TR_FUNCTIONS(Android::Internal::AndroidErrorMessage)
public:
    enum ErrorCode {
        UnknownError = 0x3000,
        SDKInstallationError,
        NDKInstallationError,
        JavaInstallationError,
        AntInstallationError,
        AdbInstallationError,
        DeviceConnectionError,
        DevicePermissionError,
        DeviceAuthorizationError,
        DeviceAPILevelError
    };
    static QString getMessage(ErrorCode errorCode, const QVariantList &parameters = QVariantList());
};

} // namespace Internal
} // namespace Android
