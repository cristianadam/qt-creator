// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "externalterminalprocessimpl.h"
#include "process.h"
#include "terminalcommand.h"
#include "utilstr.h"

#include <QTemporaryFile>

namespace Utils {

ExternalTerminalProcessImpl::ExternalTerminalProcessImpl()
{
    setStubCreator(new ProcessStubCreator(this));
}

ProcessStubCreator::ProcessStubCreator(TerminalInterface *interface)
    : m_interface(interface)
{}

static const QLatin1String TerminalAppScript{R"(
    tell application "Terminal"
        activate
        set newTab to do script "cd '%1'"
        set win to (the id of window 1 where its tab 1 = newTab) as text
        do script "clear;\"%2\" %3;osascript -e 'tell app \"Terminal\" to close window id " & win & "' &;exit" in newTab
    end tell
)"};

static const QLatin1String iTermAppScript{R"(
    tell application "iTerm"
        activate
        set newWindow to (create window with default profile)
        tell current session of newWindow
            write text "cd %1"
            write text "clear;\"%2\" %3;exit;"
        end tell
    end tell
)"};

expected_str<qint64> ProcessStubCreator::startStubProcess(const ProcessSetupData &setupData)
{
    const TerminalCommand terminal = TerminalCommand::terminalEmulator();

    if (HostOsInfo::isMacHost()) {
        static const QMap<QString, QString> terminalMap = {
            {"Terminal.app", TerminalAppScript},
            {"iTerm.app", iTermAppScript},
        };

        if (terminalMap.contains(terminal.command.toString())) {
            const QString script = terminalMap.value(terminal.command.toString())
                                       .arg(setupData.m_workingDirectory.nativePath())
                                       .arg(setupData.m_commandLine.executable().nativePath())
                                       .arg(setupData.m_commandLine.arguments().replace('"',
                                                                                        "\\\""));

            qDebug().noquote() << "Starting:\n\n" << script << "\n\n";

            Process process;

            process.setCommand({"osascript", {"-"}});
            process.setWriteData(script.toUtf8());
            process.runBlocking();

            if (process.exitCode() != 0) {
                return make_unexpected(
                    Tr::tr("Failed to start terminal process: \"%1\"").arg(process.errorString()));
            }

            return 0;
        }
    }

    bool detached = setupData.m_terminalMode == TerminalMode::Detached;

    Process *process = new Process(detached ? nullptr : this);
    if (detached)
        QObject::connect(process, &Process::done, process, &Process::deleteLater);

    QObject::connect(process, &Process::done, m_interface, &TerminalInterface::onStubExited);

    process->setWorkingDirectory(setupData.m_workingDirectory);

    if constexpr (HostOsInfo::isWindowsHost()) {
        process->setCommand(setupData.m_commandLine);
        process->setCreateConsoleOnWindows(true);
        process->setProcessMode(ProcessMode::Writer);
    } else {
        QString extraArgsFromOptions = terminal.executeArgs;
        CommandLine cmdLine = {terminal.command, {}};
        if (!extraArgsFromOptions.isEmpty())
            cmdLine.addArgs(extraArgsFromOptions, CommandLine::Raw);
        cmdLine.addCommandLineAsArgs(setupData.m_commandLine, CommandLine::Raw);
        process->setCommand(cmdLine);
    }

    process->start();
    process->waitForStarted();
    if (process->error() != QProcess::UnknownError) {
        return make_unexpected(
            Tr::tr("Failed to start terminal process: \"%1\"").arg(process->errorString()));
    }

    qint64 pid = process->processId();

    return pid;
}

} // namespace Utils
