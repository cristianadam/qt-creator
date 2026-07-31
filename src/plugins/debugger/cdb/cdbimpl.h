// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../debuggerengineinterface.h"

#include <coreplugin/icore.h>

#include <utils/filepath.h>
#include <utils/processinterface.h>
#include <utils/qtcprocess.h>

#include <functional>
#include <optional>

namespace Debugger::Internal {

// Fifth real DebuggerEngineInterface implementation - the first Windows-
// only one, wrapping real cdb.exe (Microsoft's console debugger) the same
// way GdbImpl/LldbImpl wrap gdb/lldb. Checked directly against real
// CdbEngine/cdbparsehelpers (cdb/cdbengine.{h,cpp}, cdb/cdbparsehelpers.{h,cpp})
// throughout, not assumed - cdb has no MI-like wire syntax at all, unlike
// gdb/lldb:
//  - A companion extension DLL (qtcreatorcdbext) is loaded via "-a<name>".
//    Its own real name is looked up via CdbEngine::extensionLibraryName()
//    (reused directly, not duplicated).
//  - Three command flavors, mirroring CdbEngine::CommandFlags exactly:
//    NoFlags (raw cdb command, no reply framing - used for g/t/p/gu),
//    BuiltinCommand (bracketed via ".echo \"<token>N<\""/"\"<token>N>\"" so
//    its free-format text output can be captured), ExtensionCommand
//    ("!qtcreatorcdbext.<fn> -t <token>.<chunk> <args>", replies in a
//    distinct out-of-band format:
//    "<qtcreatorcdbext>|<type>|<token>|<remainingChunks>|<what>|<message>").
//  - CdbEngine's fourth flavor, ScriptCommand (drives the newer Python
//    theDumper/cdbbridge.py bridge), is NOT ported this slice - baseline
//    Locals goes through the extension DLL's own older, fully native C++
//    pretty-printer instead (its "locals" ExtensionCommand, mirroring
//    CdbEngine::doUpdateLocals()'s own non-Python branch), which needs no
//    Python bridge at all.
//
// Initial slice only, mirroring every other backend's own first cut:
// process launch (Launch start mode only - no attach modes yet),
// execute() for Continue/Interrupt/StepIn/StepOver/StepOut/Abort,
// changeBreakpoint() for Insert/Remove/Update on BreakpointByFileAndLine/
// BreakpointByFunction - deliberately including the module-qualifier
// field (BreakpointParameters::module, prefixed "<module>!" exactly like
// real cdbAddBreakpointCommand() does) since BreakModuleCapability is the
// whole motivation for this backend existing at all - and refresh(Locals).
// Not yet ported: every other RefreshKind, attach/remote/core-file
// support, disassembly, memory access, watchpoints, tracepoints, reverse
// execution - see the redesign doc's Phase notes.
//
// Never run against a real cdb.exe session yet (no Windows access while
// drafting this) - every wire-format detail below is cross-checked
// against real CdbEngine/cdbparsehelpers source, not assumed, but this
// still needs real verification on the Windows VM before being trusted.
class DEBUGGER_EXPORT CdbImplStartData
{
public:
    Utils::ProcessRunData debuggerRunData;   // cdb.exe, extension args already added
    InferiorStartData inferiorStartData;     // Launch (ProcessRunData alternative) only this slice
    Utils::FilePath extensionDir;            // directory containing qtcreatorcdbext.dll
    QString extensionFileName;               // as returned by CdbEngine::extensionLibraryName()
    // share/qtcreator/debugger, for cdbbridge.py - only the test suite sets
    // one of its own.
    Utils::FilePath dumperScriptsDir = Core::ICore::resourcePath("debugger");
    bool nativeMixed = false;                // QML/C++ combined debugging requested
};

class DEBUGGER_EXPORT CdbImpl final : public DebuggerEngineInterface
{
    Q_OBJECT

public:
    explicit CdbImpl(const CdbImplStartData &startData);
    ~CdbImpl() override;

private:
    void start() final;
    void shutdownInferior(ShutdownMode mode) final;
    void shutdownEngine() final;

