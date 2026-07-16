// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bridgeengine.h"

#include <debugger/dap/dapclient.h>

#include <coreplugin/icore.h>

#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/qtcprocess.h>

#include <QLoggingCategory>

using namespace Core;
using namespace Utils;

namespace Debugger::Internal {

// Runs the debugger process (gdb hosting gdbbridge.py) and exposes its stdio
// to the DapClient. Kept separate from the dap/ ProcessDataProvider to avoid
// coupling the bridge engine to that translation unit.
class BridgeDataProvider : public IDataProvider
{
public:
    BridgeDataProvider(const DebuggerRunParameters &rp,
                       const CommandLine &cmd,
                       QObject *parent = nullptr)
        : IDataProvider(parent)
        , m_runParameters(rp)
        , m_cmd(cmd)
    {
        connect(&m_proc, &Process::started, this, &IDataProvider::started);
        connect(&m_proc, &Process::done, this, &IDataProvider::done);
        connect(&m_proc, &Process::readyReadStandardOutput,
                this, &IDataProvider::readyReadStandardOutput);
        connect(&m_proc, &Process::readyReadStandardError,
                this, &IDataProvider::readyReadStandardError);
    }

    ~BridgeDataProvider() override
    {
        m_proc.kill();
        m_proc.waitForFinished();
    }

    void start() override
    {
        m_proc.setProcessMode(ProcessMode::Writer);
        if (m_runParameters.debugger().workingDirectory.isDir())
            m_proc.setWorkingDirectory(m_runParameters.debugger().workingDirectory);
        m_proc.setEnvironment(m_runParameters.debugger().environment);
        m_proc.setCommand(m_cmd);
        m_proc.start();
    }

    bool isRunning() const override { return m_proc.isRunning(); }
    void writeRaw(const QByteArray &data) override
    {
        if (m_proc.state() == ProcessState::Running)
            m_proc.writeRaw(data);
    }
    void kill() override { m_proc.kill(); }
    QByteArray readAllStandardOutput() override { return m_proc.readAllStandardOutput().toUtf8(); }
    QString readAllStandardError() override { return m_proc.readAllStandardError(); }
    int exitCode() const override { return m_proc.exitCode(); }
    QString executable() const override { return m_proc.commandLine().executable().toUserOutput(); }

    QProcess::ExitStatus exitStatus() const override { return toQProcess(m_proc.exitStatus()); }
    QProcess::ProcessError error() const override { return toQProcess(m_proc.error()); }
    Utils::ProcessResult result() const override { return m_proc.result(); }
    QString exitMessage() const override { return m_proc.exitMessage(); }

private:
    Utils::Process m_proc;
    const DebuggerRunParameters m_runParameters;
    const CommandLine m_cmd;
};

class BridgeClient : public DapClient
{
public:
    BridgeClient(IDataProvider *provider, QObject *parent = nullptr)
        : DapClient(provider, parent)
    {}

private:
    const QLoggingCategory &logCategory() override
    {
        static const QLoggingCategory category("qtc.dbg.bridgeengine", QtWarningMsg);
        return category;
    }
};

BridgeEngine::BridgeEngine()
    : DapEngine()
{
    setObjectName("BridgeEngine");
    setDebuggerName("Gdb");
    setDebuggerType("Bridge");
}

void BridgeEngine::setupEngine()
{
    QTC_ASSERT(state() == EngineSetupRequested, qCDebug(logCategory()) << state());

    const DebuggerRunParameters &rp = runParameters();
    const FilePath dumperDir = ICore::resourcePath("debugger");

    // Start gdb with the bridge loaded and hand control to its DAP-shaped
    // server loop, which frames requests/responses/events as Content-Length
    // JSON on stdio (the same framing DapClient already parses).
    // TODO: theDumper.runDapServer() is the bridge-side entry point added in
    // the next step; until then this engine starts but does not converse.
    CommandLine cmd{rp.debugger().command.executable(), {"--nx", "--nw", "-q"}};
    cmd.addArgs({"-iex", "python sys.path.insert(1, '" + dumperDir.path() + "')"});
    cmd.addArgs({"-iex", "python from gdbbridge import *"});
    cmd.addArgs({"-ex", "python theDumper.runDapServer()"});

    IDataProvider *dataProvider = new BridgeDataProvider(rp, cmd, this);
    m_dapClient = new BridgeClient(dataProvider, this);

    connectDataGeneratorSignals();
    m_dapClient->dataProvider()->start();
}

bool BridgeEngine::acceptsBreakpoint(const BreakpointParameters &bp) const
{
    const auto mimeType = Utils::mimeTypeForFile(bp.fileName);
    return mimeType.matchesName(Utils::Constants::C_HEADER_MIMETYPE)
           || mimeType.matchesName(Utils::Constants::C_SOURCE_MIMETYPE)
           || mimeType.matchesName(Utils::Constants::CPP_HEADER_MIMETYPE)
           || mimeType.matchesName(Utils::Constants::CPP_SOURCE_MIMETYPE)
           || bp.type == BreakpointByFunction;
}

const QLoggingCategory &BridgeEngine::logCategory()
{
    static const QLoggingCategory category("qtc.dbg.bridgeengine", QtWarningMsg);
    return category;
}

DebuggerEngine *createBridgeEngine()
{
    return new BridgeEngine;
}

} // namespace Debugger::Internal
