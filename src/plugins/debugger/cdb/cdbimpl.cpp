// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cdbimpl.h"

#include "cdbparsehelpers.h" // for the breakpoint-id scheme constants

#include "../breakpoint.h"
#include "../debuggerconstants.h"

#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

using namespace Utils;

namespace Debugger::Internal {

// Local-only command dispatch flags - not CdbEngine::CommandFlags (which
// also carries a Silent bit this class has no use for yet). NoFlags: raw
// cdb command, no reply framing (g/t/p/gu). BuiltinCommand/ExtensionCommand:
// see the class comment.
enum CommandFlags { NoFlags = 0, BuiltinCommand = 1, ExtensionCommand = 2, ScriptCommand = 4 };

// Same synthesis helper GdbImpl/PdbImpl have (see PdbImpl's own constMi()):
// cdb never produces real GdbMi wire text for a breakpoint, so the tuples
// reported through breakpointEvent()/breakpointModified() are built by hand.
static GdbMi constMi(const QString &name, const QString &data)
{
    GdbMi mi;
    mi.m_type = GdbMi::Const;
    mi.m_name = name;
    mi.m_data = data;
    return mi;
}

// Mirrors CdbEngine's own isCdbPrompt() (cdbengine.cpp) - a "process:thread> "
// prompt, e.g. "0:000> ". Same "replicate it locally rather than export it"
// reasoning as checkCommandToken() below.
enum { CdbPromptLength = 7 };

static bool isCdbPrompt(const QString &line)
{
    return line.size() >= CdbPromptLength && line.at(6) == ' ' && line.at(5) == '>'
           && line.at(1) == ':' && line.at(0).isDigit() && line.at(2).isDigit()
           && line.at(3).isDigit() && line.at(4).isDigit();
}

// Mirrors CdbEngine's own checkCommandToken() exactly (cdbengine.cpp) -
// recognizes a ".echo"-bracketed BuiltinCommand's start/end marker line.
static bool checkCommandToken(const QString &tokenPrefix, const QString &line,
                              int *token, bool *isStart)
{
    *token = 0;
    *isStart = false;
    const int prefixSize = tokenPrefix.size();
    if (line.size() < prefixSize + 2 || !line.at(prefixSize).isDigit())
        return false;
    if (line.back() == '>')
        *isStart = false;
    else if (line.back() == '<')
        *isStart = true;
    else
        return false;
    if (!line.startsWith(tokenPrefix))
        return false;
    bool ok = false;
    *token = line.mid(prefixSize, line.size() - prefixSize - 1).toInt(&ok);
    return ok;
}

// Only BreakModuleCapability this slice - the reason this backend exists
// at all (see the class comment). Nothing else has been implemented and
// tested yet, so nothing else is claimed - same discipline already on
// record for LldbImpl's once-under-declared bitmask.
static DebuggerEngineSetupData cdbImplSetupData()
{
    DebuggerEngineSetupData data;
    // The same set real CdbEngine::hasCapability() claims, plus
    // ResetInferiorCapability - so this backend advertises the same feature
    // surface rather than a subset. The one addition is backed by an
    // implementation CdbEngine has no equivalent of, see
    // ExecutionCommand::ResetInferior in execute().
    //
    // Deliberately ahead of the implementation: several of the methods behind
    // these are still stubs here (accessMemory(), fetchDisassembly(),
    // watchPoint(), createSnapshot(), selectThread(), activateFrame()), and
    // breakpoint conditions are not wired up either. Claiming the capability is
    // what makes tst_debugger_backends exercise each one at all, so the gaps
    // show up as explicit TODO(cdb) skips there instead of being invisible.
    // Anything still skipped there is a capability claimed but not yet backed -
    // keep the two in step, and note a caller that trusts this list (the IDE
    // behind QTC_USE_GENERIC_DEBUGGER) will offer those features before they
    // work.
    data.capabilities = AdditionalQmlStackCapability
                      | AddWatcherCapability
                      | BreakConditionCapability
                      | BreakIndividualLocationsCapability
                      | BreakModuleCapability
                      | BreakOnThrowAndCatchCapability // Sort-of: can break on throw().
                      | CreateFullBacktraceCapability
                      | DisassemblerCapability
                      | JumpToLineCapability
                      | OperateByInstructionCapability
                      | RegisterCapability
                      | ReloadModuleCapability
                      | ResetInferiorCapability
                      | RunToLineCapability
                      | ShowMemoryCapability
                      | TracePointCapability
                      | WatchpointByAddressCapability;
    data.acceptsBreakpoint = [](const AcceptsBreakpointQuery &query) {
        // AttachToCore is rejected outright, same rule GdbImpl/LldbImpl apply
        // (see GdbEngine::acceptsBreakpoint()): a core dump is a snapshot,
        // nothing can be made to stop in it. Unreachable in practice while
        // startModes is Launch-only, but the rule is asserted directly by
        // acceptsBreakpointFollowsRules() rather than inferred from
        // startModes, so it has to be stated here too.
        if (query.startMode == AttachToCore)
            return false;
        if (!query.isCppBreakpoint()) {
            // A QML breakpoint, which only a native mixed session can serve -
            // the same answer CdbEngine::acceptsBreakpoint() gives
            // (isNativeMixedEnabled()). It goes to the QML interpreter through
            // the dumper bridge, not to cdb (see insertBreakpoint()).
            return query.isNativeMixedEnabled;
        }
        // The same types real CdbEngine::acceptsBreakpoint() refuses, and for
        // the same reason: Windows has no fork/vfork and no syscall-entry
        // breakpoint at all, and cdb has no watchpoint on an *expression* -
        // only "ba" on an address (see the WatchpointAtAddress arm in
        // changeBreakpoint()). Claiming them would promise something no cdb
        // command can express.
        switch (query.type) {
        case BreakpointAtFork:
        case BreakpointAtSysCall:
        case WatchpointAtExpression:
        case UnknownBreakpointType:
        case LastBreakpointType:
            return false;
        default:
            break;
        }
        return true;
    };
    data.startModes = DebuggerStartModeFlag::Launch;
    return data;
}

CdbImpl::CdbImpl(const CdbImplStartData &startData)
    : DebuggerEngineInterface(cdbImplSetupData())
    , m_startData(startData)
{
    m_cdbProc.setProcessMode(ProcessMode::Writer);
    // NOT setUseCtrlCStub(true), unlike real CdbEngine (cdbengine.cpp) - and
    // that is a genuine, known gap rather than an oversight.
    //
    // The stub is what makes Process::interrupt() reach anything on Windows
    // (ProcessHelper::interruptPid() posts a window message only
    // qtcreator_ctrlc_stub.exe listens for), so without it
    // ExecutionCommand::Interrupt cannot stop a running debuggee here. But
    // enabling it is worse: the stub puts its child in a Job Object with
    // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE (see process_ctrlc_stub.cpp), and cdb
    // launched inside that job then cannot create a *debugged* process at all -
    // confirmed live, every single launch failed with "Cannot execute
    // <inferior>, Win32 error 0n50" (ERROR_NOT_SUPPORTED) and
    // "Debuggee initialization failed", taking every test that needs a debuggee
    // with it. Why the real IDE does not hit this is not yet understood (it is
    // presumably not itself inside a restricting job); until that is worked
    // out, being able to launch at all beats being able to interrupt, so the
    // interrupt tests are skipped instead - see stepsContinuesAndInterrupts()
    // in tst_backends.cpp.

    // Mirrors CdbEngine::setupEngine()'s command-line construction for the
    // Launch case only (StartInternal/StartExternal there) - remote/attach/
    // core are not ported this slice. "-lines": source line info. "-G": no
    // implicit initial breakpoint on process start (this class inserts its
    // own via changeBreakpoint() instead). "-c .idle_cmd ...": registers
    // the extension's "idle" notification, fired every time the debuggee
    // stops - this is what session_accessible/EngineSetupOk below wait for.
    CommandLine cdbCommand = m_startData.debuggerRunData.command;
    cdbCommand.addArg("-a" + m_startData.extensionFileName);
    cdbCommand.addArgs({"-lines", "-G", "-c", ".idle_cmd " + m_extensionCommandPrefix + "idle"});
    if (std::holds_alternative<ProcessRunData>(m_startData.inferiorStartData)) {
        const auto &inferiorRunData = std::get<ProcessRunData>(m_startData.inferiorStartData);
        cdbCommand.addArg(inferiorRunData.command.executable().toUserOutput());
        cdbCommand.addArgs(inferiorRunData.command.arguments(), CommandLine::Raw);
    }
    m_cdbProc.setCommand(cdbCommand);
    m_cdbProc.setEnvironment(m_startData.debuggerRunData.environment);
    if (m_startData.debuggerRunData.workingDirectory.isDir())
        m_cdbProc.setWorkingDirectory(m_startData.debuggerRunData.workingDirectory);
    // Mirrors CdbEngine::setupEngine()'s _NT_DEBUGGER_EXTENSION_PATH env
    // var - cdb.exe needs this to find the extension DLL for a bare "-a<name>"
    // (relative) argument.
    Environment env = m_cdbProc.environment();
    env.set("_NT_DEBUGGER_EXTENSION_PATH", m_startData.extensionDir.nativePath());
    m_cdbProc.setEnvironment(env);

    // EngineSetupOk is deliberately *not* emitted here - see the first
    // "session_idle" branch in handleExtensionMessage(). Reporting it at
    // process start is both too early for the caller to send anything
    // (runCommand() silently drops commands until the process is running) and
    // too early for cdb to resolve a source-line breakpoint at all (no
    // symbols yet). Nothing worth reporting is known this early: not even the
    // pid, since Process::processId() is cdb.exe's own and not the debuggee's -
    // the extension's "pid" command reports that one, from the first
    // "session_idle" (see handleExtensionMessage()).
    connect(&m_cdbProc, &Process::readyReadStandardOutput, this, [this] {
        handleCdbOutput(QString::fromLocal8Bit(m_cdbProc.readAllRawStandardOutput()));
    });
    connect(&m_cdbProc, &Process::readyReadStandardError, this, [this] {
        emit message(QString::fromLocal8Bit(m_cdbProc.readAllRawStandardError()), LogError);
    });
    connect(&m_cdbProc, &Process::done, this, [this] {
        // StartFailed is enough while cdb is launched directly: an unusable
        // debugger path fails the launch itself, which is what
        // reportsEngineSetupFailure() checks. Reporting on the broader "ended
        // before the first session_idle" was tried and reverted - it also fires
        // for a deliberate teardown, which made callers abandon healthy
        // sessions - so if a future change routes cdb through a wrapper process
        // (which would start fine and only then fail), this needs revisiting
        // together with a way to tell teardown apart.
        // A relaunch from ExecutionCommand::ResetInferior killed cdb on
        // purpose: the caller is waiting for the restarted debuggee, not for the
        // engine process ending, so that teardown is not reported at all. A
        // relaunch that fails to start still is.
        if (m_isResetRestart && m_cdbProc.result() != ProcessResult::StartFailed)
            return;
        if (m_cdbProc.result() == ProcessResult::StartFailed)
            emit inferiorEvent(InferiorEvent::EngineSetupFailed);
        emit engineProcessFinished(m_cdbProc.resultData());
    });
}

CdbImpl::~CdbImpl()
{
    // Safety net, same reasoning as every other backend's destructor (see
    // GdbImpl::~GdbImpl()'s own comment) - a caller that destroys this
    // without a clean shutdown sequence first must not leave cdb.exe (and
    // the debuggee it launched) running forever. Plain process kill, not a
    // wire command: no signal emission risk this way (m_cdbProc is about
    // to be destroyed right after this anyway).
    // Marked as a deliberate teardown for the same reason shutdownEngine()
    // does: the Process::done handler must not mistake this kill for a startup
    // failure (see its own comment).
    m_shuttingDown = true;
    if (m_cdbProc.isRunning())
        m_cdbProc.kill();
}

void CdbImpl::start()
{
    // EngineSetupOk is emitted from the Process::started handler, not here:
    // Process::start() is asynchronous, so m_cdbProc.isRunning() is still
    // false on return, and runCommand()'s own "not running" guard would
    // silently drop anything a caller sends while reacting to that event -
    // which is exactly when callers do their initial breakpoint insertion
    // (see GenericDebuggerEngine, and launchAndStopAtBreakpoint() in
    // tst_backends.cpp). Confirmed live: emitting it here meant the first
    // breakpoint was dropped without a trace, the debuggee then ran freely
    // past where it should have stopped, and every test timed out waiting
    // for a stop that could never come.
    m_cdbProc.start();
}

void CdbImpl::shutdownInferior(ShutdownMode mode)
{
    // No Detach ported this slice - cdb.exe's own detach ("qd") needs the
    // debuggee to have been genuinely attached rather than launched by cdb
    // itself, which changes teardown semantics elsewhere too; not checked
    // live yet, so left unported rather than guessed at.
    Q_UNUSED(mode)
    if (m_cdbProc.isRunning())
        m_cdbProc.write("q\n");
    emit inferiorEvent(InferiorEvent::ShutdownFinished);
}

void CdbImpl::shutdownEngine()
{
    m_shuttingDown = true;
    if (m_cdbProc.isRunning())
        m_cdbProc.kill();
    emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
}

void CdbImpl::execute(const ExecutionRequest &request)
{
    // Any execution request against a debuggee that has already exited is
    // answered with InferiorIll rather than sent - mirrors PdbImpl's own guard.
    // Only reachable now that the exit is actually detected (see
    // handleExtensionMessage()'s "session_inaccessible"/NO_DEBUGGEE branch);
    // without it a stale Continue would be written to a cdb with no debuggee
    // left, which simply reports nothing and leaves the caller waiting. Abort
    // is exempt: tearing the session down after the debuggee is gone is valid.
    if (m_inferiorExited && request.command != ExecutionCommand::Abort) {
        emit inferiorEvent(InferiorEvent::InferiorIll);
        return;
    }
    // Native-mixed QML stepping is not ported this slice (no
    // AdditionalQmlStackCapability claimed), so currentFrameIsQml is never
    // relevant here yet.
    switch (request.command) {
    // Continue/StepIn/StepOver/StepOut all run the target, so each reports the
    // RunRequested/RunOk pair before dispatching, exactly as every other
    // backend does (see PdbImpl's own cases). None of these emitted anything
    // at all before, so a caller waiting for RunOk after Continue - which
    // GenericDebuggerEngine's state machine does, as do
    // stepsContinuesAndInterrupts() and breakpointConditionPreventsStop() -
    // waited forever even though cdb had resumed.
    case ExecutionCommand::Continue:
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        runCommand({"g", NoFlags});
        break;
    case ExecutionCommand::Interrupt:
        // Mirrors GdbImpl's/PdbImpl's own "already stopped" fast path: cdb has
        // nothing to report for an interrupt requested while the debuggee is
        // not running (no further "session_idle" will ever arrive, since it
        // never left the one it is already parked in), so answer directly
        // instead of waiting for a notification that cannot come.
        if (!m_inferiorRunning) {
            emit inferiorEvent(InferiorEvent::StopOk);
            break;
        }
        m_interruptRequested = true;
        m_cdbProc.interrupt();
        break;
    // All three steps report SpontaneousStop once they land, not StopOk -
    // GdbImpl's/LldbImpl's/PdbImpl's shared convention (see PdbImpl's own
    // comment on its StepIn case, and tst_backends.cpp): only a synchronous,
    // non-running action (Interrupt while already stopped, Return) is a
    // StopOk; anything that actually runs the target first - Continue or any
    // step - is a SpontaneousStop, no matter how briefly it ran. These were
    // inverted here, so every step's own stop came back as StopOk.
    case ExecutionCommand::StepIn:
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        adjustOperateByInstruction(request.flag);
        runCommand({"t", NoFlags}); // trace into
        break;
    case ExecutionCommand::StepOver:
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        adjustOperateByInstruction(request.flag);
        runCommand({"p", NoFlags}); // step over the current line
        break;
    case ExecutionCommand::StepOut:
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        // "gu" runs to the caller's return address, so it is a whole-frame
        // operation either way - no stepping mode to select for it.
        runCommand({"gu", NoFlags}); // go up - run until the current function returns
        break;
    case ExecutionCommand::Abort:
        shutdownEngine();
        break;
    // RunToLine/RunToFunction: a one-shot breakpoint at the target, then
    // continue - exactly what CdbEngine::executeRunToLine()/
    // executeRunToFunction() do (oneShot => cdb's "/1", which deletes the
    // breakpoint as it fires, so no cleanup is needed). The id is recorded as
    // internal so its hit is not reported through breakpointModified(): the
    // caller never asked for this breakpoint and must not see events for it.
    case ExecutionCommand::RunToLine:
    case ExecutionCommand::RunToFunction: {
        const QString id = nextBreakpointId();
        m_internalBreakpointIds.insert(id);
        QString cmd = "bu" + id + " /1 ";
        if (request.command == ExecutionCommand::RunToFunction) {
            cmd += request.functionName;
        } else if (request.context.address) {
            cmd += "0x" + QString::number(request.context.address, 16);
        } else {
            // Native separators and backticks, same as a source-line insert.
            cmd += '`' + request.context.fileName.toUserOutput() + ':'
                 + QString::number(request.context.textPosition.line) + '`';
        }
        runCommand({cmd, NoFlags});
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        runCommand({"g", NoFlags});
        break;
    }
    // JumpToLine moves the instruction pointer without running anything, so
    // it needs the target *address* first. Mirrors
    // CdbEngine::executeJumpToLine(): evaluate "? `file:line`", then set rip
    // from the answer. cdb prints "Evaluate expression: <dec> = <hex>", with a
    // backtick splitting the hex halves that has to come back out.
    case ExecutionCommand::JumpToLine: {
        const FilePath file = request.context.fileName;
        const int line = request.context.textPosition.line;
        if (request.context.address) {
            jumpToAddress(request.context.address, file, line);
            break;
        }
        const QString expr = "? `" + file.toUserOutput() + ':' + QString::number(line) + '`';
        runCommand({expr, BuiltinCommand,
                   [this, file, line](const DebuggerResponse &response) {
            const QString reply = response.data.data();
            const int eq = reply.lastIndexOf('=');
            if (eq == -1) {
                emit message("CdbImpl: could not resolve a jump target from: " + reply,
                             LogError);
                emit inferiorEvent(InferiorEvent::InferiorIll);
                return;
            }
            const QString hex = reply.mid(eq + 1).remove('`').trimmed();
            bool ok = false;
            const quint64 address = hex.toULongLong(&ok, 16);
            if (!ok) {
                emit message("CdbImpl: unparsable jump target: " + hex, LogError);
                emit inferiorEvent(InferiorEvent::InferiorIll);
                return;
            }
            jumpToAddress(address, file, line);
        }});
        break;
    }
    case ExecutionCommand::ResetInferior:
        // Kill cdb and launch it again, replaying the tracked breakpoints into
        // the new session - the same shape PdbImpl uses for its own reset, and
        // for a similar reason.
        //
        // NOT cdb's own ".restart", which was tried twice: it does restart the
        // debuggee, but it tears the session down and rebuilds it
        // *asynchronously* while announcing nothing. Everything
        // initializeSession() establishes goes with the old session, no
        // "session_idle" ever arrives for the new one (the notification this
        // class hangs all its startup work on), and queueing that work behind it
        // does not help either: cdb echoed the whole sequence, "g" included,
        // before the new session's own "CommandLine:" banner appeared, so it all
        // ran against the session being torn down and the restarted inferior ran
        // straight past every breakpoint to its exit. Both confirmed live.
        //
        // Real CdbEngine implements no reset at all (it inherits
        // DebuggerEngine::resetInferior()'s empty body), so this is a deliberate
        // addition rather than a mirrored codepath.
        emit inferiorEvent(InferiorEvent::RunRequested);
        emit inferiorEvent(InferiorEvent::RunOk);
        // Marked as a deliberate teardown, so the kill's own notifications are
        // not reported as the debuggee exiting (see the Process::done handler).
        m_shuttingDown = true;
        m_isResetRestart = true;
        m_cdbProc.kill();
        m_cdbProc.waitForFinished();
        // Everything the old session owned is gone with it: its stop state, its
        // resolved sub-location ids, its hit counts, and the extension instance
        // the dumper bridge lived in. The breakpoints themselves
        // (m_insertedBreakpoints) and their conditions are keyed by ids this
        // class owns, which the replay reuses, so those stay.
        m_shuttingDown = false;
        m_initialSessionIdleHandled = false;
        m_inferiorExited = false;
        m_inferiorRunning = false;
        m_expectSpontaneousStop = false;
        m_interruptRequested = false;
        m_evaluatingCondition = false;
        m_expandingTracepoint = false;
        m_lastOperateByInstruction.reset();
        m_parentForSubBreakpointId.clear();
        m_breakpointHitCounts.clear();
        m_pythonVersion = 0;
        m_interpreterResolverIds.clear();
        m_interpreterMessageIds.clear();
        m_pendingBridgeWork.clear();
        m_currentBuiltinResponseToken = -1;
        m_currentBuiltinResponse.clear();
        m_extensionMessageBuffer.clear();
        m_commandForToken.clear();
        m_inbuffer.clear();
        m_cdbProc.start();
        break;
    case ExecutionCommand::RepeatLastCommand:
        // Mirrors CdbEngine::debugLastCommand(), including its no-op until a
        // refresh(Locals) has stashed one - see m_lastDebuggableCommand.
        if (!m_lastDebuggableCommand.function.isEmpty())
            runCommand(m_lastDebuggableCommand);
        break;
    // Not ported this slice - see the class comment.
    case ExecutionCommand::Return:
    case ExecutionCommand::Detach:
    case ExecutionCommand::RecordReverse:
        emit message(QString("CdbImpl: execution command not ported this slice."), LogWarning);
        break;
    }
}

// Issues the cdb commands for one breakpoint under a given id. Split out of
// changeBreakpoint() so ExecutionCommand::ResetInferior can replay the tracked
// breakpoints into a relaunched session - with report=false, since the caller
// already knows about them and must not see a second Insert reply.
void CdbImpl::insertBreakpoint(quint64 requestId, const QString &id, int modelId,
                               const BreakpointParameters &params, bool report)
{
    // A QML breakpoint goes to the QML interpreter, not to cdb: the dumper
    // bridge asks the debuggee's own NativeQmlDebugger service to set it. Same
    // arguments GdbImpl sends (only the fields
    // NativeDebugger::handleSetBreakpoint() reads, plus the echoed-back
    // modelid), and the same two outcomes - a real interpreter id, or "pending"
    // when the service is not up yet.
    if (!params.isCppBreakpoint()) {
        if (!m_startData.nativeMixed) {
            if (report)
                emit breakpointEvent(requestId, BreakpointOp::Insert, false, {});
            return;
        }
        if (m_pythonVersion == 0) {
            // The bridge is still coming up: its bootstrap is a chain of
            // asynchronous ScriptCommands, and a caller inserting a QML
            // breakpoint the moment setup is reported gets here first - which is
            // the *only* moment such a breakpoint can be set usefully (the
            // service has to be enabled before the QQmlEngine is constructed).
            // Queued rather than refused, the same reason GdbImpl gates its own
            // commands on m_dumpersReady.
            m_pendingBridgeWork.append([this, requestId, modelId, params, report] {
                // Decided here rather than by re-entering insertBreakpoint(), so
                // a bridge that never came up reports a failure instead of
                // queueing itself again forever.
                if (m_pythonVersion != 0)
                    insertInterpreterBreakpoint(requestId, modelId, params, report);
                else if (report)
                    emit breakpointEvent(requestId, BreakpointOp::Insert, false, {});
            });
            return;
        }
        insertInterpreterBreakpoint(requestId, modelId, params, report);
        return;
    }
    const QString module = params.module;
    // A condition is not passed to cdb at all - it has no conditional
    // breakpoint (see m_conditionForBreakpointId) - but remembered here and
    // evaluated at every stop, exactly as CdbEngine does it.
    if (!params.condition.isEmpty())
        m_conditionForBreakpointId.insert(id, params.condition);
    // Tracked by id, which is what lets a stop tell whether the breakpoint it
    // reports is a tracepoint - cdb sets an ordinary breakpoint for one, and
    // what makes it a tracepoint (log the message, resume without reporting a
    // stop) happens at stop time, see handleExtensionMessage().
    m_insertedBreakpoints.insert(id, params);
    // cdb has no throw/catch breakpoint of its own: mirrors
    // fixWinMSVCBreakpoint()'s own emulation, a plain function breakpoint on
    // the MSVC runtime's throw/catch entry point. Both names are unqualified
    // there too ("potentially ambiguous", as its comment says), so binding
    // depends on the CRT's symbols. Confirmed live via "bl": the throw one
    // resolves (cdb matches it to "_CxxThrowException"), while the catch one
    // is only accepted as deferred unless the debuggee actually links a
    // catch block - hence the capability's "sort-of" in CdbEngine too.
    BreakpointType type = params.type;
    QString functionName = params.functionName;
    if (type == BreakpointAtThrow) {
        type = BreakpointByFunction;
        functionName = "CxxThrowException";
    } else if (type == BreakpointAtCatch) {
        type = BreakpointByFunction;
        functionName = "__CxxCallCatchBlock";
    }
    // "ba" (break on access) for a watchpoint, "bu" for everything else -
    // the same choice cdbAddBreakpointCommand() makes on this very type.
    QString cmd = (type == WatchpointAtAddress ? QLatin1String("ba") : QLatin1String("bu")) + id + ' ';
    if (params.oneShot)
        cmd += "/1 ";
    switch (type) {
    case BreakpointByFunction:
        // Resolved to real addresses first, rather than handed to cdb as a
        // name it cannot expand - see insertFunctionBreakpoint(). Not for a
        // one-shot one, which is this class's own RunToFunction: that wants
        // cdb's "/1" and needs no locations.
        if (!params.oneShot) {
            insertFunctionBreakpoint(requestId, id, params.enabled,
                                     module, functionName, report);
            return;
        }
        if (!module.isEmpty())
            cmd += module + '!';
        cmd += functionName;
        break;
    case BreakpointByFileAndLine:
        cmd += '`';
        if (!module.isEmpty())
            cmd += module + '!';
        // Native separators, not FilePath::path()'s forward slashes -
        // cdb matches this against the source file name recorded in the
        // PDB, which is a Windows path. Real CdbEngine goes through
        // cdbBreakPointFileName(), whose own comment is "Convert to
        // native", via FilePath::toUserOutput(). Confirmed live: with
        // forward slashes cdb accepts the "bu" silently, reports no
        // error at all, and simply never resolves or hits it.
        cmd += params.fileName.toUserOutput() + ':'
             + QString::number(params.textPosition.line) + '`';
        break;
    case WatchpointAtAddress: {
        // Mirrors cdbAddBreakpointCommand()'s own WatchpointAtAddress arm:
        // "r<size>" - break on read *or* write of that many bytes - then the
        // address, and no space between the two ("no space here", as its own
        // comment puts it). The size defaults to 1 exactly as there. cdb
        // requires an address aligned to the size, which 1 always is.
        const unsigned size = params.size ? params.size : 1;
        cmd += 'r' + QString::number(size) + ' ' + "0x"
             + QString::number(params.address, 16);
        break;
    }
    default:
        if (report)
            emit breakpointEvent(requestId, BreakpointOp::Insert, false, {});
        return;
    }
    const bool enabled = params.enabled;
    const QString file = params.fileName.path();
    const int line = params.textPosition.line;
    // The emulating function for throw/catch, so the reply names what cdb
    // was actually told to break on.
    const QString function = functionName;
    runCommand({cmd, BuiltinCommand,
               [this, requestId, id, enabled, file, line, function, report]
               (const DebuggerResponse &r) {
        // An ambiguous location: cdb created no breakpoint at all and
        // listed every address the location resolved to instead - a
        // template body line (one match per instantiation), an inlined
        // function, a lambda. Mirrors CdbEngine::handleBreakInsert():
        // insert one breakpoint per match, numbered inside this
        // breakpoint's own reserved id range, and report them as its
        // locations - which is what lets a caller enable or disable them
        // one at a time (BreakIndividualLocationsCapability).
        const QStringList reply = r.data.data().split('\n');
        // The error is the last line, or the one before it - the same two
        // places CdbEngine::handleBreakInsert() looks.
        bool ambiguous = false;
        for (int i = qMax(0, reply.size() - 2); i < reply.size(); ++i)
            ambiguous |= reply.at(i).startsWith("Ambiguous symbol error");
        GdbMi locations;
        locations.m_type = GdbMi::List;
        locations.m_name = "locations";
        if (ambiguous) {
            const QLatin1String matchPrefix("Matched: ");
            int subId = 0;
            for (const QString &replyLine : reply) {
                if (!replyLine.startsWith(matchPrefix))
                    continue;
                // "Matched: <module>!<function>+<offset> (<address>)",
                // the address carrying cdb's own backtick separator.
                const int addressStart = replyLine.lastIndexOf('(') + 1;
                const int addressEnd = replyLine.indexOf(')', addressStart);
                if (addressStart == 0 || addressEnd == -1)
                    continue;
                QString addressString = replyLine.mid(addressStart,
                                                      addressEnd - addressStart);
                addressString.remove('`');
                bool ok = false;
                const quint64 address = addressString.toULongLong(&ok, 16);
                if (!ok)
                    continue;
                QString matchedFunction = replyLine.mid(matchPrefix.size(),
                                              addressStart - 1 - matchPrefix.size());
                const int functionStart = matchedFunction.indexOf('!') + 1;
                const int functionOffset = matchedFunction.lastIndexOf('+');
                if (functionOffset > 0)
                    matchedFunction.truncate(functionOffset);
                if (functionStart > 0)
                    matchedFunction = matchedFunction.mid(functionStart);
                const QString hexAddress = "0x" + QString::number(address, 16);
                const QString subId_ = QString::number(id.toInt() + ++subId);
                m_parentForSubBreakpointId.insert(subId_, id);
                // By address, not by the ambiguous location again - that is
                // the whole point, and what makes each one addressable.
                runCommand({"bu" + subId_ + ' ' + hexAddress, NoFlags});
                GdbMi location;
                location.m_type = GdbMi::Tuple;
                location.addChild(constMi("number", subId_));
                location.addChild(constMi("func", matchedFunction));
                location.addChild(constMi("addr", hexAddress));
                location.addChild(constMi("enabled", enabled ? QLatin1String("y") : QLatin1String("n")));
                if (!file.isEmpty()) {
                    location.addChild(constMi("file", file));
                    location.addChild(constMi("line", QString::number(line)));
                }
                locations.addChild(location);
            }
        }
        // Otherwise "bu" never fails visibly on its own output (bad syntax
        // shows up as an error line, not checked yet) - treat any reply
        // as success this slice.
        reportBreakpointInserted(requestId, id, enabled, file, line, function,
                                 locations, report);
    }});
}

void CdbImpl::changeBreakpoint(const BreakpointChangeRequest &request)
{
    switch (request.op) {
    case BreakpointOp::Insert:
        // Mirrors breakPointCdbId()'s own counter - see the header comment.
        insertBreakpoint(request.requestId, nextBreakpointId(), request.modelId,
                         request.params, true);
        break;
    // Remove/Update/EnableSub all address an existing breakpoint by the id
    // handed back from its own Insert. Without one there is nothing to act on,
    // and appending an empty id would send a bare "bc"/"be"/"bd" - which cdb
    // reads as "every breakpoint", so an unanswerable request would silently
    // clear or toggle all of them. Report the failure instead of guessing.
    case BreakpointOp::Remove:
        if (request.responseId.isEmpty()) {
            emit breakpointEvent(request.requestId, request.op, false, {});
            break;
        }
        runCommand({"bc" + request.responseId, NoFlags});
        m_insertedBreakpoints.remove(request.responseId);
        emit breakpointEvent(request.requestId, request.op, true, {});
        break;
    case BreakpointOp::Update:
        if (request.responseId.isEmpty()) {
            emit breakpointEvent(request.requestId, request.op, false, {});
            break;
        }
        runCommand({(request.params.enabled ? QLatin1String("be") : QLatin1String("bd")) + request.responseId, NoFlags});
        emit breakpointEvent(request.requestId, request.op, true, {});
        break;
    case BreakpointOp::EnableSub:
        // Enabling or disabling one location of a multi-location breakpoint is
        // the same "be"/"bd <id>" as Update above - cdb addresses a sub-
        // breakpoint by its own id (its major.minor part, see
        // cdbparsehelpers.cpp's cdbBreakPointIdMinorPart), so nothing beyond
        // using subResponseId and the request's own "enabled" flag is needed.
        // Reporting a flat failure here, as this used to, made the operation
        // look unsupported when the command it needs was already right there.
        if (request.subResponseId.isEmpty()) {
            emit breakpointEvent(request.requestId, request.op, false, {});
            break;
        }
        runCommand({(request.enabled ? QLatin1String("be") : QLatin1String("bd")) + request.subResponseId, NoFlags});
        emit breakpointEvent(request.requestId, request.op, true, {});
        break;
    }
}

// Fills in where a function's body actually starts, from cdb's own "uf"
// (unassemble function) reply, which prefixes every instruction with its source
// line after a header naming the file and the function's first line:
//
//   inferior_msvc!multi<int> [C:\...\inferior.cpp @ 47]:
//      47 00007ff6`72a38000 894c2408  mov  dword ptr [rsp+8],ecx
//      48 00007ff6`72a38008 8b542430  mov  edx,dword ptr [rsp+30h]
//
// The first instruction whose line differs from the header's is where the
// prologue ends - the address gdb's own function breakpoints resolve to, and
// the line a caller means by "break on this function". Falls back to the entry
// address for a function whose body is all on its declaration line.
void CdbImpl::parseFunctionDisassembly(const QString &reply, ResolvedFunction *function)
{
    int declarationLine = 0;
    bool headerSeen = false;
    for (const QString &replyLine : reply.split('\n')) {
        if (!headerSeen) {
            const int bracket = replyLine.indexOf(" [");
            const int at = replyLine.lastIndexOf(" @ ");
            if (bracket <= 0 || at <= bracket || !replyLine.trimmed().endsWith("]:"))
                continue;
            const QString trimmed = replyLine.trimmed();
            function->file = trimmed.mid(bracket + 2, at - bracket - 2);
            declarationLine = trimmed.mid(at + 3, trimmed.size() - at - 5).toInt();
            headerSeen = true;
            continue;
        }
        const QStringList parts = replyLine.trimmed().split(' ', Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        // The address is found by its shape, not by its column: cdb prints one
        // with its own backtick between the halves ("00007ff7`c5038040"), and
        // how many columns come before it depends on ".asm source_line" - with
        // that option (which fetchDisassembly() needs, see start()) the source
        // line number is printed *twice*, so the address moves from the second
        // column to the third. Reading it positionally took "48" for an address
        // and put every function breakpoint at 0x48; confirmed live both ways.
        int addressIndex = -1;
        for (int i = 1; i < parts.size(); ++i) {
            if (parts.at(i).contains('`')) {
                addressIndex = i;
                break;
            }
        }
        if (addressIndex < 1)
            continue;
        QString addressString = parts.at(addressIndex);
        addressString.remove('`');
        bool addressOk = false;
        const quint64 address = addressString.toULongLong(&addressOk, 16);
        if (!addressOk)
            continue;
        // The source line is the last column before the address, whichever
        // column that turns out to be.
        bool lineOk = false;
        const int sourceLine = parts.at(addressIndex - 1).toInt(&lineOk);
        if (!lineOk)
            continue;
        if (function->address == 0) { // The entry - still inside the prologue.
            function->address = address;
            function->line = sourceLine;
        }
        if (sourceLine != declarationLine) {
            function->address = address;
            function->line = sourceLine;
            return;
        }
    }
}

void CdbImpl::insertFunctionBreakpoint(quint64 requestId, const QString &id, bool enabled,
                                       const QString &module, const QString &functionName,
                                       bool report)
{
    // cdb resolves a bare function name in neither of gdb's two senses -
    // confirmed live: unqualified it refuses outright ("Bp expression 'multi'
    // contains symbols not qualified with module name") and even qualified it
    // cannot expand a template name ("Couldn't resolve error at
    // 'inferior_msvc!multi'"), since the instantiations are separate symbols
    // named "multi<int>"/"multi<double>". So the symbol is resolved here, the
    // way gdb does it internally: every instantiation becomes one location of
    // the same breakpoint.
    //
    // "x" lists each match with its address; the trailing "*" is what finds the
    // instantiations. It over-matches on the prefix (asking for "multi" also
    // lists "MultiByteToWideChar"), so matches are filtered by exact name below.
    const QString scope = (module.isEmpty() ? QString("*") : module) + '!';
    const QString fallbackTarget = module.isEmpty() ? functionName : module + '!' + functionName;
    runCommand({"x " + scope + functionName + '*', BuiltinCommand,
               [this, requestId, id, enabled, functionName, fallbackTarget, report]
               (const DebuggerResponse &response) {
        QStringList qualifiedNames;
        for (const QString &replyLine : response.data.data().split('\n')) {
            // "00007ff6`72a38040 inferior_msvc!multi<double> (double)"
            const QString trimmed = replyLine.trimmed();
            const int space = trimmed.indexOf(' ');
            if (space <= 0)
                continue;
            QString symbol = trimmed.mid(space).trimmed();
            const int signature = symbol.lastIndexOf(" (");
            if (signature > 0)
                symbol.truncate(signature);
            const QString name = symbol.mid(symbol.indexOf('!') + 1);
            if (name != functionName && !name.startsWith(functionName + '<'))
                continue;
            qualifiedNames.append(symbol);
        }
        if (qualifiedNames.isEmpty()) {
            // Nothing matched: the module may not be loaded yet, or the name is
            // one only cdb's own matching finds ("CxxThrowException" resolves to
            // "_CxxThrowException", which the exact-name filter above rejects).
            // Hand the name to cdb unchanged then - that also keeps a breakpoint
            // in a not-yet-loaded module deferred, rather than failing it here.
            const QString file;
            runCommand({"bu" + id + ' ' + fallbackTarget, BuiltinCommand,
                       [this, requestId, id, enabled, functionName, file, report]
                       (const DebuggerResponse &) {
                reportBreakpointInserted(requestId, id, enabled, file, 0, functionName, {}, report);
            }});
            return;
        }
        // One "uf" per match, since each instantiation is its own function. The
        // replies are correlated by index so the locations keep "x"'s order
        // regardless of the order they arrive in.
        struct Resolution
        {
            int pending = 0;
            QList<ResolvedFunction> functions;
        };
        const auto resolution = std::make_shared<Resolution>();
        resolution->pending = qualifiedNames.size();
        resolution->functions.resize(qualifiedNames.size());
        for (int i = 0; i < qualifiedNames.size(); ++i) {
            const QString qualifiedName = qualifiedNames.at(i);
            runCommand({"uf " + qualifiedName, BuiltinCommand,
                       [this, requestId, id, enabled, functionName, resolution, i, qualifiedName,
                        report]
                       (const DebuggerResponse &ufResponse) {
                ResolvedFunction &function = resolution->functions[i];
                function.qualifiedName = qualifiedName;
                function.name = qualifiedName.mid(qualifiedName.indexOf('!') + 1);
                parseFunctionDisassembly(ufResponse.data.data(), &function);
                if (--resolution->pending > 0)
                    return;
                setResolvedFunctionBreakpoints(requestId, id, enabled, functionName,
                                               resolution->functions, report);
            }});
        }
    }});
}

void CdbImpl::setResolvedFunctionBreakpoints(quint64 requestId, const QString &id, bool enabled,
                                             const QString &functionName,
                                             const QList<ResolvedFunction> &functions, bool report)
{
    QList<ResolvedFunction> resolved;
    for (const ResolvedFunction &function : functions) {
        if (function.address != 0)
            resolved.append(function);
    }
    if (resolved.isEmpty()) {
        emit breakpointEvent(requestId, BreakpointOp::Insert, false, {});
        return;
    }
    const auto hex = [](quint64 address) { return "0x" + QString::number(address, 16); };
    if (resolved.size() == 1) {
        // A single match is an ordinary breakpoint - no locations to report,
        // just set past the prologue like every other debugger does.
        const ResolvedFunction &function = resolved.constFirst();
        runCommand({"bu" + id + ' ' + hex(function.address), NoFlags});
        reportBreakpointInserted(requestId, id, enabled, function.file, function.line,
                                 function.name, {}, report);
        return;
    }
    // Several matches - overloads, or a template's instantiations: one
    // breakpoint each, numbered inside this breakpoint's own reserved id range,
    // reported as its locations so a caller can enable or disable them
    // individually (BreakIndividualLocationsCapability).
    GdbMi locations;
    locations.m_type = GdbMi::List;
    locations.m_name = "locations";
    int subId = 0;
    for (const ResolvedFunction &function : resolved) {
        const QString subResponseId = QString::number(id.toInt() + ++subId);
        m_parentForSubBreakpointId.insert(subResponseId, id);
        runCommand({"bu" + subResponseId + ' ' + hex(function.address), NoFlags});
        GdbMi location;
        location.m_type = GdbMi::Tuple;
        location.addChild(constMi("number", subResponseId));
        location.addChild(constMi("func", function.name));
        location.addChild(constMi("addr", hex(function.address)));
        location.addChild(constMi("enabled", enabled ? QLatin1String("y") : QLatin1String("n")));
        location.addChild(constMi("file", function.file));
        location.addChild(constMi("line", QString::number(function.line)));
        locations.addChild(location);
    }
    const ResolvedFunction &first = resolved.constFirst();
    reportBreakpointInserted(requestId, id, enabled, first.file, first.line, functionName,
                             locations, report);
}

void CdbImpl::reportBreakpointInserted(quint64 requestId, const QString &id, bool enabled,
                                       const QString &file, int line, const QString &function,
                                       const GdbMi &locations, bool report)
{
    // A replay re-creates a breakpoint the caller already has, so it gets no reply.
    if (!report)
        return;
    GdbMi bkpt;
    bkpt.m_type = GdbMi::Tuple;
    bkpt.addChild(constMi("number", id));
    bkpt.addChild(constMi("enabled", enabled ? QLatin1String("y") : QLatin1String("n")));
    if (!file.isEmpty()) {
        bkpt.addChild(constMi("file", file));
        bkpt.addChild(constMi("line", QString::number(line)));
    }
    if (!function.isEmpty())
        bkpt.addChild(constMi("func", function));
    if (locations.childCount() > 0)
        bkpt.addChild(locations);
    GdbMi list;
    list.m_type = GdbMi::List;
    list.addChild(bkpt);
    emit breakpointEvent(requestId, BreakpointOp::Insert, true, list);
}

QString CdbImpl::nextBreakpointId()
{
    // Mirrors breakPointCdbId() (cdbparsehelpers.cpp) exactly, spacing included.
    return QString::number(cdbBreakPointStartId
                           + (m_nextBreakpointId++) * cdbBreakPointIdMinorPart);
}

// Everything a fresh cdb session needs before the debuggee may run: the
// exception and disassembly options, the symbol setup, and the extension's
// "pid" round trip. Called for the first "session_idle" and again after a
// ".restart" (see ExecutionCommand::ResetInferior), which tears the whole
// session down and takes all of this with it.
// Logs a tracepoint's message and resumes, without ever reporting a stop -
// what CdbEngine::examineStopReason() expresses as StopReportLog|
// StopIgnoreContinue for a tracepoint hit, on both channels it uses (AppOutput
// for the debuggee's own log, LogMisc for the debugger view).
//
// One thing goes beyond it: the "{expression}" captures in the message. Real
// CdbEngine logs the message *verbatim*, placeholders and all, so a cdb
// tracepoint there says "globalValue is {globalValue}". They are expanded here
// instead, through the extension's own "locals" command with one watcher per
// capture - the same mechanism refresh(Locals) already uses for watchers - and
// decoded with decodeData(), the watch model's own decoder, so a value arrives
// formatted the way the user would see it in the Locals view (a string quoted,
// an int plain).
//
// The same place in the debuggee, for deciding which breakpoints a single stop
// covers: cdb reports one id per stop, but several breakpoints can share one
// location - a tracepoint and a plain breakpoint on the same line, above all.
static bool isSameLocation(const BreakpointParameters &one, const BreakpointParameters &other)
{
    return one.type == other.type && one.fileName == other.fileName
        && one.textPosition.line == other.textPosition.line
        && one.functionName == other.functionName && one.address == other.address;
}

// Not done with cdb's own breakpoint commands (cdbAddBreakpointCommand() does
// append a command string, so ".printf ...; g" would log and resume with no stop
// round trip at all): ".printf" needs a format specifier per value, and the
// message template carries no types to pick one from - "%ma" for a string and
// "%d" for an int are not interchangeable, and neither renders the quotes a
// string value is shown with. The types only exist in the extension's reply,
// which is what this reads.
void CdbImpl::reportTracepoint(const QStringList &tracepointMessages, const GdbMi &stopData,
                               bool stopAfterwards)
{
    // Either resume (a tracepoint alone never stops) or report the stop a plain
    // breakpoint at the same place is owed, once the logging is done.
    const auto finish = [this, stopData, stopAfterwards] {
        if (stopAfterwards) {
            reportStop(stopData);
            return;
        }
        m_expectSpontaneousStop = true;
        m_inferiorRunning = true;
        runCommand({"g", NoFlags});
    };
    // "{expression}" captures, in order of appearance, across every message.
    static const QRegularExpression captureExpression(R"(\{([^{}]+)\})");
    QStringList captures;
    for (const QString &tracepointMessage : tracepointMessages) {
        QRegularExpressionMatchIterator it = captureExpression.globalMatch(tracepointMessage);
        while (it.hasNext()) {
            const QString capture = it.next().captured(1);
            if (!captures.contains(capture))
                captures.append(capture);
        }
    }
    if (captures.isEmpty()) {
        for (const QString &tracepointMessage : tracepointMessages) {
            if (tracepointMessage.isEmpty())
                continue;
            emit message(tracepointMessage, AppOutput);
            emit message(tracepointMessage, LogMisc);
        }
        finish();
        return;
    }

    QString args = "-v -D -W";
    for (int i = 0; i < captures.size(); ++i)
        args += QString(" -w watch.%1 \"%2\"").arg(i).arg(captures.at(i));
    args += " 0";
    DebuggerCommand cmd("locals", ExtensionCommand);
    cmd.args = args;
    // Guards the stop this evaluation itself parks in, the same way
    // m_evaluatingCondition guards the condition path.
    m_expandingTracepoint = true;
    cmd.callback = [this, tracepointMessages, captures, finish](const DebuggerResponse &response) {
        m_expandingTracepoint = false;
        for (const QString &tracepointMessage : tracepointMessages) {
            QString expanded = tracepointMessage;
            for (int i = 0; i < captures.size(); ++i) {
                QString value;
                for (const GdbMi &item : response.data) {
                    if (item["iname"].data() != QString("watch.%1").arg(i))
                        continue;
                    value = decodeData(item["value"].data(), item["valueencoded"].data());
                    break;
                }
                // An expression the debuggee cannot evaluate right now keeps its
                // braces, rather than silently reading as an empty value.
                if (!value.isEmpty())
                    expanded.replace('{' + captures.at(i) + '}', value);
            }
            emit message(expanded, AppOutput);
            emit message(expanded, LogMisc);
        }
        finish();
    };
    runCommand(cmd);
}

// A QML breakpoint, set in the debuggee's own QML interpreter through the dumper
// bridge's NativeQmlDebugger service calls rather than by any cdb command.
//
// The bridge prints its answer as "interpreterresult={...}" (dumper.py's
// reportInterpreterResult()), which for a ScriptCommand arrives here inside the
// extension's captured stdout rather than on a wire of its own the way gdb's MI
// delivers it - so it is picked out of those lines.
//
// "pending" means the service was not up yet, which is the normal case for a
// breakpoint set before the QQmlEngine exists - and the only way a QML
// breakpoint from the IDE ever fires, since the service has to be enabled before
// that engine is constructed. Reported as success either way, exactly as
// GdbImpl::handleInterpreterBreakpointInsert() does, with the retry armed: cdb
// has no in-debugger event loop, so the hook breakpoint that drives it lives
// here rather than in the bridge (see cdbbridge.py's own
// createResolvePendingBreakpointsHookBreakpoint()).
void CdbImpl::insertInterpreterBreakpoint(quint64 requestId, int modelId,
                                          const BreakpointParameters &params, bool report)
{
    DebuggerCommand cmd("theDumper.insertInterpreterBreakpoint", ScriptCommand);
    // Echoed back by the bridge rather than read by the service, and the only
    // thing tying a resolution back to the caller's own breakpoint - so it is the
    // caller's model id, not this class's cdb-side one.
    cmd.arg("modelid", modelId);
    cmd.arg("file", params.fileName.path());
    cmd.arg("line", params.textPosition.line);
    cmd.arg("enabled", params.enabled);
    cmd.arg("condition", toHex(params.condition));
    cmd.arg("ignorecount", params.ignoreCount);
    cmd.callback = [this, requestId, report](const DebuggerResponse &response) {
        if (response.resultClass != ResultDone) {
            if (report)
                emit breakpointEvent(requestId, BreakpointOp::Insert, false, {});
            return;
        }
        GdbMi result;
        for (const GdbMi &line : response.data["msg"]) {
            const QString text = line.data();
            if (!text.startsWith("interpreterresult="))
                continue;
            QStringDecoder decoder(QStringEncoder::Utf8);
            result.fromString(text.mid(int(strlen("interpreterresult="))), decoder);
            break;
        }
        if (!report)
            return;
        GdbMi list;
        list.m_type = GdbMi::List;
        if (result.isValid()) {
            GdbMi bkpt = result;
            bkpt.m_name = "bkpt";
            list.addChild(bkpt);
        }
        emit breakpointEvent(requestId, BreakpointOp::Insert, true, list);
    };
    runCommand(cmd);
}

// The two hooks a native mixed session needs, armed before the debuggee is ever
// resumed - which is the only moment they *can* be armed: cdb defers every
// command while the target runs, so a hook set from some later reply sits queued
// for a stop that never comes. Confirmed live: a "bu" issued from the QML
// insert's own reply was accepted and never executed.
//
//  - qt_qmlDebugConnectorOpen: the connector coming up, early enough that
//    enabling the service there still attaches the interpreter's
//    per-instruction breakpoint check to the QQmlEngine about to be built. This
//    is where a pending QML breakpoint gets resolved.
//  - qt_qmlDebugMessageAvailable: the interpreter announcing an event worth
//    stopping for - gdbbridge.py's own InterpreterMessageBreakpoint.
//
// Both live in the native QML debugger *plugin*, not in Qt6Qml: verified with
// dumpbin, qmldbg_native[d].dll exports the whole qt_qmlDebug* family and Qt6Qml
// exports none of it (only the qt_v4* hooks). That is also the module
// cdbbridge.py's serviceModuleName() looks for. cdb needs a module-qualified
// symbol to defer a breakpoint on a library that has not loaded yet, and this
// plugin loads late, so both spellings are armed - whichever module never
// appears just stays deferred, harmlessly.
//
// gdbbridge.py hooks these from Python (gdb.Breakpoint subclasses reacting in
// gdb's own event loop); cdbext exposes no breakpoint API and cdb has no
// in-debugger event loop, so here they are ordinary internal breakpoints whose
// hits handleExtensionMessage() dispatches.
void CdbImpl::armInterpreterHooks()
{
    if (!m_interpreterResolverIds.isEmpty())
        return;
    for (const QString &module : {QString("qmldbg_natived"), QString("qmldbg_native")}) {
        const QString resolverId = nextBreakpointId();
        m_internalBreakpointIds.insert(resolverId);
        m_interpreterResolverIds.insert(resolverId);
        runCommand({"bu" + resolverId + ' ' + module + "!qt_qmlDebugConnectorOpen", NoFlags});

        const QString messageId = nextBreakpointId();
        m_internalBreakpointIds.insert(messageId);
        m_interpreterMessageIds.insert(messageId);
        runCommand({"bu" + messageId + ' ' + module + "!qt_qmlDebugMessageAvailable", NoFlags});
    }
}

// Brings up the extension's embedded Python and the cdbbridge.py dumper, the
// same sequence CdbEngine::setupScripting() runs for a local session: ask for
// sys.version (which also proves the interpreter is there at all), then put the
// dumper directory on sys.path and construct theDumper.
//
// The interpreter only exists when qtcreatorcdbext was built WITH_PYTHON - its
// CMake skips that whenever Python3's Development component is missing, and for a
// debug build it wants the debug import library (python3xx_d.lib) specifically.
// Without it the "script" command answers an error, which is what leaves
// m_pythonVersion at 0; every caller has to treat that as "no Python here"
// rather than assume a bridge exists.
// Runs whatever was queued while the bridge came up (or failed to) - see
// m_pendingBridgeWork.
void CdbImpl::flushPendingBridgeWork()
{
    const QList<std::function<void()>> pending = m_pendingBridgeWork;
    m_pendingBridgeWork.clear();
    for (const std::function<void()> &work : pending)
        work();
}

void CdbImpl::setupScripting()
{
    runCommand({"print(sys.version)", ScriptCommand, [this](const DebuggerResponse &response) {
        const GdbMi data = response.data["msg"];
        if (response.resultClass != ResultDone || data.childCount() == 0) {
            emit DebuggerEngineInterface::message(
                QString("CdbImpl: no Python in the cdb extension, so no dumper bridge: %1")
                    .arg(response.data["msg"].data()), LogWarning);
            flushPendingBridgeWork();
            return;
        }
        // "3.12.10 (tags/v3.12.10:...) [MSC v.1938 64 bit (AMD64)]" - only the
        // first token is the version, packed the way CdbEngine packs it.
        const QStringList version = data.childAt(0).data().split(' ').constFirst().split('.');
        if (version.size() != 3) {
            emit DebuggerEngineInterface::message(
                QString("CdbImpl: cannot parse sys.version: %1").arg(data.childAt(0).data()),
                LogWarning);
            return;
        }
        bool ok = false;
        unsigned packed = version.at(0).toUInt(&ok);
        for (int i = 1; ok && i < 3; ++i)
            packed = (packed << 8) | version.at(i).toUInt(&ok);
        if (!ok) {
            emit DebuggerEngineInterface::message(
                QString("CdbImpl: cannot parse sys.version: %1").arg(data.childAt(0).data()),
                LogWarning);
            return;
        }
        m_pythonVersion = packed;
        emit DebuggerEngineInterface::message(
            QString("CdbImpl: Python %1 in the cdb extension").arg(data.childAt(0).data().trimmed()),
            LogMisc);

        // Local sessions only, as there: the AttachToRemoteServer path reads
        // loadorder.txt and pushes every dumper module across as hex, which this
        // slice has no start mode for.
        QString dumperPath = m_startData.dumperScriptsDir.toUserOutput();
        dumperPath.replace('\\', "\\\\");
        runCommand({"sys.path.insert(1, '" + dumperPath + "')", ScriptCommand});
        runCommand({"from cdbbridge import Dumper", ScriptCommand});
        // Anything that needs theDumper runs from this command's own reply, not
        // from here: every step above is asynchronous, so the object only exists
        // once cdb has actually executed this one.
        runCommand({"theDumper = Dumper()", ScriptCommand,
                   [this](const DebuggerResponse &response) {
            if (response.resultClass != ResultDone) {
                emit DebuggerEngineInterface::message(
                    QString("CdbImpl: could not construct the dumper bridge: %1")
                        .arg(response.data["msg"].data()), LogError);
                m_pythonVersion = 0;
            }
            flushPendingBridgeWork();
        }});
    }});
}

void CdbImpl::initializeSession(const std::function<void()> &whenReady)
{
    runCommand({"sxn ibp", NoFlags});
    // Source line in assembly, exactly as handleInitialSessionIdle()
    // sends it - and required, not cosmetic: parseCdbDisassembler()
    // only finds anything with it set (confirmed live - without it
    // fetchDisassembly()'s reply covers no address at all). It also
    // makes "uf" print the source line number *twice* per instruction,
    // which is why parseFunctionDisassembly() locates the address by
    // shape rather than by column.
    runCommand({".asm source_line", NoFlags});
    // Force the just-launched module's symbols in before any
    // breakpoint is inserted. Without this a source-line "bu" is
    // accepted, reports no error whatsoever, and silently stays
    // unresolved forever - "bl" shows it as "eu" (enabled,
    // unresolved) and the debuggee runs straight past it. Verified in
    // isolation against a real cdb.exe: with a forced reload the same
    // breakpoint resolves to a real address and hits; with a bare
    // ".sympath <dir>" instead, it does not.
    //
    // Real CdbEngine issues no ".reload" at all, so this may yet turn
    // out to paper over a difference in *when* symbols first get
    // loaded there (its own breakpoints go in from
    // handleInitialSessionIdle(), after a "pid" extension round trip)
    // rather than being genuinely required - worth revisiting once
    // more of this backend works.
    // Point the symbol path at the debuggee's own directory first, and
    // only there. cdb's built-in default is "srv*" - the Microsoft
    // symbol *server* - so a forced reload against it goes to the
    // network for every module, which is slow and, worse, varies from
    // run to run: it doubled this suite's runtime and made the number
    // of tests that beat their timeout differ between identical runs.
    // The debuggee's PDB sits next to the executable, so nothing here
    // needs a server at all. Real CdbEngine likewise sets .sympath
    // explicitly (from settings) rather than inheriting the default.
    if (std::holds_alternative<ProcessRunData>(m_startData.inferiorStartData)) {
        const FilePath inferiorDir = std::get<ProcessRunData>(
            m_startData.inferiorStartData).command.executable().parentDir();
        if (!inferiorDir.isEmpty())
            runCommand({".sympath \"" + inferiorDir.nativePath() + '"', NoFlags});
    }
    runCommand({".reload /f", NoFlags});
    // The extension's "pid", exactly as handleInitialSessionIdle() sends
    // it, and for two reasons - the second one not obvious from the
    // command's name at all:
    //
    //  - It carries the *debuggee's* pid. What was reported here before
    //    came from Process::processId(), which is cdb.exe's own - so
    //    every consumer of inferiorPidKnown() (an interrupt via
    //    DebugBreakProcess, above all) had the wrong process.
    //  - It is the only thing that installs the extension's event
    //    callbacks: the "pid" command calls
    //    ExtensionContext::hookCallbacks() (qtcreatorcdbextension.cpp),
    //    and only those callbacks fill in a stop's own reason -
    //    EventCallback::Breakpoint() calls setStopReason() with the
    //    breakpoint id (eventcallback.cpp). Without it every stop
    //    notification arrives as reason="unknown" with no breakpointId
    //    (completeStopReasons() substitutes that literal), so nothing
    //    could tell which breakpoint stopped the debuggee - which is
    //    what breakpoint conditions below need. Confirmed live both
    //    ways.
    // The Python bridge first, so its own chain of ScriptCommands is queued
    // ahead of the resume below: cdb defers every command while the debuggee
    // runs, so anything issued after "g" would only execute at the next stop -
    // which for a bridge that is never constructed means never. Confirmed live:
    // with the resume happening first, "theDumper = Dumper()" was accepted and
    // silently never ran, and every QML breakpoint waited forever.
    //
    // Sending it before "pid" is what CdbEngine does too, and for the same
    // reason: replies come back in order, so its setupScripting() callback has
    // already queued the rest by the time the "pid" callback resumes.
    // Armed here, ahead of the bridge and the resume below - see
    // armInterpreterHooks() on why there is no later opportunity.
    if (m_startData.nativeMixed)
        armInterpreterHooks();
    setupScripting();
    runCommand({"pid", ExtensionCommand, [this, whenReady](const DebuggerResponse &response) {
        if (response.resultClass == ResultDone)
            emit inferiorPidKnown(response.data.toProcessHandle());
        else
            emit DebuggerEngineInterface::message(
                QString("CdbImpl: failed to determine the inferior pid: %1")
                    .arg(response.data["msg"].data()), LogError);
        if (whenReady)
            whenReady();
    }});
}

// The extension's "locals" reply, in the shape the dumper-driven backends
// report: the items under a "data" child, and their values decoded. The
// extension's own symbol-group format hex-encodes every value, an int included
// ("3000" is utf16 for "0") - the watch model would decode that, but a caller
// reading the value straight out of the report cannot.
static GdbMi dumperShapedLocals(const GdbMi &reply)
{
    GdbMi items;
    items.m_type = GdbMi::List;
    items.m_name = "data";
    for (const GdbMi &item : reply) {
        const QString encoding = item["valueencoded"].data();
        GdbMi decoded;
        decoded.m_type = GdbMi::Tuple;
        for (const GdbMi &field : item) {
            if (field.m_name == "valueencoded")
                continue;
            if (field.m_name == "value" && !encoding.isEmpty())
                decoded.addChild(constMi("value", decodeData(field.data(), encoding)));
            else
                decoded.addChild(field);
        }
        items.addChild(decoded);
    }
    GdbMi result;
    result.m_type = GdbMi::Tuple;
    result.addChild(items);
    return result;
}

void CdbImpl::refresh(const RefreshRequest &request)
{
    if (request.kind == RefreshKind::FullBacktrace) {
        // Mirrors CdbEngine::createFullBacktrace(): "~*kp" - every thread's
        // stack, with parameters - collected as a BuiltinCommand, whose reply is
        // the raw text cdb printed between the token markers. Handed back as a
        // nameless Const, the same shape GdbImpl uses for this kind, since the
        // payload is human-readable text rather than a structured tree.
        const quint64 requestId = request.requestId;
        runCommand({"~*kp", BuiltinCommand,
                   [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::FullBacktrace,
                                     constMi({}, response.data.data()));
        }});
        return;
    }
    if (request.kind == RefreshKind::Modules) {
        // Mirrors CdbEngine::reloadModules() exactly - the extension's own
        // "modules" command, no arguments. This is what backs the claimed
        // ReloadModuleCapability; note refresh(AllSymbols) separately issues
        // ".reload /f" to make cdb load the symbols those modules refer to.
        const quint64 requestId = request.requestId;
        m_pendingModulesRequestId = requestId;
        runCommand({"modules", ExtensionCommand,
                   [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::Modules, response.data);
        }});
        return;
    }
    if (request.kind == RefreshKind::Registers) {
        // Mirrors CdbEngine::reloadRegisters() exactly - the extension's own
        // "registers" command, no arguments. Its reply is already the flat list
        // of {name,size,type,value} tuples this signal's consumer expects
        // (GenericDebuggerEngine's Registers branch, itself mirroring
        // CdbEngine::handleRegistersExt()), so unlike FullStack below it needs
        // no reshaping at all. Values arrive 0x-prefixed hex from the
        // extension's own formatDebugValue(), which is exactly what that
        // consumer parses them as (HexadecimalFormat).
        const quint64 requestId = request.requestId;
        runCommand({"registers", ExtensionCommand,
                   [this, requestId](const DebuggerResponse &response) {
            // Reported rather than swallowed, mirroring
            // CdbEngine::handleRegistersExt()'s own failure branch: the
            // extension answers 'N' with an error text whenever the dbgeng
            // register interfaces refuse (see gdbmiRegisters()), and a silent
            // empty register view is the least debuggable outcome there is.
            if (response.resultClass != ResultDone) {
                emit message(QString("CdbImpl: failed to determine registers: %1")
                                 .arg(response.data["msg"].data()), LogError);
                // An empty list, not the failure's own data: the reply still
                // has to arrive so a caller waiting for this kind is not left
                // hanging, but it must not look like content - the error tuple
                // has a child ("msg"), and a consumer counting children would
                // take that for a register. Same shape GdbImpl emits when its
                // own register command fails.
                GdbMi empty;
                empty.m_type = GdbMi::List;
                emit refreshDataReceived(requestId, RefreshKind::Registers, empty);
                return;
            }
            emit refreshDataReceived(requestId, RefreshKind::Registers, response.data);
        }});
        return;
    }
    if (request.kind == RefreshKind::FullStack) {
        // Mirrors CdbEngine::reloadFullStack() exactly: the extension's own
        // "stack" command, with "unlimited" so the reply is not truncated to
        // the default depth. The stop notification's payload already carries a
        // stack (see handleExtensionMessage()'s "session_idle" branch, which
        // takes the top frame's location from it), but that one is capped by
        // the extension's own maxStackDepth and is only sent on a stop - an
        // explicit request has to ask for its own.
        const quint64 requestId = request.requestId;
        m_pendingStackRequestId = requestId;
        DebuggerCommand cmd("stack", ExtensionCommand,
                           [this, requestId](const DebuggerResponse &response) {
            // Reshaped into the "stack={frames=[...]}" tree every consumer of
            // this signal expects - GenericDebuggerEngine reads
            // data["stack"]["frames"] directly, and GdbImpl gets that shape for
            // free by passing gdb's MI reply straight through. The extension
            // instead answers a flat list of "frame=" tuples, so emitting its
            // reply unwrapped left that lookup empty: the stack view would never
            // have populated, and only a loose "does the text mention bump()"
            // assertion hid it.
            GdbMi frames = response.data;
            frames.m_name = "frames";
            GdbMi stack;
            stack.m_type = GdbMi::Tuple;
            stack.m_name = "stack";
            stack.addChild(frames);
            GdbMi wrapper;
            wrapper.m_type = GdbMi::Tuple;
            wrapper.addChild(stack);
            emit refreshDataReceived(requestId, RefreshKind::FullStack, wrapper);
        });
        cmd.args = QString("unlimited");
        runCommand(cmd);
        return;
    }
    if (request.kind == RefreshKind::DebuggingHelpers) {
        // No "reloadDumpers" step, unlike GdbImpl/LldbImpl/PdbImpl: this
        // backend's Locals goes through the extension DLL's own "locals"
        // command, not the Python dumper machinery (see the class comment), so
        // there are no dumpers to reload - re-fetching the locals is the whole
        // of it. Same chaining shape those three use, and what the caller
        // observes is identical: a Locals refresh comes back.
        refresh({request.requestId, RefreshKind::Locals});
        return;
    }
    if (request.kind == RefreshKind::AllSymbols) {
        // ".reload /f" is cdb's own "load every module's symbols now, for
        // real" - the same command the initial session_idle already uses to
        // get the debuggee's symbols in (see handleExtensionMessage()), which
        // is exactly what "refresh all symbols" means here. Then re-read the
        // stack, since that is what those symbols change: frames that
        // previously resolved to bare addresses can now carry names and
        // locations. Mirrors PdbImpl's/GdbImpl's own chaining, minus the
        // Modules refresh neither the extension nor this slice supports yet.
        runCommand({".reload /f", NoFlags});
        refresh({request.requestId, RefreshKind::FullStack});
        return;
    }
    if (request.kind != RefreshKind::Locals) {
        // Not ported this slice - see the class comment.
        emit message(QString("CdbImpl: refresh kind not ported this slice."), LogWarning);
        return;
    }
    // Mirrors CdbEngine::doUpdateLocals()'s own non-Python (ScriptCommand-
    // less) branch, minus its uninitialized-variable/type-format arguments.
    const quint64 requestId = request.requestId;
    m_pendingLocalsRequestId = requestId;
    DebuggerCommand cmd("locals", ExtensionCommand,
                       [this, requestId](const DebuggerResponse &response) {
        emit refreshDataReceived(requestId, RefreshKind::Locals,
                                 dumperShapedLocals(response.data));
    });
    // Watchers get the extension's own "-W" (synchronize its watch symbol
    // group) plus one "-w <iname> \"<expression>\"" each, exactly as
    // CdbEngine::doUpdateLocals()'s non-Python branch builds them. The
    // expression arrives hex-encoded (RefreshRequest::watchers carries the
    // Python bridge's own convention), so it has to be decoded first - the
    // extension takes a plain expression. The frame index stays last, as there.
    QString args = "-v -D";
    for (const QJsonValue &value : request.watchers) {
        const QJsonObject watcher = value.toObject();
        const QString expr = QString::fromUtf8(
            QByteArray::fromHex(watcher.value("exp").toString().toUtf8()));
        if (expr.isEmpty())
            continue;
        if (!args.contains(" -W"))
            args += " -W";
        args += " -w " + watcher.value("iname").toString() + " \"" + expr + '"';
    }
    // The frame to read, last as in CdbEngine::doUpdateLocals(), which takes it
    // from stackHandler()->currentIndex() - the same thing activateFrame() below
    // records here.
    args += ' ' + QString::number(m_currentFrameIndex);
    cmd.args = args;
    // Stashed for execute(RepeatLastCommand), the same way
    // CdbEngine::doUpdateLocals() stashes it. Without the callback: the repeat is
    // fire-and-forget, so its reply must not be reported a second time as the
    // answer to this request.
    m_lastDebuggableCommand = cmd;
    m_lastDebuggableCommand.callback = {};
    runCommand(cmd);
}