    void execute(const ExecutionRequest &request) final;
    void changeBreakpoint(const BreakpointChangeRequest &request) final;
    void refresh(const RefreshRequest &request) final;

    void activateFrame(int index) final;
    void selectThread(const QString &threadId) final;

    void setRegisterValue(const QString &name, const QString &value) final;
    void assignValueInDebugger(const WatchItemData &item, const QString &expr,
                               const QString &value) final;

    // Not ported this slice.
    void accessMemory(MemoryOp op, quint64 requestId, quint64 addr, quint64 lengthOrSize,
                      const QByteArray &data) final;
    void fetchDisassembly(quint64 requestId, quint64 address, const QString &functionName) final;
    void setPeripheralRegisterValue(quint64 address, quint64 value) final;
    void watchPoint(quint64 requestId, const QPoint &pnt) final;
    void createSnapshot(quint64 requestId) final;

    // Real: forwards the raw command via NoFlags, mirrors
    // CdbEngine::executeDebuggerCommand()'s own plain-command path.
    void executeDebuggerCommand(const QString &command,
                                const WatchItemData &inspectorItem) final;

    // Dispatches by first character/prefix, mirroring
    // CdbEngine::parseOutputLine()/handleExtensionMessage() exactly:
    // "<qtcreatorcdbext>|..." extension replies/notifications (accumulated
    // across chunks, dispatched once remainingChunks==0), and
    // "<token>N<"/"<token>N>" bracket lines delimiting a BuiltinCommand's
    // free-format output. Anything else (module-load lines, the cdb
    // banner, prompts) is only shown via message(), not parsed further
    // this slice.
    void handleCdbOutput(const QString &output);
    void handleExtensionMessage(char type, int token, const QString &what, const QString &message);
    // Reports a stop's location and the stop itself - deferred until after a
    // breakpoint's condition has been evaluated, when it has one.
    void reportStop(const GdbMi &stopData);

    // Issues one breakpoint's cdb commands under a given id; report=false for a
    // replay that re-creates a breakpoint the caller already knows about.
    void insertBreakpoint(quint64 requestId, const QString &id, int modelId,
                          const BreakpointParameters &params, bool report);
    // Logs the tracepoints that hit at one stop, expanding their captures, then
    // either resumes or reports the stop a plain breakpoint there is owed.
    void reportTracepoint(const QStringList &tracepointMessages, const GdbMi &stopData,
                          bool stopAfterwards);

    void runCommand(const DebuggerCommand &command);
    // The setup a fresh cdb session needs - see its definition.
    void initializeSession(const std::function<void()> &whenReady);
    // Sets a QML breakpoint through the dumper bridge's NativeQmlDebugger
    // service calls - see its definition.
    void insertInterpreterBreakpoint(quint64 requestId, int modelId,
                                     const BreakpointParameters &params, bool report);
    // Arms the internal hook breakpoint that resolves pending QML breakpoints.
    void armInterpreterHooks();
    // Brings up the extension's embedded Python and the cdbbridge.py dumper -
    // mirrors CdbEngine::setupScripting(). Sets m_pythonVersion.
    void setupScripting();
    // Runs work queued while the bridge was still coming up.
    void flushPendingBridgeWork();
    // Selects cdb's stepping granularity before a step - see
    // m_lastOperateByInstruction.
    void adjustOperateByInstruction(bool operateByInstruction);
    // See ExecutionCommand::JumpToLine.
    void jumpToAddress(quint64 address, const Utils::FilePath &file, int line);
    // The next unused cdb-side breakpoint id - see m_nextBreakpointId.
    QString nextBreakpointId();
    // A function breakpoint's target, once resolved - see
    // insertFunctionBreakpoint().
    struct ResolvedFunction
    {
        QString name;          // "multi<int>", spelled as cdb spells it
        QString qualifiedName; // "inferior_msvc!multi<int>"
        QString file;
        int line = 0;          // the first statement's line, past the prologue
        quint64 address = 0;   // and that line's own address
    };
    // Reads a function's file, first-statement line and its address out of
    // cdb's own "uf" (unassemble function) reply.
    static void parseFunctionDisassembly(const QString &reply, ResolvedFunction *function);
    // cdb cannot break on a function *name* the way gdb can, so the symbol is
    // resolved here first and the breakpoint set by address.
    void insertFunctionBreakpoint(quint64 requestId, const QString &id, bool enabled,
                                 const QString &module, const QString &functionName,
                                 bool report);
    void setResolvedFunctionBreakpoints(quint64 requestId, const QString &id, bool enabled,
                                        const QString &functionName,
                                        const QList<ResolvedFunction> &functions,
                                        bool report);
    // Emits an Insert's own reply, with the breakpoint's locations if it
    // resolved to more than one.
    void reportBreakpointInserted(quint64 requestId, const QString &id, bool enabled,
                                  const QString &file, int line, const QString &function,
                                  const GdbMi &locations, bool report);

