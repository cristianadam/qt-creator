// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pdbimpl.h"

#include "../breakpoint.h"
#include "../debuggerconstants.h"
#include "../procinterrupt.h"

#include <utils/environment.h>
#include <utils/qtcassert.h>

#include <QStringDecoder>

using namespace Utils;

namespace Debugger::Internal {

// Same synthesis helper as GdbImpl's own constMi() (gdb/gdbimpl.cpp) - pdb
// never produces real GdbMi wire text for a breakpoint reply (just the
// plain "Breakpoint N at file:line" line parsed in handleOutputLine()), so
// the "bkpt"-shaped tuple GenericDebuggerEngine::applyBkptData() expects
// has to be built by hand.
static GdbMi constMi(const QString &name, const QString &data)
{
    GdbMi mi;
    mi.m_type = GdbMi::Const;
    mi.m_name = name;
    mi.m_data = data;
    return mi;
}

// Shared by changeBreakpoint()'s Insert case and execute(ResetInferior)'s
// replay of m_activeBreakpoints into the freshly relaunched process - the
// same "[filename:]lineno[, condition]"/"function[, condition]" text
// do_break() accepts either way.
static QString breakpointLocation(const BreakpointParameters &params)
{
    QString loc;
    if (params.type == BreakpointByFunction)
        loc = params.functionName;
    else
        loc = params.fileName.path() + ':' + QString::number(params.textPosition.line);
    if (!params.condition.isEmpty())
        loc += ", " + params.condition;
    return loc;
}

// Mirrors PdbEngine::PdbEngine()'s hasCapability()/acceptsBreakpoint()
// exactly - see the class comment on what's deliberately not claimed.
static DebuggerEngineSetupData pdbImplSetupData()
{
    DebuggerEngineSetupData data;
    data.capabilities = BreakConditionCapability
                      | JumpToLineCapability
                      | ReloadModuleCapability
                      | ReloadModuleSymbolsCapability
                      | ResetInferiorCapability
                      | RunToLineCapability
                      | ShowModuleSymbolsCapability;
    // No attach-related InferiorStartData alternative at all - see this
    // class' own comment (bdb has no attach-to-a-running-process mechanism).
    data.startModes = DebuggerStartModeFlag::Launch;
    data.toolTipHandling = ToolTipHandling::IfStoppedInferior;
    data.acceptsBreakpoint = [](const AcceptsBreakpointQuery &query) {
        // AttachToCore mirrors GdbImpl's/LldbImpl's own guard - unreachable
        // in practice (AttachToCore isn't supported here at all), kept for
        // consistency with the other two backends' own rule.
        if (query.startMode == AttachToCore)
            return false;
        return query.fileName.endsWith(".py");
    };
    return data;
}

PdbImpl::PdbImpl(const PdbImplStartData &startData)
    : DebuggerEngineInterface(pdbImplSetupData())
    , m_startData(startData)
{
    m_pdbProc.setProcessMode(ProcessMode::Writer);

    connect(&m_pdbProc, &Process::started, this, [this] {
        if (m_isResetRestart) {
            // See ExecutionCommand::ResetInferior's own comment: replays
            // every breakpoint this class still believes is inserted into
            // the fresh process (which starts with none of its own),
            // silently - "Breakpoint N at ..." is the same reply do_break()
            // always sends, but this isn't a new, caller-visible Insert.
            m_isResetRestart = false;
            for (const BreakpointChangeRequest &bp : m_activeBreakpoints) {
                ++m_pendingSilentBreakpointReplies;
                postDirectCommand("break " + breakpointLocation(bp.params));
            }
            m_expectSpontaneousStop = true;
            m_inferiorRunning = true;
            postDirectCommand("continue");
            return;
        }
        // Mirrors PdbEngine::handlePdbStarted(): no separate dumper-load/
        // executable-load handshake to wait for - the process launch
        // itself already ran the script through pdbbridge.py, so setup is
        // complete the instant the process starts (real PdbEngine's
        // notifyEngineSetupOk() immediately followed by
        // notifyEngineRunAndInferiorStopOk(), both synchronous).
        // No separate inferior pid to report (see the class comment) -
        // this process's own pid doubles as both.
        emit inferiorPidKnown(ProcessHandle(m_pdbProc.processId()));
        emit inferiorEvent(InferiorEvent::EngineSetupOk);
        emit inferiorEvent(InferiorEvent::RunAndInferiorStopOk);
        // Mirrors GdbImpl's own start(): auto-runs immediately once setup
        // completes, unconditionally - a caller that wants to stop at the
        // very first line instead just inserts a breakpoint there first,
        // synchronously, while still reacting to EngineSetupOk above (the
        // two emits above are synchronous too, so any breakpoint the
        // caller inserts from within them is sent - and, for pdb, already
        // sitting in do_break()'s own queue - before "continue" below is).
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunAndInferiorRunOk);
        postDirectCommand("continue");
    });
    connect(&m_pdbProc, &Process::readyReadStandardOutput, this, [this] {
        handlePdbOutput(m_pdbProc.readAllStandardOutput());
    });
    connect(&m_pdbProc, &Process::readyReadStandardError, this, [this] {
        emit message(m_pdbProc.readAllStandardError(), LogError);
    });
    connect(&m_pdbProc, &Process::done, this, [this] {
        m_inferiorExited = true;
        if (m_pdbProc.result() == ProcessResult::StartFailed) {
            emit inferiorEvent(InferiorEvent::EngineSetupFailed);
        } else if (!m_shuttingDown) {
            // The only way to learn the script ran to completion at all -
            // pdbbridge.py has no "state=\"inferiorexited\"" report of its
            // own to react to instead (confirmed: grepping pdbbridge.py
            // shows no such report exists - real PdbEngine's own
            // refreshState() handles that case for a report that's never
            // actually sent). Matches real PdbEngine's own
            // handlePdbDone()->notifyEngineSpontaneousShutdown() in
            // treating "the pdb process itself exited" as "the session is
            // over", just split into the two distinct events this
            // interface exposes for that.
            //
            // Default InferiorResultData, not m_pdbProc.resultData():
            // pdbbridge.py always calls sys.exit(0) itself regardless of
            // the script's own outcome, so there's nothing real to
            // extract - matches real PdbEngine's own limitation.
            //
            // Gated on !m_shuttingDown - see shutdownEngine()'s own
            // comment: a deliberate kill (shutdownEngine(), or Abort - see
            // execute()) is not the debuggee spontaneously exiting.
            emit inferiorDone({});
        }
        emit engineProcessFinished(m_pdbProc.resultData());
    });
}

