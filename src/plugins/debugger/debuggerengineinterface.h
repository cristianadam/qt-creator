// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "debugger_global.h"
#include "breakpoint.h"
#include "debuggerconstants.h"
#include "debuggerprotocol.h"
#include "disassemblerlines.h"

#include <utils/filepath.h>
#include <utils/processhandle.h>
#include <utils/processinterface.h>

#include <functional>
#include <variant>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QUrl>

namespace Utils { class ProcessResultData; }

// tests/auto/debugger/tst_backends.cpp - see the friend grant on
// DebuggerEngineInterface below for why.
class tst_backends;
// tests/auto/debugger/tst_pdbimpl.cpp - same reason as tst_backends above,
// for PdbImpl specifically (see that file's own class comment on why it's
// a separate suite rather than a Backend::Pdb row in tst_backends.cpp).
class tst_pdbimpl;
// tests/auto/debugger/tst_backends.cpp's DebuggerBackend - same reason as
// tst_backends above.
class DebuggerBackend;

namespace Debugger::Internal {

Q_NAMESPACE_EXPORT(DEBUGGER_EXPORT)

// Sketch only, not wired into DebuggerEngine yet.
// Modeled after Utils::ProcessInterface: a narrow, mostly pure-virtual port,
// with results/events reported back via signals rather than virtual getters.
//
// Contract for every command virtual below (execute/refresh/changeBreakpoint/
// accessMemory/shutdownInferior/shutdownEngine): implementations must only
// enqueue a request and return, never mutate fragile single-shot state
// synchronously. Checked against the real GdbEngine bodies these replace -
// interruptInferior()/shutdownInferior()/shutdownEngine() are already just
// runCommand(...) pushes - so a DebuggerEngine slot reacting to a signal is
// free to call straight back into any of these, including reentrantly,
// without special handling on either side.

enum class ExecutionCommand {
    Continue, Interrupt, StepOver, StepIn, StepOut, Return,
    RunToLine, RunToFunction, JumpToLine,
    Detach, ResetInferior, Abort, RecordReverse, RepeatLastCommand,
};

class DEBUGGER_EXPORT ExecutionRequest
{
public:
    ExecutionCommand command = ExecutionCommand::Continue;
    bool flag = false;              // byInstruction (Step*) / enabled (RecordReverse)
    ContextData context = {};       // RunToLine / JumpToLine
    QString functionName = {};      // RunToFunction
    // Continue/StepOver/StepIn/StepOut only, native-mixed sessions only -
    // mirrors DebuggerEngine::isNativeMixedActiveFrame() (current
    // StackFrame::language == QmlLanguage), a GUI model query the backend
    // can't make itself. Decides whether to dispatch to the interpreter
    // (QML-side stepping) or plain gdb commands.
    bool currentFrameIsQml = false;
};

enum class RefreshKind {
    Modules, ModuleSymbols, ModuleSections,
    Registers, PeripheralRegisters,
    SourceFiles, FullStack, StackSymbols, AllSymbols, QmlStack,
    DebuggingHelpers, Threads,
    // Not part of the original 66-method collapse (doUpdateLocals() has its
    // own non-pure virtual on DebuggerEngine, shared/non-virtual entry points
    // updateAll()/updateItem()/updateWatchData() call it directly) - folded in
    // here anyway since the shape (one command out, one structured response
    // back) is identical; see project_debugger_redesign_proposal.md.
    Locals,
    // Real GdbEngine::createFullBacktrace()/CdbEngine's own - every thread's
    // backtrace at once, for the "Create Full Backtrace" action. A RefreshKind
    // rather than a virtual of its own: the shape is the same "one command
    // out, one response back", so it needs no new interface method. The
    // response is the finished text in a single Const GdbMi, not a structure -
    // it only ever goes into a scratch editor. Only asked of a backend that
    // declares CreateFullBacktraceCapability.
    FullBacktrace,
    // The Inspector view's object tree - the QML "Inspect" mode real QmlEngine
    // gets from its own QmlInspectorAgent (a live QObject tree of the running
    // scene, not a stack frame's variables). Same shape as Locals: a "data"
    // list of watch items, demand-driven through RefreshRequest::
    // expandedINames, inserted by GenericDebuggerEngine with
    // WatchHandler::insertItems(). Two things are unlike every other kind
    // here, both mirroring the agent it replaces:
    //
    // - inames live under "inspect." rather than "local.", which is what makes
    //   WatchItem::isInspect() true and puts them in the Inspector view;
    // - a backend may report it *unprompted*, with no refresh() having asked
    //   (requestId 0): the tree changes when the scene does, so QmlImpl pushes
    //   a rebuild when a QML engine registers or an object is created, exactly
    //   as QmlInspectorAgent reacts to LIST_ENGINES_R/OBJECT_CREATED itself.
    //   Each item carries the object's own debug id in "id", which
    //   assignValueInDebugger()/executeDebuggerCommand() then evaluate against.
    InspectorTree,
};

class DEBUGGER_EXPORT RefreshRequest
{
public:
    quint64 requestId = 0;
    RefreshKind kind = RefreshKind::Modules;
    Utils::FilePath path = {};       // module name, when applicable
    QString partialVariable = {};   // Locals only - matches UpdateParameters::partialVariable
    // Locals only - the current frame's StackFrame::context (opaque QML/JS
    // interpreter frame id, empty for a plain C++ frame), forwarded to the
    // "context" dumper argument (see GdbEngine::doUpdateLocals()). Same
    // reasoning as addresses below: a GUI model query (stackHandler()->
    // currentFrame()) the backend can't make itself.
    QString context = {};
    // PeripheralRegisters only - GdbEngine::reloadPeripheralRegisters() reads
    // this from peripheralRegisterHandler()->activeRegisters() itself (a GUI
    // model query the backend can't make), so DebuggerEngine has to gather it
    // and pass it down, same additive-field reasoning as breakpointEvent()'s
    // GdbMi data parameter - not a redesign, see
    // project_debugger_redesign_proposal.md's "checked for a wall" section.
    QList<quint64> addresses = {};
    // Locals only - the exact "watchers" JSON array real GdbEngine/
    // LldbEngine attach to their own updateLocals() command via
    // WatchHandler::appendWatchersAndTooltipRequests() ({iname, hex-encoded
    // exp} objects - see WatchHandler::watcher()). GenericDebuggerEngine::
    // doUpdateLocals() extracts it from a throwaway DebuggerCommand rather
    // than this class depending on WatchHandler's command-building
    // internals directly. Only meaningful when AddWatcherCapability is
    // declared - passed straight through to the dumper bridge's own
    // "watchers" arg, which already knows how to turn each entry into a
    // top-level "watch.N" response item (see dumper.py's handleWatches()).
    QJsonArray watchers = {};
    // Locals and InspectorTree - the inames the Locals/watch/Inspector view
    // currently has expanded
    // (WatchHandler::expandedINames()). A backend whose wire protocol only
    // reports a container's *child count* up front, not its members, needs this
    // to know which ones are worth a second round trip: real QmlEngine gates
    // exactly that on WatchHandler::isExpandedIName() (see handleScope()), and
    // the dumper-based backends already receive the equivalent as their own
    // "expanded" argument (see WatchHandler's own command building). Empty means
    // "nothing expanded", so a backend may report every container collapsed.
    // Locals and InspectorTree - see RefreshKind::InspectorTree on how the
    // object tree uses the very same demand-driven gating one level at a time.
    QSet<QString> expandedINames = {};
    // Locals only - mirrors GdbEngine::doUpdateLocals()'s cmd.arg(
    // "allowinferiorcalls", s.allowInferiorCalls()): dumper.py's own
    // parseAndEvaluate() (used by handleWatches() above) silently refuses
    // to evaluate anything at all unless this is set (see its "breaks
    // symbol discovery" FIXME comment) - a real gdb-only gap found via
    // AddWatcherCapability's own dedicated test, not exercised by any
    // earlier one. GdbImpl is the only consumer: lldbbridge.py's own
    // nativeParseAndEvaluate() override never checks this at all, matching
    // real LldbEngine, which never sends the arg either.
    bool allowInferiorCalls = true;
    // Locals only - mirrors GdbEngine::doUpdateLocals()'s cmd.arg(
    // "autoderef", s.autoDerefPointers()). Before this field existed,
    // GdbImpl/LldbImpl both hardcoded the dumper's "autoderef" arg to true
    // unconditionally, so AutoDerefPointersCapability had no way to
    // actually be turned off through this interface at all - found via its
    // own dedicated test. Gates dumper.py's putDerefedPointer() (only
    // caller that sets the "autoderefcount" field the dedicated test
    // checks for).
    bool autoDerefPointers = true;
};

enum class BreakpointOp { Insert, Remove, Update, EnableSub };

enum class LibraryEvent { Loaded, Unloaded };

// Breakpoint/SubBreakpoint (QPointer<BreakpointItem>/QPointer<SubBreakpointItem>,
// both tree-model GUI items) are deliberately not passed: unlike WatchItem, the
// real overrides don't just read them once - e.g. GdbEngine::insertBreakpoint()
// captures bp into the async response callback and hands it to handleWatchInsert()
// later, so the model object is alive and touched again well after this call
// returns. Instead: requestId is a caller-supplied opaque token, echoed back via
// breakpointEvent() below when the response arrives; responseId/subResponseId are
// the engine-assigned handles (BreakpointItem::responseId()/SubBreakpointItem::
// responseId - plain QStrings) needed to address an already-inserted breakpoint
// for Remove/Update/EnableSub. DebuggerEngine keeps the requestId -> Breakpoint
// map and does the actual bp->notifyInsertionSucceeded()-style model mutation
// itself once breakpointEvent() fires; the backend never sees the model object.
class DEBUGGER_EXPORT BreakpointChangeRequest
{
public:
    BreakpointOp op = BreakpointOp::Insert;
    quint64 requestId = 0;
    BreakpointParameters params;    // Insert / Update / Remove (Remove only
                                     // needs isCppBreakpoint() off it - see
                                     // GdbImpl::changeBreakpoint())
    QString responseId;             // Remove / Update / EnableSub
    QString subResponseId;          // EnableSub only
    bool enabled = true;            // EnableSub only
    // Insert only, QML/JS breakpoints only (see BreakpointParameters::
    // isCppBreakpoint()) - a GUI model query the backend can't make itself
    // (same reasoning as PeripheralRegisters::addresses/RefreshRequest::
    // context), needed only because a QML breakpoint can still be pending
    // (the NativeQmlDebugger service not up yet) when this insert reply
    // arrives, so it has no responseId yet to be found by later - see
    // breakpointModified()'s comment.
    int modelId = 0;
};

enum class MemoryOp { Fetch, Change };

// shutdownInferior()'s parameter - the two concrete commands GdbImpl (and
// friends) actually send, named directly rather than a bare bool. Mirrors
// the Kill/Detach half of DebuggerCloseMode (debuggerengine.h) - not that
// whole enum, since KillAndExitMonitorAtClose has no equivalent here and
// runParameters() isn't available on this interface anyway (see
// GenericDebuggerEngine::shutdownInferior(), which is the one place that
// maps DebuggerCloseMode down to this).
enum class ShutdownMode { Kill, Detach };

class DEBUGGER_EXPORT AttachToProcessData
{
public:
    Utils::ProcessHandle pid;
};

// A local run with "Run in Terminal" enabled - mirrors GdbEngine::
// isTermEngine() (isLocalRunEngine() && usesTerminal()), a genuinely
// different mechanism from AttachToProcessData's "Attach to Running
// Application" despite both ending in an "attach <pid>": the target is
// spawned by a terminal-owning Utils::Process elsewhere (debuggerruncontrol.cpp's
// terminalRecipe(), outside this interface entirely) and held stopped until
// kicked off - see DebuggerEngineInterface::kickoffTerminalProcessRequested()/
// interruptTerminalRequested() below. pid/mainThreadId mirror
// DebuggerRunParameters::applicationPid()/applicationMainThreadId(), already
// resolved by the caller before the backend is even constructed - same
// "caller resolves it, backend just consumes a plain value" shape as
// AttachToProcessData::pid. executable: only needed for the Windows +
// gdb >= 10.0.0 QTCREATORBUG-26208 workaround (see GdbImpl::start()'s
// comment) - empty is fine everywhere else.
class DEBUGGER_EXPORT AttachToTerminalStubData
{
public:
    Utils::ProcessHandle pid;
    qint64 mainThreadId = -1;
    Utils::FilePath executable;
};

// channel: "host:port" (or similar), already resolved by whoever set up the
// actual connection (device port-forwarding, gdbserver, etc.) - mirrors
// DebuggerRunParameters::remoteChannel(), the same plain string GdbEngine
// hands straight to "target remote"/"target extended-remote" without ever
// knowing how it was established.
//
// Plain "target remote" (attachPid and remoteExecutable both empty): the
// remote side already has a single process running and stopped, waiting
// for gdb to connect (e.g. "gdbserver host:port ./program") - mirrors
// GdbEngine::callTargetRemote()'s plain branch.
//
// attachPid set: connects via "target extended-remote" instead, then
// attaches to that already-running remote pid (e.g. a "gdbserver --multi"
// session) - mirrors GdbEngine::handleTargetExtendedRemote()'s
// attachPid().isValid() branch.
//
// remoteExecutable set (attachPid empty): also "target extended-remote",
// but tells the remote side which executable to run via "-gdb-set remote
// exec-file" instead of attaching to an existing pid - mirrors the same
// function's other branch.
//
// useQnxTarget: connects via "target qnx" instead of "target (extended-)
// remote" - mirrors GdbEngine::callTargetRemote()'s m_isQnxGdb branch, a
// genuinely different MI command for QNX's pdebug agent, gated there on
// parsing gdb's own version string for a QNX marker. GdbImpl has no such
// version-query machinery (and no way to add and verify it - no QNX-
// flavored gdb build available), so this is caller-provided instead,
// consistent with every other fact on this struct.
class DEBUGGER_EXPORT AttachToRemoteServerData
{
public:
    QString channel;
    Utils::FilePath symbolFile;
    Utils::ProcessHandle attachPid;
    Utils::FilePath remoteExecutable;
    bool useQnxTarget = false;
};

// executable: unlike GdbEngine::setupInferior()'s isCoreEngine() branch,
// GdbImpl doesn't replicate CoreInfo::readExecutableNameFromCore()'s
// "infer the executable from the core file itself" fallback - the caller
// always supplies it directly, same reasoning as every other fact on
// these structs (no incidental complexity beyond what's actually needed).
class DEBUGGER_EXPORT AttachToCoreData
{
public:
    Utils::FilePath coreFile;
    Utils::FilePath executable;
};

// Mirrors DebuggerRunParameters::qmlServer() exactly - a plain host+port
// QUrl, already resolved by whoever set up the actual QML debug connection
// (same "caller resolves it, backend just consumes a plain value" reasoning
// as every other InferiorStartData alternative). AttachToRemoteServerData
// doesn't fit here: 4 of its 5 fields (symbolFile/attachPid/
// remoteExecutable/useQnxTarget) are gdb-"target-remote"-specific and don't
// apply to a QML debug connection at all.
class DEBUGGER_EXPORT AttachToQmlServerData
{
public:
    QUrl server;
};

// What a backend's constructor is given to start from - grows one
// alternative per attach/remote/core mode a backend implements (see
// project_debugger_redesign_proposal.md's phase plan); start() itself never
// changes, only this.
using InferiorStartData = std::variant<
    Utils::ProcessRunData,
    AttachToProcessData,
    AttachToTerminalStubData,
    AttachToRemoteServerData,
    AttachToCoreData,
    AttachToQmlServerData
>;

// One bit per InferiorStartData alternative above, same order - lets a
// backend declare, at setup-data time, which of them it actually
// implements (most backends implement one; Gdb/Lldb implement several).
// Mirrors DebuggerCapabilities' own "backend declares a fixed bitmask"
// shape (debuggerconstants.h), just for "can this backend start() like
// this at all" instead of "can this backend do X once running" - a
// distinct question DebuggerCapabilities has no field for (e.g. Pdb claims
// no attach-related capability either way, since none of them describe
// "supports attach" in the first place). Named distinctly from the
// existing (unrelated, finer-grained, non-flag) Debugger::DebuggerStartMode
// enum (debuggerconstants.h) - an enum class, unlike that one, so the two
// can never be confused for each other at a call site either. Follows Qt's
// own Alignment/AlignmentFlag naming convention (qnamespace.h): the enum
// holding the actual bit values is Flag-suffixed and singular,
// DebuggerStartModes (below) is the QFlags<DebuggerStartModeFlag> type.
enum class DebuggerStartModeFlag
{
    Launch               = 1 << 0, // Utils::ProcessRunData
    AttachToProcess      = 1 << 1, // AttachToProcessData
    AttachToTerminalStub = 1 << 2, // AttachToTerminalStubData
    AttachToRemoteServer = 1 << 3, // AttachToRemoteServerData
    AttachToCore         = 1 << 4, // AttachToCoreData
    AttachToQmlServer    = 1 << 5, // AttachToQmlServerData
};
Q_DECLARE_FLAGS(DebuggerStartModes, DebuggerStartModeFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(DebuggerStartModes)
Q_FLAG_NS(DebuggerStartModes)

// Plain data extracted from WatchItem (a Utils::TypedTreeItem-derived GUI/model
// class) - covers every field the current 6 engine reimplementations of
// assignValueInDebugger() actually read (id, iname, type, isLocal/Watcher/Inspect);
// nothing else is used anywhere.
class DEBUGGER_EXPORT WatchItemData
{
public:
    qint64 id = -1;
    QString iname;
    QString type;
    bool isLocal = false;
    bool isWatcher = false;
    bool isInspect = false;
};

// One value per real notify*() source method on DebuggerEngine today, kept
// 1:1 rather than collapsed further: notifyInferiorIll() and notifyEngineIll()
// have different bodies/consequences (checked directly - see
// project_debugger_redesign_proposal.md) and must stay distinguishable as
// InferiorIll/EngineIll, not one shared Ill value; EngineRunFailed and
// EngineSpontaneousShutdown were missing entirely.
//
// No "inferior done" value here on purpose - that's inferiorDone()
// below (exit, crash, or Detach, all three). shutdownInferior(Detach)
// still uses its own pre-existing ShutdownFinished.
enum class InferiorEvent {
    RunOk, RunFailed, RunRequested,
    StopOk, StopFailed, SpontaneousStop,
    InferiorIll, ShutdownFinished,
    EngineSetupOk, EngineSetupFailed, EngineRunFailed, EngineIll,
    EngineShutdownFinished, EngineSpontaneousShutdown,
    RunAndInferiorStopOk, RunAndInferiorRunOk, RunOkAndInferiorUnrunnable,
};

// Mirrors Utils::ProcessExitStatus's Normal/Crash shape, plus a third
// value it has no room for: still alive, just no longer traced
// (execute(ExecutionCommand::Detach)'s completion).
enum class InferiorExitStatus {
    Normal,
    Crash,
    Detached,
};

// inferiorDone()'s payload. Not Utils::ProcessResultData: its
// error/errorString describe a Utils::Process's own operational health,
// which doesn't apply - GdbImpl et al. only parse the real debugger's
// text reports, they don't own the inferior as a Utils::Process.
class DEBUGGER_EXPORT InferiorResultData
{
public:
    // Only meaningful when exitStatus == Normal; 0 otherwise.
    int exitCode = 0;
    InferiorExitStatus exitStatus = InferiorExitStatus::Normal;
};

// Was acceptsBreakpoint(const BreakpointParameters &bp) const - grepping
// every real override (Gdb, Lldb, Cdb, Uvsc, Pdb, Dap + its 4 subclasses,
// Qml), including what isCppBreakpoint()/isQmlFileAndLineBreakpoint() (see
// below) touch internally, shows exactly type/fileName are ever read - none
// of BreakpointParameters' other fields (condition/ignoreCount/address/
// expression/threadSpec/functionName/module/command/message/tracepoint/
// oneShot/pending/hitCount/textPosition/pathUsage/enabled/size/bitpos/
// bitsize) are touched by any of them. So this holds just those two
// fields, not the full (much heavier) BreakpointParameters. startMode/
// isNativeMixedEnabled are passed as plain values rather than a const
// DebuggerRunParameters &: that type lives in debuggerengine.h, which pulls
// in Core/ProjectExplorer/TextEditor - exactly the dependency
// dumperScriptsDir's constructor comment (and locationChanged()'s Location,
// below) already avoids for GdbImpl - and no real override needs anything
// else off it anyway. All four bundled into one struct rather than passed
// as separate function parameters so same-typed-ish values (two enums, a
// bool) can't be silently swapped at a call site.
class DEBUGGER_EXPORT AcceptsBreakpointQuery
{
public:
    // Copied from BreakpointParameters' own methods of the same name (not
    // reused/forwarded) - see the class comment above on why only
    // type/fileName are needed here at all. Defined in
    // debuggerengineinterface.cpp.
    bool isCppBreakpoint() const;
    bool isQmlFileAndLineBreakpoint() const;

    BreakpointType type = UnknownBreakpointType;
    Utils::FilePath fileName;
    DebuggerStartMode startMode = NoStartMode;
    bool isNativeMixedEnabled = false;
};

// Static, per-backend-type facts, known at construction time rather than
// computed per call - mirrors Utils::ProcessInterface's protected
// ProcessSetupData m_setup, except these fields are fixed by the backend
// implementation itself (like a capability bitmask), not configured
// externally by whoever is about to start it. Passed into the constructor
// instead of a mutable protected member since nothing needs to change it
// after construction, unlike ProcessSetupData's working directory/
// environment/etc.
class DEBUGGER_EXPORT DebuggerEngineSetupData
{
public:
    // Listed first: unlike capabilities/toolTipHandling below, every real
    // backend needs its own real answer here (grepping every acceptsBreakpoint()
    // override shows none of them would behave sensibly falling back to
    // "accepts everything" - see the field's own comment on the {} default,
    // which exists for GenericDebuggerEngine's sake, not as a value a
    // backend should actually rely on leaving unset). Was the last pure
    // query on the interface (see the removed "Pure queries" comment block
    // in DebuggerEngineInterface) - unlike capabilities/toolTipHandling,
    // every real override genuinely depends on its call-time argument (the
    // breakpoint being considered), so a fixed field can't replace it - but
    // it's still a static, hardcoded-per-backend function of that argument,
    // never anything requiring a fresh round-trip, so it fits the same
    // "known at construction time" shape. Empty ({}) means "accepts
    // everything", matching DapEngine's own base-class default -
    // GenericDebuggerEngine falls back to that when a backend doesn't set
    // this.
    std::function<bool(const AcceptsBreakpointQuery &)> acceptsBreakpoint;
    // Was hasCapability(unsigned cap) const - grepping every real override
    // (Gdb, Lldb, Cdb, Uvsc, Pdb, Dap, Qml) shows the bitmask is always a
    // fixed, hardcoded value, never computed from anything - so a stored
    // field replaces the query. Applies to every start mode except
    // AttachToCore, which reads the field below instead.
    unsigned capabilities = 0;
    // A full replacement of (not a subset of, and not filtered against)
    // capabilities above when the session's start mode is AttachToCore -
    // real GdbEngine drops most capabilities there, having no live process to
    // jump/continue/write in, and a capability could equally be relevant only
    // post-mortem. Not a subset on purpose: nothing here has to also appear
    // in capabilities above. A backend that doesn't support AttachToCore at
    // all leaves this 0 (PdbImpl/CdbImpl/QmlImpl).
    unsigned attachToCoreCapabilities = 0;
    // See Debugger::DebuggerExtraCapability's own comment.
    DebuggerExtraCapabilities extraCapabilities;
    // Which InferiorStartData alternative(s) start() actually knows how to
    // handle for this backend - an OR of DebuggerStartModeFlag values (a
    // QFlags, unlike capabilities above, so DebuggerStartModeFlag stays an
    // enum class without needing manual casts at every OR site). Default-
    // constructed to empty, which would mean "can never be started at
    // all" - no real backend leaves this unset.
    DebuggerStartModes startModes;
    ToolTipHandling toolTipHandling = ToolTipHandling::IfStoppedInferiorAndCppEditor;
};

class DEBUGGER_EXPORT DebuggerEngineInterface : public QObject
{
    Q_OBJECT

public:
    const DebuggerEngineSetupData &setupData() const { return m_setupData; }
    // Not a DebuggerEngine::hasCapability() override - takes the session's
    // start mode, which that one reads off runParameters() itself. Same
    // (old) DebuggerStartMode enum AcceptsBreakpointQuery::startMode already
    // carries, so no mapping is needed at either call site. Only AttachToCore
    // ever changes the answer (see attachToCoreCapabilities), so the default
    // stands for "any live-process session", not for a particular mode.
    bool hasCapability(unsigned cap, DebuggerStartMode startMode = NoStartMode) const;
    // Not a DebuggerEngine::hasCapability() override.
    bool hasExtraCapability(DebuggerExtraCapability cap) const;

signals:
    // Outbound channel - replaces the ~25-method notify* family plus
    // showMessage()/showStatusMessage() call sites found in the current engines.
    void message(const QString &text, int channel, int timeout = -1);

    void inferiorEvent(InferiorEvent event);

    // data carries the raw response tree for Insert (checked against real
    // GdbEngine::handleBreakInsert1(): a single -break-insert response can
    // contain several "bkpt" entries - one main breakpoint plus N
    // sub-breakpoints, e.g. for a templated function resolving to several
    // addresses - so a plain bool isn't enough). Empty for Remove/Update/
    // EnableSub, which don't produce anything beyond success/failure.
    //
    // requestId 0 means the debugger did this by itself, with no request behind
    // it: a breakpoint from a .gdbinit, or a "break"/"delete" typed into the
    // Debugger Console. Only Insert/Remove occur that way, ok is always true,
    // and data carries the "bkpt" tuple for Insert or just "number" for Remove -
    // the engine adopts or drops it (BreakHandler::handleAlienBreakpoint()).
    // Same "0 means nobody asked" convention refreshDataReceived() uses. A
    // backend must not report its own internal one-shot breakpoints this way
    // (see GdbImpl::m_internalBreakpointNumbers).
    void breakpointEvent(quint64 requestId, BreakpointOp op, bool ok, const GdbMi &data = {});

    // Plain FilePath/line pair rather than the Location value class it ends
    // up wrapped in: Location lives in debuggerengine.h, which pulls in
    // Core/ProjectExplorer/TextEditor - exactly the dependency dumperScriptsDir's
    // constructor comment already avoids for GdbImpl. GenericDebuggerEngine
    // already needs debuggerengine.h anyway (it's the base class), so it
    // builds the Location itself.
    void locationChanged(const Utils::FilePath &fileName, int lineNumber);

    // The interface's interaction with the inferior is over - a real
    // exit, a crash, or execute(ExecutionCommand::Detach), all funneled
    // through the same signal (see InferiorExitStatus). Always emitted
    // exactly once for any of those - GenericDebuggerEngine only calls
    // notifyExitCode() when exitStatus != Detached, then always calls
    // notifyInferiorExited().
    void inferiorDone(const InferiorResultData &resultData);

    void inferiorPidKnown(const Utils::ProcessHandle &pid);

    // The one notify* case that does carry structured data -
    // notifyDebuggerProcessFinished(ProcessResultData) - gets its own signal
    // instead of a QVariant payload; ProcessResultData is already a plain
    // existing type (exitCode/exitStatus/error/errorString).
    void engineProcessFinished(const Utils::ProcessResultData &resultData);

    // requestId is whatever accessMemory() was called with; a single request can
    // yield several chunks over time (see MemoryAgentCookie chunking in GdbEngine
    // today), so this may fire more than once per request.
    void memoryDataReceived(quint64 requestId, quint64 address, const QByteArray &data);

    // Same requestId/token pattern as memoryDataReceived(), for the same reason
    // (see fetchDisassembly() below).
    void disassemblyReceived(quint64 requestId, const DisassemblerLines &lines);

    // Response to createSnapshot(): ok mirrors real GdbEngine::
    // handleMakeSnapshot()'s ResultDone check; coreFile is only meaningful
    // when ok is true.
    void snapshotCreated(quint64 requestId, bool ok, const Utils::FilePath &coreFile);

    // Response to watchPoint(): address is 0 if no widget was found at pnt,
    // matching real GdbEngine/LldbEngine's own "selected" check; expr is
    // still a well-formed "(QWidget*)0x..." string either way.
    void watchPointResolved(quint64 requestId, quint64 address, const QString &expr);

    // Response to refresh(): data is the raw response tree (checked against
    // real GdbEngine::handleFetchVariables(): it hands response.data straight
    // to DebuggerEngine::updateLocalsView(), which is already non-virtual and
    // shared - not GdbEngine-specific - so DebuggerEngine can consume this
    // directly without the backend needing to know about WatchHandler at all).
    void refreshDataReceived(quint64 requestId, RefreshKind kind, const GdbMi &data);

    void libraryEvent(LibraryEvent event, const GdbMi &data);

    // A list of "bkpt"-shaped tuples (number/file/line/... - same shape
    // applyBkptData() already consumes), spontaneous updates to already-
    // inserted breakpoints (tracepoint modification, or an interpreter
    // breakpoint that was pending at insert time and just got a real number
    // - see GenericDebuggerEngine::handleBreakpointModified()'s modelId
    // fallback for that latter case, keyed off BreakpointChangeRequest::
    // modelId echoed back in each tuple).
    void breakpointModified(const GdbMi &data);

    // A spontaneous signal/exception stop - name empty for an exception
    // (meaning carries the description). Filtering/dialog decisions live
    // in GenericDebuggerEngine, not the backend.
    void signalReceived(const QString &name, const QString &meaning);

    // AttachToTerminalStubData sessions only - asks whoever owns the actual
    // terminal-spawned process (a plain Utils::Process elsewhere, see
    // AttachToTerminalStubData's comment) to interrupt it/send it its
    // initial resume signal, since this backend has no handle on that
    // process itself. Mirrors DebuggerEngine::interruptTerminalRequested()/
    // kickoffTerminalProcessRequested(), which GenericDebuggerEngine forwards
    // these straight into.
    void interruptTerminalRequested();
    void kickoffTerminalProcessRequested();

protected:
    // No QObject parent: ownership is via the unique_ptr GenericDebuggerEngine
    // holds it in, not Qt parent/child. setupData is stored, not copied
    // per-call - see DebuggerEngineSetupData's own comment.
    explicit DebuggerEngineInterface(const DebuggerEngineSetupData &setupData)
        : m_setupData(setupData)
    {}

private:
    // Lifecycle - three genuinely distinct phases. Name collides with
    // Utils::Process::start() (called from within this) - unrelated otherwise.
    virtual void start() = 0;

    // mode: real GdbEngine::shutdownInferior() picks "detach" over "kill"
    // based on runParameters().closeMode() == DetachAtClose - not available
    // here (no DebuggerRunParameters - see the class comment), so
    // GenericDebuggerEngine computes it and passes the plain ShutdownMode
    // down, same additive-parameter reasoning as AcceptsBreakpointQuery.
    //
    // Every backend's own destructor should call shutdownInferior(Kill)
    // (or otherwise ensure the inferior actually dies) even if a caller
    // never did: a debuggee whose tracer (this backend's own gdb/lldb/etc.
    // process) dies uncleanly doesn't die with it - ptrace auto-detaches
    // AND RESUMES a tracee when its tracer exits, so a caller that
    // destroys the interface without a clean shutdownInferior()/
    // shutdownEngine() sequence first (e.g. an abrupt "Abort Debugging")
    // would otherwise leave the debuggee running as an orphaned process
    // indefinitely. Calling it from the backend's OWN destructor (e.g.
    // GdbImpl::~GdbImpl()) is safe - nothing has been torn down yet at
    // that point, unlike a hypothetical call from this base's own
    // destructor, which could never reach the override at all by then.
    virtual void shutdownInferior(ShutdownMode mode) = 0;

    virtual void shutdownEngine() = 0;

    // No pure queries left here anymore - hasCapability()/canHandleToolTip()/
    // acceptsBreakpoint() all used to sit in this group (no wire I/O, unlike
    // everything below, which is why the group existed at all - a candidate
    // to split into its own small interface, once floated here). All three
    // turned out to only ever read a fixed, hardcoded-per-backend value or
    // function of their call-time argument, never anything requiring a fresh
    // round-trip - so all three became DebuggerEngineSetupData fields
    // instead of virtuals; see its own comment, and AcceptsBreakpointQuery's.
    // canHandleToolTip() specifically also only needed state() - already
    // available on DebuggerEngine itself, nothing backend-specific - so it
    // was never actually wired up here in the first place (GenericDebuggerEngine
    // had no override forwarding to it, unlike hasCapability/acceptsBreakpoint);
    // collapsed into DebuggerEngine::canHandleToolTip()'s ToolTipHandling enum
    // instead, see debuggerconstants.h.

    // Collapsed command dispatch - was 14 separate virtuals.
    virtual void execute(const ExecutionRequest &request) = 0;

    // Collapsed refresh dispatch - was 11 separate virtuals.
    virtual void refresh(const RefreshRequest &request) = 0;

    // Collapsed breakpoint CRUD - was 4 separate virtuals.
    virtual void changeBreakpoint(const BreakpointChangeRequest &request) = 0;

    // Collapsed memory access - was 2 separate virtuals. MemoryAgent (a QObject-
    // derived GUI class wrapping a bin-editor view) is not passed at all: grepping
    // every real fetchMemory()/changeMemory() override (Gdb, Lldb, Cdb, Uvsc) shows
    // the only thing ever called on it is agent->addData(address, data) - so the
    // caller-supplied requestId is echoed back via memoryDataReceived() instead,
    // and DebuggerEngine maps requestId -> MemoryAgent* on its own side.
    virtual void accessMemory(MemoryOp op, quint64 requestId,
                              quint64 addr, quint64 lengthOrSize, const QByteArray &data = {}) = 0;

    // Escape hatches - irreducible signatures, kept as-is.
    // Thread is a QPointer<ThreadItem> tree-model GUI item; grepping all 7
    // reimplementations shows only thread->id() is ever read (Dap also calls
    // threadsHandler()->setCurrentThread(thread), but that's DebuggerEngine's
    // own bookkeeping on the translation side, not something the backend needs).
    virtual void selectThread(const QString &threadId) = 0;

    virtual void activateFrame(int index) = 0;

    // DisassemblerAgent is a QObject-derived GUI class; grepping every real
    // fetchDisassembler() override (Gdb, Lldb, Cdb, Uvsc) shows only
    // agent->location().address()/functionName() are ever read (agent->address()
    // is just a shortcut for the same location().address()), and the only sink
    // call is agent->setContents(DisassemblerLines) - already a plain data type,
    // so it travels back as-is via disassemblyReceived() instead.
    virtual void fetchDisassembly(quint64 requestId, quint64 address, const QString &functionName) = 0;

    // inspectorItem is the Inspector view's currently selected object, or a
    // default-constructed one (isInspect false, id -1) when nothing there is
    // selected. GUI state the backend cannot query itself - real
    // QmlEngine::executeDebuggerCommand() reads it straight off
    // inspectorView()->currentIndex() - so GenericDebuggerEngine gathers it and
    // passes it down, the same additive reasoning as RefreshRequest::context/
    // addresses. A whole WatchItemData rather than the bare debug id, matching
    // assignValueInDebugger() below: what "id" means is the backend's own
    // business (QmlImpl puts the QML debug id there, see its inspectItem()),
    // so the backend decides, off isInspect, whether the item is even its own.
    // Only ever set while the inferior *runs*, and only QmlImpl reads it: with
    // a stopped inferior every backend evaluates in the current frame instead.
    virtual void executeDebuggerCommand(const QString &command,
                                        const WatchItemData &inspectorItem) = 0;

    // value was QVariant, but every real override (Gdb, Lldb, Cdb, Pdb, Uvsc)
    // calls value.toString() immediately, and QmlEngine even asserts
    // editValue.typeId() == QMetaType::QString before doing the same - so the
    // wire-level type is already always a QString in practice.
    virtual void assignValueInDebugger(const WatchItemData &item, const QString &expr, const QString &value) = 0;

    virtual void setRegisterValue(const QString &name, const QString &value) = 0;

    virtual void setPeripheralRegisterValue(quint64 address, quint64 value) = 0;

    // Not supported by PdbImpl - see the class comment. Response comes back
    // via watchPointResolved().
    virtual void watchPoint(quint64 requestId, const QPoint &pnt) = 0;

    // Mirrors real GdbEngine::createSnapshot() (runs "gcore", then reports
    // the resulting core file back via snapshotCreated()) - not supported
    // by LldbEngine/PdbEngine either (LldbEngine's own SnapshotCapability
    // check is commented out; PdbEngine never declares it at all), so only
    // GdbImpl needs a real body here.
    virtual void createSnapshot(quint64 requestId) = 0;

    friend class DebuggerEngine;
    // Temporary, explicit exception for the migration's "alternative path"
    // step: GenericDebuggerEngine is a DebuggerEngine subclass, but C++
    // friendship isn't inherited, so it needs its own grant to call these.
    // Expected to go away once DebuggerEngine itself no longer needs
    // subclassing (see migration steps 4-5 in
    // project_debugger_redesign_proposal.md).
    friend class GenericDebuggerEngine;
    // tests/auto/debugger/tst_backends.cpp drives backend implementations
    // (GdbImpl, and future ones) directly against a real gdb process,
    // deliberately bypassing GenericDebuggerEngine (see
    // project_debugger_redesign_proposal.md's testing strategy) - it needs
    // the same private access GenericDebuggerEngine has, for the same
    // reason (calling start()/changeBreakpoint()/execute()/etc.
    // itself instead of through a DebuggerEngine).
    friend class ::tst_backends;
    friend class ::tst_pdbimpl;
    // DebuggerBackend (tst_backends.cpp) wraps a backend instance and
    // forwards a few calls (e.g. execute()) on tst_backends' behalf.
    friend class ::DebuggerBackend;

    DebuggerEngineSetupData m_setupData;
};

} // namespace Debugger::Internal
