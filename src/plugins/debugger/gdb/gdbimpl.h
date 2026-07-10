// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../debuggerengineinterface.h"

#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/processinterface.h>
#include <utils/qtcprocess.h>

#include <memory>

#include <QHash>
#include <QSet>
#include <QStringDecoder>

namespace Debugger::Internal {

// First attempt at a real DebuggerEngineInterface implementation, to check
// whether the interface sketch is actually sufficient - not a replacement
// for GdbEngine. Reuses the existing engine-independent DebuggerCommand/
// DebuggerResponse/GdbMi/DebuggerOutputParser types from debuggerprotocol.h.
// Real: process launch, MI command dispatch, the Python dumper bridge
// (needed for breakpoints and locals - both go through it), breakpoint
// insert/remove/update/enableSub for every BreakpointType, including
// message tracepoints with capture expressions, refresh() for every kind
// (Locals/FullStack/QmlStack/Registers/Modules/
// PeripheralRegisters/SourceFiles/AllSymbols/StackSymbols/DebuggingHelpers/
// ModuleSymbols/ModuleSections - see refresh()'s comment on each), thread/
// frame selection, register writes, peripheral register writes,
// watch-expression value assignment, memory read/write (including the
// split-and-retry recovery for a read spanning an unmapped page - see
// MemoryRequestCookie/fetchMemoryHelper() below), disassembly (the plain
// CLI range query only - see fetchDisassembly()'s comment), the current line marker
// on every stop (the common case only - see the *stopped handling in
// handleOutputLine()'s comment), run-to-line/run-to-function/jump-to-line/
// reverse-debugging record toggle/debug-last-command (see execute()'s
// comment on each), and the RunRequested/RunOk/RunFailed/StopOk/
// SpontaneousStop bookkeeping continue/step/interrupt need (see
// runRunRequestCommand() and m_interruptRequested) - GenericDebuggerEngine
// already reacts to all of these generically, this class just wasn't
// emitting them yet.
// No DebuggerRunParameters yet - the gdb command line, the inferior to
// launch, and the debugger helper scripts directory are passed directly
// into the constructor instead. DebuggerEngineSetupData (see
// debuggerengineinterface.h) covers a narrower, different need - fixed
// per-backend-type facts (capabilities, tooltip handling), not run
// configuration.
// Namespace-scope, not nested in GdbImpl: parseTracepointCaptures() (see
// gdbimpl.cpp) needs these but is a free function, not a member - a
// private nested type wouldn't be visible to it. GdbImpl-prefixed since
// gdbengine.cpp (it includes gdbimpl.h) already has its own, differently-
// shaped TracepointCaptureType/Data at this same namespace scope
// (QVariant-based, not QString) - genuinely different types, not a
// redundant redeclaration, so a plain name would collide.
enum class GdbImplTracepointCaptureType {
    Address, Caller, Callstack, FilePos, Function,
    Pid, ProcessName, Tick, Tid, ThreadName, Expression
};
struct GdbImplTracepointCaptureData
{
    GdbImplTracepointCaptureType type;
    QString expression;
    int start = 0;
    int end = 0;
};
struct GdbImplTracepointInfo
{
    QString message;
    QList<GdbImplTracepointCaptureData> captures;
};

// dumperScriptsDir: passed in rather than ICore::resourcePath("debugger")
// looked up internally, so GdbImpl stays free of any Core dependency.
// mainFunctionName: mirrors GdbEngine::mainFunction()'s already-computed
// result (not the toolchain Abi/useTerminal() facts it derives from - no
// DebuggerRunParameters here to do that derivation itself) - a Windows GUI
// (non-console) Qt app has its real main() renamed to qMain internally by
// the qtmain shim, so the break-at-main breakpoint needs that name instead.
// nativeMixedDebugging: mirrors DebuggerRunParameters::isNativeMixedDebugging(),
// fixed for the session - drives every refresh()/tracepoint "nativemixed" arg.
// isElfTarget: mirrors GdbEngine::setLinuxOsAbi()'s own isElf computation
// (toolchain Abi's binary format, falling back to inspecting the actual
// inferior binary via Abi::abisOfBinary()) - same "no DebuggerRunParameters/
// Abi dependency here" reasoning as mainFunctionName above.
class DEBUGGER_EXPORT GdbImplStartData
{
public:
    Utils::ProcessRunData debuggerRunData;
    InferiorStartData inferiorStartData;
    Utils::FilePath dumperScriptsDir;
    QString mainFunctionName = "main";
    bool nativeMixedDebugging = false;
    bool isElfTarget = false;
};

class DEBUGGER_EXPORT GdbImpl final : public DebuggerEngineInterface
{
    Q_OBJECT

public:
    explicit GdbImpl(const GdbImplStartData &startData);
    ~GdbImpl() override;

private:
    void start() final;
    void shutdownInferior(ShutdownMode mode) final;
    void shutdownEngine() final;

