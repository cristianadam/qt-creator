/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "qtcassert.h"

#include <QByteArray>
#include <QDebug>

#if defined(Q_OS_UNIX)
#include <stdio.h>
#include <signal.h>
#include <execinfo.h>
#endif

namespace Utils {

void dumpBacktrace(int maxdepth)
{
    const int ArraySize = 1000;
    if (maxdepth < 0 || maxdepth > ArraySize)
        maxdepth = ArraySize;
#if defined(Q_OS_UNIX)
    void *bt[ArraySize] = {nullptr};
    int size = backtrace(bt, maxdepth);
    char **lines = backtrace_symbols(bt, size);
    for (int i = 0; i < size; ++i)
        qDebug() << "0x" + QByteArray::number(quintptr(bt[i]), 16) << lines[i];
    free(lines);
#endif
}

void writeAssertLocation(const char *msg)
{
    const bool goBoom = qEnvironmentVariableIsSet("QTC_FATAL_ASSERTS");
    if (goBoom)
        qFatal("SOFT ASSERT made fatal: %s", msg);
    else
        qDebug("SOFT ASSERT: %s", msg);

    const int maxdepth = qEnvironmentVariableIntValue("QTC_BACKTRACE_MAXDEPTH");
    if (maxdepth != 0)
        dumpBacktrace(maxdepth);
}

} // namespace Utils