void CdbImpl::activateFrame(int index)
{
    // No command of its own: cdb's ".frame" only moves the *builtin* commands'
    // notion of the current frame, while locals come from the extension, which
    // takes the frame index as an argument instead - so this only records what
    // the next refresh(Locals) has to ask for, exactly as CdbEngine reads
    // stackHandler()->currentIndex() when building that command.
    m_currentFrameIndex = index;
}

void CdbImpl::selectThread(const QString &threadId)
{
    Q_UNUSED(threadId)
}

void CdbImpl::setRegisterValue(const QString &name, const QString &value)
{
    // Mirrors CdbEngine::setRegisterValue(): cdb's own "r <name>=<value>",
    // which takes the value as decimal or 0x-prefixed hex - the same format
    // the register view hands over. Fire-and-forget, minus that method's own
    // trailing reloadRegisters(): GenericDebuggerEngine::setRegisterValue()
    // already asks for the Registers refresh right after this call, the same
    // reasoning GdbImpl/LldbImpl note for their own version of this.
    runCommand({"r " + name + '=' + value, NoFlags});
}

void CdbImpl::accessMemory(MemoryOp op, quint64 requestId, quint64 addr, quint64 lengthOrSize,
                           const QByteArray &data)
{
    if (op == MemoryOp::Change) {
        // Mirrors CdbEngine::changeMemory() exactly, down to reusing
        // cdbWriteMemoryCommand() (cdbparsehelpers.cpp) rather than formatting
        // cdb's own "e<size> <address> <bytes>" a second time here.
        // Fire-and-forget, as there: nothing in the reply is needed, and a
        // caller re-reads the memory afterwards if it cares.
        QTC_ASSERT(!data.isEmpty(), return);
        runCommand({cdbWriteMemoryCommand(addr, data), NoFlags});
        return;
    }
    // Fetch: the extension's own "memory <address> <length>", whose reply is the
    // bytes hex-encoded - mirrors CdbEngine::fetchMemory(), including what it
    // does when the read fails: report the requested number of zero bytes rather
    // than nothing at all. A caller waits for exactly one reply per request
    // (MemoryAgent there, the memory view behind it), so staying silent would
    // hang it. GdbImpl reaches the same end state differently, by splitting a
    // failing read in half down to single bytes and zero-filling only the ones
    // that still fail - cdb needs no such retry, since the extension answers a
    // partial read as a plain error.
    const quint64 length = lengthOrSize;
    DebuggerCommand cmd("memory", ExtensionCommand);
    // Decimal, as CdbEngine writes them: the extension parses both with its own
    // integerFromString().
    cmd.args = QString("%1 %2").arg(addr).arg(length);
    cmd.callback = [this, requestId, addr, length](const DebuggerResponse &response) {
        QByteArray bytes;
        if (response.resultClass == ResultDone) {
            bytes = QByteArray::fromHex(response.data.data().toUtf8());
        } else {
            emit message(QString("CdbImpl: failed to read %1 bytes at 0x%2: %3")
                             .arg(length).arg(addr, 0, 16)
                             .arg(response.data["msg"].data()), LogWarning);
        }
        if (quint64(bytes.size()) != length)
            bytes = QByteArray(int(length), char(0));
        emit memoryDataReceived(requestId, addr, bytes);
    };
    runCommand(cmd);
}