    void execute(const ExecutionRequest &request) final;
    void changeBreakpoint(const BreakpointChangeRequest &request) final;
    void refresh(const RefreshRequest &request) final;

    void selectThread(const QString &threadId) final;
    void activateFrame(int index) final;
    void setRegisterValue(const QString &name, const QString &value) final;
    // Fetch: splits and retries in half on a failed read, recursively -
    // mirrors GdbEngine::handleFetchMemory(); see MemoryRequestCookie and
    // fetchMemoryHelper() below.
    void accessMemory(MemoryOp op, quint64 requestId, quint64 addr, quint64 lengthOrSize,
                      const QByteArray &data) final;
    void fetchDisassembly(quint64 requestId, quint64 address, const QString &functionName) final;
    // GdbEngine::fetchDisassembler()'s three routes, tried in its order: mixed
    // source+assembly for the whole function, then a mixed range, then a plain
    // range. Only the mixed ones put source in the Disassembler view.
    void fetchDisassemblyPointMixed(quint64 requestId, quint64 address,
                                    const QString &functionName);
    void fetchDisassemblyRangeMixed(quint64 requestId, quint64 address);
    void fetchDisassemblyRangePlain(quint64 requestId, quint64 address);
    bool reportDisassemblyIfUsable(quint64 requestId, quint64 address,
                                   const QString &consoleStreamOutput);
    QChar mixedDisasmFlag() const;

    // Ported for real: fire-and-forget, no callback - mirrors GdbEngine::
    // assignValueInDebugger(), minus its handleVarAssign() callback (just
    // setTokenBarrier()+updateLocals(), both GenericDebuggerEngine-side
    // concerns - it already does the locals refresh itself right after
    // calling this, unconditionally, same simplification changeMemory()
    // already makes; see GenericDebuggerEngine::assignValueInDebugger()'s
    // comment).
    void assignValueInDebugger(const WatchItemData &item, const QString &expr,
                               const QString &value) final;
    // Ported for real: mirrors GdbEngine::setPeripheralRegisterValue().
    void setPeripheralRegisterValue(quint64 address, quint64 value) final;

    // Ported for real: mirrors GdbEngine::watchPoint().
    void watchPoint(quint64 requestId, const QPoint &pnt) final;

    // Ported for real: mirrors GdbEngine::createSnapshot()/handleMakeSnapshot().
    void createSnapshot(quint64 requestId) final;

    // Ported for real: just forwards the raw command over MI, same as
    // GdbEngine::executeDebuggerCommand().
    void executeDebuggerCommand(const QString &command,
                                const WatchItemData &inspectorItem) final;

    void insertBreakpointCommand(const BreakpointChangeRequest &request);
    void updateBreakpointCommand(const BreakpointChangeRequest &request);
    void handleWatchInsert(quint64 requestId, const DebuggerResponse &response);
    void handleInterpreterBreakpointInsert(quint64 requestId, const DebuggerResponse &response);
    void handleLocalAttach(const DebuggerResponse &response);
    void handleTerminalStubAttach(const DebuggerResponse &response, qint64 mainThreadId);
    void handleTargetRemote(const DebuggerResponse &response);
    void handleExtendedRemoteAttach(const DebuggerResponse &response);
    void continueAfterAttach();
    // AttachToTerminalStubData's Windows/gdb>=10 path only (QTCREATORBUG-26208 -
    // see GdbImpl::start()'s comment) - parses "show version"'s reply into
    // m_gdbVersion via the same free function GdbEngine::handleShowVersion()
    // uses. No other GdbImpl path needs a gdb version at all, so this is
    // the only place "show version" is ever sent.
    void handleShowVersion(const DebuggerResponse &response);

