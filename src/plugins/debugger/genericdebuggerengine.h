// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "debuggerengine.h"
#include "debuggerengineinterface.h"

#include <memory>

#include <QHash>
#include <QMultiMap>
#include <QPointer>

namespace Debugger::Internal {

// General-purpose DebuggerEngine that holds a DebuggerEngineInterface by
// composition instead of implementing the old 66-method surface's protocol
// logic itself - the shape DebuggerEngine is meant to collapse into once
// every backend has its own DebuggerEngineInterface implementation (see
// project_debugger_redesign_proposal.md). Deliberately backend-agnostic:
// nothing here names GdbImpl or any other concrete implementation - the
// concrete DebuggerEngineInterface is built by the caller (which already has
// the DebuggerRunParameters a backend needs - see createGdbEngine()) and
// handed to the constructor already-constructed; m_backend is never
// downcast. Today only GdbImpl is wired up (see gdb/gdbengine.cpp's
// createGdbEngine()); this class itself has no gdb-specific knowledge at all.
//
// Only overrides what a DebuggerEngineInterface actually exposes -
// insertBreakpoint/removeBreakpoint/updateBreakpoint/selectThread are pure
// virtuals on DebuggerEngine so they must be overridden. enableSubBreakpoint()
// isn't pure (the base default just QTC_CHECK(false)s), but is overridden
// anyway - GdbImpl::changeBreakpoint()'s EnableSub case is real, so leaving
// the base default in place would silently drop sub-breakpoint (one location
// of a multi-location breakpoint) enable/disable requests. Breakpoint insert/
// remove/update/enableSub keep a requestId -> Breakpoint map and, on
// breakpointEvent(), apply the response data themselves (mirroring
// GdbEngine::handleBkpt(), which needs nothing GdbEngine-specific - see
// applyBkptData()) since the backend never sees the Breakpoint object. doUpdateLocals() forwards to
// refresh(Locals) and feeds the response straight to the already-shared,
// non-virtual DebuggerEngine::updateLocalsView(). activateFrame()/
// reloadFullStack() mirror GdbEngine's stack-navigation logic, which turned
// out to need nothing GdbEngine-specific either - only stackHandler()/
// gotoCurrentLocation(), both already shared/non-virtual on DebuggerEngine -
// so selectThread() reuses reloadFullStack() rather than replicating
// GdbEngine's private, capped-depth reloadStack() helper (RefreshKind only
// exposes the uncapped "full" kind; see reloadFullStack() below).
// activateFrame() reads frame.level/frame.language off the already-shared
// StackFrame itself to decide whether/how to tell the backend which frame
// is active - QML frames (spliced in by loadAdditionalQmlStack() below)
// aren't real backend stack frames, so nothing is sent to the backend for
// those, same as GdbEngine's own activateFrame(). loadSymbolsForStack()
// does its frame/module matching here too (stackHandler()/modulesHandler(),
// same reasoning) and only tells the backend which one module path to load
// symbols for, via RefreshKind::StackSymbols - see its comment in
// gdbimpl.cpp.
// reloadRegisters() is the same shape again, feeding registerHandler()
// from a GdbMi tree GdbImpl builds itself (see GdbImpl::fetchRegisterValues()'s
// comment on why - gdb has no single command producing name+value together).
// fetchMemory()/changeMemory()/fetchDisassembler() keep a requestId -> agent
// map, the same pattern breakpoints use, since accessMemory()/
// fetchDisassembly() deliberately don't pass MemoryAgent/DisassemblerAgent
// through (no QObject-derived GUI types in the interface - see
// debuggerengineinterface.h). hasCapability() reads m_backend->setupData()
// .capabilities directly rather than forwarding the call, and the
// constructor pulls .toolTipHandling from the same struct into
// setToolTipHandling() - both are fixed-per-backend facts, not per-call
// queries; see DebuggerEngineSetupData's own comment. acceptsBreakpoint() is the same
// shape, minus the "no per-call queries" part - it reads .acceptsBreakpoint
// off the same struct, but that field is itself a function of the
// breakpoint being considered, so it's still called per query, just via a
// stored std::function instead of a virtual - builds the AcceptsBreakpointQuery
// argument from runParameters().startMode()/isNativeMixedEnabled() (both
// already available here, unlike on the backend) and falls back to true
// (matches DapEngine's own default) when a backend leaves the field unset.
// setupEngine() also builds a
// Location from locationChanged()'s plain FilePath/line pair and calls the
// already-shared gotoLocation() itself, gated on operatesByInstruction() -
// that state lives here, not on the backend, and Location's own class lives
// in this file's own header chain already, unlike GdbImpl's (see
// locationChanged()'s comment in debuggerengineinterface.h).
// executeRunToLine/executeRunToFunction/executeJumpToLine/
// executeRecordReverse/debugLastCommand/setPeripheralRegisterValue/
// assignValueInDebugger aren't pure either (same empty-default shape as
// enableSubBreakpoint above), but are overridden for the same reason:
// GdbImpl has real bodies for all of them now, so leaving the base defaults
// in place would silently drop them. assignValueInDebugger() converts the
// WatchItem it's given into a plain WatchItemData (id/iname/type/isLocal/
// isWatcher/isInspect - everything every real override actually reads) and
// calls updateLocals() itself right after, unconditionally, same
// simplification changeMemory() already makes (see its comment) - skips
// GdbEngine::handleVarAssign()'s response-gated version. Everything else
// not overridden here falls back to DebuggerEngine's empty defaults, same
// as any engine that hasn't implemented a given method would.
class GenericDebuggerEngine final : public DebuggerEngine
{
public:
    // Takes a raw, newly-owned pointer rather than a unique_ptr: the caller
    // (createGdbEngine() et al.) shouldn't need to name
    // std::unique_ptr<DebuggerEngineInterface> just to hand it over - same
    // reasoning as createGdbEngine() itself returning a raw DebuggerEngine*.
    // Built by the caller, not by a factory invoked later: the caller
    // already has the DebuggerRunParameters a backend needs to construct
    // (see createGdbEngine()) - deferring construction into setupEngine()
    // used to mean hasCapability()/acceptsBreakpoint() could be (and, via
    // checkBreakpoints(), routinely are) called before m_backend existed at
    // all, silently rejecting every breakpoint.
    explicit GenericDebuggerEngine(const QString &debuggerTypeName, DebuggerEngineInterface *backend);

private:
    void setupEngine() final;
    void shutdownInferior() final;
    void shutdownEngine() final;