PdbImpl::~PdbImpl()
{
    // Unlike GdbImpl/LldbImpl, there is no separate inferior process to
    // worry about leaking past this object's own destruction (see the
    // class comment) - killing m_pdbProc, if still running, is the whole
    // teardown.
    if (m_pdbProc.isRunning())
        m_pdbProc.kill();
}

void PdbImpl::start()
{
    startPdbProcess();
}

void PdbImpl::startPdbProcess()
{
    const auto &inferiorRunData = std::get<ProcessRunData>(m_startData.inferiorStartData);
    CommandLine cmd{m_startData.debuggerRunData.command.executable(),
                    {m_startData.dumperScriptsDir.pathAppended("pdbbridge.py").path(),
                     inferiorRunData.command.executable().path()}};
    cmd.addArg(inferiorRunData.workingDirectory.path());
    cmd.addArg("--");
    cmd.addArgs(inferiorRunData.command.arguments(), CommandLine::Raw);
    m_pdbProc.setCommand(cmd);

    // Only one real OS process exists for pdb (see the class comment) - the
    // inferior's own configured environment is applied directly to that
    // same process instead, layered on top of the debugger's own; there is
    // no separate channel to send it to. Real PdbEngine never does this at
    // all (the debuggee just inherits python3's own environment
    // unconditionally) - a real, standalone gap this closes, not carried
    // forward. appliedToEnvironment() (not a hand-rolled Environment::diff()
    // + set()/unset() loop, which this used to be) is the same idiomatic
    // "layer configured changes onto a base" mechanism real code already
    // uses for this exact pattern - see e.g. TerminalInterface::onStarted()
    // or GitClient::processEnvironment().
    m_pdbProc.setEnvironment(inferiorRunData.environment.appliedToEnvironment(
        m_startData.debuggerRunData.environment));
    if (inferiorRunData.workingDirectory.isDir())
        m_pdbProc.setWorkingDirectory(inferiorRunData.workingDirectory);
    m_pdbProc.start();
}