    // Shared by execute()'s Continue/StepOver/StepIn/StepOut/Return/RunToLine/
    // RunToFunction/JumpToLine cases: each is a "resume execution" MI command
    // using the same RunRequested-now/RunOk-or-RunFailed-on-response shape
    // (mirrors what GdbEngine::continueInferior()/executeStepIn() etc. do
    // directly, and handleAsyncOutput()'s "running" case does redundantly for
    // modern gdb - not replicated here, see the class comment). flags is only
    // ever DebuggerCommand::NativeCommand in practice, for RunToLine's bare
    // "continue" - every other caller's command string already contains a
    // dash or space, which runCommand() already treats as native.
    void runRunRequestCommand(const QString &function, int flags = 0);

    void fetchRegisterValues(quint64 requestId);
    void handleModulesList(quint64 requestId, const DebuggerResponse &response);
    void handleModuleSymbols(quint64 requestId, const Utils::FilePath &modulePath,
                             const Utils::FilePath &tempFilePath, const DebuggerResponse &response);
    void requestModuleSections(quint64 requestId, const Utils::FilePath &modulePath,
                               bool useLegacyAllObjKeyword);
    void handleModuleSections(quint64 requestId, const Utils::FilePath &modulePath,
                              const DebuggerResponse &response,
                              bool isRetryWithLegacyKeyword);

    // Mirrors GdbEngine::MemoryAgentCookie: accumulator/pendingRequests are
    // shared across every split retry of one accessMemory() call (see
    // fetchMemoryHelper()'s comment) - shared_ptr instead of GdbEngine's
    // manually new'd/deleted raw pointers, same ownership shape without the
    // manual bookkeeping. base/offset/length are per-cookie: base+offset is
    // the address this particular sub-request reads, length is how much.
    struct MemoryRequestCookie
    {
        std::shared_ptr<QByteArray> accumulator;
        std::shared_ptr<int> pendingRequests;
        quint64 requestId = 0;
        quint64 base = 0;
        quint64 offset = 0;
        quint64 length = 0;
    };
    void fetchMemoryHelper(const MemoryRequestCookie &cookie);
    void handleFetchMemory(const DebuggerResponse &response, const MemoryRequestCookie &cookie);

    // Mirrors GdbEngine::runCommand()'s NeedsTemporaryStop/NeedsFullStop
    // handling: defers a command needing a stopped target to m_onStopCommands
    // if the inferior is currently running (interrupting it first, unless an
    // interrupt - ours or execute(Interrupt)'s - is already pending), instead
    // of sending it straight to runCommandNow(). Replayed once the next
    // *stopped arrives (see handleOutputLine()'s comment). Unlike
    // NeedsTemporaryStop's real state()-based check, this tracks "is the
    // inferior running" itself (m_inferiorRunning), purely from MI responses/
    // async records already parsed here - this class has no visibility into
    // DebuggerEngine's state machine (see m_interruptRequested's comment).
    void runCommand(const DebuggerCommand &command);
    // Delivers an interrupt through whatever channel actually works for the
    // current inferiorStartData - OS-level signal for local run/attach
    // (interruptProcess()), the terminal-stub protocol for AttachToTerminal-
    // StubData, "-exec-interrupt" only for remote/extended-remote, where it's
    // the only channel available. Shared by execute(Interrupt) and
    // runCommand()'s own NeedsTemporaryStop/NeedsFullStop deferral - the
    // latter used to always send "-exec-interrupt" unconditionally, which
    // silently never arrives for a local run: with "set target-async off"
    // (see the start() call site - local runs need it off, not on, for
    // gdbbridge.py's internal gdb.execute('continue') to behave), gdb's MI
    // reader is fully blocked while the target runs, so nothing sent over
    // that channel gets processed until the target stops on its own.
    void requestInferiorInterrupt();
    // The actual "assign a token, wrap Python-bridge calls, write to the
    // process" logic - what runCommand() used to be before the deferral
    // check above was added. Called both by runCommand() once a command is
    // safe to send, and directly for things that must never be deferred
    // (the "-exec-interrupt" runCommand() itself sends to unblock a stopped
    // target).
    void runCommandNow(const DebuggerCommand &command);
    void handleOutputLine(const QString &line);
    void handleResultRecord(DebuggerResponse *response);