// The first address an "x" reply names, its lines reading
// "<address> <module>!<symbol> (<signature>)" with cdb's own backtick inside the
// address.
static quint64 firstSymbolAddress(const QString &reply)
{
    const QStringList lines = reply.split(QChar::LineFeed);
    for (const QString &line : lines) {
        QString token = line.trimmed().section(' ', 0, 0);
        token.remove('`');
        bool ok = false;
        const quint64 address = token.toULongLong(&ok, 16);
        if (ok && address)
            return address;
    }
    return 0;
}

void CdbImpl::fetchDisassembly(quint64 requestId, quint64 address, const QString &functionName)
{
    // Mirrors CdbEngine::postDisassemblerCommand(): cdb's "u <start> <end>" over
    // a range centred on the address, because "u" on a bare address disassembles
    // a default-length block that need not contain it (see the comment above
    // CdbEngine::fetchDisassembler()), and the reply is parsed by the very same
    // exported helper, parseCdbDisassembler().
    //
    // A request carrying only a function name is resolved through "x" first, the
    // same route CdbEngine::fetchDisassembler() takes into postResolveSymbol()
    // before posting the identical range command. Its m_symbolAddressCache has no
    // counterpart here: one lookup per request is cheap enough.
    if (!address) {
        if (functionName.isEmpty()) {
            emit message("CdbImpl: cannot disassemble without an address or a name.",
                         LogWarning);
            emit disassemblyReceived(requestId, {});
            return;
        }
        runCommand({"x *!" + functionName, BuiltinCommand,
                   [this, requestId, functionName](const DebuggerResponse &response) {
            const quint64 resolved = firstSymbolAddress(response.data.data());
            if (!resolved) {
                emit message(QString("CdbImpl: cannot resolve \"%1\" to disassemble it.")
                                 .arg(functionName), LogWarning);
                emit disassemblyReceived(requestId, {});
                return;
            }
            fetchDisassembly(requestId, resolved, functionName);
        }});
        return;
    }
    enum { DisassemblerRange = 512 }; // The same span CdbEngine asks for.
    const quint64 start = address - DisassemblerRange / 2;
    const quint64 end = address + DisassemblerRange / 2;
    const QString cmd = QString("u 0x%1 0x%2").arg(start, 0, 16).arg(end, 0, 16);
    runCommand({cmd, BuiltinCommand, [this, requestId](const DebuggerResponse &response) {
        emit disassemblyReceived(requestId, parseCdbDisassembler(response.data.data()));
    }});
}

