// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../debuggerengineinterface.h"

#include <utils/filepath.h>
#include <utils/processinterface.h>
#include <utils/qtcprocess.h>

#include <QHash>

#include <optional>

namespace Debugger::Internal {

// Second real DebuggerEngineInterface implementation, alongside GdbImpl -
// proves the interface (and tst_backends.cpp's backend-agnostic test
// bodies, data-driven over a Backend enum for exactly this reason) actually
// generalizes to a second real backend, not just gdb.
//
// Grew well past the initial slice: execute()/changeBreakpoint()/every
// RefreshKind, attach-to-process/terminal/running-remote-server/core-file,
// conditional/tracepoint breakpoints, and native-mixed QML+C++ debugging
// are all covered now - same "additive, not a redesign" growth path
// GdbImpl itself followed (see project_debugger_redesign_proposal.md).
// Not yet ported: attach-by-pid over extended-remote and running a remote
// executable via extended-remote (both blocked on a confirmed lldb bug,
// not fixable from here - see project_lldbimpl_extended_remote_hang.md),
// and one Windows-specific gap (AttachToTerminalStubData's handling is
// missing GdbImpl's CREATE_SUSPENDED/ResumeThread special case).
//
// Talks to a real lldb process the same way real LldbEngine
// (lldb/lldbengine.cpp) does: spawn lldb, load lldbbridge.py, then wrap
// every command uniformly as "script theDumper.<function>(<args>)" - unlike
// gdb, lldb's Creator integration has no separate native-command channel,
// so there's no MI-vs-python-bridge branching to replicate here (see
// LldbEngine::runCommand()). Replies come back as GdbMi-syntax text - the
// same wire format gdb produces, since GdbMi is a generic value tree, not
// gdb-specific (confirmed: LldbEngine::handleResponse() already parses lldb
// replies via GdbMi::fromStringMultiple()) - just wrapped in "@\n...\n@"
// block markers instead of gdb MI's per-line framing (see
// handleLldbOutput()'s comment).
//
// No NeedsTemporaryStop/NeedsFullStop deferral here, unlike GdbImpl:
// confirmed against real LldbEngine::executeDebuggerCommand() (and every
// other command this slice covers) that none of them use those flags -
// lldb's own command interpreter doesn't have gdb's "MI channel fully
// blocked while the inferior runs" limitation that made GdbImpl's
// m_onStopCommands deferral necessary in the first place.
class DEBUGGER_EXPORT LldbImplStartData
{
public:
    Utils::ProcessRunData debuggerRunData;
    InferiorStartData inferiorStartData;
    Utils::FilePath dumperScriptsDir;
    bool nativeMixedDebugging = false;
};

class DEBUGGER_EXPORT LldbImpl final : public DebuggerEngineInterface
{
    Q_OBJECT

public:
    explicit LldbImpl(const LldbImplStartData &startData);
    ~LldbImpl() override;

private:
    void start() final;
    void shutdownInferior(ShutdownMode mode) final;
    void shutdownEngine() final;

    void execute(const ExecutionRequest &request) final;
    void changeBreakpoint(const BreakpointChangeRequest &request) final;
    void refresh(const RefreshRequest &request) final;

    // Not yet ported this slice - see the class comment. Each reports
    // failure/no-op rather than crashing; a later slice gives them real
    // bodies the same incremental way GdbImpl grew its own.
    void selectThread(const QString &threadId) final;
    void activateFrame(int index) final;
    void setRegisterValue(const QString &name, const QString &value) final;
    void accessMemory(MemoryOp op, quint64 requestId, quint64 addr, quint64 lengthOrSize,
                      const QByteArray &data) final;
    void fetchDisassembly(quint64 requestId, quint64 address, const QString &functionName) final;
    void assignValueInDebugger(const WatchItemData &item, const QString &expr,
                               const QString &value) final;
    void setPeripheralRegisterValue(quint64 address, quint64 value) final;
    void watchPoint(quint64 requestId, const QPoint &pnt) final;
    void createSnapshot(quint64 requestId) final;

    // Real: just forwards the raw command, mirroring real
    // LldbEngine::executeDebuggerCommand() - a single "command" argument to
    // the theDumper bridge call, not a raw native command the way
    // GdbImpl::executeDebuggerCommand() sends it (lldb has no equivalent
    // native-command channel here - see the class comment).
    void executeDebuggerCommand(const QString &command,
                                const WatchItemData &inspectorItem) final;