void PdbImpl::shutdownInferior(ShutdownMode)
{
    // Mirrors PdbEngine::shutdownInferior() exactly: a pure no-op for both
    // Kill and Detach - see the class comment on why pdb has no partial-
    // detach concept of its own.
    emit inferiorEvent(InferiorEvent::ShutdownFinished);
}

void PdbImpl::shutdownEngine()
{
    if (!m_pdbProc.isRunning()) {
        emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
        return;
    }
    // Regression test for the same "engine process finishing after a
    // normal shutdown wrongly reports Exited" bug GdbImpl/LldbImpl
    // already guard against (see tst_backends.cpp's shutsDownCleanly()) -
    // Exited is meant for the debuggee spontaneously ending, not this
    // deliberate kill; checked by the Process::done handler in the
    // constructor.
    m_shuttingDown = true;
    connect(&m_pdbProc, &Process::done, this, [this] {
        emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
    }, Qt::SingleShotConnection);
    m_pdbProc.kill();
}

void PdbImpl::execute(const ExecutionRequest &request)
{
    if (m_inferiorExited && request.command != ExecutionCommand::Abort) {
        emit inferiorEvent(InferiorEvent::InferiorIll);
        return;
    }
    switch (request.command) {
    case ExecutionCommand::Continue:
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        m_continueConfirmedRunning = false;
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        // Callback will be triggered e.g. when a breakpoint is hit (mirrors
        // PdbEngine::continueInferior()'s own comment).
        postDirectCommand("continue");
        break;
    case ExecutionCommand::Interrupt:
        // Mirrors GdbImpl's own "already stopped" fast path (see its
        // m_inferiorRunning comment) - confirmed live that pdb's own
        // behavior is even less forgiving than gdb's "^done with no
        // *stopped ever following" here: sending SIGINT while already
        // blocked reading the next command in cmdloop() (allow_kbdint is
        // set for exactly that duration) raises a bare KeyboardInterrupt,
        // caught by interaction()'s own try/except ("--KeyboardInterrupt--",
        // a plain, unprefixed print()) - no "state=" report at all, ever,
        // for this specific case. Reporting StopOk directly avoids ever
        // sending it, rather than waiting forever for a reply that will
        // never come.
        if (!m_inferiorRunning) {
            emit inferiorEvent(InferiorEvent::StopOk);
            break;
        }
        // See m_continueConfirmedRunning's own comment: sending SIGINT any
        // earlier than this races "continue" itself still being read off
        // stdin - and unlike a plain "hasn't happened yet, wait a bit"
        // race, retrying blindly makes it worse (confirmed live): each new
        // SIGINT re-interrupts whatever read is currently in flight, so
        // "continue" can never fully be read at all. Deferring the actual
        // signal until pdbbridge.py's own confirmation arrives avoids the
        // race entirely instead of gambling on timing.
        if (!m_continueConfirmedRunning) {
            m_interruptPending = true;
            break;
        }
        m_interruptRequested = true;
        {
            QString error;
            interruptProcess(m_pdbProc.processId(), &error);
        }
        break;
    case ExecutionCommand::StepIn:
        // SpontaneousStop, not StopOk, once it lands - matches GdbImpl's/
        // LldbImpl's own convention (see tst_backends.cpp's
        // executesStepInAndReturn()): only a synchronous, non-running
        // action (Interrupt while already stopped, Return) is a StopOk;
        // anything that actually runs the target first - Continue or any
        // step - is a SpontaneousStop once it stops again, no matter how
        // briefly it ran.
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        postDirectCommand("step");
        break;
    case ExecutionCommand::StepOver:
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        postDirectCommand("next");
        break;
    case ExecutionCommand::StepOut:
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        postDirectCommand("return");
        break;
    case ExecutionCommand::Abort:
        m_pdbProc.kill();
        break;
    case ExecutionCommand::RunToLine:
        // "tbreak loc" + "continue" - verified live that pdb's own
        // temporary breakpoint reports and then auto-removes itself
        // ("Deleted breakpoint N...", a bare line this class already
        // ignores - see handleOutputLine()), so no cleanup bookkeeping is
        // needed here, only suppressing its "Breakpoint N at..." reply as
        // an insert this class itself asked for, not a new one to report.
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        ++m_pendingSilentBreakpointReplies;
        postDirectCommand("tbreak " + request.context.fileName.path() + ':'
                          + QString::number(request.context.textPosition.line));
        postDirectCommand("continue");
        break;
    case ExecutionCommand::RunToFunction:
        // Same as RunToLine, just "tbreak function" instead of a location.
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        ++m_pendingSilentBreakpointReplies;
        postDirectCommand("tbreak " + request.functionName);
        postDirectCommand("continue");
        break;
    case ExecutionCommand::JumpToLine:
        // "jump line" alone repositions the frame's own line pointer
        // (f_lineno) without resuming anything at all - no "location="
        // report to react to, ever, since we never leave cmdloop(), let
        // alone reach interaction() again. Verified live that following
        // it with a resuming command (next/step) doesn't report the jump
        // target either: "next" means "execute the current statement and
        // stop at the *following* one", so after jumping to line N, "next"
        // executes line N and reports N+1 - one past what was asked for.
        // Querying the position directly instead (mirrors LldbImpl's own
        // fetchLocationAfterStop() - a manual round trip for the same
        // reason, a synchronous action with no async notification of its
        // own to react to) - see the "stack=" handling in
        // handleOutputLine() for the other half of this.
        m_awaitingJumpToLineLocation = true;
        postDirectCommand("jump " + QString::number(request.context.textPosition.line));
        runCommand({"stackListFrames"});
        break;
    case ExecutionCommand::ResetInferior:
        // Mirrors GdbImpl's own kill-and-relaunch in spirit, but for a
        // different underlying reason: gdb's breakpoints belong to its
        // still-alive target and simply survive a kill-and-re-exec of the
        // inferior alone, while pdb has no such persistent session to
        // restart within - relaunching means a wholly fresh interpreter
        // process, with none of this class's own tracked breakpoints
        // (m_activeBreakpoints) - replayed into it once started (see the
        // Process::started handler's own comment).
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        m_pdbProc.kill();
        m_pdbProc.waitForFinished();
        m_sawInitialLocation = false;
        m_inferiorExited = false;
        m_inferiorRunning = false;
        m_expectSpontaneousStop = false;
        m_interruptRequested = false;
        m_isResetRestart = true;
        startPdbProcess();
        break;
    case ExecutionCommand::RepeatLastCommand:
        // Mirrors GdbImpl::execute()'s own RepeatLastCommand case - see
        // m_lastDebuggableCommand's comment. No-op if Locals were never
        // fetched (m_lastDebuggableCommand still default-constructed).
        if (!m_lastDebuggableCommand.function.isEmpty())
            runCommand(m_lastDebuggableCommand);
        break;
    case ExecutionCommand::Detach:
    case ExecutionCommand::Return:
    case ExecutionCommand::RecordReverse:
        // Not supported this slice - real PdbEngine has no working
        // equivalent either, and neither does pdb itself: bdb has no
        // partial-detach concept (the debuggee and the debugger are the
        // same OS process - see the class comment), no synchronous
        // frame-pop without running (unlike gdb's "-exec-return", pdb's
        // own "return" command runs the target until the frame actually
        // returns), and no process-record/reverse-execution feature at
        // all.
        emit message("PdbImpl::execute() does not support this command", LogWarning);
        break;
    }
}

