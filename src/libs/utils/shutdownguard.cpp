// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <shutdownguard.h>

#include <threadutils.h>
#include <qtcassert.h>

namespace Utils {

/*!
    Returns an object that can be used as the parent for objects that should be
    destroyed just at the end of the applications lifetime.
    The object is destroyed after all plugins' aboutToShutdown methods
    have finished, just before the plugins are deleted.

    Only use this from the application's main thread.

    \sa ExtensionSystem::IPlugin::aboutToShutdown()
*/
QObject *shutdownGuard()
{
    static QObject *theShutdownGuard = nullptr;

    if (!theShutdownGuard) {
        QTC_CHECK(Utils::isMainThread());
        theShutdownGuard = new QObject;
    }
    return theShutdownGuard;
}

} // Utils