void CdbImpl::setPeripheralRegisterValue(quint64 address, quint64 value)
{
    // No CdbEngine counterpart to mirror: peripheral registers are a bare-metal
    // concept (uVision), and it never supported them. The operation itself is
    // only a poke at an address though - GdbImpl's own version is a plain
    // "set {int}0x...=..." - so it goes through the very same write path
    // accessMemory(Change) uses here, at that same int width. Writing the
    // quint64 whole would clobber the four bytes behind the target, which in
    // this backend's own test inferior is another live variable.
    const quint32 intValue = quint32(value);
    const QByteArray data(reinterpret_cast<const char *>(&intValue), sizeof(intValue));
    runCommand({cdbWriteMemoryCommand(address, data), NoFlags});
}

void CdbImpl::watchPoint(quint64 requestId, const QPoint &pnt)
{
    Q_UNUSED(requestId)
    Q_UNUSED(pnt)
}

void CdbImpl::createSnapshot(quint64 requestId)
{
    Q_UNUSED(requestId)
}

void CdbImpl::assignValueInDebugger(const WatchItemData &item, const QString &expr,
                                    const QString &value)
{
    // Was an empty stub, so assignments silently did nothing at all - the
    // caller's follow-up locals refresh simply still showed the old value.
    //
    // The extension's own "assign" command, hex-encoding both sides the way
    // real CdbEngine::assignValueInDebugger() does (cdb would otherwise choke
    // on spaces and quotes inside either). Always the "-e" form, which assigns
    // to an *expression*: that is exactly what this interface hands over, and
    // it needs no locals tree to have been fetched first, unlike the iname form
    // real CdbEngine prefers for non-watchers (its WatchItem always has one
    // because its watch model produced it; here there is no such guarantee).
    // Sent as a tokenized ExtensionCommand, not as a raw command the way real
    // CdbEngine does it. Untokenized, the extension answers with token=-1 and
    // its output recording is left without a host, so the *next* extension
    // command - the locals re-read a caller does to observe the new value -
    // fails with "ExtensionContext::startRecordingOutput() called with no
    // output host" and returns nothing. With a token the reply is correlated
    // normally and the following locals fetch works.
    const auto hex = [](const QString &s) { return QString::fromUtf8(s.toUtf8().toHex()); };
    Q_UNUSED(item)
    DebuggerCommand cmd("assign", ExtensionCommand);
    cmd.args = QString("-h -e " + hex(expr) + '=' + hex(value));
    runCommand(cmd);
}