    CdbImplStartData m_startData;
    Utils::Process m_cdbProc;
    QString m_inbuffer;

    QString m_extensionCommandPrefix = "!qtcreatorcdbext.";
    QString m_tokenPrefix = "<token>";
    int m_nextCommandToken = 0;
    QHash<int, DebuggerCommand> m_commandForToken;

    // Set while gathering a BuiltinCommand's bracketed free-format output -
    // mirrors CdbEngine::m_currentBuiltinResponseToken/m_currentBuiltinResponse
    // exactly (-1/empty when no BuiltinCommand is currently outstanding).
    int m_currentBuiltinResponseToken = -1;
    QString m_currentBuiltinResponse;

    // Mirrors CdbEngine::m_extensionMessageBuffer - accumulates a
    // multi-chunk extension message until remainingChunks reaches 0.
    QString m_extensionMessageBuffer;

    // Set once the "session_accessible" extension notification (registered
    // via "-c .idle_cmd <prefix>idle" at startup, fired whenever the
    // debuggee stops) has been seen for the first time - gates
    // EngineSetupOk/RunAndInferiorStopOk, mirroring CdbEngine's own
    // m_initialSessionIdleHandled.
    bool m_initialSessionIdleHandled = false;
    // Set right before dispatching Continue/StepIn/StepOver/StepOut,
    // cleared once the matching "session_accessible" notification arrives -
    // distinguishes a breakpoint interrupting a free-running Continue
    // (SpontaneousStop) from a step landing on its intended line (StopOk),
    // same reasoning as every other backend's own such flag (e.g.
    // PdbImpl::m_expectSpontaneousStop).
    bool m_expectSpontaneousStop = false;
    // Whether the debuggee is currently running, tracked purely from what has
    // been dispatched/received here (true once Continue or any step is sent,
    // false again on the next "session_idle"). Same purpose and reasoning as
    // GdbImpl's/PdbImpl's own flag of this name: an Interrupt requested while
    // the target is already stopped has to be answered directly, because cdb
    // will never send another "session_idle" for it.
    bool m_inferiorRunning = false;
    bool m_interruptRequested = false;
    bool m_inferiorExited = false;
    bool m_shuttingDown = false;

    // Whether cdb was last told to step by instruction ("l-t") or by source
    // line ("l+t") - mirrors CdbEngine::adjustOperateByInstruction()'s own
    // m_lastOperateByInstruction, including only sending the command when the
    // mode actually changes. std::optional so the very first step always sends
    // it: cdb's own default is *instruction* stepping, so without an explicit
    // "l+t" a source-level StepIn/StepOver just advances a few instructions
    // and stays on the same line (confirmed live).
    std::optional<bool> m_lastOperateByInstruction;