void PdbImpl::changeBreakpoint(const BreakpointChangeRequest &request)
{
    const quint64 requestId = request.requestId;
    switch (request.op) {
    case BreakpointOp::Insert:
        // Mirrors PdbEngine::insertBreakpoint() - file/line or function,
        // plus the condition real PdbEngine's own hasCapability() already
        // claims support for (BreakConditionCapability) but its
        // insertBreakpoint() never actually sends - do_break()'s own
        // docstring confirms it accepts "[filename:]lineno[, condition]",
        // so this sends it for real instead of carrying the same gap
        // forward.
        m_pendingBreakpointInserts.append(request);
        postDirectCommand("break " + breakpointLocation(request.params));
        break;
    case BreakpointOp::Remove:
        // Pretend it succeeds without waiting for a response - mirrors
        // PdbEngine::removeBreakpoint() exactly (do_clear()'s own reply,
        // if any, is never parsed by either engine).
        for (int i = m_activeBreakpoints.size() - 1; i >= 0; --i) {
            if (m_activeBreakpoints.at(i).responseId == request.responseId)
                m_activeBreakpoints.removeAt(i);
        }
        postDirectCommand("clear " + request.responseId);
        emit breakpointEvent(requestId, BreakpointOp::Remove, true);
        break;
    case BreakpointOp::Update:
        // Mirrors PdbEngine::updateBreakpoint(): only the enabled/disabled
        // toggle is ported - see the class comment's "FIXME" reference.
        // Mirrors GdbImpl::updateBreakpointCommand()'s own early-return
        // guard too: an empty responseId means the insert reply hasn't
        // arrived yet (GenericDebuggerEngine::changeBreakpoint() checks
        // for this before ever calling here - see its own comment), so
        // there is nothing real to send a command for at all.
        if (request.responseId.isEmpty()) {
            emit breakpointEvent(requestId, BreakpointOp::Update, false);
            break;
        }
        if (request.params.enabled)
            postDirectCommand("enable " + request.responseId);
        else
            postDirectCommand("disable " + request.responseId);
        emit breakpointEvent(requestId, BreakpointOp::Update, true);
        break;
    case BreakpointOp::EnableSub:
        // Genuinely sends the real command (unlike a bare no-op, which
        // would be reporting success for something never even attempted) -
        // pdb's own do_enable()/do_disable() accept any breakpoint number,
        // "sub" or not, identically. Optimistic success, not correlated to
        // any reply, for the same reason Remove/Update above are: do_enable()/
        // do_disable() print a plain "Enabled <bp>"/"Disabled <bp>" line on
        // success but nothing at all on failure (self.error() is a no-op
        // there - see pdbbridge.py), so there is no reliable way to
        // distinguish the two from here - mirrors real PdbEngine's own
        // "pretend it succeeds without waiting for a response" precedent
        // for Remove, applied to the same-shaped problem here.
        postDirectCommand((request.enabled ? QLatin1String("enable ") : QLatin1String("disable "))
                          + request.subResponseId);
        emit breakpointEvent(requestId, BreakpointOp::EnableSub, true);
        break;
    }
}

