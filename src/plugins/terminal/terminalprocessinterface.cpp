// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "terminaltr.h"

#include "terminalprocessinterface.h"
#include "terminalwidget.h"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QTemporaryFile>
#include <QTimer>

Q_LOGGING_CATEGORY(terminalProcessLog, "qtc.terminal.stubprocess", QtDebugMsg)

using namespace Utils;

namespace Terminal {

static QString msgCommChannelFailed(const QString &error)
{
    return Tr::tr("Cannot set up communication channel: %1").arg(error);
}

static QString msgCannotCreateTempFile(const QString &why)
{
    return Tr::tr("Cannot create temporary file: %1").arg(why);
}

static QString msgCannotWriteTempFile()
{
    return Tr::tr("Cannot write temporary file. Disk full?");
}

static QString msgCannotCreateTempDir(const QString &dir, const QString &why)
{
    return Tr::tr("Cannot create temporary directory \"%1\": %2").arg(dir, why);
}

static QString msgUnexpectedOutput(const QByteArray &what)
{
    return Tr::tr("Unexpected output from helper program (%1).").arg(QString::fromLatin1(what));
}

static QString msgCannotChangeToWorkDir(const FilePath &dir, const QString &why)
{
    return Tr::tr("Cannot change to working directory \"%1\": %2").arg(dir.toString(), why);
}

static QString msgCannotExecute(const QString &p, const QString &why)
{
    return Tr::tr("Cannot execute \"%1\": %2").arg(p, why);
}

class TerminalProcessPrivate
{
public:
    TerminalProcessPrivate(QObject *parent)
        : m_stubServer(parent)
    {}

    qint64 m_processId = 0;
    ProcessResultData m_result;
    QLocalServer m_stubServer;
    QLocalSocket *m_stubSocket = nullptr;
    QTemporaryFile *m_envListFile = nullptr;

    // Used on Unix only
    QTimer *m_stubConnectTimer = nullptr;

