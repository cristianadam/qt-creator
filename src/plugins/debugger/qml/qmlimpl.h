// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../debuggerengineinterface.h"

#include <qmldebug/qmldebugclient.h>
#include <qmldebug/qmldebugconnection.h>
#include <qmldebug/qmlenginedebugclient.h>

#include <QHash>
#include <QList>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace Debugger::Internal {

// Fourth real DebuggerEngineInterface implementation, alongside GdbImpl/
// LldbImpl/PdbImpl - the first attaching over a plain TCP connection
// instead of spawning a local process (see AttachToQmlServerData's own
// comment), and the first talking V8-debugger-style JSON instead of GdbMi
// wire syntax (see qmlv8debuggerclientconstants.h) - GdbMi here is only
// ever synthesized by hand, the same way PdbImpl already does for its own
// non-GdbMi wire format (see this file's constMi() helper).
//
// AttachToQmlServer only this slice - attach to an already-running app's
// QML/JS interpreter (started with e.g. "-qmljsdebugger=port:N,block,
// services:..."), never spawning anything itself. Checked directly against
// real QmlEngine (qml/qmlengine.{h,cpp}) throughout.
//
// Scope: start()/shutdownInferior()/shutdownEngine() for attach-only;
// execute() for Continue/Interrupt/StepIn/StepOver/StepOut/RunToLine;
// changeBreakpoint() for BreakpointByFileAndLine/BreakpointOnQmlSignalEmit/
// BreakpointAtJavaScriptThrow (Insert/Remove/Update); refresh(FullStack),
// refresh(SourceFiles) and refresh(Locals) - locals, watchers and
// demand-driven container expansion alike, see refreshLocals();
// refresh(InspectorTree), the live object tree of the running scene, see
// refreshInspectorTree(); executeDebuggerCommand() and
// assignValueInDebugger(), each in both of their branches (a stack frame while
// stopped, an Inspector object while running). Everything else pure-virtual on
// DebuggerEngineInterface gets only a trivial/no-op body this slice - real
// QmlEngine either has no equivalent at all (memory/registers/disassembly/
// snapshot - an interpreted JS engine has none of these concepts, ever) or its
// own body is a stub too (RunToFunction/JumpToLine - see execute()'s own
// comment on why real QmlEngine's own working executeJumpToLine() isn't ported
// here either).
//
// What remains unported of QmlInspectorAgent is its GUI half only: the separate
// "QmlInspectorTool" service behind the select and show-app-on-top toolbar
// actions, and jump-to-definition. Those reach into Core/the editor and need a
// QQuickWindow app - neither a backend's job nor reachable from a headless
// test - whereas its protocol half (the object tree, per-object fetches,
// expression evaluation and property watches) lives here, see
// refreshInspectorTree().
//
// changeBreakpoint(Insert) does NOT need the pending/breakpointModified
// two-step some other backends use for a not-yet-resolved breakpoint:
// unlike gdb's "-function multi" (which can come back with no breakpoint
// number at all until fully resolved), v8's own "setbreakpoint" response
// always carries a real breakpoint number immediately (confirmed in
// QmlEngine::insertBreakpoint()'s response handler - bp->setResponseId()
// runs unconditionally, before the actual_locations-empty check that only
// gates the model's own "pending" flag). So breakpointEvent(Insert, true,
// ...) with the real responseId fires right away every time here.
class DEBUGGER_EXPORT QmlImplStartData
{
public:
    InferiorStartData inferiorStartData;  // AttachToQmlServerData, this slice
};

class DEBUGGER_EXPORT QmlImpl final : public DebuggerEngineInterface
{
    Q_OBJECT

public:
    explicit QmlImpl(const QmlImplStartData &startData);
    ~QmlImpl() override;

private:
    void start() final;
    void shutdownInferior(ShutdownMode mode) final;
    void shutdownEngine() final;

    void execute(const ExecutionRequest &request) final;
    void changeBreakpoint(const BreakpointChangeRequest &request) final;
    bool isEnabledOnlyChange(const BreakpointChangeRequest &request) const;
    void refresh(const RefreshRequest &request) final;