    GdbImplStartData m_startData;
    qint64 m_inferiorPid = -1;
    Utils::Process m_gdbProc;
    QString m_inbuffer;
    // Shared by "attach <pid>" (local), "target remote", and "target
    // extended-remote" + its own "attach <pid>" follow-up (see
    // handleLocalAttach()/handleTargetRemote()/handleExtendedRemoteAttach()):
    // gdb's own *stopped for the newly-connected target can arrive before
    // or after the setup command's own reply - whichever gets to
    // handleOutputLine()'s '*' case first decides the outcome.
    //
    //   Idle           - no attach/connect in progress.
    //   AwaitingConnect - setup command sent, no reply or *stopped seen yet.
    //   Stopped        - settled and stopped (reply and/or *stopped both
    //                    seen) - reused by continueAfterAttach() to also
    //                    mean "about to continue", see its comment.
    //   Continuing     - a "-exec-continue" is outstanding as part of
    //                    settling (extended-remote's attachPid sub-case
    //                    only - see continueAfterAttach()); a *stopped
    //                    arriving here is superseded by that continue's own
    //                    reply, not a new event.
    //
    // Whether a given attach should auto-continue once Stopped is decided
    // by re-checking m_inferiorStartData directly (AttachToRemoteServerData::
    // attachPid), not stored separately here.
    enum class AttachPhase { Idle, AwaitingConnect, Stopped, Continuing };
    AttachPhase m_attachPhase = AttachPhase::Idle;
    // Accumulates '~' console-stream text between result records. Python
    // bridge commands (fetchVariables, insertBreakpoint's fallback, ...)
    // return their actual payload by printing "result={token=\"N\",...}" to
    // the console stream rather than as normal MI result data (checked
    // against gdbbridge.py's reportResult() and GdbEngine::handleResponse()'s
    // "Python commands output their result as console stream data" handling) -
    // so it has to be scanned for that marker once the matching numeric
    // result record arrives.
    QString m_pendingConsoleStreamOutput;
    QStringDecoder m_outputDecoder{"UTF-8"};
    QHash<int, DebuggerCommand> m_commandForToken;
    // Set by execute(Interrupt), cleared by the next *stopped: distinguishes
    // an explicitly requested stop (StopOk) from a breakpoint hit or a step
    // completing on its own (SpontaneousStop) - mirrors the state() checks in
    // GdbEngine::updateStateForStop(), which GdbImpl can't replicate directly
    // since it has no visibility into DebuggerEngine's state machine. Also
    // set when runCommand() interrupts to run a deferred NeedsTemporaryStop/
    // NeedsFullStop command (see its comment) - that stop is just as much
    // "explicitly requested" as execute(Interrupt)'s.
    bool m_interruptRequested = false;
    // AttachToTerminalStubData only - mirrors GdbEngine::m_expectTerminalTrap:
    // the post-attach handshake's own stop (Windows: no reason at all;
    // non-Windows: the harmless SIGCONT the kickoff delivers) needs to be
    // swallowed rather than reported as a real stop - see
    // handleOutputLine()'s '*' case and handleTerminalStubAttach().
    bool m_expectTerminalTrap = false;
    // AttachToTerminalStubData's Windows path only - see handleShowVersion()'s
    // comment. Same major*10000+minor*100+patch shape as GdbEngine::m_gdbVersion
    // (matches what extractGdbVersion() fills in).
    int m_gdbVersion = 0;
    // Tracks whether the inferior is currently running, purely from MI
    // responses/async records already parsed here (true once a run/resume
    // command gets ResultRunning back, false on the next *stopped) - see
    // runCommand()'s comment on why this exists instead of the real state()
    // check.
    bool m_inferiorRunning = false;
    // Set synchronously the instant a run/resume command (any "-exec-run"
    // site, or runRunRequestCommand()) is dispatched - before its reply can
    // possibly have arrived - and cleared once that reply is known, either
    // way. m_inferiorRunning alone lags behind by a full round trip: a
    // caller that reacts to EngineSetupOk (itself emitted synchronously,
    // inline, from inside the same call that goes on to dispatch the
    // initial "-exec-run" right after - see start()'s "-file-exec-and-
    // symbols" callback) and immediately sends a plain runCommand() of its
    // own races that dispatch with m_inferiorRunning still false, so
    // runCommand()'s deferral check alone would let it through unguarded,
    // straight into gdb's now-fully-blocked (target-async off) MI channel -
    // confirmed by hand against passesInferiorEnvironmentDiffToDebugger(),
    // whose "show environment" command sent right after EngineSetupOk
    // otherwise never gets answered.
    bool m_runCommandPending = false;
    // Set instead of calling requestInferiorInterrupt() immediately when a
    // NeedsTemporaryStop/NeedsFullStop command arrives while only
    // m_runCommandPending (not yet m_inferiorRunning) is true. Confirmed by
    // hand against a real gdb: the raw SIGINT requestInferiorInterrupt()
    // sends for a local run reliably kills the inferior instead of pausing
    // it if sent before the pending run/continue command's own reply
    // confirms it's actually running (ResultRunning) - even though
    // m_inferiorPid may already be known by then ("=thread-group-started"
    // can arrive before that reply). Checked and cleared once
    // runRunRequestCommand()'s callback sees that reply.
    bool m_interruptOnceRunning = false;
    // Mirrors GdbEngine::m_onStop (a DebuggerCommandSequence): commands
    // deferred by runCommand() while the inferior is running, replayed once
    // stopped. wantContinue mirrors DebuggerCommandSequence::append()'s own
    // behavior exactly - the last deferred command's flag wins, not "was any
    // of them NeedsTemporaryStop".
    QList<DebuggerCommand> m_onStopCommands;
    bool m_onStopWantContinue = false;
    int m_lastToken = 0;

