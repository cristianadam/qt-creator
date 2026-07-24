// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../debuggerengineinterface.h"

#include <utils/filepath.h>
#include <utils/processinterface.h>
#include <utils/qtcprocess.h>

namespace Debugger::Internal {

// Third real DebuggerEngineInterface implementation, alongside GdbImpl/
// LldbImpl - the first one debugging a fundamentally different kind of
// debuggee (an interpreted Python script via pdb, not a compiled native
// binary via gdb/lldb). Checked directly against real PdbEngine
// (pdb/pdbengine.{h,cpp}) rather than assumed: unlike gdb/lldb, pdb's
// wire protocol has no per-command token/correlation scheme at all -
// replies are matched purely by which GdbMi-shaped prefix they start
// with ("stack="/"data="/"modules="/"symbols="/"location="/"state="),
// relying on strict request/reply ordering (see handleOutputLine()) -
// the same assumption real PdbEngine::handleOutput2() already makes, not
// a new limitation introduced here. Two send channels, not one: plain
// text commands written directly to stdin (postDirectCommand(), mirrors
// PdbEngine::postDirectCommand() - "step"/"next"/"continue"/"return"/
// "break loc"/"enable id"/"disable id"/"clear id"/"global expr=value"),
// and "qdebug('<function>', <args>)"-wrapped calls for structured data
// (runCommand(), mirrors PdbEngine::runCommand() -
// stackListFrames/updateData/listModules/listSymbols).
//
// Slice goes a bit further than what real PdbEngine actually supports
// today: its own executeRunToLine()/executeRunToFunction()/
// executeJumpToLine() are literal QTC_CHECK("FIXME..." && false) stubs,
// but pdb itself already has everything needed underneath - "tbreak
// loc"/"tbreak function" (a self-deleting one-shot breakpoint, verified
// live: pdb reports and then auto-removes it, so no cleanup bookkeeping
// is needed here) plus "continue" covers RunToLine/RunToFunction; "jump
// line" (repositions the frame's line pointer, verified live to need a
// follow-up "next" to actually resume execution from there - it does not
// resume on its own) covers JumpToLine. ResetInferior is real too, but
// for a different reason than GdbImpl's own version: gdb's breakpoints
// belong to its still-alive target and simply survive a kill-and-re-exec,
// while a freshly relaunched pdb process starts with zero breakpoints of
// its own - see m_activeBreakpoints/startPdbProcess()'s comments for how
// this replays them instead. updateBreakpoint() only ever toggles
// enabled/disabled (real PdbEngine's own comment: "FIXME figure out what
// needs to be changed"); no disassembly/registers/memory access (real
// PdbEngine's hasCapability() never claims any of those - Python has
// nothing resembling an address or a CPU register to expose); Detach/
// Return/reverse-debugging have no pdb equivalent at all (bdb has no
// partial-detach or process-record concept, and no synchronous frame-pop
// without running); shutdownInferior() is a pure no-op for both Kill and
// Detach, matching real PdbEngine exactly - every session ends via
// shutdownEngine() killing the whole interpreter process.
//
// One deliberate deviation from real PdbEngine's own wiring: an
// explicitly requested Interrupt is reported as StopOk here, not
// SpontaneousStop - real PdbEngine's refreshState() calls
// notifyInferiorSpontaneousStop() for the "state=\"stopped\"" report
// sigint_handler() sends, even though that report only ever follows an
// explicit interrupt (the only call site - checked in pdbbridge.py).
// Reporting it as StopOk instead matches GdbImpl/LldbImpl's own
// Interrupt handling and GenericDebuggerEngine's expectations for the
// two events' different messaging - see handleOutputLine()'s "state="
// case.
class DEBUGGER_EXPORT PdbImplStartData
{
public:
    Utils::ProcessRunData debuggerRunData;   // the python3 interpreter
    InferiorStartData inferiorStartData;     // the script to debug (ProcessRunData alternative)
    Utils::FilePath dumperScriptsDir;
};

class DEBUGGER_EXPORT PdbImpl final : public DebuggerEngineInterface
{
    Q_OBJECT

public:
    explicit PdbImpl(const PdbImplStartData &startData);
    ~PdbImpl() override;

private:
    void start() final;
    void shutdownInferior(ShutdownMode mode) final;
    void shutdownEngine() final;

    void execute(const ExecutionRequest &request) final;
    void changeBreakpoint(const BreakpointChangeRequest &request) final;
    void refresh(const RefreshRequest &request) final;

