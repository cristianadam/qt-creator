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

#include "applicationlauncher.h"

#include "devicesupport/desktopdevice.h"
#include "projectexplorer.h"
#include "projectexplorersettings.h"
#include "runcontrol.h"
#include "windebuginterface.h"

#include <coreplugin/icore.h>

#include <utils/fileutils.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QTextCodec>
#include <QTimer>

/*!
    \class ProjectExplorer::ApplicationLauncher

    \brief The ApplicationLauncher class is the application launcher of the
    ProjectExplorer plugin.

    Encapsulates processes running in a console or as GUI processes,
    captures debug output of GUI processes on Windows (outputDebugString()).

    \sa Utils::QtcProcess
*/

using namespace Utils;

namespace ProjectExplorer {

using namespace Internal;

namespace Internal {

class ApplicationLauncherPrivate : public QObject
{
public:
    enum State { Inactive, Run };
    explicit ApplicationLauncherPrivate(ApplicationLauncher *parent);

    ~ApplicationLauncherPrivate() override {
        if (m_state == Run)
            emit q->done();
    }

    void start();
    void stop();

    void handleStandardOutput();
    void handleStandardError();
    void handleDone();

    // Local
    qint64 applicationPID() const;
    bool isRunning() const;

public:
    ApplicationLauncher *q;

    bool m_isLocal = true;
    bool m_runAsRoot = false;

    QtcProcess m_process;

    QTextCodec *m_outputCodec = nullptr;
    QTextCodec::ConverterState m_outputCodecState;
    QTextCodec::ConverterState m_errorCodecState;

    // Keep track whether we need to emit a finished signal
    bool m_processRunning = false;

    // Remote
    State m_state = Inactive;
    bool m_stopRequested = false;

    Runnable m_runnable;