    // Accumulates one refresh() across its several async legs - for Locals each
    // watcher, the frame, each scope and the batched lookup; for InspectorTree
    // the engine list, each engine's root context and each expanded object's
    // fetch. Held by shared_ptr so every leg's callback keeps it alive, and
    // reported in one go by whichever leg finishes last (legFinisher()). See
    // refreshLocals()/refreshInspectorTree().
    struct RefreshCollector
    {
        quint64 requestId = 0;
        RefreshKind kind = RefreshKind::Locals;
        int remaining = 0;
        GdbMi items;
        // Copied from the request - see RefreshRequest::expandedINames.
        QSet<QString> expandedINames;
        // InspectorTree only: every object this walk has reported, so the
        // parentless ones missing from it can be fetched by id afterwards - see
        // m_knownDelegateIds. Per walk rather than read off m_inameForDebugId,
        // which keeps entries from earlier rebuilds and would make an absent
        // object look present.
        QSet<int> seenDebugIds;
    };
    std::shared_ptr<RefreshCollector> makeCollector(const RefreshRequest &request);
    std::function<void()> legFinisher(const std::shared_ptr<RefreshCollector> &pending);
    // One handle waiting for a "lookup" reply, plus where the resolved value
    // belongs in the tree - real QmlEngine's own LookupData. Carried per round
    // so a container's children can chain their iname/exp off their parent's
    // while the traversal descends. See lookupHandles().
    struct LookupRequest
    {
        int handle = 0;
        QString iname;
        QString name;
        QString exp;
    };
    void refreshLocals(const RefreshRequest &request);
    void handleScopeReply(const QVariantMap &response,
                          const std::shared_ptr<RefreshCollector> &pending,
                          const std::function<void()> &finishLeg);
    void lookupHandles(const QList<LookupRequest> &requests,
                       const std::shared_ptr<RefreshCollector> &pending,
                       const std::function<void()> &finishLeg);
    static GdbMi emptyLocalsData();
    void refreshSourceFiles(const RefreshRequest &request);

    // The QmlDebugger service's replies are neither GdbMi nor JSON but decoded
    // QVariants (QmlDebug's own ObjectReference/ContextReference/
    // EngineReference, see baseenginedebugclient.h), so the Inspector side
    // needs its own callback type - same reasoning as QmlCallback further down.
    using InspectorCallback = std::function<void(const QVariant &value,
                                                const QByteArray &type)>;
    void refreshInspectorTree(const RefreshRequest &request);
    // Registers cb against a query id the caller just got back from
    // m_engineClient - the "QmlDebugger" service answers every query through
    // one result() signal keyed by that id, so this is the exact same
    // correlate-by-token shape runCommand() already uses for V8Debugger.
    // Mirrors what QmlInspectorAgent tracks with its own m_engineQueryId/
    // m_rootContextQueryIds/m_objectTreeQueryIds member lists instead.
    void runInspectorQuery(quint32 queryId, const InspectorCallback &cb);
    // Appends one object plus, when the view has it expanded, its properties
    // and children - and fetches it first if the context tree only carried a
    // stub. Mirrors QmlInspectorAgent::addWatchData() (its append=true half)
    // together with the fetchObject() its updateWatchData() would trigger.
    void appendObjectItems(const QmlDebug::ObjectReference &object, const QString &parentIname,
                           int engineId, const std::shared_ptr<RefreshCollector> &pending,
                           const std::function<void()> &finishLeg);
    void addObjectWatch(int debugId);
    // Unprompted rebuild after the scene changed - see
    // RefreshKind::InspectorTree on why a backend may push one.
    void rebuildInspectorTree();
    void handleObjectCreated(int engineId, int objectId, int parentId);
    void handlePropertyValueChanged(int debugId, const QByteArray &name, const QVariant &value);
    // Evaluates against an Inspector object rather than a stack frame - what
    // the QmlDebugger service offers instead of V8Debugger's "evaluate" while
    // the VM runs. Shared by executeDebuggerCommand()'s running branch and
    // assignValueInDebugger()'s isInspect one, exactly as real QmlEngine
    // routes both through QmlInspectorAgent::queryExpressionResult().
    void queryObjectExpression(int debugId, const QString &expression);