    // Not ported this slice - see the class comment (real PdbEngine has no
    // working body for these either, beyond activateFrame()'s trivial
    // model-index bookkeeping, which belongs to DebuggerEngine/
    // GenericDebuggerEngine here, not this backend).
    void selectThread(const QString &threadId) final;
    void activateFrame(int index) final;
    void setRegisterValue(const QString &name, const QString &value) final;
    void accessMemory(MemoryOp op, quint64 requestId, quint64 addr, quint64 lengthOrSize,
                      const QByteArray &data) final;
    void fetchDisassembly(quint64 requestId, quint64 address, const QString &functionName) final;
    void setPeripheralRegisterValue(quint64 address, quint64 value) final;
    void watchPoint(quint64 requestId, const QPoint &pnt) final;
    void createSnapshot(quint64 requestId) final;

    // Real: mirrors PdbEngine::assignValueInDebugger() exactly, including
    // its "global expr;expr=value" trick - see the .cpp for the caveat
    // this carries over unfixed.
    void assignValueInDebugger(const WatchItemData &item, const QString &expr,
                               const QString &value) final;

    // Real: just forwards the raw command, mirrors
    // PdbEngine::executeDebuggerCommand().
    void executeDebuggerCommand(const QString &command,
                                const WatchItemData &inspectorItem) final;

    // Splits accumulated stdout on '\n' (mirrors PdbEngine::handleOutput()
    // exactly - pdbbridge.py's own report() framing, "@\n...\n@\n", is
    // just two bare "@" lines once split this way; they match no known
    // prefix below and are silently ignored, same as the plain print()
    // debug noise pdbbridge.py emits unconditionally - confirmed real
    // PdbEngine::handleOutput2() already relies on this exact tolerance,
    // not a new gap introduced here).
    void handlePdbOutput(const QString &output);
    void handleOutputLine(const QString &line);

    void runCommand(const DebuggerCommand &command);
    void postDirectCommand(const QString &command);

    PdbImplStartData m_startData;
    Utils::Process m_pdbProc;
    QString m_inbuffer;

    // No separate inferior process - pdb traces the script in-process (see
    // the class comment), so there is no pid distinct from m_pdbProc's own
    // to interrupt.
    //
    // Tracks whether the target is currently running, purely from what's
    // been sent/received here (true right after Continue/StepIn/StepOver/
    // StepOut are dispatched, false once the matching "location="/
    // "state=" reply arrives) - mirrors GdbImpl's/LldbImpl's own
    // m_inferiorRunning, needed for the same reason: pdb's "location="
    // report fires for every kind of stop (breakpoint hit during a
    // Continue, or a step/next/return landing), so this - not the report
    // itself - is what distinguishes a real new stop from the harmless
    // second "location=" that always follows the interrupt-triggered
    // "state=\"stopped\"" report (see handleOutputLine()'s comment,
    // mirroring PdbEngine::refreshLocation()'s own state()-gated no-op).
    bool m_inferiorRunning = false;
    // See handleOutputLine()'s "location=" comment - swallows pdb's own
    // natural pre-main stop (always the first one reported, before
    // anything sent from here has taken effect) so it can never be
    // misreported as the result of whatever request happened to be
    // outstanding at the time.
    bool m_sawInitialLocation = false;
    // Set right before dispatching Continue, cleared before dispatching
    // any of StepIn/StepOver/StepOut - distinguishes a breakpoint
    // interrupting a free-running Continue (SpontaneousStop) from a
    // step/next/return landing on its intended line (StopOk) once
    // "location=" arrives, since pdb's own reply carries no such
    // distinction itself.
    bool m_expectSpontaneousStop = false;
    // Set right before sending SIGINT for execute(Interrupt); cleared - and
    // consumed to pick StopOk over SpontaneousStop - once the matching
    // "state=\"stopped\"" reply arrives. See the class comment on why this
    // maps to StopOk here, unlike real PdbEngine's own SpontaneousStop
    // choice.
    bool m_interruptRequested = false;
    // Set false right before dispatching Continue, true once pdbbridge.py's
    // do_continue() confirms (via "state=\"running\"") that it has actually
    // installed sigint_handler - only from that point on is it safe to send
    // SIGINT at all. Before that, do_continue()'s own "continue" line may
    // still be mid-flight through cmdloop()'s blocking readline(), where a
    // SIGINT is caught by Python's *default* handler instead and silently
    // lost (confirmed live) - worse, retrying immediately re-interrupts that
    // same in-progress read, so "continue" can never get fully read at all.
    // See m_interruptPending's own comment for the other half of this.
    bool m_continueConfirmedRunning = false;
    // Set instead of sending SIGINT immediately when execute(Interrupt)
    // arrives before m_continueConfirmedRunning - consumed (sending the
    // real interrupt then) the moment that confirmation arrives.
    bool m_interruptPending = false;
    // Set once the pdb subprocess itself exits (Process::done) - guards
    // execute()/changeBreakpoint() against writing to a dead process
    // afterward, mirroring LldbImpl's own m_inferiorExited (for a
    // different underlying reason: there is no separate inferior process
    // to detect exiting independently - see the class comment - the whole
    // session is over once this fires, same as real PdbEngine's
    // handlePdbDone() unconditionally treating process exit as the
    // session ending).
    bool m_inferiorExited = false;