    ProcessResultData m_resultData;
};

} // Internal

static QProcess::ProcessChannelMode defaultProcessChannelMode()
{
    return ProjectExplorerPlugin::appOutputSettings().mergeChannels
            ? QProcess::MergedChannels : QProcess::SeparateChannels;
}

ApplicationLauncherPrivate::ApplicationLauncherPrivate(ApplicationLauncher *parent)
    : q(parent)
{
    m_process.setProcessChannelMode(defaultProcessChannelMode());
    connect(&m_process, &QtcProcess::started, q, &ApplicationLauncher::started);
    connect(&m_process, &QtcProcess::done, this, &ApplicationLauncherPrivate::handleDone);
    connect(&m_process, &QtcProcess::readyReadStandardError,
                this, &ApplicationLauncherPrivate::handleStandardError);
    connect(&m_process, &QtcProcess::readyReadStandardOutput,
                this, &ApplicationLauncherPrivate::handleStandardOutput);

    if (WinDebugInterface::instance()) {
        connect(WinDebugInterface::instance(), &WinDebugInterface::cannotRetrieveDebugOutput,
            this, [this] {
                disconnect(WinDebugInterface::instance(), nullptr, this, nullptr);
                emit q->appendMessage(ApplicationLauncher::tr("Cannot retrieve debugging output.")
                          + QLatin1Char('\n'), ErrorMessageFormat);
        });

        connect(WinDebugInterface::instance(), &WinDebugInterface::debugOutput,
            this, [this](qint64 pid, const QString &message) {
            if (applicationPID() == pid)
                emit q->appendMessage(message, DebugFormat);
        });
    }
}

ApplicationLauncher::ApplicationLauncher(QObject *parent) : QObject(parent),
    d(std::make_unique<ApplicationLauncherPrivate>(this))
{
}

ApplicationLauncher::~ApplicationLauncher() = default;

void ApplicationLauncher::setUseTerminal(bool on)
{
    d->m_process.setTerminalMode(on ? Utils::TerminalMode::On : Utils::TerminalMode::Off);
}

void ApplicationLauncher::setRunAsRoot(bool on)
{
    d->m_runAsRoot = on;
}

void ApplicationLauncher::setRunnable(const Runnable &runnable)
{
    d->m_runnable = runnable;
}

void ApplicationLauncher::stop()
{
    d->stop();
}

void ApplicationLauncherPrivate::stop()
{
    m_resultData.m_exitStatus = QProcess::CrashExit;
    if (m_isLocal) {
        if (!isRunning())
            return;
        m_process.stopProcess();
        QTimer::singleShot(100, this, [this] { emit q->done(); });
    } else {
        if (m_stopRequested)
            return;
        m_stopRequested = true;
        emit q->appendMessage(ApplicationLauncher::tr("User requested stop. Shutting down..."),
                              NormalMessageFormat);
        switch (m_state) {
            case Run:
                m_process.terminate();
                break;
            case Inactive:
                break;
        }
    }
}

bool ApplicationLauncherPrivate::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

ProcessHandle ApplicationLauncher::applicationPID() const
{
    return ProcessHandle(d->applicationPID());
}

qint64 ApplicationLauncherPrivate::applicationPID() const
{
    if (!isRunning())
        return 0;

    return m_process.processId();
}

ProcessResultData ApplicationLauncher::resultData() const
{
    return d->m_resultData;
}

void ApplicationLauncherPrivate::handleDone()
{
    m_resultData = m_process.resultData();

    if (m_isLocal) {
        if (m_resultData.m_error == QProcess::UnknownError) {
            emit q->done();
            return;
        }
        // TODO: why below handlings are different?
        if (m_process.usesTerminal()) {
            emit q->appendMessage(m_process.errorString(), ErrorMessageFormat);
            if (m_processRunning && m_process.processId() == 0) {
                m_processRunning = false;
                m_resultData.m_exitCode = -1; // FIXME: Why?
            }
        } else {
            QString errorString;
            switch (m_resultData.m_error) {
            case QProcess::FailedToStart:
                errorString = ApplicationLauncher::tr("Failed to start program. Path or permissions wrong?");
                break;
            case QProcess::Crashed:
                m_resultData.m_exitStatus = QProcess::CrashExit;
                break;
            default:
                errorString = ApplicationLauncher::tr("Some error has occurred while running the program.");
            }
            if (!errorString.isEmpty())
                emit q->appendMessage(errorString, ErrorMessageFormat);
            if (m_processRunning && !isRunning()) {
                m_processRunning = false;
                m_resultData.m_exitCode = -1;
            }
        }

    } else {
        QTC_ASSERT(m_state == Run, emit q->done(); return);
        if (m_resultData.m_error == QProcess::FailedToStart) {
            m_resultData.m_exitStatus = QProcess::CrashExit;
        } else if (m_resultData.m_exitStatus == QProcess::CrashExit) {
            m_resultData.m_error = QProcess::Crashed;
        }
        m_state = Inactive;
    }
    emit q->done();
}

void ApplicationLauncherPrivate::handleStandardOutput()
{
    const QByteArray data = m_process.readAllStandardOutput();
    const QString msg = m_outputCodec->toUnicode(
                data.constData(), data.length(), &m_outputCodecState);
    emit q->appendMessage(msg, StdOutFormat, false);
}

void ApplicationLauncherPrivate::handleStandardError()
{
    const QByteArray data = m_process.readAllStandardError();
    const QString msg = m_outputCodec->toUnicode(
                data.constData(), data.length(), &m_errorCodecState);
    emit q->appendMessage(msg, StdErrFormat, false);
}

void ApplicationLauncher::start()
{
    d->start();
}

void ApplicationLauncherPrivate::start()
{
    m_isLocal = m_runnable.device.isNull() || m_runnable.device.dynamicCast<const DesktopDevice>();

    m_resultData = {};

    if (m_isLocal) {
        // Work around QTBUG-17529 (QtDeclarative fails with 'File name case mismatch' ...)
        const FilePath fixedPath = m_runnable.workingDirectory.normalizedPathName();
        m_process.setWorkingDirectory(fixedPath);

        Environment env = m_runnable.environment;
        if (m_runAsRoot)
            RunControl::provideAskPassEntry(env);

        m_process.setEnvironment(env);

        m_processRunning = true;

        WinDebugInterface::startIfNeeded();

        CommandLine cmdLine = m_runnable.command;

        if (HostOsInfo::isMacHost()) {
            CommandLine disclaim(Core::ICore::libexecPath("disclaim"));
            disclaim.addCommandLineAsArgs(cmdLine);
            cmdLine = disclaim;
        }

        m_process.setRunAsRoot(m_runAsRoot);
        m_process.setCommand(cmdLine);
    } else {
        QTC_ASSERT(m_state == Inactive, return);

        if (!m_runnable.device) {
            m_resultData.m_errorString = ApplicationLauncher::tr("Cannot run: No device.");
            m_resultData.m_error = QProcess::FailedToStart;
            m_resultData.m_exitStatus = QProcess::CrashExit;
            emit q->done();
            return;
        }

        if (!m_runnable.device->isEmptyCommandAllowed() && m_runnable.command.isEmpty()) {
            m_resultData.m_errorString = ApplicationLauncher::tr("Cannot run: No command given.");
            m_resultData.m_error = QProcess::FailedToStart;
            m_resultData.m_exitStatus = QProcess::CrashExit;
            emit q->done();
            return;
        }

        m_state = Run;
        m_stopRequested = false;

        m_process.setCommand(m_runnable.command);
        m_process.setWorkingDirectory(m_runnable.workingDirectory);
        m_process.setEnvironment(m_runnable.environment);
        m_process.setExtraData(m_runnable.extraData);
    }

    if (m_isLocal)
        m_outputCodec = QTextCodec::codecForLocale();
    else
        m_outputCodec = QTextCodec::codecForName("utf8");

    m_process.start();
}

} // namespace ProjectExplorer