    // Not ported this slice - see the class comment. activateFrame() is
    // real (needed for executeDebuggerCommand()'s "evaluate in the current
    // frame"), selectThread() is trivial the same way it is for every other
    // backend (QML has no real thread concept either - model bookkeeping
    // belongs to DebuggerEngine/GenericDebuggerEngine, not here).
    void selectThread(const QString &threadId) final;
    void activateFrame(int index) final;
    void setRegisterValue(const QString &name, const QString &value) final;
    void accessMemory(MemoryOp op, quint64 requestId, quint64 addr, quint64 lengthOrSize,
                      const QByteArray &data) final;
    void fetchDisassembly(quint64 requestId, quint64 address, const QString &functionName) final;
    void setPeripheralRegisterValue(quint64 address, quint64 value) final;
    void watchPoint(quint64 requestId, const QPoint &pnt) final;
    void createSnapshot(quint64 requestId) final;

    // Real: mirrors QmlEngine::assignValueInDebugger()'s "<expr> = <literal>;"
    // evaluate, including its own per-type quoting. Its isInspect() branch
    // needs the Inspect mode (see the class comment).
    void assignValueInDebugger(const WatchItemData &item, const QString &expr,
                               const QString &value) final;

    // Real: mirrors both branches of QmlEngine::executeDebuggerCommand() -
    // stopped, it evaluates in the current frame through V8Debugger; running,
    // against the Inspector view's selected object through QmlDebugger (see
    // queryObjectExpression()). Either way the result goes back via message().
    void executeDebuggerCommand(const QString &command,
                                const WatchItemData &inspectorItem) final;

    // Composition, not inheritance: QmlDebug::QmlDebugClient and
    // DebuggerEngineInterface are both QObject-derived, so QmlImpl can't be
    // both at once (the same wall that keeps GdbImpl/LldbImpl/PdbImpl from
    // subclassing GdbEngine/LldbEngine/PdbEngine). Owned by m_connection,
    // not by this class directly.
    class V8Client;
    friend class V8Client;

    void handleStateChanged(QmlDebug::QmlDebugClient::State state);
    void handleMessageReceived(const QByteArray &data);

    // Builds the outer QPacket-framed "V8DEBUG"/<type>/<msg> triple every
    // message (in either direction) uses - real gdb/lldb/pdb never needed
    // this extra layer (their own framing is either MI's line protocol or
    // plain text) - mirrors QmlEnginePrivate::runDirectCommand() exactly.
    void runDirectCommand(const QByteArray &type, const QByteArray &msg = {});

    // Wire responses are QVariantMap (parsed V8 JSON), not GdbMi - so this
    // needs its own callback type rather than reusing DebuggerCommand's
    // (GdbMi-typed) Callback. Returns the seq actually used, so a caller
    // that needs to correlate a later async event (not just this response)
    // can key off it - mirrors QmlEnginePrivate::runCommand() plus
    // exposing what real code tracks via its own free-standing "sequence"
    // member right after the call instead.
    using QmlCallback = std::function<void(const QVariantMap &)>;
    int runCommand(const DebuggerCommand &command, const QmlCallback &cb = {});


    void handleV8Message(const QByteArray &payload);
    void handleConnectHandshakeDone();
    // Shared by changeBreakpoint()'s Insert case and its Update fallback
    // (always clear+re-set - see changeBreakpoint()'s own comment on why
    // "changebreakpoint" isn't used at all this slice).
    void setScriptBreakpoint(quint64 requestId, const BreakpointChangeRequest &request);
    // Async, unprompted - dispatched from handleV8Message()'s "event" case,
    // not tied to any single request's own callback.
    void handleBreakEvent(const QVariantMap &response);
    // Same shape as handleBreakEvent(): an uncaught JS throw pauses the VM
    // like a breakpoint does, once BreakpointAtJavaScriptThrow is set.
    void handleExceptionEvent(const QVariantMap &response);

    void beginConnection();

