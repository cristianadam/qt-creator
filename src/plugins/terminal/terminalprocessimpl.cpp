// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "terminalprocessimpl.h"
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

class ProcessStubCreator : public Utils::StubCreator
{
public:
    ProcessStubCreator(TerminalProcessImpl *interface, TerminalPane *terminalPane)
        : m_terminalPane(terminalPane)
        , m_interface(interface)
    {}

    void startStubProcess(const Utils::CommandLine &cmd, const ProcessSetupData &setup) override
    {
        Utils::Id id = Utils::Id::fromString(setup.m_commandLine.executable().toUserOutput());

        TerminalWidget *terminal = m_terminalPane->stoppedTerminalWithId(id);

        const Utils::Terminal::OpenTerminalParameters
            openParameters{cmd, std::nullopt, std::nullopt, Utils::Terminal::ExitBehavior::Keep, id};

        if (!terminal) {
            terminal = new TerminalWidget(nullptr, openParameters);

            terminal->setShellName(setup.m_commandLine.executable().fileName());
            m_terminalPane->addTerminal(terminal, "App");
        } else {
            terminal->restart(openParameters);
        }

        connect(terminal, &TerminalWidget::destroyed, this, [this] {
            if (m_interface->inferiorProcessId())
                m_interface->emitFinished(-1, QProcess::CrashExit);
        });
    }

    TerminalPane *m_terminalPane;
    TerminalProcessImpl *m_interface;
};

TerminalProcessImpl::TerminalProcessImpl(TerminalPane *terminalPane)
{
    auto creator = new ProcessStubCreator(this, terminalPane);
    creator->moveToThread(qApp->thread());
    setStubCreator(creator);
}

} // namespace Terminal