void PdbImpl::refresh(const RefreshRequest &request)
{
    const quint64 requestId = request.requestId;
    switch (request.kind) {
    case RefreshKind::Locals: {
        // Mirrors PdbEngine::updateLocals(): frame is always the top/
        // current frame (0) - there is no stack-navigation model on this
        // side of the interface to ask for anything else (activateFrame()
        // is a GenericDebuggerEngine/DebuggerEngine-side concern here, see
        // the class comment).
        m_pendingLocalsRequestId = requestId;
        DebuggerCommand cmd("updateData");
        cmd.arg("nativeMixed", false);
        cmd.arg("fancy", true);
        cmd.arg("frame", 0);
        // Stashed for execute(RepeatLastCommand) - see
        // m_lastDebuggableCommand's own comment.
        m_lastDebuggableCommand = cmd;
        runCommand(cmd);
        return;
    }
    case RefreshKind::FullStack:
        // Mirrors PdbEngine::updateAll()'s "stackListFrames" call.
        m_pendingStackRequestId = requestId;
        runCommand({"stackListFrames"});
        return;
    case RefreshKind::Modules:
        // Mirrors PdbEngine::reloadModules().
        m_pendingModulesRequestId = requestId;
        runCommand({"listModules"});
        return;
    case RefreshKind::ModuleSymbols: {
        // Mirrors PdbEngine::requestModuleSymbols().
        m_pendingModuleSymbolsRequestId = requestId;
        DebuggerCommand cmd("listSymbols");
        cmd.arg("module", request.path.path());
        runCommand(cmd);
        return;
    }
    case RefreshKind::DebuggingHelpers:
        // Mirrors GdbImpl's own case exactly: "reloadDumpers" is a
        // DumperBase method (dumper.py), not a gdb/lldb-specific one - pdb
        // already goes through the same dumper machinery for Locals (see
        // the Locals case above), so this Just Works.
        runCommand({"reloadDumpers"});
        refresh({requestId, RefreshKind::Locals});
        return;
    case RefreshKind::AllSymbols:
        // Mirrors GdbImpl's own case, minus its leading "sharedlibrary .*" -
        // that reloads gdb's own shared-library symbol tables, which have
        // no pdb equivalent (Python modules don't need a reload step for
        // their symbols to be visible - they already are, the moment
        // they're imported). The three refreshes it chains into are real
        // and already working (Modules/FullStack/Locals, same as above).
        refresh({requestId, RefreshKind::Modules});
        refresh({requestId, RefreshKind::FullStack});
        refresh({requestId, RefreshKind::Locals});
        return;
    case RefreshKind::StackSymbols:
        // Genuinely a no-op, not a stub: mirrors GdbImpl's own case, which
        // is itself just "sharedlibrary <module>" fire-and-forget with no
        // reply of its own (see gdbimpl.cpp) - the same reasoning as
        // AllSymbols above means there is nothing to send at all here.
        return;
    default:
        // Not supported this slice - real PdbEngine's hasCapability() never
        // claims Registers/Memory/Disassembly/etc., and SourceFiles/
        // Threads/QmlStack have no pdbbridge.py equivalent either.
        emit refreshDataReceived(requestId, request.kind, {});
        return;
    }
}