void CdbImpl::executeDebuggerCommand(const QString &command,
                               const WatchItemData &inspectorItem)
{
    Q_UNUSED(inspectorItem) // Inspector view is QML-only, see the interface.
    runCommand({command, NoFlags});
}

void CdbImpl::handleCdbOutput(const QString &output)
{
    m_inbuffer += output;
    int newline;
    while ((newline = m_inbuffer.indexOf('\n')) >= 0) {
        QString line = m_inbuffer.left(newline);
        m_inbuffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);

        // Strip any leading cdb prompts before anything below tries to match
        // the line, exactly as CdbEngine::parseOutput() does. Confirmed live
        // against a real cdb.exe: without this, every builtin command's own
        // "<token>N<"/"<token>N>" echo arrives as "0:000> <token>1<", which
        // checkCommandToken()'s startsWith() rejects - so no builtin command
        // ever completes, no callback fires, and the session stalls after the
        // very first one (breakpoint insertion never reports back, so nothing
        // ever gets as far as resuming the inferior). More than one prompt can
        // precede a single line - "0:000> 0:000> <token>1>" was observed on
        // the first run - hence a loop, matching CdbEngine's own comment that
        // sequences of prompts are possible while the extension's output
        // callback isn't hooked yet.
        while (isCdbPrompt(line))
            line.remove(0, CdbPromptLength);

        static const QString extPrefix = "<qtcreatorcdbext>|";
        if (line.startsWith(extPrefix)) {
            // "<qtcreatorcdbext>|<type>|<token>|<remainingChunks>|<what>|<message>"
            const char type = char(line.at(extPrefix.size()).unicode());
            const int tokenPos = extPrefix.size() + 2;
            const int tokenEnd = line.indexOf('|', tokenPos);
            QTC_ASSERT(tokenEnd != -1, continue);
            const int token = line.mid(tokenPos, tokenEnd - tokenPos).toInt();
            const int chunksPos = tokenEnd + 1;
            const int chunksEnd = line.indexOf('|', chunksPos);
            QTC_ASSERT(chunksEnd != -1, continue);
            const int remainingChunks = line.mid(chunksPos, chunksEnd - chunksPos).toInt();
            const int whatPos = chunksEnd + 1;
            const int whatEnd = line.indexOf('|', whatPos);
            QTC_ASSERT(whatEnd != -1, continue);
            const QString what = line.mid(whatPos, whatEnd - whatPos);
            m_extensionMessageBuffer += line.mid(whatEnd + 1);
            if (remainingChunks == 0) {
                handleExtensionMessage(type, token, what, m_extensionMessageBuffer);
                m_extensionMessageBuffer.clear();
            }
            continue;
        }

        int token = 0;
        bool isStart = false;
        const bool isCommandToken = checkCommandToken(m_tokenPrefix, line, &token, &isStart);
        if (m_currentBuiltinResponseToken != -1) {
            QTC_ASSERT(!isStart, continue);
            if (isCommandToken) {
                const DebuggerCommand command = m_commandForToken.take(token);
                if (command.callback) {
                    DebuggerResponse response;
                    response.token = token;
                    response.resultClass = ResultDone;
                    response.data.m_name = "data";
                    response.data.m_data = m_currentBuiltinResponse;
                    response.data.m_type = GdbMi::Tuple;
                    command.callback(response);
                }
                m_currentBuiltinResponseToken = -1;
                m_currentBuiltinResponse.clear();
            } else {
                if (!m_currentBuiltinResponse.isEmpty())
                    m_currentBuiltinResponse.push_back('\n');
                m_currentBuiltinResponse.push_back(line);
            }
            continue;
        }
        if (isCommandToken) {
            m_currentBuiltinResponseToken = token;
            continue;
        }
        // "Breakpoint <n> hit" - cdb's own announcement, and the only place a
        // hit is reported at all. The extension's "breakpoints" reply carries
        // enabled/address/passcount but no hit count (checked live), and real
        // CdbEngine never needs one: it owns a BreakHandler and reports through
        // that instead of this interface's breakpointModified(). So the count is
        // kept here, per cdb breakpoint id, and reported in the same shape
        // GdbImpl/LldbImpl/PdbImpl use - a one-element list whose tuple carries
        // "number" and "times".
        static const QRegularExpression hitRe("^Breakpoint (\\d+) hit$");
        const QRegularExpressionMatch hit = hitRe.match(line);
        if (hit.hasMatch()) {
            const QString number = hit.captured(1);
            // An internal one-shot breakpoint (RunToLine/RunToFunction) is this
            // class's own business - the caller never inserted it, so reporting
            // its hit would be a notification about a breakpoint that, from the
            // caller's point of view, does not exist.
            if (m_internalBreakpointIds.contains(number)) {
                emit message(line, LogMisc);
                continue;
            }
            // A sub-location's hit belongs to the breakpoint the caller asked
            // for: when an insert was ambiguous, only these per-match ids exist
            // on cdb's side, and the caller has never seen them as breakpoints
            // of their own (see changeBreakpoint()'s ambiguous-symbol path).
            const QString reportedNumber = m_parentForSubBreakpointId.value(number, number);
            const int times = ++m_breakpointHitCounts[reportedNumber];
            GdbMi bkpt;
            bkpt.m_type = GdbMi::Tuple;
            bkpt.addChild(constMi("number", reportedNumber));
            bkpt.addChild(constMi("enabled", "y"));
            bkpt.addChild(constMi("times", QString::number(times)));
            GdbMi list;
            list.m_type = GdbMi::List;
            list.addChild(bkpt);
            emit breakpointModified(list);
            emit message(line, LogMisc);
            continue;
        }
        // Module-load lines, the cdb banner, prompts - only shown, not
        // parsed further this slice (see the class comment).
        emit message(line, LogMisc);
    }
}