    // No token/correlation channel exists on the wire (see the class
    // comment) - each of these tracks the one outstanding requestId for
    // its RefreshKind, matching real PdbEngine's own implicit single-
    // outstanding-request-per-kind assumption (nothing there queues or
    // de-dupes concurrent requests of the same kind either).
    quint64 m_pendingStackRequestId = 0;
    quint64 m_pendingLocalsRequestId = 0;
    quint64 m_pendingModulesRequestId = 0;
    quint64 m_pendingModuleSymbolsRequestId = 0;

    // Stashed by refresh(Locals) for execute(RepeatLastCommand) - mirrors
    // GdbImpl::m_lastDebuggableCommand (see its own comment): re-sends the
    // last Locals-fetch command, fire-and-forget, so the Log Window's
    // "repeat" button can retrigger a dumper crash to see its raw traceback.
    // Default-constructed (empty function name) until the first
    // refresh(Locals) - execute(RepeatLastCommand) no-ops until then.
    DebuggerCommand m_lastDebuggableCommand;

    // FIFO of requests for breakpoints inserted but not yet confirmed -
    // pdb's own reply ("Breakpoint N at file:line") carries neither a
    // token nor anything else this class supplied at insert time, so the
    // only available correlation is arrival order: do_break() runs (and
    // prints its reply) synchronously, before pdb's stdin reader moves on
    // to the next queued command, so replies arrive in the same order
    // their requests were sent. The full request (not just its id) is
    // kept so a confirmed insert can be copied into m_activeBreakpoints.
    QList<BreakpointChangeRequest> m_pendingBreakpointInserts;
    // Counts "Breakpoint N at ..." replies that belong to this class's own
    // internal bookkeeping, not a real Insert request - a RunToLine/
    // RunToFunction's own "tbreak" (see execute()'s comment: pdb auto-
    // deletes it and reports that too, as a bare, unprefixed "Deleted
    // breakpoint N..." line this class already ignores - only the initial
    // "Breakpoint N at..." insert reply needs suppressing here) or a
    // ResetInferior replaying m_activeBreakpoints into the freshly
    // relaunched process. Checked before m_pendingBreakpointInserts so
    // neither of those ever steals or corrupts a real, caller-visible
    // insert's place in that FIFO.
    int m_pendingSilentBreakpointReplies = 0;
    // Set by execute(JumpToLine) right before its own "stackListFrames"
    // query, cleared once handleOutputLine()'s "stack=" branch answers it -
    // see execute()'s own comment on why a query is needed at all here,
    // unlike every other stop.
    bool m_awaitingJumpToLineLocation = false;
    // Every breakpoint currently believed inserted (added on a successful
    // Insert reply, removed on Remove) - needed only to replay them into a
    // freshly relaunched process: unlike gdb/lldb's own ResetInferior
    // (breakpoints belong to the still-alive debugger session, e.g.
    // gdb's own target object, and simply survive a kill-and-re-exec), a
    // new pdb process is a wholly fresh interpreter with zero breakpoints
    // of its own - see execute()'s ResetInferior case.
    QList<BreakpointChangeRequest> m_activeBreakpoints;

    // Builds the CommandLine/environment/working directory and starts
    // m_pdbProc - split out of start() so ResetInferior (execute()'s own
    // case) can relaunch a fresh process the same way without duplicating
    // it, mirroring GdbImpl's own kill-and-relaunch approach for the one
    // mode both slices support (plain local launch).
    void startPdbProcess();
    // Set right before startPdbProcess() is called from ResetInferior
    // (never from the initial start()) - checked by the Process::started
    // handler to replay m_activeBreakpoints and send "continue" instead of
    // emitting EngineSetupOk/RunAndInferiorStopOk again, which only make
    // sense once, for the session's original launch.
    bool m_isResetRestart = false;
    // Set by shutdownEngine() right before it kills m_pdbProc - checked by
    // the Process::done handler so that deliberate kill isn't reported as
    // the debuggee spontaneously exiting (InferiorEvent::Exited) - see
    // shutdownEngine()'s own comment.
    bool m_shuttingDown = false;
};

} // namespace Debugger::Internal
