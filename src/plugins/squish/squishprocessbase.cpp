// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "squishprocessbase.h"

#include "squishserverprocess.h"

#include <utils/qtcassert.h>

namespace Squish::Internal {

SquishProcessBase::SquishProcessBase(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &Utils::QtcProcess::readyReadStandardError,
            this, &SquishProcessBase::onErrorOutput);
    connect(&m_process, &Utils::QtcProcess::done,
            this, &SquishProcessBase::onDone);
}
void SquishProcessBase::setExecutable(const Utils::FilePath &executable)
{
    QTC_ASSERT(m_process.state() == QProcess::NotRunning, return);
    m_process.setCommand({executable, {}});
}

void SquishProcessBase::setEnvironment(const Utils::Environment &environment)
{
    QTC_ASSERT(m_process.state() == QProcess::NotRunning, return);
    m_process.setEnvironment(environment);
}

SquishRunnerProcessBase::SquishRunnerProcessBase(QObject *parent)
    : SquishProcessBase(parent)
{
}

void SquishRunnerProcessBase::setServer(SquishServerProcess *server)
{
    if (m_server) { // cut former connect if there is any
        disconnect(m_server, &SquishServerProcess::stateChanged,
                   this, &SquishRunnerProcessBase::onServerStateChanged);
    }
    m_server = server;
    if (m_server) { // establish new connect if we got a server
        connect(m_server, &SquishServerProcess::stateChanged,
                this, &SquishRunnerProcessBase::onServerStateChanged);
    }
}

} // namespace Squish::Internal