    bool hasCapability(unsigned cap) const final;
    bool acceptsBreakpoint(const BreakpointParameters &bp) const final;
    void insertBreakpoint(const Breakpoint &bp) final;
    void removeBreakpoint(const Breakpoint &bp) final;
    void updateBreakpoint(const Breakpoint &bp) final;
    void enableSubBreakpoint(const SubBreakpoint &sbp, bool enabled) final;
    void selectThread(const Thread &thread) final;
    void activateFrame(int index) final;
    void reloadModules() final;
    void requestModuleSymbols(const Utils::FilePath &moduleName) final;
    void requestModuleSections(const Utils::FilePath &moduleName) final;
    void reloadFullStack() final;
    void loadAdditionalQmlStack() final;
    void loadSymbolsForStack() final;
    void loadSymbols(const Utils::FilePath &moduleName) final;
    void reloadRegisters() final;
    void reloadPeripheralRegisters() final;
    void reloadSourceFiles() final;
    void loadAllSymbols() final;
    void reloadDebuggingHelpers() final;
    void setRegisterValue(const QString &name, const QString &value) final;
    void setPeripheralRegisterValue(quint64 address, quint64 value) final;
    void assignValueInDebugger(WatchItem *item, const QString &expression,
                               const QVariant &value) final;
    void fetchMemory(MemoryAgent *agent, quint64 addr, quint64 length) final;
    void changeMemory(MemoryAgent *agent, quint64 addr, const QByteArray &data) final;
    void fetchDisassembler(DisassemblerAgent *agent) final;
    void watchPoint(const QPoint &pnt) final;
    void createSnapshot() final;

    void continueInferior() final;
    void interruptInferior() final;
    void executeStepOver(bool byInstruction) final;
    void executeStepIn(bool byInstruction) final;
    void executeStepOut() final;
    void executeReturn() final;
    void executeRunToLine(const ContextData &data) final;
    void executeRunToFunction(const QString &functionName) final;
    void executeJumpToLine(const ContextData &data) final;
    void executeRecordReverse(bool record) final;
    void debugLastCommand() final;
    void detachDebugger() final;
    void resetInferior() final;
    void abortDebuggerProcess() final;
    void executeDebuggerCommand(const QString &command) final;
    void doUpdateLocals(const UpdateParameters &params) final;
    // Unlike every other override here this one still delegates to
    // DebuggerEngine's own body for anything outside the Inspector tree - see
    // the implementation.
    void expandItem(const QString &iname) final;
    void refreshInspectorTree();

    void handleBreakpointEvent(quint64 requestId, BreakpointOp op, bool ok, const GdbMi &data);
    void applyBkptData(const GdbMi &bkpt, const Breakpoint &bp);
    void handleBreakpointModified(const GdbMi &data);
    void handleSignalReceived(const QString &name, const QString &meaning);
    void reloadThreads();
    // Mirrors GdbEngine::cleanupFullName(): applied to every raw "fullname"
    // a backend reports (the locationChanged()/RefreshKind::SourceFiles
    // handlers below) - lives here rather than on the backend itself, since
    // it needs runParameters()/settings(), and both of this method's real
    // callers are already outbound signals this class receives and further
    // processes (see debuggerengineinterface.h's locationChanged() comment
    // on why - not a new dependency, an existing one this class already has).
    Utils::FilePath cleanupFullName(const QString &fileName);

    const std::unique_ptr<DebuggerEngineInterface> m_backend;
    // Mirrors GdbEngine::m_baseNameToFullName: cleanupFullName()'s "search
    // sysroot/usr/src/debug by basename" fallback cache, built once per
    // session.
    QMultiMap<QString, Utils::FilePath> m_baseNameToFullName;
    quint64 m_nextBreakpointRequestId = 1;
    quint64 m_nextRefreshRequestId = 1;
    quint64 m_nextMemoryRequestId = 1;
    quint64 m_nextDisassemblyRequestId = 1;
    quint64 m_nextSnapshotRequestId = 1;
    // Never correlated with anything (the response only opens an editor), but
    // refresh() takes a requestId, so it gets a real one rather than 0.
    quint64 m_nextFullBacktraceRequestId = 1;
    quint64 m_nextWatchPointRequestId = 1;
    QHash<quint64, Breakpoint> m_pendingBreakpoints;
    QHash<quint64, QPointer<MemoryAgent>> m_pendingMemoryRequests;
    QHash<quint64, QPointer<DisassemblerAgent>> m_pendingDisassemblyRequests;
};

} // namespace Debugger::Internal