void PdbImpl::selectThread(const QString &)
{
    // Mirrors PdbEngine::selectThread(): pdb is always single-threaded
    // from the debugger's point of view (bdb has no thread concept), so
    // this is a no-op there too.
}

void PdbImpl::activateFrame(int)
{
    // GenericDebuggerEngine-side model bookkeeping only (stackHandler()->
    // setCurrentIndex()/gotoLocation()) - mirrors PdbEngine::activateFrame(),
    // minus that part, same reasoning as GdbImpl/LldbImpl's own no-op
    // overrides of this method.
}

void PdbImpl::setRegisterValue(const QString &, const QString &)
{
    // Not supported - see the class comment (no RegisterCapability claimed).
}

void PdbImpl::accessMemory(MemoryOp, quint64, quint64, quint64, const QByteArray &)
{
    // Not supported - see the class comment (no ShowMemoryCapability claimed).
}

void PdbImpl::fetchDisassembly(quint64, quint64, const QString &)
{
    // Not supported - see the class comment (no DisassemblerCapability claimed).
}

void PdbImpl::assignValueInDebugger(const WatchItemData &item, const QString &expr,
                                    const QString &value)
{
    // Real PdbEngine::assignValueInDebugger() always sends "global
    // expr;expr=value" - confirmed live (against this class's own copy of
    // that exact command) that this is a real bug, not just a suspected
    // one: pdb's default() handler execs arbitrary commands with the
    // current frame's own locals dict passed straight through
    // (pdbbridge.py's `exec(code, pyGlobals, pyLocals)`), so a plain "expr
    // = value" already updates a local correctly - unconditionally
    // prefixing "global" instead forces the assignment into the *module*
    // namespace, silently leaving the actual local untouched (verified:
    // the watched local's value in the locals refresh that follows never
    // changed). Only add "global" for a genuine module-level watch item,
    // where it's actually needed and correct.
    if (item.isLocal)
        postDirectCommand(expr + '=' + value);
    else
        postDirectCommand("global " + expr + ';' + expr + '=' + value);
}

void PdbImpl::setPeripheralRegisterValue(quint64, quint64)
{
    // Not supported - pdb has no peripheral-register concept at all.
}

void PdbImpl::watchPoint(quint64, const QPoint &)
{
    // Not supported - see the class comment (no WatchWidgetsCapability
    // claimed); real PdbEngine has no equivalent either.
}

void PdbImpl::createSnapshot(quint64)
{
    // Not supported - see the class comment (no SnapshotCapability
    // claimed); real PdbEngine has no equivalent either.
}

void PdbImpl::executeDebuggerCommand(const QString &command,
                               const WatchItemData &inspectorItem)
{
    Q_UNUSED(inspectorItem) // Inspector view is QML-only, see the interface.
    // Mirrors PdbEngine::executeDebuggerCommand(): a plain passthrough.
    postDirectCommand(command);
}