void CdbImpl::handleExtensionMessage(char type, int token, const QString &what,
                                     const QString &message)
{
    if (type == 'R' || type == 'N') {
        if (token == -1)
            return;
        const DebuggerCommand command = m_commandForToken.take(token);
        if (!command.callback)
            return;
        DebuggerResponse response;
        response.token = token;
        response.data.m_name = "data";
        if (type == 'R') {
            response.resultClass = ResultDone;
            QStringDecoder decoder(QStringEncoder::System);
            response.data.fromString(message, decoder);
            if (!response.data.isValid()) {
                response.data.m_data = message;
                response.data.m_type = GdbMi::Tuple;
            }
        } else {
            // Mirrors CdbEngine::handleExtensionMessage()'s own 'N' branch: on a
            // failure the whole payload *is* the error text, and it is the only
            // account of what went wrong the extension ever sends. Dropping it,
            // as this did, left a failed extension command indistinguishable
            // from one that succeeded with an empty reply - the callback saw a
            // ResultFail carrying no data at all.
            response.resultClass = ResultFail;
            GdbMi msg;
            msg.m_name = "msg";
            msg.m_data = message;
            msg.m_type = GdbMi::Tuple;
            response.data.m_type = GdbMi::Tuple;
            response.data.addChild(msg);
        }
        command.callback(response);
        return;
    }

    // The debuggee's exit, with its exit code: the extension's EventCallback::
    // ExitProcess() reports "Process exited (<code>)" (eventcallback.cpp), which
    // is also what CdbEngine's own "event" branch keys on - it just discards the
    // code, having no interface to carry one. This only arrives because setup
    // sends the extension's "pid", which hooks those callbacks in the first
    // place (see initializeSession()); the note that it never arrives at all
    // predates that.
    if (what == "event" && message.startsWith("Process exited")) {
        m_inferiorRunning = false;
        if (!m_shuttingDown && !m_inferiorExited) {
            m_inferiorExited = true;
            static const QRegularExpression exitCode(R"(\((\d+)\))");
            const QRegularExpressionMatch match = exitCode.match(message);
            emit inferiorDone({match.hasMatch() ? match.captured(1).toInt() : 0,
                               InferiorExitStatus::Normal});
        }
        return;
    }

    // The debuggee is gone. This arrives as "session_inaccessible" carrying
    // DEBUG_STATUS_NO_DEBUGGEE (7) - the extension's payload for these session
    // notifications is a raw DEBUG_STATUS_* value, confirmed live against a
    // real cdb.exe across a whole run: 1 (GO) while the debuggee runs, 6
    // (BREAK) at every stop - matching session_idle's own
    // executionStatus="6" - and 7 exactly once, as the last thing seen after
    // the debuggee exits.
    //
    // The previous check here - "event" with a message starting "Process
    // exited" - was dead code: the extension never sends a notification named
    // "event" at all in this configuration (verified by logging every one that
    // arrives), so a spontaneous exit was never reported and callers waiting
    // for inferiorDone() waited forever.
    //
    // Guarded like every other backend's own deliberate-kill flag (e.g.
    // PdbImpl::m_shuttingDown): shutdownEngine()'s and Abort's own kill of
    // m_cdbProc produce the same notification, which must not be reported as
    // the debuggee exiting on its own on top of a shutdown already underway.
    // m_inferiorExited additionally keeps this to one report per session.
    if (what == "session_inaccessible" && message.trimmed() == "7") {
        m_inferiorRunning = false;
        if (!m_shuttingDown && !m_inferiorExited) {
            m_inferiorExited = true;
            // Exit code is not carried by this notification; real CdbEngine
            // gets one from its own "event" report, which never arrives here.
            emit inferiorDone({0, InferiorExitStatus::Normal});
        }
        return;
    }

    // "session_idle", not "idle" - the notification name the extension
    // actually sends (see qtcreatorcdbext's own reportSessionIdle(), and
    // CdbEngine::handleExtensionMessage()'s matching case). Confirmed live
    // against a real cdb.exe: the extension emits
    // "<qtcreatorcdbext>|E|0|0|session_idle|{executionStatus=...}", so
    // matching on the bare "idle" this once used never fired at all - the
    // whole session stalled right after startup, since this is what reports
    // both the initial run and every subsequent stop.
    if (what == "session_idle") {
        const bool firstTime = !m_initialSessionIdleHandled;
        m_initialSessionIdleHandled = true;
        if (firstTime) {
            // This first notification is the loader breakpoint cdb always
            // stops at (ntdll!LdrpDoDebuggerBreak) - the debuggee exists but
            // has not run a single instruction of its own yet, so reporting
            // "running" without actually resuming leaves the inferior parked
            // there forever and every test waiting for a stop times out
            // (confirmed live). Real CdbEngine resumes at exactly this point
            // too - handleInitialSessionIdle() ends in runEngine(), whose
            // last act is doContinueInferior()'s own "g". "sxn ibp" first, so
            // that resume doesn't stop again on further initial breakpoints,
            // matching runEngine()'s own exception setup.
            // Everything below runs from initializeSession()'s own last round
            // trip, not from here: the setup it queues is asynchronous, and
            // resuming before it has been *issued* means cdb defers the rest
            // until the next stop (see its comment on the bridge).
            initializeSession([this] {
                if (m_isResetRestart) {
                    // A relaunch from ExecutionCommand::ResetInferior, not a
                    // fresh session: the caller already had its setup reported
                    // once and is waiting for the restarted debuggee to stop, so
                    // no setup events here. The breakpoints go back in under
                    // their original ids - silently, since the caller never
                    // removed them - and only then does the debuggee run, or it
                    // would race straight past them.
                    m_isResetRestart = false;
                    for (auto it = m_insertedBreakpoints.cbegin();
                             it != m_insertedBreakpoints.cend(); ++it) {
                        insertBreakpoint(0, it.key(), 0, it.value(), false);
                    }
                } else {
                    // Only now report setup done, so a caller inserting its
                    // initial breakpoints in response (GenericDebuggerEngine, and
                    // launchAndStopAtBreakpoint() in tst_backends.cpp) does so
                    // with symbols already loaded, and against a debuggee that
                    // has not run yet. This mirrors real CdbEngine, whose
                    // notifyEngineSetupOk() likewise only fires from
                    // handleInitialSessionIdle(), not from process start. Both
                    // emits are synchronous, so anything the caller sends lands
                    // ahead of the "g" that follows.
                    emit inferiorEvent(InferiorEvent::EngineSetupOk);
                    emit inferiorEvent(InferiorEvent::RunAndInferiorRunOk);
                }
                // Same expectation ExecutionCommand::Continue sets for its own
                // "g": this resumes a freely running debuggee, so whatever stops
                // it next (a breakpoint being hit) is a SpontaneousStop, not the
                // StopOk of a synchronous request. Without this the very first
                // breakpoint hit was reported as StopOk and every test waiting on
                // SpontaneousStop timed out even though cdb had stopped exactly
                // where it should.
                m_expectSpontaneousStop = true;
                m_inferiorRunning = true;
                runCommand({"g", NoFlags});
            });
            return;
        }
        // Report where we stopped before reporting the stop itself, same order
        // as PdbImpl/GdbImpl. The notification's own payload already carries
        // the whole stack, so no extra round trip is needed:
        // stack=[frame={level="0",...,fullname="<full path>",file="<base>",
        // line="21"},...]. "fullname" (not "file", which is the bare basename)
        // is what callers compare against the source they asked to break in.
        // Nothing emitted this at all before, so every location-checking test
        // saw an empty stoppedFile().
        {
            GdbMi stopData;
            QStringDecoder decoder(QStringEncoder::System);
            stopData.fromString(message, decoder);
            // The resolver hook: qt_qmlDebugConnectorOpen has been reached, so
            // the service can be enabled and every pending QML breakpoint
            // retried - the work gdbbridge.py's own Resolver does in its
            // interpreterEventHandler(). Then straight on, without reporting a
            // stop: this breakpoint is this class's own business, and the
            // debuggee has not reached anything the caller asked about.
            if (stopData["reason"].data() == "breakpoint"
                    && m_interpreterResolverIds.contains(stopData["breakpointId"].data())) {
                runCommand({"theDumper.resolvePendingInterpreterBreakpoints()", ScriptCommand,
                           [this](const DebuggerResponse &response) {
                    if (response.resultClass != ResultDone) {
                        emit DebuggerEngineInterface::message(
                            QString("CdbImpl: could not resolve the pending QML breakpoints: %1")
                                .arg(response.data["msg"].data()), LogWarning);
                    } else {
                        // Each breakpoint that resolved reports itself as
                        // "interpreterasync={...,asyncclass=breakpointmodified}"
                        // (dumper.py's reportInterpreterAsync()), which is the
                        // caller's only word that its pending breakpoint now has
                        // a real interpreter id. Same handling GdbImpl and
                        // LldbImpl give that report, only read out of a script
                        // command's reply rather than the wire.
                        for (const GdbMi &line : response.data["msg"]) {
                            const QString text = line.data();
                            if (!text.startsWith("interpreterasync="))
                                continue;
                            GdbMi all;
                            QStringDecoder decoder(QStringEncoder::Utf8);
                            all.fromStringMultiple(text, decoder);
                            if (all["asyncclass"].data() != "breakpointmodified")
                                continue;
                            GdbMi list;
                            list.m_type = GdbMi::List;
                            list.addChild(all["interpreterasync"]);
                            emit breakpointModified(list);
                        }
                    }
                    m_expectSpontaneousStop = true;
                    m_inferiorRunning = true;
                    runCommand({"g", NoFlags});
                }});
                return;
            }
            // A conditional breakpoint: cdb stopped for it unconditionally, so
            // whether this counts as a stop at all is decided here, exactly as
            // CdbEngine::examineStopReason() does it - evaluate the condition
            // through the extension's "expression" command and either report the
            // stop or resume, silently. Nothing was reported to the caller yet,
            // so a resume here leaves no trace of a stop that never happened.
            //
            // The reported id can be a sub-location of a multi-location
            // breakpoint, whose condition belongs to the breakpoint as a whole.
            // reason/breakpointId only arrive at all because setup sends the
            // extension's "pid" (see above), which is what hooks the event
            // callbacks that fill them in.
            const QString stoppedId = m_parentForSubBreakpointId.value(
                stopData["breakpointId"].data(), stopData["breakpointId"].data());
            const QString condition = m_conditionForBreakpointId.value(stoppedId);
            if (!condition.isEmpty() && !m_evaluatingCondition
                    && stopData["reason"].data() == "breakpoint") {
                // Quoted the same way CdbEngine quotes it: the extension splits
                // its arguments on blanks, so an expression containing one has
                // to arrive as a single argument.
                QString args = condition;
                if (args.contains(' ') && !args.startsWith('"'))
                    args = '"' + args + '"';
                m_evaluatingCondition = true;
                DebuggerCommand cmd("expression", ExtensionCommand);
                cmd.args = args;
                cmd.callback = [this, stopData, condition](const DebuggerResponse &response) {
                    m_evaluatingCondition = false;
                    // Only an int64 expression can be evaluated (the extension's
                    // own evaluateInt64Expression()); a failure is reported and
                    // treated as "stop", so a mistyped condition cannot silently
                    // swallow every hit of the breakpoint.
                    const bool failed = response.resultClass != ResultDone;
                    if (failed) {
                        emit DebuggerEngineInterface::message(
                            QString("CdbImpl: could not evaluate the condition \"%1\": %2")
                                .arg(condition, response.data["msg"].data()), LogError);
                    }
                    const int value = failed ? 1 : response.data.data().toInt();
                    emit DebuggerEngineInterface::message(
                        QString("CdbImpl: condition \"%1\" evaluated to %2, %3.")
                            .arg(condition).arg(value)
                            .arg(value ? QLatin1String("stopping") : QLatin1String("continuing")), LogMisc);
                    if (value) {
                        reportStop(stopData);
                        return;
                    }
                    // Resume, and keep expecting a spontaneous stop: the next
                    // one is whatever the freely running debuggee trips next.
                    m_expectSpontaneousStop = true;
                    m_inferiorRunning = true;
                    runCommand({"g", NoFlags});
                };
                runCommand(cmd);
                return;
            }
            // A tracepoint: cdb stopped for it like for any other breakpoint, so
            // what makes it one is decided here - log the message and resume,
            // reporting no stop the caller never asked for. Every breakpoint at
            // that location is considered, not just the one cdb names: cdb reports
            // a single id per stop, so a plain breakpoint sharing the line with a
            // tracepoint is still owed its stop once the logging is done.
            if (stopData["reason"].data() == "breakpoint" && !m_expandingTracepoint
                    && m_insertedBreakpoints.contains(stoppedId)) {
                const BreakpointParameters stopped = m_insertedBreakpoints.value(stoppedId);
                QStringList tracepointMessages;
                bool stopAfterwards = false;
                for (const BreakpointParameters &params : std::as_const(m_insertedBreakpoints)) {
                    if (!isSameLocation(params, stopped))
                        continue;
                    if (params.tracepoint)
                        tracepointMessages.append(params.message);
                    else
                        stopAfterwards = true;
                }
                if (!tracepointMessages.isEmpty()) {
                    reportTracepoint(tracepointMessages, stopData, stopAfterwards);
                    return;
                }
            }
            reportStop(stopData);
        }
        return;
    }
}