    QmlImplStartData m_startData;
    QmlDebug::QmlDebugConnection m_connection;
    V8Client *m_v8Client = nullptr;
    // Second service on the same connection, for the Inspector tree only:
    // "QmlDebugger", Qt's own QDataStream-based object-inspection service,
    // nothing to do with V8Debugger's JSON above. Used through QmlDebug's
    // ready-made client (which already decodes the wire into
    // ObjectReference/ContextReference) rather than hand-rolled the way
    // V8Client is - the very same client real QmlInspectorAgent uses. Owned by
    // m_connection, like m_v8Client.
    QmlDebug::QmlEngineDebugClient *m_engineClient = nullptr;
    // The target's own "-qmljsdebugger=port:N,block,..." may not have
    // reached listen() yet by the time start() first tries to connect
    // (confirmed live: an immediate connectionFailed(), not a timeout) -
    // mirrors QmlEngine::connectionStartupFail()'s identical retry (there,
    // every 3s, indefinitely; here, bounded and much shorter, since a test
    // scenario's target is already known to be starting up right now, not
    // possibly never coming up at all).
    int m_connectRetriesLeft = 50;

    int m_sequence = 0;
    QHash<int, QmlCallback> m_callbackForToken;

    // requestId -> the same request, keyed by the real v8 breakpoint number
    // (always known immediately - see the class comment) - needed for
    // Remove/Update, which only carry responseId/subResponseId, never the
    // original requestId.
    QHash<QString, BreakpointChangeRequest> m_activeBreakpointsByResponseId;

    // From the service's own "version" reply - see
    // handleConnectHandshakeDone(). False until it answers, so an Update
    // arriving before that just takes the clear+re-set path.
    bool m_supportChangeBreakpoint = false;
    int m_currentFrameIndex = 0;
    // A refresh(Locals) that arrived while the target was running - v8 needs a
    // paused frame to evaluate in, so it waits for the next stop, which is
    // what AddWatcherWhileRunningCapability promises here. See
    // refreshLocals()/handleBreakEvent().
    std::optional<RefreshRequest> m_deferredWatchers;
    bool m_inferiorRunning = false;
    // Set by shutdownEngine() right before closing m_connection - checked
    // by the disconnected() handler so a deliberate close isn't reported as
    // the connection spontaneously dying (EngineIll), mirroring PdbImpl's
    // own m_shuttingDown.
    bool m_shuttingDown = false;

    // --- Inspector tree (the "QmlDebugger" service above) ---
    QHash<quint32, InspectorCallback> m_inspectorCallbackForQueryId;
    QList<QmlDebug::EngineReference> m_qmlEngines;
    // debugId -> the iname the object was reported under, and the QML engine it
    // belongs to. Mirrors QmlInspectorAgent::m_debugIdToIname plus what its own
    // engineId() walks the parent chain to recover - kept directly here, since
    // this class never sees the WatchItem tree that walk needs.
    QHash<int, QString> m_inameForDebugId;
    QHash<int, int> m_engineIdForDebugId;
    // The last expanded set a refresh() carried, reused by an unprompted
    // rebuild - which has no request of its own to read it from.
    QSet<QString> m_expandedInspectorINames;
    // Objects a "watch" was set on, so a property change reports itself
    // (handlePropertyValueChanged()) instead of needing a poll. Mirrors
    // QmlInspectorAgent::m_objectWatches.
    QList<int> m_objectWatches;
    // debugId -> engine id, for every object reported created with no QObject
    // parent. Those are absent from the context tree the walk uses (the debug
    // service's buildObjectList() only collects the root context's instance
    // list), so they have to be fetched by id and attached under their engine -
    // Qt Quick delegates above all, whose visual parent is not a QObject
    // parent. Mirrors QmlInspectorAgent::m_knownDelegateIds, which keeps the
    // ids alone and recovers the engine from the item's iname instead.
    QHash<int, int> m_knownDelegateIds;
    // A QML engine can register long after the connection comes up (the app may
    // only create one on some later code path), so an empty LIST_ENGINES_R is a
    // transient, not an answer - bounded retry, same reasoning as
    // m_connectRetriesLeft above (real QmlInspectorAgent retries every 100ms
    // indefinitely instead).
    int m_engineQueryRetriesLeft = 50;
    // Coalesces a burst of OBJECT_CREATED into one rebuild, exactly what
    // QmlInspectorAgent::m_delayQueryTimer does.
    QTimer *m_objectCreatedTimer = nullptr;
};

} // namespace Debugger::Internal
