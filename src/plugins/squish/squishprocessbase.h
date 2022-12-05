// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "squishconstants.h"

#include <utils/qtcprocess.h>

#include <QObject>

namespace Squish::Internal {

class SquishServerProcess;

class SquishProcessBase : public QObject
{
    Q_OBJECT
public:
    explicit SquishProcessBase(QObject *parent = nullptr);
    ~SquishProcessBase() = default;

    void setExecutable(const Utils::FilePath &executable);
    void setEnvironment(const Utils::Environment &environment);

    SquishProcessState processState() const { return m_state; }

    inline bool isRunning() const { return m_process.isRunning(); }
    inline Utils::ProcessResult result() const { return m_process.result(); }
    inline QProcess::ProcessError error() const { return m_process.error(); }
    inline QProcess::ProcessState state() const { return m_process.state(); }

    void closeProcess() { m_process.close(); }

signals:
    void logOutputReceived(const QString &output);
    void stateChanged(SquishProcessState state);

protected:
    virtual void onDone() {}
    virtual void onErrorOutput() {}

    Utils::QtcProcess m_process;
    SquishProcessState m_state = Idle;
};

class SquishRunnerProcessBase : public SquishProcessBase
{
    Q_OBJECT
public:
    explicit SquishRunnerProcessBase(QObject *parent = nullptr);

    void setServer(SquishServerProcess *server);

protected:
    inline SquishServerProcess *server() const { return m_server; }
    virtual void onServerStateChanged(SquishProcessState toServerState) {}

private:
    SquishServerProcess *m_server = nullptr; // not owned
};

} // namespace Squish::Internal