// Reports where the debuggee stopped and then the stop itself, in that order -
// same as PdbImpl/GdbImpl. The stop notification's own payload already carries
// the whole stack, so no extra round trip is needed:
// stack=[frame={level="0",...,fullname="<full path>",file="<base>",line="21"},
// ...]. "fullname" (not "file", which is the bare basename) is what callers
// compare against the source they asked to break in.
void CdbImpl::reportStop(const GdbMi &stopData)
{
    const GdbMi stack = stopData["stack"];
    if (stack.childCount() > 0) {
        const GdbMi &topFrame = stack.childAt(0);
        const QString fullName = topFrame["fullname"].data();
        if (!fullName.isEmpty())
            emit locationChanged(FilePath::fromUserInput(fullName), topFrame["line"].toInt());
    }
    m_inferiorRunning = false;
    if (m_interruptRequested) {
        m_interruptRequested = false;
        emit inferiorEvent(InferiorEvent::StopOk);
    } else if (m_expectSpontaneousStop) {
        emit inferiorEvent(InferiorEvent::SpontaneousStop);
    } else {
        emit inferiorEvent(InferiorEvent::StopOk);
    }
    m_expectSpontaneousStop = false;
}

// Mirrors CdbEngine::adjustOperateByInstruction() exactly, including only
// sending anything when the mode actually changes - see
// m_lastOperateByInstruction's own comment on why the first step must always
// send it.
void CdbImpl::adjustOperateByInstruction(bool operateByInstruction)
{
    if (m_lastOperateByInstruction == operateByInstruction)
        return;
    m_lastOperateByInstruction = operateByInstruction;
    runCommand({operateByInstruction ? QLatin1String("l-t") : QLatin1String("l+t"), NoFlags});
}