    // Splits accumulated stdout on "@\n"/"@\r\n" (mirrors real
    // LldbEngine::readLldbStandardOutput() exactly), parses each complete
    // block via GdbMi::fromStringMultiple() (reused as-is - the payload
    // format is the same GdbMi tuple/list syntax gdb produces), and
    // dispatches each top-level item by name, mirroring real
    // LldbEngine::handleResponse().
    void handleLldbOutput(const QString &output);
    // Mirrors LldbEngine::handleStateNotification()'s exact state-name ->
    // InferiorEvent mapping - the state vocabulary lldbbridge.py reports
    // corresponds 1:1 to InferiorEvent's own enumerators.
    void handleStateReport(const GdbMi &item);
    // Mirrors LldbEngine::handleTracepointHit(): lldbbridge.py's
    // breakpointCallback() reports each "{expr}" as a raw value/valueencoded
    // pair (item["expressions"]), not a pre-substituted message - substitute
    // and decodeData() them here, the single source of truth for all string
    // encodings and the quoting convention (same as GdbImpl's own
    // handleTracepointHit()).
    void handleTracepointHit(const GdbMi &item);
    // See its own comment in lldbimpl.cpp. Emits the given InferiorEvent
    // itself, only after the fetched location (if any) is applied - not
    // the caller's job, since this round-trip is asynchronous (see the
    // comment).
    void fetchLocationAfterStop(InferiorEvent event);

    // The "assign a token, wrap as theDumper.<function>(<args>), write to
    // the process" logic. No deferral to check here - see the class
    // comment - so unlike GdbImpl's runCommand()/runCommandNow() split,
    // this is the only dispatch method; kept under this name (rather than
    // e.g. runCommandNow()) so a later slice that does need
    // NeedsFullStop-style deferral can insert it here without a rename.
    void runCommand(const DebuggerCommand &command);
    void reportInferiorExitIfComplete();

    LldbImplStartData m_startData;
    Utils::Process m_lldbProc;
    QString m_inbuffer;
    QHash<int, DebuggerCommand> m_commandForToken;
    int m_lastToken = 0;
    // Stashed by refresh(Locals) for execute(RepeatLastCommand) - mirrors
    // LldbEngine::m_lastDebuggableCommand (and GdbImpl's own field of the same
    // name): re-sends the last Locals-fetch command, fire-and-forget, so the Log
    // Window's "repeat" button can retrigger a dumper crash to see its raw
    // traceback. Default-constructed (empty function name) until the first
    // refresh(Locals) - execute(RepeatLastCommand) no-ops until then.
    DebuggerCommand m_lastDebuggableCommand;
    // Set once handleStateReport() sees "inferiorexited" - checked by
    // execute()'s Continue case before sending anything to lldb at all.
    // Confirmed the hard way (manual SBProcess repro outside this class
    // entirely) that lldb's own SBProcess::Continue() can itself hang
    // indefinitely when called again on an already-exited process - not
    // just fail fast the way gdb's MI does ("The program is not being
    // run.") - so the stale-Continue-after-exit case has to be caught
    // client-side before ever reaching lldb, not detected from its reply.
    bool m_inferiorExited = false;
    // The real status from lldbbridge.py's own "exited" report, and whether
    // inferiorDone() has already gone out - see reportInferiorExitIfComplete().
    std::optional<int> m_inferiorExitCode;
    bool m_inferiorExitReported = false;
    // Set/cleared alongside RunOk-ish/stopped-ish state reports - checked by
    // execute()'s Interrupt case, mirroring GdbImpl's own m_inferiorRunning
    // check: confirmed against real lldb that interruptInferior() (like
    // GdbImpl's own comment about "-exec-interrupt") replies success for an
    // already-stopped target without any subsequent state report ever
    // following to resolve the request, so asking lldb at all would leave
    // the caller waiting forever for a StopOk that never comes.
    bool m_inferiorRunning = false;
    // Set from the "pid" report runEngine() sends on a successful launch -
    // mirrors GdbImpl's own m_inferiorPid field/name exactly. Used only as
    // the destructor's last-resort cleanup (see its own comment); GdbImpl
    // itself only ever needs this for interruptProcess(), not a teardown
    // kill, since gdb reliably kills its own debuggee on exit (ptrace
    // exit-kill) - lldb was confirmed (the hard way, via orphaned
    // qmlstack_inferior/qmlmix_inferior processes surviving whole test-
    // process teardowns) to not do this reliably in this harness.
    qint64 m_inferiorPid = -1;
};

} // namespace Debugger::Internal
