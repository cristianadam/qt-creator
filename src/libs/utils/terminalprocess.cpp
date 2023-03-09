// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "terminalprocess_p.h"

#include "terminalcommand.h"

#include <QTemporaryFile>

namespace Utils {
namespace Internal {

class ProcessStubCreator : public Utils::StubCreator
{
public:
    ProcessStubCreator(TerminalImpl *interface)
        : m_interface(interface)
    {}

    void startStubProcess(const CommandLine &cmd, const ProcessSetupData &) override
    {
        if (HostOsInfo::isWindowsHost()) {
            m_terminalProcess.setCommand(cmd);
            QObject::connect(&m_terminalProcess, &QtcProcess::done, this, [this]() {
                m_interface->onStubExited();
            });
            m_terminalProcess.start();
        } else {
            if (HostOsInfo::isMacHost()) {
                QTemporaryFile f;
                f.setAutoRemove(false);
                f.open();
                f.setPermissions(QFile::ExeUser | QFile::ReadUser | QFile::WriteUser);
                f.write("#!/bin/sh\n");
                f.write(QString("exec '%1' %2\n")
                            .arg(cmd.executable().nativePath())
                            .arg(cmd.arguments())
                            .toUtf8());
                f.close();

                const QString path = f.fileName();
                const QString exe
                    = QString("tell app \"Terminal\" to do script \"'%1'; rm -f '%1'; exit\"")
                          .arg(path);

                m_terminalProcess.setCommand({"osascript", {"-e", exe}});
                m_terminalProcess.runBlocking();
            } else {
                const TerminalCommand terminal = TerminalCommand::terminalEmulator();

                CommandLine cmdLine = {terminal.command, {terminal.executeArgs}};
                cmdLine.addCommandLineAsArgs(cmd, CommandLine::Raw);

                qDebug() << cmdLine.toUserOutput();

                m_terminalProcess.setCommand(cmdLine);
                m_terminalProcess.start();
            }
        }
    }

    TerminalImpl *m_interface;
    QtcProcess m_terminalProcess;
};

TerminalImpl::TerminalImpl()
{
    setStubCreator(new ProcessStubCreator(this));
}

} // Internal
} // Utils