void PdbImpl::postDirectCommand(const QString &command)
{
    QTC_ASSERT(m_pdbProc.isRunning(), return);
    emit message(command, LogInput);
    m_pdbProc.write(command + '\n');
}

void PdbImpl::runCommand(const DebuggerCommand &cmd)
{
    QTC_ASSERT(m_pdbProc.isRunning(), return);
    const QString command = "qdebug('" + cmd.function + "'," + cmd.argsToPython() + ")";
    emit message(command, LogInput);
    m_pdbProc.write(command + '\n');
}

void PdbImpl::handlePdbOutput(const QString &output)
{
    // Mirrors PdbEngine::handleOutput() exactly: split on '\n', trim, hand
    // each line to the dispatcher separately.
    m_inbuffer.append(output);
    while (true) {
        const int pos = m_inbuffer.indexOf('\n');
        if (pos == -1)
            break;
        const QString line = m_inbuffer.left(pos).trimmed();
        m_inbuffer = m_inbuffer.mid(pos + 1);
        handleOutputLine(line);
    }
}

void PdbImpl::handleOutputLine(const QString &line)
{
    if (line.isEmpty())
        return;

    GdbMi item;
    QStringDecoder decoder(QStringEncoder::System);
    item.fromString(line, decoder);

    emit message(line, LogOutput);

    if (line.startsWith("stack={")) {
        // See execute()'s JumpToLine case: its own "stackListFrames" query,
        // answered here like any other one - except its result reports the
        // jump's own new position instead of being forwarded as a normal
        // FullStack refresh.
        if (m_awaitingJumpToLineLocation) {
            m_awaitingJumpToLineLocation = false;
            const GdbMi frames = item["frames"];
            if (frames.childCount() > 0) {
                const GdbMi &topFrame = frames.childAt(0);
                emit locationChanged(FilePath::fromString(topFrame["file"].data()),
                                     topFrame["line"].toInt());
            }
            emit inferiorEvent(InferiorEvent::SpontaneousStop);
            return;
        }
        emit refreshDataReceived(m_pendingStackRequestId, RefreshKind::FullStack, item);
    } else if (line.startsWith("data={")) {
        emit refreshDataReceived(m_pendingLocalsRequestId, RefreshKind::Locals, item);
    } else if (line.startsWith("modules=[")) {
        emit refreshDataReceived(m_pendingModulesRequestId, RefreshKind::Modules, item);
    } else if (line.startsWith("symbols={")) {
        emit refreshDataReceived(m_pendingModuleSymbolsRequestId, RefreshKind::ModuleSymbols, item);
    } else if (line.startsWith("location={")) {
        // pdb always reports its own natural pre-main stop this way -
        // _wait_for_mainpyfile in pdbbridge.py's user_line()/user_call()
        // unconditionally stops at the script's very first line, before
        // any command sent from here (a breakpoint insert, a Continue) has
        // even been read from stdin yet, let alone taken effect. Confirmed
        // the hard way: without this guard, that report races whatever
        // request was already queued by the time it arrives - m_inferiorRunning
        // is often already true by then (set synchronously the instant
        // execute(Continue) is called, well before the process has done
        // anything), so this harmless bootstrap stop gets misreported as
        // the result of that request, and m_inferiorRunning's reset to
        // false afterward then causes the *real* stop that follows to be
        // silently dropped (see the guard below). Unconditionally the
        // first "location=" ever seen, never anything to act on.
        if (!m_sawInitialLocation) {
            m_sawInitialLocation = true;
            return;
        }
        // See m_inferiorRunning's own comment: this report also follows,
        // harmlessly, right after the "state=\"stopped\"" report an
        // interrupt already handled below - matches PdbEngine::
        // refreshLocation()'s own state()-gated no-op in that case.
        if (!m_inferiorRunning)
            return;
        m_inferiorRunning = false;
        const FilePath file = FilePath::fromString(item["file"].data());
        const int lineNumber = item["line"].toInt();
        emit locationChanged(file, lineNumber);
        emit inferiorEvent(m_expectSpontaneousStop ? InferiorEvent::SpontaneousStop
                                              : InferiorEvent::StopOk);
        m_expectSpontaneousStop = false;
    } else if (line.startsWith("state=")) {
        // The only site pdbbridge.py sends this from is sigint_handler()
        // - i.e. always in direct response to execute(Interrupt). See the
        // class comment on why this maps to StopOk rather than real
        // PdbEngine's own SpontaneousStop choice.
        if (item.data() == "stopped") {
            m_inferiorRunning = false;
            if (m_interruptRequested) {
                m_interruptRequested = false;
                emit inferiorEvent(InferiorEvent::StopOk);
            } else {
                emit inferiorEvent(InferiorEvent::SpontaneousStop);
            }
        } else if (item.data() == "running") {
            // Sent once by do_continue() itself, confirming sigint_handler
            // is now installed - see m_continueConfirmedRunning's comment.
            m_continueConfirmedRunning = true;
            if (m_interruptPending) {
                m_interruptPending = false;
                m_interruptRequested = true;
                QString error;
                interruptProcess(m_pdbProc.processId(), &error);
            }
        }
    } else if (line.startsWith("Breakpoint")) {
        // See m_pendingSilentBreakpointReplies' own comment: a RunToLine/
        // RunToFunction's internal "tbreak", or ResetInferior's replay of
        // m_activeBreakpoints - checked first so neither can steal (or
        // underflow) the real-insert FIFO below.
        if (m_pendingSilentBreakpointReplies > 0) {
            --m_pendingSilentBreakpointReplies;
            return;
        }
        // Mirrors PdbEngine::handleOutput2()'s "Breakpoint" text-parsing
        // branch exactly (do_break()'s self.message('Breakpoint %d at
        // %s:%d', ...) - see pdbbridge.py) - just resolved against the
        // FIFO queue instead of scanning breakHandler() for a location
        // match (see m_pendingBreakpointInserts' own comment).
        const int pos1 = line.indexOf(" at ");
        QTC_ASSERT(pos1 != -1, return);
        const int pos2 = line.lastIndexOf(':');
        QTC_ASSERT(pos2 != -1, return);
        const QString fileName = line.mid(pos1 + 4, pos2 - pos1 - 4);
        const QString lineNumber = line.mid(pos2 + 1);
        const QString bpnr = line.mid(11, pos1 - 11);

        QTC_ASSERT(!m_pendingBreakpointInserts.isEmpty(), return);
        BreakpointChangeRequest request = m_pendingBreakpointInserts.takeFirst();
        request.responseId = bpnr;
        m_activeBreakpoints.append(request);

        GdbMi bkpt;
        bkpt.m_type = GdbMi::Tuple;
        bkpt.addChild(constMi("number", bpnr));
        bkpt.addChild(constMi("file", fileName));
        bkpt.addChild(constMi("fullname", fileName));
        bkpt.addChild(constMi("line", lineNumber));
        bkpt.addChild(constMi("enabled", "y"));
        GdbMi data;
        data.m_type = GdbMi::List;
        data.addChild(bkpt);
        emit breakpointEvent(request.requestId, BreakpointOp::Insert, true, data);
    } else if (line.startsWith("breakpointmodified=")) {
        const QString bpnr = item["number"].data();
        const auto it = std::find_if(m_activeBreakpoints.cbegin(), m_activeBreakpoints.cend(),
                                     [&bpnr](const BreakpointChangeRequest &request) {
            return request.responseId == bpnr;
        });
        if (it == m_activeBreakpoints.cend())
            return; // an internal (e.g. RunToLine's own tbreak) breakpoint, not ours to report

        GdbMi bkpt;
        bkpt.m_type = GdbMi::Tuple;
        bkpt.addChild(constMi("number", bpnr));
        bkpt.addChild(constMi("file", it->params.fileName.path()));
        bkpt.addChild(constMi("fullname", it->params.fileName.path()));
        bkpt.addChild(constMi("line", QString::number(it->params.textPosition.line)));
        bkpt.addChild(constMi("enabled", "y"));
        bkpt.addChild(constMi("times", item["times"].data()));
        GdbMi list;
        list.m_type = GdbMi::List;
        list.addChild(bkpt);
        emit breakpointModified(list);
    }
}

} // namespace Debugger::Internal