    // A python-bridge call (runCommandNow()'s isPythonCommand, wrapped as
    // "theDumper.<function>(...)") needs theDumper to already exist in
    // gdb's own python namespace, true only once loadDumpers' reply comes
    // back (see the constructor's runCommand({"loadDumpers", ...})). Until
    // then, such calls are buffered here and replayed in order instead of
    // being sent immediately - GenericDebuggerEngine::EngineSetupOk claims
    // pending breakpoints synchronously, before this class's own start()
    // has even queued the dumper-loading commands, so a QML/interpreter
    // breakpoint set before debugging starts would otherwise hit gdb's
    // "name 'theDumper' is not defined" for real, not just as a same-
    // instant timing nuance (confirmed with a raw MI wire probe sending
    // commands in that exact order).
    bool m_dumpersReady = false;
    QList<DebuggerCommand> m_bufferedDumperCommands;

    // Set by refresh(Locals) (forced passexceptions on, stashed before the
    // callback is added - see its comment); re-sent verbatim, uncalled-back,
    // by execute(RepeatLastCommand) - mirrors GdbEngine::m_lastDebuggableCommand/
    // debugLastCommand(): a debugging-the-debugger tool for the Log Window's
    // "repeat" button, not a general-purpose repeat-anything feature.
    DebuggerCommand m_lastDebuggableCommand;

    // Name/size/type are static for the session and fetched once via
    // "maintenance print register-groups", keyed by gdb's own internal
    // register number (not the same as its position in this map) - mirrors
    // GdbEngine::m_registers, minus the "groups" field (not used here, no
    // register-group filtering ported). Merged with -data-list-register-
    // values's numeric values on every RefreshKind::Registers request; see
    // fetchRegisterValues().
    struct RegisterInfo
    {
        QString name;
        int size = 0;
        QString reportedType;
    };
    QHash<int, RegisterInfo> m_registerInfoByNumber;
    bool m_registerNamesListed = false;

    void handleTracepointInsert(quint64 requestId, const DebuggerResponse &response,
                                const QString &message,
                                const QList<GdbImplTracepointCaptureData> &captures);
    void handleTracepointHit(const GdbMi &data);
    QHash<QString, GdbImplTracepointInfo> m_tracepointsByNumber;

    // Mirrors lldbbridge.py's own internalBreakpointIds (see
    // "LldbEngine: Track remaining internal breakpoints", 754817) -
    // RunToLine/RunToFunction/JumpToLine's own one-shot "tbreak"/
    // "-break-insert -t" breakpoints reach handleAsyncOutput()'s
    // "breakpoint-modified" (and tracepoint/interpreter equivalents) the
    // same way a real user-requested one does, and would otherwise emit a
    // spurious breakpointModified() for a breakpoint the caller never
    // asked for (confirmed live: gdb reports "=breakpoint-modified" with
    // times="1" for these once hit). Populated only from the three
    // internal call sites below - a user's own one-shot breakpoint
    // (BreakpointChangeRequest::oneShot) also produces a "Temporary
    // breakpoint N at ..." reply but must still be reported normally, so
    // this can't be a blanket text-pattern match; it has to be scoped to
    // the specific commands this class itself issues internally.
    void registerInternalBreakpointNumber(const QString &number);
    QSet<QString> m_internalBreakpointNumbers;
};

} // namespace Debugger::Internal