    // Used on Windows only
    qint64 m_appMainThreadId = 0;
};

TerminalProcessInterface::TerminalProcessInterface(TerminalPane *terminalPane)
    : d(new TerminalProcessPrivate(this))
    , m_terminalPane(terminalPane)
{
    connect(&d->m_stubServer,
            &QLocalServer::newConnection,
            this,
            &TerminalProcessInterface::stubConnectionAvailable);
}

TerminalProcessInterface::~TerminalProcessInterface()
{
    stopProcess();
    delete d;
}

void TerminalProcessInterface::start()
{
    if (isRunning())
        return;

    d->m_result = {};

    ProcessArgs::SplitError perr;

    QString pcmd;
    QStringList pargs;

    if (HostOsInfo::isWindowsHost()) {
        if (m_setup.m_terminalMode != TerminalMode::Run) {
            // The debugger engines already pre-process the arguments.
            pcmd = m_setup.m_commandLine.executable().toString();
            pargs = {m_setup.m_commandLine.arguments()};
        } else {
            ProcessArgs outArgs;
            ProcessArgs::prepareCommand(m_setup.m_commandLine,
                                        &pcmd,
                                        &outArgs,
                                        &m_setup.m_environment,
                                        &m_setup.m_workingDirectory);
            pargs = {outArgs.toWindowsArgs()};
        }
    } else {
        ProcessArgs processArgs = ProcessArgs::prepareArgs(m_setup.m_commandLine.arguments(),
                                                           &perr,
                                                           HostOsInfo::hostOs(),
                                                           &m_setup.m_environment,
                                                           &m_setup.m_workingDirectory,
                                                           m_setup.m_abortOnMetaChars);

        if (perr == ProcessArgs::SplitOk) {
            pcmd = m_setup.m_commandLine.executable().toString();
        } else {
            if (perr != ProcessArgs::FoundMeta) {
                emitError(QProcess::FailedToStart, Tr::tr("Quoting error in command."));
                return;
            }
            if (m_setup.m_terminalMode == TerminalMode::Debug) {
                // FIXME: QTCREATORBUG-2809
                emitError(QProcess::FailedToStart,
                          Tr::tr("Debugging complex shell commands in a terminal"
                                 " is currently not supported."));
                return;
            }
            pcmd = qtcEnvironmentVariable("SHELL", "/bin/sh");
            processArgs = ProcessArgs::createUnixArgs(
                {"-c",
                 (ProcessArgs::quoteArg(m_setup.m_commandLine.executable().toString()) + ' '
                  + m_setup.m_commandLine.arguments())});
        }

        pargs = processArgs.toUnixArgs();
    }

    const QString err = stubServerListen();
    qCDebug(terminalProcessLog) << "stubServerListen() returned" << err;
    if (!err.isEmpty()) {
        emitError(QProcess::FailedToStart, msgCommChannelFailed(err));
        return;
    }

    m_setup.m_environment.unset(QLatin1String("TERM"));

    Environment finalEnv = m_setup.m_environment;

    if (HostOsInfo::isWindowsHost()) {
        if (!finalEnv.hasKey("PATH")) {
            const QString path = qtcEnvironmentVariable("PATH");
            if (!path.isEmpty())
                finalEnv.set("PATH", path);
        }
        if (!finalEnv.hasKey("SystemRoot")) {
            const QString systemRoot = qtcEnvironmentVariable("SystemRoot");
            if (!systemRoot.isEmpty())
                finalEnv.set("SystemRoot", systemRoot);
        }
    }

    if (finalEnv.hasChanges()) {
        d->m_envListFile = new QTemporaryFile(this);
        if (!d->m_envListFile->open()) {
            cleanupAfterStartFailure(msgCannotCreateTempFile(d->m_envListFile->errorString()));
            return;
        }
        QTextStream stream(d->m_envListFile);
        finalEnv.forEachEntry([&stream](const QString &key, const QString &value, bool) {
            stream << key << '=' << value << '\0';
        });

        if (d->m_envListFile->error() != QFile::NoError) {
            cleanupAfterStartFailure(msgCannotWriteTempFile());
            return;
        }
    }

    const QString stubPath = QString("%1/%2/%3")
                                 .arg(QCoreApplication::applicationDirPath())
                                 .arg(QLatin1String(RELATIVE_LIBEXEC_PATH))
                                 .arg(HostOsInfo::isWindowsHost()
                                          ? QLatin1String("qtc_debughelper.exe")
                                          : QLatin1String("qtc_debughelper"));

    QStringList allArgs{
        "-s",
        d->m_stubServer.fullServerName(),
        "-w",
        m_setup.m_workingDirectory.nativePath(),
    };

    if (m_setup.m_terminalMode == TerminalMode::Debug)
        allArgs << "-d";

    if (terminalProcessLog().isDebugEnabled())
        allArgs << "-v";

    if (d->m_envListFile)
        allArgs << "-e" << d->m_envListFile->fileName();

    allArgs << "--" << pcmd << pargs;

    Utils::Id id = Utils::Id::fromString(m_setup.m_commandLine.executable().toUserOutput());

    TerminalWidget *terminal = m_terminalPane->stoppedTerminalWithId(id);

    const Utils::Terminal::OpenTerminalParameters
        openParameters{Utils::CommandLine{FilePath::fromUserInput(stubPath), allArgs},
                       std::nullopt,
                       std::nullopt,
                       Utils::Terminal::ExitBehavior::Keep,
                       id};

    if (!terminal) {
        terminal = new TerminalWidget(nullptr, openParameters);

        terminal->setShellName(m_setup.m_commandLine.executable().fileName());
        m_terminalPane->addTerminal(terminal, "App");
    } else {
        terminal->restart(openParameters);
    }

    connect(terminal, &TerminalWidget::destroyed, this, [this] {
        if (d->m_processId)
            emitFinished(-1, QProcess::CrashExit);
    });

    d->m_stubConnectTimer = new QTimer(this);
    connect(d->m_stubConnectTimer, &QTimer::timeout, this, &TerminalProcessInterface::stopProcess);
    d->m_stubConnectTimer->setSingleShot(true);
    d->m_stubConnectTimer->start(10000);
}

void TerminalProcessInterface::cleanupAfterStartFailure(const QString &errorMessage)
{
    stubServerShutdown();
    emitError(QProcess::FailedToStart, errorMessage);
    delete d->m_envListFile;
    d->m_envListFile = nullptr;
}

void TerminalProcessInterface::sendControlSignal(ControlSignal controlSignal)
{
    switch (controlSignal) {
    case ControlSignal::Terminate:
    case ControlSignal::Kill:
        killProcess();
        break;
    case ControlSignal::Interrupt:
        sendCommand('i');
        break;
    case ControlSignal::KickOff:
        sendCommand('c');
        break;
    case ControlSignal::CloseWriteChannel:
        QTC_CHECK(false);
        break;
    }
}

void TerminalProcessInterface::sendCommand(char c)
{
    if (d->m_stubSocket && d->m_stubSocket->isWritable()) {
        d->m_stubSocket->write(&c, 1);
        d->m_stubSocket->flush();
    }
}

void TerminalProcessInterface::killProcess()
{
    sendCommand('k');
    d->m_processId = 0;
}

void TerminalProcessInterface::killStub()
{
    if (!isRunning())
        return;

    sendCommand('s');
    stubServerShutdown();
}

void TerminalProcessInterface::stopProcess()
{
    killProcess();
    killStub();
}

bool TerminalProcessInterface::isRunning() const
{
    return (d->m_stubSocket && d->m_stubSocket->isOpen());
}

QString TerminalProcessInterface::stubServerListen()
{
    if (HostOsInfo::isWindowsHost()) {
        if (d->m_stubServer.listen(QString::fromLatin1("creator-%1-%2")
                                       .arg(QCoreApplication::applicationPid())
                                       .arg(rand())))
            return {};
        return d->m_stubServer.errorString();
    }

    // We need to put the socket in a private directory, as some systems simply do not
    // check the file permissions of sockets.
    if (!QDir(m_tempDir.path())
             .mkdir("socket")) { //  QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
        return msgCannotCreateTempDir(m_tempDir.filePath("socket"),
                                      QString::fromLocal8Bit(strerror(errno)));
    }

    if (!QFile::setPermissions(m_tempDir.filePath("socket"),
                               QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        return Tr::tr("Cannot set permissions on temporary directory \"%1\": %2")
            .arg(m_tempDir.filePath("socket"))
            .arg(QString::fromLocal8Bit(strerror(errno)));
    }

    const QString socketPath = m_tempDir.filePath("socket/stub-socket");
    if (!d->m_stubServer.listen(socketPath)) {
        return Tr::tr("Cannot create socket \"%1\": %2")
            .arg(socketPath, d->m_stubServer.errorString());
    }
    return {};
}

void TerminalProcessInterface::stubServerShutdown()
{
    if (d->m_stubSocket) {
        readStubOutput(); // we could get the shutdown signal before emptying the buffer
        d->m_stubSocket->disconnect(); // avoid getting queued readyRead signals
        d->m_stubSocket
            ->deleteLater(); // we might be called from the disconnected signal of m_stubSocket
    }
    d->m_stubSocket = nullptr;
    if (d->m_stubServer.isListening()) {
        d->m_stubServer.close();
    }
}

void TerminalProcessInterface::stubConnectionAvailable()
{
    if (d->m_stubConnectTimer) {
        delete d->m_stubConnectTimer;
        d->m_stubConnectTimer = nullptr;
    }

    d->m_stubSocket = d->m_stubServer.nextPendingConnection();
    connect(d->m_stubSocket, &QIODevice::readyRead, this, &TerminalProcessInterface::readStubOutput);

    if (HostOsInfo::isAnyUnixHost())
        connect(d->m_stubSocket,
                &QLocalSocket::disconnected,
                this,
                &TerminalProcessInterface::stubExited);
}

static QString errorMsg(int code)
{
    return QString::fromLocal8Bit(strerror(code));
}

void TerminalProcessInterface::readStubOutput()
{
    while (d->m_stubSocket->canReadLine()) {
        QByteArray out = d->m_stubSocket->readLine();
        out.chop(1); // remove newline
        if (out.startsWith("err:chdir ")) {
            emitError(QProcess::FailedToStart,
                      msgCannotChangeToWorkDir(m_setup.m_workingDirectory,
                                               errorMsg(out.mid(10).toInt())));
        } else if (out.startsWith("err:exec ")) {
            emitError(QProcess::FailedToStart,
                      msgCannotExecute(m_setup.m_commandLine.executable().toString(),
                                       errorMsg(out.mid(9).toInt())));
        } else if (out.startsWith("spid ")) {
            delete d->m_envListFile;
            d->m_envListFile = nullptr;
        } else if (out.startsWith("pid ")) {
            d->m_processId = out.mid(4).toInt();
            emit started(d->m_processId, d->m_appMainThreadId);
        } else if (out.startsWith("thread ")) { // Windows only
            d->m_appMainThreadId = out.mid(7).toLongLong();
        } else if (out.startsWith("exit ")) {
            emitFinished(out.mid(5).toInt(), QProcess::NormalExit);
        } else if (out.startsWith("crash ")) {
            emitFinished(out.mid(6).toInt(), QProcess::CrashExit);
        } else {
            emitError(QProcess::UnknownError, msgUnexpectedOutput(out));
            break;
        }
    }
}

void TerminalProcessInterface::stubExited()
{
    // The stub exit might get noticed before we read the pid for the kill on Windows
    // or the error status elsewhere.
    if (d->m_stubSocket && d->m_stubSocket->state() == QLocalSocket::ConnectedState)
        d->m_stubSocket->waitForDisconnected();

    stubServerShutdown();
    delete d->m_envListFile;
    d->m_envListFile = nullptr;
    if (d->m_processId)
        emitFinished(-1, QProcess::CrashExit);
}

void TerminalProcessInterface::emitError(QProcess::ProcessError error, const QString &errorString)
{
    d->m_result.m_error = error;
    d->m_result.m_errorString = errorString;
    if (error == QProcess::FailedToStart)
        emit done(d->m_result);
}

void TerminalProcessInterface::emitFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    d->m_processId = 0;
    d->m_result.m_exitCode = exitCode;
    d->m_result.m_exitStatus = exitStatus;
    emit done(d->m_result);
}

} // namespace Terminal
