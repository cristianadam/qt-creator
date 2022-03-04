/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
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

#pragma once

#include "utils_global.h"

#include "environment.h"
#include "commandline.h"
#include "processenums.h"

#include <QProcess>

namespace Utils {

class QTCREATOR_UTILS_EXPORT ProcessSetupData
{
public:
    using Ptr = std::shared_ptr<ProcessSetupData>;

    ProcessImpl m_processImpl = ProcessImpl::Default;
    ProcessMode m_processMode = ProcessMode::Reader;
    TerminalMode m_terminalMode = TerminalMode::Off;

    CommandLine m_commandLine;
    FilePath m_workingDirectory;
    Environment m_environment;
    Environment m_remoteEnvironment;
    QByteArray m_writeData;
    QProcess::ProcessChannelMode m_processChannelMode = QProcess::SeparateChannels;
    QVariantHash m_extraData;
    QString m_standardInputFile;
    QString m_errorString; // partial internal
    QString m_nativeArguments; // internal, dependent on specific code path

    bool m_abortOnMetaChars = true;
    bool m_runAsRoot = false;
    bool m_haveEnv = false;
    bool m_lowPriority = false;
    bool m_unixTerminalDisabled = false;
    bool m_useCtrlCStub = false; // debug only
    bool m_belowNormalPriority = false; // internal, dependent on other fields and specific code path
};

class QTCREATOR_UTILS_EXPORT ProcessInterface : public QObject
{
    Q_OBJECT

public:
    ProcessInterface(QObject *parent = nullptr) : QObject(parent), m_setup(new ProcessSetupData) {}
    ProcessInterface(ProcessSetupData::Ptr setup) : m_setup(setup) {}

    virtual void start() = 0;
    virtual void interrupt() = 0;
    virtual void terminate() = 0;
    virtual void kill() = 0;
    virtual void close() = 0;

    virtual QByteArray readAllStandardOutput() = 0;
    virtual QByteArray readAllStandardError() = 0;
    virtual qint64 write(const QByteArray &data) = 0;

    virtual qint64 processId() const = 0;
    virtual QProcess::ProcessState state() const = 0;
    virtual int exitCode() const = 0;
    virtual QProcess::ExitStatus exitStatus() const = 0;

    virtual QProcess::ProcessError error() const = 0;
    virtual QString errorString() const = 0;
    virtual void setErrorString(const QString &str) = 0;

    virtual bool waitForStarted(int msecs) = 0;
    virtual bool waitForReadyRead(int msecs) = 0;
    virtual bool waitForFinished(int msecs) = 0;

    virtual void kickoffProcess();
    virtual qint64 applicationMainThreadID() const;

signals:
    void started();
    void finished();
    void errorOccurred(QProcess::ProcessError error);
    void readyReadStandardOutput();
    void readyReadStandardError();

protected:
    ProcessSetupData::Ptr m_setup;
    friend class ProcessProxyInterface;
    friend class QtcProcess;
};

class QTCREATOR_UTILS_EXPORT ProcessProxyInterface : public ProcessInterface
{
    Q_OBJECT

public:
    ProcessProxyInterface(ProcessInterface *target)
        : ProcessInterface(target->m_setup)
        , m_target(target)
    {
        m_target->setParent(this);
        connect(m_target, &ProcessInterface::started, this, &ProcessInterface::started);
        connect(m_target, &ProcessInterface::finished, this, &ProcessInterface::finished);
        connect(m_target, &ProcessInterface::errorOccurred, this, &ProcessInterface::errorOccurred);
        connect(m_target, &ProcessInterface::readyReadStandardOutput,
                this, &ProcessInterface::readyReadStandardOutput);
        connect(m_target, &ProcessInterface::readyReadStandardError,
                this, &ProcessInterface::readyReadStandardError);
    }

    void start() override { m_target->start(); }
    void interrupt() override { m_target->interrupt(); };
    void terminate() override { m_target->terminate(); }
    void kill() override { m_target->kill(); }
    void close() override { m_target->close(); }

    QByteArray readAllStandardOutput() override { return m_target->readAllStandardOutput(); }
    QByteArray readAllStandardError() override { return m_target->readAllStandardError(); }
    qint64 write(const QByteArray &data) override { return m_target->write(data); }

    qint64 processId() const override { return m_target->processId(); }
    QProcess::ProcessState state() const override { return m_target->state(); }
    int exitCode() const override { return m_target->exitCode(); }
    QProcess::ExitStatus exitStatus() const override { return m_target->exitStatus(); }

    QProcess::ProcessError error() const override { return m_target->error(); }
    QString errorString() const override { return m_target->errorString(); }
    void setErrorString(const QString &str) override { m_target->setErrorString(str); }

    bool waitForStarted(int msecs) override { return m_target->waitForStarted(msecs); }
    bool waitForReadyRead(int msecs) override { return m_target->waitForReadyRead(msecs); }
    bool waitForFinished(int msecs) override { return m_target->waitForFinished(msecs); }

    void kickoffProcess() override { m_target->kickoffProcess(); }
    qint64 applicationMainThreadID() const override { return m_target->applicationMainThreadID(); }

protected:
    ProcessInterface *m_target;
};


} // namespace Utils