// Sets the instruction pointer and reports the new position.
//
// The stop has to be synthesized here: cdb's "r rip=" only moves the pointer,
// the session never leaves the stop it is already in, so no "session_idle"
// notification follows and nothing else would ever report one. It is reported
// as SpontaneousStop, matching the rest of the interface rather than this
// command's mechanics: GdbImpl reaches the same end state through gdb's own
// "jump", which *resumes* at the target and trips an internal one-shot
// breakpoint there, so callers see a spontaneous stop from a JumpToLine on
// every backend.
void CdbImpl::jumpToAddress(quint64 address, const Utils::FilePath &file, int line)
{
    runCommand({"r rip=0x" + QString::number(address, 16), NoFlags});
    if (!file.isEmpty())
        emit locationChanged(file, line);
    emit inferiorEvent(InferiorEvent::SpontaneousStop);
}

void CdbImpl::runCommand(const DebuggerCommand &dbgCmd)
{
    if (!m_cdbProc.isRunning())
        return;
    if (dbgCmd.flags & ScriptCommand) {
        // Repacked into the extension's own "script" command, exactly as
        // CdbEngine::runCommand() does it: that command hands the string to the
        // Python interpreter embedded in qtcreatorcdbext (PyRun_SimpleString),
        // which only exists when the extension was built WITH_PYTHON.
        DebuggerCommand scriptCmd("script", ExtensionCommand, dbgCmd.callback);
        scriptCmd.args = dbgCmd.args.isNull()
                       ? dbgCmd.function
                       : QString(dbgCmd.function + '(' + dbgCmd.argsToPython() + ')');
        runCommand(scriptCmd);
        return;
    }
    QString fullCmd;
    if (dbgCmd.flags == NoFlags) {
        fullCmd = dbgCmd.function + '\n';
    } else if (dbgCmd.flags & BuiltinCommand) {
        const int token = ++m_nextCommandToken;
        m_commandForToken.insert(token, dbgCmd);
        fullCmd = ".echo \"" + m_tokenPrefix + QString::number(token) + "<\"\n"
                + dbgCmd.function + "\n"
                + ".echo \"" + m_tokenPrefix + QString::number(token) + ">\"\n";
    } else if (dbgCmd.flags & ExtensionCommand) {
        const int token = ++m_nextCommandToken;
        m_commandForToken.insert(token, dbgCmd);
        const QString prefix = m_extensionCommandPrefix + dbgCmd.function;
        const QString arguments = dbgCmd.args.isString() ? dbgCmd.args.toString() : QString();
        fullCmd = prefix + " -t " + QString::number(token) + ".0 " + arguments + '\n';
    }
    // Every other backend logs what it sends (see PdbImpl::postDirectCommand(),
    // and real CdbEngine's own showMessage(..., LogInput)) - without it there is
    // no way to tell "the command was never sent" from "it was sent and cdb
    // ignored it", since a builtin command's own "<token>N<"/"<token>N>" echo is
    // consumed by handleCdbOutput() rather than logged.
    emit message(fullCmd.trimmed(), LogInput);
    m_cdbProc.write(fullCmd);
}

} // namespace Debugger::Internal