    // Every breakpoint's cdb-side id ("bu<id> ...") is generated here on
    // Insert, not supplied by the caller - mirrors breakPointCdbId()'s own
    // counter, including its id *spacing*: ids are cdbBreakPointStartId +
    // n * cdbBreakPointIdMinorPart, which reserves the numbers in between for
    // that breakpoint's own sub-locations (see nextBreakpointId() and the
    // ambiguous-symbol path in changeBreakpoint()). Remove/Update/EnableSub get
    // the id back via request.responseId instead (the caller's own persistent
    // handle, set from this class's own Insert reply - see
    // BreakpointChangeRequest's class comment) - no requestId -> id map needed.
    int m_nextBreakpointId = 1;

    // One outstanding requestId per RefreshKind, matching the same implicit
    // single-outstanding-request-per-kind assumption PdbImpl documents: the
    // reply is correlated by the command token, so these only exist so a
    // caller's own id can be handed back with it.
    quint64 m_pendingLocalsRequestId = 0;
    quint64 m_pendingStackRequestId = 0;
    quint64 m_pendingModulesRequestId = 0;

    // Hit count per cdb breakpoint id, counted from cdb's own "Breakpoint
    // <n> hit" lines - see handleCdbOutput(). Neither cdb nor the extension
    // reports a running total, and this interface's breakpointModified()
    // carries a "times" field, so it is tracked here.
    QHash<QString, int> m_breakpointHitCounts;

    // cdb ids of breakpoints this class inserted for its own purposes
    // (RunToLine/RunToFunction's one-shot target), never requested by a
    // caller - their hits must not surface as breakpointModified().
    QSet<QString> m_internalBreakpointIds;

    // Sub-location id -> the id of the breakpoint it belongs to, for the
    // locations an ambiguous insert resolved into (see changeBreakpoint()).
    // Only these ids exist on cdb's side in that case, so a hit has to be
    // attributed back to the breakpoint the caller actually asked for.
    QHash<QString, QString> m_parentForSubBreakpointId;

    // Condition per cdb breakpoint id, for the ones inserted with one. cdb has
    // no conditional breakpoint of its own (cdbparsehelpers.cpp says
    // "Condition currently unsupported" and means it), so the condition is
    // evaluated here at every stop instead - see handleExtensionMessage().
    QHash<QString, QString> m_conditionForBreakpointId;
    // Every breakpoint this class currently has in cdb, by its id: what a stop
    // consults to tell a tracepoint from a plain breakpoint, and what lets a
    // relaunched session get them back under the same ids.
    QHash<QString, BreakpointParameters> m_insertedBreakpoints;
    // Set between such a relaunch and the new session's first "session_idle".
    bool m_isResetRestart = false;
    // The frame refresh(Locals) reads, as last set by activateFrame() - cdb has
    // no current-frame state of its own the extension would follow.
    int m_currentFrameIndex = 0;
    // The last locals fetch, for execute(RepeatLastCommand) - mirrors
    // CdbEngine::m_lastDebuggableCommand.
    DebuggerCommand m_lastDebuggableCommand;
    // Set while a stop is waiting for its breakpoint's condition to be
    // evaluated, so the evaluation's own stop does not re-enter that path -
    // mirrors CdbEngine's own conditionalBreakPointTriggered flag.
    bool m_evaluatingCondition = false;
    // The same guard for a tracepoint's own capture evaluation.
    bool m_expandingTracepoint = false;
    // Python's own version inside the extension, in CdbEngine's own packed form
    // (major << 16 | minor << 8 | patch), or 0 when the extension was built
    // without Python at all - which is what gates every ScriptCommand, exactly
    // as CdbEngine::m_pythonVersion does.
    unsigned m_pythonVersion = 0;
    // cdb ids of the internal QML hook breakpoints, armed once per native mixed
    // session before the debuggee first runs - see armInterpreterHooks().
    QSet<QString> m_interpreterResolverIds;
    QSet<QString> m_interpreterMessageIds;
    // Work that needs theDumper, queued while the bridge is still coming up -
    // the bootstrap is asynchronous, and a caller inserting a QML breakpoint on
    // EngineSetupOk gets there first. Same reason GdbImpl gates commands on its
    // own m_dumpersReady. Flushed by setupScripting().
    QList<std::function<void()>> m_pendingBridgeWork;
};

} // namespace Debugger::Internal
