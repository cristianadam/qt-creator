// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "registerpostmortemaction.h"

#ifdef Q_OS_WIN
#include <registryaccess.h>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QString>

#include <utils/hostosinfo.h>

#ifdef Q_OS_WIN
#ifdef QTCREATOR_PCH_H
#define CALLBACK WINAPI
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

using namespace RegistryAccess;

#endif

namespace Debugger::Internal {

static void registerNow(bool value)
{
    Q_UNUSED(value)
#ifdef Q_OS_WIN
    const QString debuggerExe = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + '/'
                                + QLatin1String(debuggerApplicationFileC) + ".exe");
    const ushort *debuggerWString = debuggerExe.utf16();

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFO shExecInfo;
    shExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
    shExecInfo.fMask  = SEE_MASK_NOCLOSEPROCESS;
    shExecInfo.hwnd   = NULL;
    shExecInfo.lpVerb = L"runas";
    shExecInfo.lpFile = reinterpret_cast<LPCWSTR>(debuggerWString);
    shExecInfo.lpParameters = value ? L"-register" : L"-unregister";
    shExecInfo.lpDirectory  = NULL;
    shExecInfo.nShow        = SW_SHOWNORMAL;
    shExecInfo.hProcess     = NULL;
    if (ShellExecuteEx(&shExecInfo) && shExecInfo.hProcess)
        WaitForSingleObject(shExecInfo.hProcess, INFINITE);
    CoUninitialize();
    readSettings();
#endif
}

RegisterPostMortemAction::RegisterPostMortemAction(Utils::AspectContainer *container)
    : BoolAspect(container)
{
    setVisible(Utils::HostOsInfo::isWindowsHost());
    connect(this, &BaseAspect::changed, this, [this] { registerNow(value()); });
}

void RegisterPostMortemAction::readSettings()
{
#ifdef Q_OS_WIN
    Q_UNUSED(debuggerRegistryValueNameC) // avoid warning from MinGW
    bool registered = false;
    HKEY handle = NULL;
    QString errorMessage;
    if (openRegistryKey(HKEY_LOCAL_MACHINE, debuggerRegistryKeyC, false, &handle, &errorMessage))
        registered = isRegistered(handle, debuggerCall(), &errorMessage);
    if (handle)
        RegCloseKey(handle);
    setValueQuietly(registered);
#endif
}

} // Debugger::Internal
