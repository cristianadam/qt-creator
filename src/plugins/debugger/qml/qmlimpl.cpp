// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlimpl.h"

#include "qmlv8debuggerclientconstants.h"

#include "../breakpoint.h"
#include "../debuggerconstants.h"
#include "../debuggertr.h"

#include <qmldebug/qpacketprotocol.h>

#include <utils/qtcassert.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <utility>

using namespace Utils;

namespace Debugger::Internal {

// Same synthesis helper as GdbImpl's/PdbImpl's own constMi() - the V8
// debugger protocol is JSON, not GdbMi wire syntax at all (see the class
// comment), so the "bkpt"-shaped tuple GenericDebuggerEngine expects has to
// be built by hand from the parsed JSON fields.
static GdbMi constMi(const QString &name, const QString &data)
{
    GdbMi mi;
    mi.m_type = GdbMi::Const;
    mi.m_name = name;
    mi.m_data = data;
    return mi;
}

// Mirrors QmlEngine's own hasCapability()/acceptsBreakpoint()/
// setToolTipHandling(Always) - the same four capabilities it claims, no more.
static DebuggerEngineSetupData qmlImplSetupData()
{
    DebuggerEngineSetupData data;
    // AddWatcherWhileRunning is claimed on the same terms real QmlEngine
    // claims it: adding a watcher while the VM runs is accepted, the value
    // just materializes at the next stop - v8's "evaluate" needs a paused VM
    // and a frame, so QmlEngine::updateItem() only evaluates in
    // InferiorStopOk too (it silently does nothing otherwise). See refresh()'s
    // own Locals case.
    data.capabilities = AddWatcherCapability
                      | AddWatcherWhileRunningCapability
                      | RunToLineCapability
                      | WatchComplexExpressionsCapability;
    // Detach: attach-only (see AttachToQmlServerData's own comment), and the
    // only meaningful shutdown mode there anyway - see shutdownInferior().
    // SourceFiles: v8 has a real "scripts" command for exactly this, which
    // real QmlEngine::reloadSourceFiles() uses too - see refreshSourceFiles().
    // Not claimed, with no real body here at all: LibraryEvent/
    // RunCommandDeferral/SignalReceived/Threads - an interpreted JS engine has
    // no OS-level signals, no thread concept of its own, and no
    // command-deferral machinery ported this slice.
    data.extraCapabilities = DebuggerExtraCapability::Detach
                           | DebuggerExtraCapability::SourceFiles;
    data.startModes = DebuggerStartModeFlag::AttachToQmlServer;
    data.toolTipHandling = ToolTipHandling::Always;
    data.acceptsBreakpoint = [](const AcceptsBreakpointQuery &query) {
        // Mirrors GdbImpl's/LldbImpl's/PdbImpl's own guard - a core file has
        // no running process to insert a real breakpoint into, regardless of
        // what kind of breakpoint it is.
        if (query.startMode == AttachToCore)
            return false;
        if (query.type == BreakpointOnQmlSignalEmit || query.type == BreakpointAtJavaScriptThrow)
            return true;
        return query.isQmlFileAndLineBreakpoint();
    };
    return data;
}

// Composition, not inheritance - see the class comment. Forwards straight
// back to the owning QmlImpl; carries no state of its own.
class QmlImpl::V8Client : public QmlDebug::QmlDebugClient
{
public:
    V8Client(QmlImpl *owner, QmlDebug::QmlDebugConnection *connection)
        : QmlDebug::QmlDebugClient(QLatin1String("V8Debugger"), connection)
        , m_owner(owner)
    {}

    void stateChanged(State state) override { m_owner->handleStateChanged(state); }
    void messageReceived(const QByteArray &data) override { m_owner->handleMessageReceived(data); }

private:
    QmlImpl *m_owner;
};

QmlImpl::QmlImpl(const QmlImplStartData &startData)
    : DebuggerEngineInterface(qmlImplSetupData())
    , m_startData(startData)
    , m_connection(this)
{
    m_v8Client = new V8Client(this, &m_connection);
    // Both clients are created up front, before connectToHost(): a
    // QmlDebugConnection only announces the services it already knows about
    // (see QmlDebugConnectionPrivate::advertisePlugins()), so a client added
    // after the handshake would never be enabled at all.
    m_engineClient = new QmlDebug::QmlEngineDebugClient(&m_connection);
    connect(m_engineClient, &QmlDebug::BaseEngineDebugClient::result,
            this, [this](quint32 queryId, const QVariant &value, const QByteArray &type) {
        if (const InspectorCallback cb = m_inspectorCallbackForQueryId.take(queryId))
            cb(value, type);
        // Unregistered ids are ordinary: addObjectWatch() fires and forgets, so
        // its own WATCH_OBJECT_R lands here with nothing to run (confirmed
        // live). Real QmlInspectorAgent::onResult() reaches the same point by
        // matching against its query-id lists and falling through.
    });
    connect(m_engineClient, &QmlDebug::BaseEngineDebugClient::newObject,
            this, &QmlImpl::handleObjectCreated);
    connect(m_engineClient, &QmlDebug::BaseEngineDebugClient::valueChanged,
            this, &QmlImpl::handlePropertyValueChanged);
    m_objectCreatedTimer = new QTimer(this);
    m_objectCreatedTimer->setInterval(100); // QmlInspectorAgent's own interval
    m_objectCreatedTimer->setSingleShot(true);
    connect(m_objectCreatedTimer, &QTimer::timeout, this, &QmlImpl::rebuildInspectorTree);

    connect(&m_connection, &QmlDebug::QmlDebugConnection::connectionFailed, this, [this] {
        if (m_connectRetriesLeft > 0) {
            --m_connectRetriesLeft;
            QTimer::singleShot(100, this, [this] { beginConnection(); });
            return;
        }
        emit inferiorEvent(InferiorEvent::EngineSetupFailed);
    });
    connect(&m_connection, &QmlDebug::QmlDebugConnection::disconnected, this, [this] {
        if (m_shuttingDown)
            return;
        // Spontaneous disconnect while attached - the target process isn't
        // ours to have exited (see shutdownInferior()'s own comment), so
        // this is reported as the connection itself going bad, not as an
        // inferior exit.
        emit inferiorEvent(InferiorEvent::EngineIll);
    });
}

QmlImpl::~QmlImpl()
{
    // Same flag shutdownEngine() sets, for the same reason - but needed here
    // too, for the case where the engine is simply destroyed while still
    // connected: ~QmlDebugConnection() (a member, so destroyed before this
    // object's QObject base, leaving every connection above still live) runs
    // socketDisconnected(), which emits disconnected(). Without this, the
    // handler above would then emit inferiorEvent(EngineIll) from a
    // half-destroyed object, which aborts outright.
    m_shuttingDown = true;
}

void QmlImpl::start()
{
    beginConnection();
}

void QmlImpl::beginConnection()
{
    const auto &qmlData = std::get<AttachToQmlServerData>(m_startData.inferiorStartData);
    m_connection.connectToHost(qmlData.server.host(), quint16(qmlData.server.port()));
}

void QmlImpl::shutdownInferior(ShutdownMode)
{
    // Detach is the only meaningful mode here - QmlImpl never spawned the
    // debuggee's process (see the class comment), so there's nothing to
    // "kill" either way; disconnecting just leaves the target running.
    emit inferiorEvent(InferiorEvent::ShutdownFinished);
}

void QmlImpl::shutdownEngine()
{
    m_shuttingDown = true;
    m_connection.close();
    emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
}

void QmlImpl::handleStateChanged(QmlDebug::QmlDebugClient::State state)
{
    if (state != QmlDebug::QmlDebugClient::Enabled)
        return;
    // Deferred rather than handled synchronously here - avoids reentering
    // QmlDebugConnection's own state machine from inside its own signal
    // (mirrors QmlEnginePrivate::stateChanged()'s identical
    // QTimer::singleShot(0, ...), though for a narrower reason: that one
    // also waits for deferred breakpoint-claiming, which doesn't apply to
    // this bare interface at all).
    QTimer::singleShot(0, this, [this] { handleConnectHandshakeDone(); });
}

void QmlImpl::handleConnectHandshakeDone()
{
    QJsonObject parameters;
    parameters.insert(QLatin1String("redundantRefs"), false);
    parameters.insert(QLatin1String("namesAsObjects"), false);
    runDirectCommand(CONNECT, QJsonDocument(parameters).toJson());

    // What this service can do, before anything relies on it: the reply carries
    // "ChangeBreakpoint", "UnpausedEvaluate" and "ContextEvaluate" (all three
    // true in Qt's own V4 service, see qv4debugservice.cpp's V4VersionRequest).
    // Mirrors QmlEnginePrivate::handleVersion(). Only ChangeBreakpoint is acted
    // on so far - see changeBreakpoint()'s Update case; the other two would let
    // executeDebuggerCommand() evaluate through V8Debugger while the VM runs
    // instead of through the Inspector object, which is the route the tests
    // cover today.
    runCommand({VERSION}, [this](const QVariantMap &resp) {
        const QVariantMap body = resp.value(QLatin1String(BODY)).toMap();
        m_supportChangeBreakpoint = body.value("ChangeBreakpoint", false).toBool();
    });

    emit inferiorEvent(InferiorEvent::EngineSetupOk);
    // block mode (see the class comment on the launch args this needs)
    // only delays the native process from proceeding until the initial
    // debug-connection handshake completes (confirmed live, and in
    // QQmlDebugServerImpl::open()'s own "wait for hello" logic) - it does
    // NOT leave the QML/JS engine itself paused at the v8-debugger-protocol
    // level once that's done (confirmed live: an immediate execute(Continue)
    // fails with v8's own "Debugger has to be paused in order to continue").
    // So unlike GdbImpl's plain "target remote" attach (already stopped,
    // RunAndInferiorStopOk), a Qml attach's target is already genuinely
    // running from the moment this fires - RunAndInferiorRunOk, matching
    // GdbImpl's own "target extended-remote" attach-while-running case.
    // Which is also why m_inferiorRunning is set here and not only by
    // execute(Continue): before this, the flag claimed a freshly attached
    // target was stopped, so anything keyed off it (refreshLocals()'s deferral,
    // executeDebuggerCommand()'s frame-versus-Inspector branch) took the
    // stopped path and failed against a VM that has no frame at all.
    m_inferiorRunning = true;
    emit inferiorEvent(InferiorEvent::RunAndInferiorRunOk);
}

void QmlImpl::runDirectCommand(const QByteArray &type, const QByteArray &msg)
{
    emit message(QString("%1 %2").arg(QString::fromLatin1(type), QString::fromUtf8(msg)), LogInput);
    QmlDebug::QPacket rs(m_v8Client->dataStreamVersion());
    rs << QByteArray(V8DEBUG) << type << msg;
    m_v8Client->sendMessage(rs.data());
}

int QmlImpl::runCommand(const DebuggerCommand &command, const QmlCallback &cb)
{
    ++m_sequence;
    QJsonObject object;
    object.insert(QLatin1String(SEQ), m_sequence);
    object.insert(QLatin1String(TYPE), QLatin1String(REQUEST));
    object.insert(QLatin1String(COMMAND), command.function);
    object.insert(QLatin1String(ARGUMENTS), command.args);
    if (cb)
        m_callbackForToken[m_sequence] = cb;
    runDirectCommand(V8REQUEST, QJsonDocument(object).toJson(QJsonDocument::Compact));
    return m_sequence;
}

void QmlImpl::handleMessageReceived(const QByteArray &data)
{
    QmlDebug::QPacket ds(m_v8Client->dataStreamVersion(), data);
    QByteArray command;
    ds >> command;
    if (command != V8DEBUG)
        return;
    QByteArray type;
    QByteArray payload;
    ds >> type >> payload;
    emit message(QString("%1 %2").arg(QString::fromLatin1(type), QString::fromUtf8(payload)),
                  LogOutput);
    if (type == V8MESSAGE)
        handleV8Message(payload);
    // CONNECT/INTERRUPT/BREAKONSIGNAL/DISCONNECT direct-command echoes:
    // nothing to do, matches real QmlEnginePrivate::messageReceived()'s own
    // empty branches for these.
}

void QmlImpl::handleV8Message(const QByteArray &payload)
{
    const QVariantMap resp = QJsonDocument::fromJson(payload).toVariant().toMap();
    const QString type = resp.value(QLatin1String(TYPE)).toString();
    if (type == QLatin1String("response")) {
        const int requestSeq = resp.value(QLatin1String("request_seq")).toInt();
        const QmlCallback cb = m_callbackForToken.take(requestSeq);
        if (cb)
            cb(resp);
    } else if (type == QLatin1String("event")) {
        const QString event = resp.value(QLatin1String("event")).toString();
        if (event == QLatin1String("break"))
            handleBreakEvent(resp);
        else if (event == QLatin1String("exception"))
            handleExceptionEvent(resp);
        // "afterCompile" and others: not ported this slice.
    }
}

void QmlImpl::handleBreakEvent(const QVariantMap &response)
{
    // Mirrors QmlEngine::messageReceived()'s "break" branch, minus the
    // anonymous-binding-wrapper relocation and the InternalFunction/
    // previousStepAction skip-continue cases (native-mixed/interpreter
    // internals GdbImpl/LldbImpl's own dumper bridge never surfaces here
    // either) - not ported this slice.
    m_inferiorRunning = false;
    const QVariantMap body = response.value(QLatin1String(BODY)).toMap();
    const QVariantList hitBreakpointIds = body.value(QLatin1String("breakpoints")).toList();
    for (const QVariant &id : hitBreakpointIds) {
        const QString responseId = QString::number(id.toInt());
        if (!m_activeBreakpointsByResponseId.contains(responseId))
            continue; // an internal one-shot breakpoint (RunToLine), not caller-visible
    }
    // Mirrors GdbImpl's/PdbImpl's own "quickly set the location marker"
    // right alongside their stop notification (gdbimpl.cpp/pdbimpl.cpp) -
    // the break event body already carries it, just like theirs does.
    // script.name is the script's full "qrc:/..." (or plain file:/...) URL;
    // QUrl::fileName() reduces it to a bare basename, matching how
    // setScriptBreakpoint()'s own TARGET is only ever a bare basename too
    // (see its own comment on why that's enough for v8's scriptRegExp
    // match).
    const QVariantMap script = body.value(QLatin1String("script")).toMap();
    const QString scriptName = script.value(QLatin1String(NAME)).toString();
    const int lineNumber = body.value(QLatin1String("sourceLine")).toInt() + 1;
    if (!scriptName.isEmpty())
        emit locationChanged(FilePath::fromUserInput(QUrl(scriptName).fileName()), lineNumber);
    emit inferiorEvent(InferiorEvent::SpontaneousStop);

    // Now that there is a frame to evaluate in, answer whatever watcher
    // request arrived while the target was still running - see
    // refreshLocals(). Taken out first, so a failure can't replay forever.
    if (m_deferredWatchers) {
        const RefreshRequest deferred = *std::exchange(m_deferredWatchers, std::nullopt);
        refreshLocals(deferred);
    }
}

// Mirrors QmlEngine::messageReceived()'s "exception" branch: an uncaught JS
// throw with BreakpointAtJavaScriptThrow set pauses the VM exactly like a
// breakpoint hit, so it is reported the same way - location, the exception's
// own text, then SpontaneousStop. Not ported from real code: the
// highlightExceptionCode() source annotation, which edits the document in the
// editor (a GUI concern that lives on DebuggerEngine's side, not here).
void QmlImpl::handleExceptionEvent(const QVariantMap &response)
{
    m_inferiorRunning = false;
    const QVariantMap body = response.value(QLatin1String(BODY)).toMap();

    const QVariantMap script = body.value(QLatin1String("script")).toMap();
    const QString scriptName = script.value(QLatin1String(NAME)).toString();
    const int lineNumber = body.value(QLatin1String("sourceLine")).toInt() + 1;
    if (!scriptName.isEmpty())
        emit locationChanged(FilePath::fromUserInput(QUrl(scriptName).fileName()), lineNumber);

    const QVariantMap exception = body.value(QLatin1String("exception")).toMap();
    const QString text = exception.value(QLatin1String("text")).toString();
    if (!text.isEmpty())
        emit message(text, ConsoleOutput);

    emit inferiorEvent(InferiorEvent::SpontaneousStop);
}

void QmlImpl::setScriptBreakpoint(quint64 requestId, const BreakpointChangeRequest &request)
{
    const BreakpointParameters &params = request.params;
    DebuggerCommand cmd(SETBREAKPOINT);
    cmd.arg(TYPE, SCRIPTREGEXP);
    cmd.arg(TARGET, params.fileName.toUrlishString());
    cmd.arg(ENABLED, params.enabled);
    cmd.arg(LINE, params.textPosition.line - 1);
    if (!params.condition.isEmpty())
        cmd.arg(CONDITION, params.condition);

    runCommand(cmd, [this, requestId, params, request](const QVariantMap &resp) {
        const bool success = resp.value(QLatin1String(SUCCESS)).toBool();
        if (!success) {
            emit breakpointEvent(requestId, BreakpointOp::Insert, false, {});
            return;
        }
        const QVariantMap body = resp.value(QLatin1String(BODY)).toMap();
        // Unlike gdb's "-function multi" (which can come back with no
        // breakpoint number at all until fully resolved - see
        // QmlImplStartData's own comment on the class), v8's own
        // "setbreakpoint" response always carries a real number here,
        // confirmed against real QmlEngine::insertBreakpoint()'s response
        // handler (bp->setResponseId() runs unconditionally).
        const QString responseId = QString::number(body.value(QLatin1String(BREAKPOINT)).toInt());
        BreakpointChangeRequest active = request;
        active.responseId = responseId;
        m_activeBreakpointsByResponseId[responseId] = active;

        int line = params.textPosition.line;
        const QVariantList actualLocations = body.value(QLatin1String("actual_locations")).toList();
        if (!actualLocations.isEmpty())
            line = actualLocations.constFirst().toMap().value(QLatin1String(LINE)).toInt() + 1;

        GdbMi bkpt;
        bkpt.m_type = GdbMi::Tuple;
        bkpt.addChild(constMi(QLatin1String(NUMBER), responseId));
        bkpt.addChild(constMi(QLatin1String("file"), params.fileName.toUserOutput()));
        bkpt.addChild(constMi(QLatin1String("line"), QString::number(line)));
        bkpt.addChild(constMi(QLatin1String(ENABLED), params.enabled ? QStringLiteral("y")
                                                                     : QStringLiteral("n")));
        GdbMi data;
        data.m_type = GdbMi::List;
        data.addChild(bkpt);
        emit breakpointEvent(requestId, BreakpointOp::Insert, true, data);
    });
}

// Whether an Update only flips "enabled" - the one thing "changebreakpoint" can
// carry. Compared against what was actually set for that breakpoint (see
// m_activeBreakpointsByResponseId): anything else changed, and the breakpoint has
// to be re-set from scratch.
bool QmlImpl::isEnabledOnlyChange(const BreakpointChangeRequest &request) const
{
    const auto it = m_activeBreakpointsByResponseId.constFind(request.responseId);
    if (it == m_activeBreakpointsByResponseId.constEnd())
        return false;
    const BreakpointParameters &was = it->params;
    const BreakpointParameters &now = request.params;
    return was.enabled != now.enabled
           && was.fileName == now.fileName
           && was.textPosition == now.textPosition
           && was.condition == now.condition
           && was.ignoreCount == now.ignoreCount
           && was.command == now.command;
}

void QmlImpl::changeBreakpoint(const BreakpointChangeRequest &request)
{
    const BreakpointParameters &params = request.params;
    switch (request.op) {
    case BreakpointOp::Insert:
        if (params.type == BreakpointAtJavaScriptThrow) {
            DebuggerCommand cmd(SETEXCEPTIONBREAK);
            cmd.arg(TYPE, ALL);
            if (params.enabled)
                cmd.arg(ENABLED, params.enabled);
            runCommand(cmd);
            // No real per-breakpoint number for exception breaks in v8
            // either - real QmlEngine reports success immediately, no
            // round trip (bp->setPending(false); notifyBreakpointInsertOk(bp);
            // right where it sends this, not from a callback).
            emit breakpointEvent(request.requestId, BreakpointOp::Insert, true, {});
        } else if (params.type == BreakpointOnQmlSignalEmit) {
            QmlDebug::QPacket rs(m_v8Client->dataStreamVersion());
            rs << params.functionName.toUtf8() << params.enabled;
            runDirectCommand(BREAKONSIGNAL, rs.data());
            emit breakpointEvent(request.requestId, BreakpointOp::Insert, true, {});
        } else {
            setScriptBreakpoint(request.requestId, request);
        }
        break;
    case BreakpointOp::Remove:
        if (params.type == BreakpointAtJavaScriptThrow) {
            DebuggerCommand cmd(SETEXCEPTIONBREAK);
            cmd.arg(TYPE, ALL);
            runCommand(cmd);
        } else if (params.type == BreakpointOnQmlSignalEmit) {
            QmlDebug::QPacket rs(m_v8Client->dataStreamVersion());
            rs << params.functionName.toUtf8() << false;
            runDirectCommand(BREAKONSIGNAL, rs.data());
        } else {
            DebuggerCommand cmd(CLEARBREAKPOINT);
            cmd.arg(BREAKPOINT, request.responseId.toInt());
            runCommand(cmd);
            m_activeBreakpointsByResponseId.remove(request.responseId);
        }
        emit breakpointEvent(request.requestId, BreakpointOp::Remove, true, {});
        break;
    case BreakpointOp::Update:
        if (params.type == BreakpointAtJavaScriptThrow) {
            DebuggerCommand cmd(SETEXCEPTIONBREAK);
            cmd.arg(TYPE, ALL);
            if (params.enabled)
                cmd.arg(ENABLED, params.enabled);
            runCommand(cmd);
            emit breakpointEvent(request.requestId, BreakpointOp::Update, true, {});
        } else if (params.type == BreakpointOnQmlSignalEmit) {
            QmlDebug::QPacket rs(m_v8Client->dataStreamVersion());
            rs << params.functionName.toUtf8() << params.enabled;
            runDirectCommand(BREAKONSIGNAL, rs.data());
            emit breakpointEvent(request.requestId, BreakpointOp::Update, true, {});
        } else if (m_supportChangeBreakpoint && isEnabledOnlyChange(request)) {
            // Toggling enabled is all "changebreakpoint" can do - the service
            // hands it straight to enableBreakPoint() and ignores everything
            // else (qv4debugservice.cpp's V4ChangeBreakPointRequest), so any
            // other edit still needs the clear+re-set below. Mirrors
            // QmlEnginePrivate::changeBreakpoint().
            DebuggerCommand cmd(CHANGEBREAKPOINT);
            cmd.arg(BREAKPOINT, request.responseId.toInt());
            cmd.arg(ENABLED, params.enabled);
            const quint64 requestId = request.requestId;
            runCommand(cmd, [this, requestId, request](const QVariantMap &resp) {
                const bool ok = resp.value(QLatin1String(SUCCESS)).toBool();
                if (ok)
                    m_activeBreakpointsByResponseId.insert(request.responseId, request);
                emit breakpointEvent(requestId, BreakpointOp::Update, ok, {});
            });
        } else {
            // Everything else: clear and re-set, which is also real QmlEngine's
            // own !canChangeBreakpoint() fallback.
            DebuggerCommand clearCmd(CLEARBREAKPOINT);
            clearCmd.arg(BREAKPOINT, request.responseId.toInt());
            runCommand(clearCmd);
            m_activeBreakpointsByResponseId.remove(request.responseId);
            setScriptBreakpoint(request.requestId, request);
        }
        break;
    case BreakpointOp::EnableSub:
        // No sub-location concept for QML breakpoints (unlike gdb's
        // multi-location function breakpoints) - never reached;
        // BreakIndividualLocationsCapability isn't claimed here.
        break;
    }
}

void QmlImpl::execute(const ExecutionRequest &request)
{
    switch (request.command) {
    case ExecutionCommand::Continue:
        emit inferiorEvent(InferiorEvent::RunRequested);
        m_inferiorRunning = true;
        // RunOk unconditionally, NOT derived from the response's "running"
        // field: that field means "is the VM running after sending this
        // response", so it is legitimately false whenever the target already
        // reached the next breakpoint before v8 composed the reply - which
        // made this report RunFailed for a perfectly successful continue,
        // roughly 2 runs in 5. Real QmlEngine::continueInferior() doesn't look
        // at it either; it calls notifyInferiorRunOk() straight after a
        // fire-and-forget runCommand(). Same for the two cases below.
        runCommand({CONTINEDEBUGGING}, [this](const QVariantMap &) {
            emit inferiorEvent(InferiorEvent::RunOk);
        });
        break;
    case ExecutionCommand::Interrupt:
        runDirectCommand(INTERRUPT);
        break;
    case ExecutionCommand::StepIn:
    case ExecutionCommand::StepOver:
    case ExecutionCommand::StepOut: {
        DebuggerCommand cmd(CONTINEDEBUGGING);
        cmd.arg(STEPACTION, request.command == ExecutionCommand::StepIn ? IN
                           : request.command == ExecutionCommand::StepOut ? OUT : NEXT);
        emit inferiorEvent(InferiorEvent::RunRequested);
        m_inferiorRunning = true;
        runCommand(cmd, [this](const QVariantMap &) {
            emit inferiorEvent(InferiorEvent::RunOk);
        });
        break;
    }
    case ExecutionCommand::RunToLine: {
        // Mirrors QmlEngine::executeRunToLine() exactly: a plain
        // (non-one-shot) breakpoint at the target line, then continue -
        // real QmlEngine never removes it afterward either.
        DebuggerCommand cmd(SETBREAKPOINT);
        cmd.arg(TYPE, SCRIPTREGEXP);
        cmd.arg(TARGET, request.context.fileName.toUrlishString());
        cmd.arg(ENABLED, true);
        cmd.arg(LINE, request.context.textPosition.line - 1);
        runCommand(cmd);

        emit inferiorEvent(InferiorEvent::RunRequested);
        m_inferiorRunning = true;
        runCommand({CONTINEDEBUGGING}, [this](const QVariantMap &) {
            emit inferiorEvent(InferiorEvent::RunOk);
        });
        break;
    }
    case ExecutionCommand::Detach:
        // Mirrors shutdownInferior()'s own reasoning exactly: QmlImpl never
        // spawned the debuggee's process (see the class comment), so
        // "detach" just means closing the connection and leaving the
        // target running - already what shutdownInferior() does
        // unconditionally, this just reports it through the different
        // signal execute(Detach)'s own callers (as opposed to
        // shutdownInferior(ShutdownMode::Detach)'s) expect.
        emit inferiorDone({0, InferiorExitStatus::Detached});
        break;
    default:
        // RunToFunction/JumpToLine/Return/ResetInferior/Abort/
        // RecordReverse/RepeatLastCommand: no real QmlEngine body (or,
        // JumpToLine, a real body but a deliberately unclaimed capability -
        // see the class comment) - not ported this slice.
        break;
    }
}

// Condensed QmlEnginePrivate::extractData(): maps a v8 value object onto the
// (type, value) pair a WatchItem needs. The (type, value) pair only - a
// container's own members live in "properties", which the callers walk
// themselves as they build the tree (see handleScopeReply()/lookupHandles()),
// and its child count comes from v8ChildCount() rather than from "value",
// which for a container holds that count instead of anything displayable.
static std::pair<QString, QString> v8TypeAndValue(const QVariantMap &data)
{
    const QString type = data.value(QLatin1String(TYPE)).toString();
    const QVariant value = data.value(QLatin1String(VALUE));
    if (type == "undefined")
        return {"undefined", "undefined"};
    if (type == "null") // typeof(null) == "object" in JavaScript
        return {"object", "null"};
    if (type == "string") // quoted, matching extractData()
        return {type, '"' + value.toString() + '"'};
    if (type == "boolean" || type == "number")
        return {type, value.toString()};
    if (type == "function")
        return {type, data.value(QLatin1String(NAME)).toString()};
    if (type == "object") {
        // extractData()'s own null check: an object carrying a "value" that
        // isn't a real property count is JavaScript's null.
        if (data.contains(QLatin1String(VALUE)) && (!value.isValid() || value.isNull()))
            return {type, "null"};
        return {type, "{...}"};
    }
    return {type, value.toString()};
}


// Real local variables plus watchers, in one response. Mirrors the whole
// QmlEnginePrivate locals chain: "frame" gives the receiver ("this") and the
// scope chain, each non-global scope is fetched with "scope", and any property
// whose value only came back as a handle is resolved with one batched "lookup"
// - see handleFrame()/handleScope()/handleLookup() there. Unlike real
// QmlEngine, which inserts into WatchHandler incrementally as each reply
// lands, everything is collected here and reported in a single
// refreshDataReceived(): GenericDebuggerEngine's Locals case calls
// updateLocalsView() + notifyUpdateFinished() exactly once per response.
// One collector per accumulating refresh, ready to fill: it remembers the id and
// kind to report under, so every leg can stay ignorant of both. remaining is the
// caller's to set - it knows how many legs it is about to start.
std::shared_ptr<QmlImpl::RefreshCollector> QmlImpl::makeCollector(const RefreshRequest &request)
{
    const auto pending = std::make_shared<RefreshCollector>();
    pending->requestId = request.requestId;
    pending->kind = request.kind;
    pending->items.m_type = GdbMi::List;
    pending->items.m_name = QStringLiteral("data");
    pending->expandedINames = request.expandedINames;
    return pending;
}

// The tail every leg shares: drop this leg, and if it was the last one, report
// everything collected as one response. Legs may be added from inside another
// leg's callback (before that leg finishes), so remaining cannot reach zero
// early - see refreshLocals()/lookupHandles().
std::function<void()> QmlImpl::legFinisher(const std::shared_ptr<RefreshCollector> &pending)
{
    return [this, pending] {
        if (--pending->remaining > 0)
            return;
        GdbMi all;
        all.m_type = GdbMi::Tuple;
        all.addChild(pending->items);
        emit refreshDataReceived(pending->requestId, pending->kind, all);
    };
}

void QmlImpl::refreshLocals(const RefreshRequest &request)
{
    const QJsonArray watchers = request.watchers;

    // v8 can only evaluate/inspect with a paused VM and a frame, so a request
    // arriving while the target runs is held until the next stop rather than
    // answered (answering it empty would make AddWatcherWhileRunningCapability
    // vacuous) - handleBreakEvent() replays it. Same deferral real QmlEngine
    // does by having updateItem() do nothing outside InferiorStopOk and letting
    // the next updateLocals() pick the watcher up.
    if (m_inferiorRunning) {
        m_deferredWatchers = request;
        return;
    }

    // Shared by every leg below (each watcher, the frame, each scope, the
    // lookup): remaining counts replies still outstanding, and whichever leg
    // finishes last emits. Legs are only ever added from inside another leg's
    // own callback, before that leg finishes, so the count can't reach zero
    // early.
    const auto pending = makeCollector(request);
    pending->remaining = 1 + int(watchers.size()); // 1 = the frame leg
    const auto finishLeg = legFinisher(pending);

    for (const QJsonValue &watcherValue : watchers) {
        const QJsonObject watcher = watcherValue.toObject();
        const QString iname = watcher.value("iname").toString();
        // Hex-encoded on the wire (see RefreshRequest::watchers) - decoded for
        // v8, but passed through as-is for "wname", which WatchItem::parse()
        // itself expects hex-encoded.
        const QString hexExp = watcher.value("exp").toString();
        const QString exp = QString::fromUtf8(QByteArray::fromHex(hexExp.toLatin1()));

        DebuggerCommand cmd(EVALUATE);
        cmd.arg(EXPRESSION, exp);
        cmd.arg(FRAME, m_currentFrameIndex);
        runCommand(cmd, [iname, hexExp, pending, finishLeg](const QVariantMap &resp) {
            const QVariantMap body = resp.value(QLatin1String(BODY)).toMap();
            GdbMi item;
            item.m_type = GdbMi::Tuple;
            item.addChild(constMi(QStringLiteral("iname"), iname));
            item.addChild(constMi(QStringLiteral("wname"), hexExp));
            if (resp.value(QLatin1String(SUCCESS)).toBool()) {
                const auto [type, value] = v8TypeAndValue(body);
                item.addChild(constMi(QStringLiteral("type"), type));
                item.addChild(constMi(QStringLiteral("value"), value));
            } else {
                // No type on failure, mirroring handleEvaluateExpression().
                item.addChild(constMi(QStringLiteral("value"),
                                      body.value(QLatin1String("text")).toString()));
            }
            item.addChild(constMi(QStringLiteral("numchild"), QStringLiteral("0")));
            pending->items.addChild(item);
            finishLeg();
        });
    }

    DebuggerCommand frameCmd(FRAME);
    frameCmd.arg(NUMBER, m_currentFrameIndex);
    runCommand(frameCmd, [this, pending, finishLeg](const QVariantMap &resp) {
        const QVariantMap body = resp.value(QLatin1String(BODY)).toMap();

        // "this" always, matching handleFrame()'s own unconditional entry.
        const QVariantMap receiver = body.value(QLatin1String("receiver")).toMap();
        const auto [thisType, thisValue] = v8TypeAndValue(receiver);
        GdbMi thisItem;
        thisItem.m_type = GdbMi::Tuple;
        thisItem.addChild(constMi(QStringLiteral("iname"), QStringLiteral("local.this")));
        thisItem.addChild(constMi(QStringLiteral("name"), QStringLiteral("this")));
        thisItem.addChild(constMi(QStringLiteral("type"), thisType));
        thisItem.addChild(constMi(QStringLiteral("value"), thisValue));
        thisItem.addChild(constMi(QStringLiteral("numchild"), QStringLiteral("0")));
        pending->items.addChild(thisItem);

        const QVariantList scopes = body.value(QLatin1String("scopes")).toList();
        for (const QVariant &scopeValue : scopes) {
            const QVariantMap scope = scopeValue.toMap();
            // Skip global scope (type 0), exactly as handleFrame() does -
            // "showing global properties increases clutter".
            if (scope.value(QLatin1String(TYPE)).toInt() == 0)
                continue;
            ++pending->remaining;
            DebuggerCommand scopeCmd(SCOPE);
            scopeCmd.arg(NUMBER, scope.value(QLatin1String("index")).toInt());
            scopeCmd.arg(FRAMENUMBER, m_currentFrameIndex);
            runCommand(scopeCmd, [this, pending, finishLeg](const QVariantMap &scopeResp) {
                handleScopeReply(scopeResp, pending, finishLeg);
                finishLeg();
            });
        }
        finishLeg();
    });
}

// Mirrors QmlV8ObjectData::hasChildren(): for a container v8 reports only a
// *count* up front ("value" holds the property count, not something displayable
// - see extractData()'s object branch), so the members are a separate round
// trip. Returns 0 for a leaf.
static int v8ChildCount(const QVariantMap &data)
{
    const QString type = data.value(QLatin1String(TYPE)).toString();
    if (type != "object" && type != "function")
        return 0;
    const QVariantList properties = data.value(QLatin1String("properties")).toList();
    if (!properties.isEmpty())
        return int(properties.size());
    const QVariant value = data.value(QLatin1String(VALUE));
    // A real null is an object with no members; anything else here is the count.
    if (!value.isValid() || value.isNull())
        return 0;
    return value.toInt();
}

// One watch item, with numchild taken from the wire's own count so the view shows
// an expander even before the members are fetched - exactly what real QmlEngine
// does via setWatchItemHasChildren() in handleScope(). debugId is for Inspector
// items only (see appendObjectItems()): it carries the QML object's own debug
// id, which is what WatchItem::parse() puts in WatchItem::id and what
// assignValueInDebugger()/executeDebuggerCommand() then evaluate against. A
// Locals item has no such id and omits the field.
static GdbMi watchItem(const QString &iname, const QString &name, const QString &exp,
                       const QString &type, const QString &value, int numchild,
                       int debugId = -1)
{
    GdbMi item;
    item.m_type = GdbMi::Tuple;
    item.addChild(constMi(QStringLiteral("iname"), iname));
    item.addChild(constMi(QStringLiteral("name"), name));
    if (debugId != -1)
        item.addChild(constMi(QStringLiteral("id"), QString::number(debugId)));
    if (!exp.isEmpty())
        item.addChild(constMi(QStringLiteral("exp"), exp));
    if (!type.isEmpty())
        item.addChild(constMi(QStringLiteral("type"), type));
    item.addChild(constMi(QStringLiteral("value"), value));
    item.addChild(constMi(QStringLiteral("numchild"), QString::number(numchild)));
    return item;
}

// Turns one "scope" reply into the top level of the tree, and hands whatever
// came back as a bare handle to lookupHandles(). Mirrors handleScope(): v8
// reports a scope's properties as {name, ref} pairs, so a property's real
// type/value is often only reachable by resolving that handle - without the
// lookup most locals would show up with no value at all.
void QmlImpl::handleScopeReply(const QVariantMap &response,
                               const std::shared_ptr<RefreshCollector> &pending,
                               const std::function<void()> &finishLeg)
{
    const QVariantMap body = response.value(QLatin1String(BODY)).toMap();
    const QVariantMap object = body.value(QLatin1String("object")).toMap();

    QList<LookupRequest> lookups; // whatever needs a second round trip
    for (const QVariant &propertyValue : object.value(QLatin1String("properties")).toList()) {
        const QVariantMap property = propertyValue.toMap();
        const QString name = property.value(QLatin1String(NAME)).toString();
        // v8's own internal entries, skipped by handleScope() too.
        if (name.isEmpty() || name.startsWith('.'))
            continue;

        const QString iname = "local." + name;
        const int numchild = v8ChildCount(property);
        const auto [type, value] = v8TypeAndValue(property);
        const int handle = property.value(QLatin1String(REF),
                                         property.value(QLatin1String(HANDLE))).toInt();

        // A container is reported collapsed but expandable, and its members are
        // only fetched once the view actually has it expanded - the same
        // demand-driven shape as handleScope()'s isExpandedIName() check. Doing
        // it eagerly would cost a lookup per container on every refresh.
        if (numchild > 0) {
            pending->items.addChild(watchItem(iname, name, name, type, value, numchild));
            if (handle != 0 && pending->expandedINames.contains(iname))
                lookups.append({handle, iname, name, name});
            continue;
        }
        // A leaf with no inline value at all is just a handle - resolve it, or
        // it would show up blank.
        if (!property.contains(QLatin1String(VALUE)) && handle != 0) {
            lookups.append({handle, iname, name, name});
            continue;
        }
        pending->items.addChild(watchItem(iname, name, name, type, value, 0));
    }

    lookupHandles(lookups, pending, finishLeg);
}

// Resolves a batch of handles and, for each one that came back a container,
// emits its members - descending again into any member the view has expanded
// too. Mirrors the handleLookup()/insertSubItems() recursion in real
// QmlEngine: one "lookup" per tree level, each level's members gated on their
// own iname being expanded, so a collapsed subtree costs nothing.
void QmlImpl::lookupHandles(const QList<LookupRequest> &requests,
                            const std::shared_ptr<RefreshCollector> &pending,
                            const std::function<void()> &finishLeg)
{
    if (requests.isEmpty())
        return;

    // The reply is keyed by handle alone, so two items sharing one handle (the
    // same object reachable under two names) can't be told apart - one request
    // per handle, exactly as lookup()'s own currentlyLookingUp does.
    QHash<int, LookupRequest> requestForHandle;
    QList<int> handles;
    for (const LookupRequest &request : requests) {
        if (requestForHandle.contains(request.handle))
            continue;
        requestForHandle.insert(request.handle, request);
        handles.append(request.handle);
    }

    ++pending->remaining;
    DebuggerCommand cmd(LOOKUP);
    cmd.arg(HANDLES, handles);
    runCommand(cmd, [this, pending, requestForHandle, finishLeg](const QVariantMap &lookupResp) {
        const QVariantMap body = lookupResp.value(QLatin1String(BODY)).toMap();
        QList<LookupRequest> nextRound;
        for (auto it = body.begin(), end = body.end(); it != end; ++it) {
            const auto requestIt = requestForHandle.constFind(it.key().toInt());
            if (requestIt == requestForHandle.constEnd())
                continue;
            const QString iname = requestIt->iname;
            const QVariantMap resolved = it.value().toMap();
            const auto [type, value] = v8TypeAndValue(resolved);
            const QVariantList properties = resolved.value(QLatin1String("properties")).toList();

            // Re-emitted with what the lookup actually resolved, which is all
            // an unexpanded leaf needed. insertSubItems() overwrites the
            // provisional item the same way, via a second insertItem().
            const int numchild = properties.isEmpty() ? v8ChildCount(resolved)
                                                      : int(properties.size());
            pending->items.addChild(watchItem(iname, requestIt->name, requestIt->exp, type, value,
                                              numchild));

            for (const QVariant &childValue : properties) {
                const QVariantMap child = childValue.toMap();
                const QString childName = child.value(QLatin1String(NAME)).toString();
                // v8's own internal entries, skipped by insertSubItems() too.
                if (childName.isEmpty() || childName.startsWith('.'))
                    continue;
                // exp chaining mirrors insertSubItems(): Array members in
                // brackets, Object members after a dot.
                const QString childExp = value == "Array"
                        ? QString(requestIt->exp + '[' + childName + ']')
                        : QString(requestIt->exp + '.' + childName);
                const QString childIName = iname + '.' + childName;
                const auto [childType, childText] = v8TypeAndValue(child);
                const int childNumChild = v8ChildCount(child);
                pending->items.addChild(watchItem(childIName, childName, childExp, childType,
                                                  childText, childNumChild));

                // A member that is itself an expanded container needs one more
                // round trip for its own members, and a member that arrived
                // with no type at all needs one to show anything but a blank
                // value - the same two reasons insertSubItems() has for its own
                // recursion, and the same pair handleScopeReply() applies a
                // level up. So a deep tree costs one lookup per expanded level,
                // not one per node.
                const int childHandle = child.value(QLatin1String(REF),
                                                    child.value(QLatin1String(HANDLE))).toInt();
                const bool childExpanded = childNumChild > 0
                                           && pending->expandedINames.contains(childIName);
                if (childHandle != 0 && (childType.isEmpty() || childExpanded))
                    nextRound.append({childHandle, childIName, childName, childExp});
            }
        }
        // Added from inside this leg's own callback, i.e. before the
        // finishLeg() below drops this leg - so remaining can't reach zero
        // between the two and cut the traversal short (see refreshLocals()).
        lookupHandles(nextRound, pending, finishLeg);
        finishLeg();
    });
}

// GenericDebuggerEngine's Locals case reads data["data"] and hands it to
// WatchHandler::insertItems(), which is happy with an empty list - but the
// "data" child has to exist for the update to be considered finished.
GdbMi QmlImpl::emptyLocalsData()
{
    GdbMi items;
    items.m_type = GdbMi::List;
    items.m_name = QStringLiteral("data");
    GdbMi all;
    all.m_type = GdbMi::Tuple;
    all.addChild(items);
    return all;
}

// v8's own "scripts" command - every script the JS engine has loaded. Mirrors
// QmlEngine::reloadSourceFiles()'s scripts(4, ...) call: type 4 is "normal"
// scripts (2 would add native ones, which are the interpreter's own internals,
// not the user's files), and no source text, since only the names are wanted
// here.
void QmlImpl::refreshSourceFiles(const RefreshRequest &request)
{
    const quint64 requestId = request.requestId;
    DebuggerCommand cmd(SCRIPTS);
    cmd.arg(TYPES, 4);
    runCommand(cmd, [this, requestId](const QVariantMap &resp) {
        GdbMi files;
        files.m_type = GdbMi::List;
        // A bare list, unlike v8's usual {"body": {...}} - the scripts reply
        // puts the array directly in "body".
        const QVariantList scripts = resp.value(QLatin1String(BODY)).toList();
        for (const QVariant &scriptValue : scripts) {
            const QString name = scriptValue.toMap().value(QLatin1String(NAME)).toString();
            if (name.isEmpty())
                continue;
            GdbMi entry;
            entry.m_type = GdbMi::Tuple;
            // No "fullname": a script name here is an interpreted resource URL
            // ("qrc:/main.qml"), not a path on disk that cleanupFullName()
            // could resolve - GenericDebuggerEngine's own SourceFiles case
            // already treats a missing fullname as an empty FilePath.
            entry.addChild(constMi(QStringLiteral("file"), name));
            files.addChild(entry);
        }
        emit refreshDataReceived(requestId, RefreshKind::SourceFiles, files);
    });
}

// --------------------------------------------------------------------------
// The Inspector tree: the "QmlDebugger" service, i.e. the live QObject tree of
// the running scene rather than any stack frame's variables. Mirrors
// QmlInspectorAgent's protocol half - LIST_ENGINES, then LIST_OBJECTS per
// engine, then FETCH_OBJECT per object the view expands - but reports the
// result as watch items through refreshDataReceived(RefreshKind::InspectorTree)
// instead of inserting into WatchHandler itself, which is
// GenericDebuggerEngine's half of the same job. Its GUI half (the select tool
// and show-app-on-top actions of the separate "QmlInspectorTool" service, plus
// jump-to-definition) is not ported: those need a QQuickWindow app and reach
// straight into Core/the editor, neither of which a backend can or should do.
//
// One of its refinements is deliberately left out: the m_objectStack walk that
// fetches an object's parents one by one until it reaches one already in the
// tree. That exists because real code inserts incrementally, where this rebuilds
// from the engines down every time, so every object here arrives with the
// context tree that owns it and a parent chain is never missing.
//
// Its m_knownDelegateIds bookkeeping, on the other hand, is needed and ported -
// see that member.
// --------------------------------------------------------------------------

// QmlInspectorAgent's own cap on a single tree walk, same value - a malformed
// or pathologically deep scene can't turn one refresh into an endless one.
static const int MaxInspectorTreeNodes = 100000;

// A map or a list has members worth showing; anything else is a leaf. Mirrors
// what QmlInspectorAgent's own insertChildren() returns as a bool, needed as a
// count here because an item carries its child count up front (see
// watchItem()) rather than being appended to after the fact.
static int valueChildCount(const QVariant &value)
{
    if (value.typeId() == QMetaType::QVariantMap)
        return int(value.toMap().size());
    if (value.typeId() == QMetaType::QVariantList)
        return int(value.toList().size());
    return 0;
}

// A property whose value is a map or a list gets its members as items of their
// own, parent before child - mirrors insertChildren(), including its
// valueEditable=false for everything it produces (only the property itself is
// ever assignable, never a member of its value).
static void appendValueChildren(const QString &parentIname, const QVariant &value,
                                int debugId, GdbMi *items)
{
    const auto appendOne = [&](const QString &name, const QVariant &childValue) {
        const QString iname = parentIname + '.' + name;
        GdbMi child = watchItem(iname, name, {}, QLatin1String(childValue.typeName()),
                                childValue.toString(), valueChildCount(childValue), debugId);
        child.addChild(constMi(QStringLiteral("valueeditable"), QStringLiteral("false")));
        items->addChild(child);
        appendValueChildren(iname, childValue, debugId, items);
    };

    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        for (auto it = map.begin(), end = map.end(); it != end; ++it)
            appendOne(it.key(), it.value());
    } else if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        for (int i = 0, end = int(list.size()); i != end; ++i)
            appendOne(QString::number(i), list.at(i));
    }
}

void QmlImpl::runInspectorQuery(quint32 queryId, const InspectorCallback &cb)
{
    // A zero id is the client's own "not connected / nothing sent" answer -
    // there will be no reply to correlate, so registering cb would leak it.
    if (queryId == 0)
        return;
    m_inspectorCallbackForQueryId.insert(queryId, cb);
}

void QmlImpl::addObjectWatch(int debugId)
{
    // Mirrors QmlInspectorAgent::addObjectWatch(), minus its
    // settings().showQmlObjectTree() check - that's a GUI setting, gated
    // engine-side in GenericDebuggerEngine before a refresh is even asked for.
    if (debugId == -1 || m_objectWatches.contains(debugId))
        return;
    if (m_engineClient->addWatch(debugId))
        m_objectWatches.append(debugId);
}

void QmlImpl::appendObjectItems(const QmlDebug::ObjectReference &object,
                                const QString &parentIname, int engineId,
                                const std::shared_ptr<RefreshCollector> &pending,
                                const std::function<void()> &finishLeg)
{
    const int debugId = object.debugId();
    if (!object.isValid())
        return;
    if (pending->items.childCount() > MaxInspectorTreeNodes)
        return;

    const QString iname = parentIname + '.' + QString::number(debugId);
    m_inameForDebugId.insert(debugId, iname);
    m_engineIdForDebugId.insert(debugId, engineId);
    pending->seenDebugIds.insert(debugId);

    // Exactly addWatchData()'s own fallback chain for a displayable name.
    QString name = object.idString();
    if (name.isEmpty())
        name = object.className();
    if (name.isEmpty())
        name = object.name();
    if (name.isEmpty()) {
        const QmlDebug::FileReference file = object.source();
        name = file.url().fileName() + ':' + QString::number(file.lineNumber());
    }
    if (name.isEmpty())
        name = Tr::tr("<anonymous>");

    // Always expandable, whether or not its members are known yet - the same
    // unconditional wantsChildren addWatchData() sets, which is what makes the
    // view ask for them (expandItem() -> another refresh, see
    // RefreshKind::InspectorTree).
    pending->items.addChild(watchItem(iname, name, name, object.className(),
                                     QStringLiteral("object"), 1, debugId));
    addObjectWatch(debugId);

    const bool expanded = pending->expandedINames.contains(iname);
    if (!expanded && object.needsMoreData())
        return; // collapsed and only a stub: nothing more to report yet

    if (expanded && object.needsMoreData()) {
        // A stub the view *has* expanded: its properties and children only
        // exist once fetched, so descend on the reply instead (a leg added from
        // inside another leg's callback, see refreshLocals() on why remaining
        // can't reach zero in between). This is the fetchObject() real
        // QmlInspectorAgent reaches through updateWatchData().
        ++pending->remaining;
        runInspectorQuery(m_engineClient->queryObject(debugId),
                          [this, parentIname, engineId, pending, finishLeg]
                          (const QVariant &value, const QByteArray &) {
            appendObjectItems(qvariant_cast<QmlDebug::ObjectReference>(value), parentIname,
                              engineId, pending, finishLeg);
            finishLeg();
        });
        return;
    }

    const QList<QmlDebug::PropertyReference> properties = object.properties();
    if (!properties.isEmpty()) {
        // One grouping node holding every property, named and shaped exactly as
        // addWatchData() builds it (a "list" of them, hence no type).
        const QString propertiesIName = iname + ".[properties]";
        pending->items.addChild(watchItem(propertiesIName, Tr::tr("Properties"), {}, {},
                                         QStringLiteral("list"), int(properties.size()),
                                         debugId));
        for (const QmlDebug::PropertyReference &property : properties) {
            const QString propertyName = property.name();
            if (propertyName.isEmpty())
                continue;
            const QString propertyIName = propertiesIName + '.' + propertyName;
            pending->items.addChild(watchItem(propertyIName, propertyName, propertyName,
                                             property.valueTypeName(),
                                             property.value().toString(),
                                             valueChildCount(property.value()), debugId));
            appendValueChildren(propertyIName, property.value(), debugId, &pending->items);
        }
    }

    const QList<QmlDebug::ObjectReference> children = object.children();
    for (const QmlDebug::ObjectReference &child : children)
        appendObjectItems(child, iname, engineId, pending, finishLeg);
}

void QmlImpl::refreshInspectorTree(const RefreshRequest &request)
{
    // Remembered for the unprompted rebuilds below, which have no request of
    // their own to read it from.
    m_expandedInspectorINames = request.expandedINames;

    const quint64 requestId = request.requestId;
    if (!m_engineClient || m_engineClient->state() != QmlDebug::QmlDebugClient::Enabled) {
        // The service isn't there (a target started with
        // "services:V8Debugger" only, say) - an empty tree, not a failure. Same
        // answer QmlInspectorAgent's own isConnected() guards produce.
        emit refreshDataReceived(requestId, RefreshKind::InspectorTree, emptyLocalsData());
        return;
    }

    const auto pending = makeCollector(request);
    pending->remaining = 1; // the engine-list leg
    const auto finishLeg = legFinisher(pending);

    runInspectorQuery(m_engineClient->queryAvailableEngines(),
                      [this, pending, finishLeg](const QVariant &value, const QByteArray &) {
        const auto engines = qvariant_cast<QList<QmlDebug::EngineReference>>(value);
        if (engines.isEmpty()) {
            // See m_engineQueryRetriesLeft: no engine registered *yet*.
            if (m_engineQueryRetriesLeft > 0) {
                --m_engineQueryRetriesLeft;
                QTimer::singleShot(100, this, [this] { rebuildInspectorTree(); });
            }
            finishLeg();
            return;
        }
        m_qmlEngines = engines;
        for (const QmlDebug::EngineReference &engine : engines) {
            const int engineId = engine.debugId();
            QString name = engine.name();
            if (name.isEmpty())
                name = Tr::tr("Engine %1").arg(engineId);
            // The engine itself is the tree's top level, the one iname built
            // from the "inspect" root rather than from a parent's - mirrors
            // verifyAndInsertObjectInTree(ObjectReference(engine.debugId(),
            // name), engine.debugId()).
            const QString iname = "inspect." + QString::number(engineId);
            m_inameForDebugId.insert(engineId, iname);
            m_engineIdForDebugId.insert(engineId, engineId);
            pending->items.addChild(watchItem(iname, name, {}, {},
                                             QStringLiteral("object"), 1, engineId));
            ++pending->remaining;
            runInspectorQuery(m_engineClient->queryRootContexts(engine),
                              [this, pending, finishLeg, iname, engineId]
                              (const QVariant &contextValue, const QByteArray &) {
                // Walk the context tree iteratively, for the same reason
                // updateObjectTree() does (QTCREATORBUG-33434).
                QList<QmlDebug::ContextReference> contexts{
                    qvariant_cast<QmlDebug::ContextReference>(contextValue)};
                int visited = 0;
                while (!contexts.isEmpty()) {
                    const QmlDebug::ContextReference context = contexts.takeLast();
                    const QList<QmlDebug::ObjectReference> objects = context.objects();
                    for (const QmlDebug::ObjectReference &object : objects)
                        appendObjectItems(object, iname, engineId, pending, finishLeg);
                    if (++visited > MaxInspectorTreeNodes)
                        break;
                    contexts.append(context.contexts());
                }
                // Whatever the context tree didn't carry: an object created
                // without a QObject parent is missing from it entirely, so it
                // gets fetched by id and attached under its engine - see
                // m_knownDelegateIds, and verifyAndInsertObjectInTree()'s own
                // "parentId = engineId" for a parentless object. Done here,
                // after the walk, so seenDebugIds is complete for this engine.
                for (auto it = m_knownDelegateIds.cbegin(), end = m_knownDelegateIds.cend();
                     it != end; ++it) {
                    if (it.value() != engineId || pending->seenDebugIds.contains(it.key()))
                        continue;
                    ++pending->remaining;
                    runInspectorQuery(m_engineClient->queryObject(it.key()),
                                      [this, pending, finishLeg, iname, engineId]
                                      (const QVariant &objectValue, const QByteArray &) {
                        appendObjectItems(qvariant_cast<QmlDebug::ObjectReference>(objectValue),
                                          iname, engineId, pending, finishLeg);
                        finishLeg();
                    });
                }
                finishLeg();
            });
        }
        finishLeg();
    });
}

void QmlImpl::rebuildInspectorTree()
{
    // Unprompted: requestId 0, since nothing asked - see
    // RefreshKind::InspectorTree on why a backend may do this at all.
    RefreshRequest request;
    request.kind = RefreshKind::InspectorTree;
    request.expandedINames = m_expandedInspectorINames;
    refreshInspectorTree(request);
}

void QmlImpl::handleObjectCreated(int engineId, int objectId, int parentId)
{
    // Mirrors QmlInspectorAgent::newObject(). An object with no QObject parent
    // is remembered unconditionally, before the engine check below and whether
    // or not any engine is known yet: OBJECT_CREATED for the initial scene
    // arrives while QML is still parsing, ahead of LIST_ENGINES_R, so gating
    // this would drop every initial delegate. See m_knownDelegateIds.
    if (parentId == -1 && objectId != -1)
        m_knownDelegateIds.insert(objectId, engineId);

    // The rebuild itself does wait for a known engine, exactly as real code's
    // own engine loop does - there is nothing to hang a tree off before that.
    for (const QmlDebug::EngineReference &engine : std::as_const(m_qmlEngines)) {
        if (engine.debugId() == engineId) {
            m_objectCreatedTimer->start();
            return;
        }
    }
}

void QmlImpl::handlePropertyValueChanged(int debugId, const QByteArray &name,
                                         const QVariant &value)
{
    // Mirrors QmlInspectorAgent::onValueChanged(): one property item, under the
    // object's own "[properties]" node. Reported as a one-item InspectorTree
    // push rather than mutated in place (that's WatchHandler's side of the
    // job) - which is also why an unknown object is simply ignored here: with
    // no iname for it, there is nothing for the view to merge this into.
    const QString objectIName = m_inameForDebugId.value(debugId);
    if (objectIName.isEmpty())
        return;
    const QString iname = objectIName + ".[properties]." + QString::fromLatin1(name);

    GdbMi items;
    items.m_type = GdbMi::List;
    items.m_name = QStringLiteral("data");
    items.addChild(watchItem(iname, QString::fromLatin1(name), QString::fromLatin1(name), {},
                             value.toString(), valueChildCount(value), debugId));
    appendValueChildren(iname, value, debugId, &items);
    GdbMi all;
    all.m_type = GdbMi::Tuple;
    all.addChild(items);
    emit refreshDataReceived(0, RefreshKind::InspectorTree, all);
}

void QmlImpl::queryObjectExpression(int debugId, const QString &expression)
{
    if (!m_engineClient || m_engineClient->state() != QmlDebug::QmlDebugClient::Enabled) {
        emit message(Tr::tr("The application has to be stopped in a breakpoint in order to "
                            "evaluate expressions."), ConsoleOutput);
        return;
    }
    const int engineId = m_engineIdForDebugId.value(debugId, -1);
    const quint32 queryId = m_engineClient->queryExpressionResult(debugId, expression, engineId);
    if (queryId == 0) {
        emit message(Tr::tr("The application has to be stopped in a breakpoint in order to "
                            "evaluate expressions."), ConsoleOutput);
        return;
    }
    runInspectorQuery(queryId, [this](const QVariant &value, const QByteArray &) {
        // The console, not the log: an evaluation result is what the user asked
        // for, and real QmlEngine prints it with debuggerConsole()->printItem()
        // (see handleExecuteDebuggerCommand()). Same sink as the stopped branch
        // in executeDebuggerCommand(). There is no correlated reply signal for
        // either - this whole method's result is a console line, matching every
        // real engine's own executeDebuggerCommand().
        emit message(value.toString(), ConsoleOutput);
    });
}

void QmlImpl::refresh(const RefreshRequest &request)
{
    if (request.kind == RefreshKind::Locals) {
        refreshLocals(request);
        return;
    }
    if (request.kind == RefreshKind::InspectorTree) {
        refreshInspectorTree(request);
        return;
    }
    if (request.kind == RefreshKind::SourceFiles) {
        refreshSourceFiles(request);
        return;
    }
    if (request.kind != RefreshKind::FullStack)
        return; // the rest deferred - see the class comment.

    const quint64 requestId = request.requestId;
    runCommand({BACKTRACE}, [this, requestId](const QVariantMap &resp) {
        const QVariantMap body = resp.value(QLatin1String(BODY)).toMap();
        const QVariantList v8Frames = body.value(QLatin1String("frames")).toList();

        GdbMi frames;
        frames.m_type = GdbMi::List;
        for (const QVariant &v8FrameVal : v8Frames) {
            const QVariantMap v8Frame = v8FrameVal.toMap();
            const QVariantMap script = v8Frame.value(QLatin1String("script")).toMap();
            QString function = v8Frame.value(QLatin1String(FUNCTION)).toString();
            if (function.isEmpty())
                function = v8Frame.value(QLatin1String("func")).toString();

            GdbMi frame;
            frame.m_type = GdbMi::Tuple;
            frame.addChild(constMi(QLatin1String("level"),
                                   QString::number(v8Frame.value(QLatin1String("index")).toInt())));
            frame.addChild(constMi(QLatin1String(FUNCTION), function));
            frame.addChild(constMi(QLatin1String("file"), script.value(QLatin1String(NAME)).toString()));
            frame.addChild(constMi(QLatin1String(LINE),
                                   QString::number(v8Frame.value(QLatin1String(LINE)).toInt() + 1)));
            // Every frame here is QML/JS by construction (see the class
            // comment) - StackFrame::parseFrame() keys off this to pick
            // QmlLanguage over CppLanguage.
            frame.addChild(constMi(QLatin1String("language"), QStringLiteral("js")));
            frames.addChild(frame);
        }

        GdbMi stack;
        stack.m_type = GdbMi::Tuple;
        stack.addChild(frames);
        // GenericDebuggerEngine's own RefreshKind::FullStack case reads
        // data["stack"]["frames"] - "stack" is the only key looked up on
        // the outer tuple, so it's the only one needed here; frames.m_name
        // set separately, GdbMi keys off child->name(), not position.
        GdbMi &stackFrames = const_cast<GdbMi &>(stack.childAt(0));
        stackFrames.m_name = QStringLiteral("frames");
        stack.m_name = QStringLiteral("stack");

        GdbMi data;
        data.m_type = GdbMi::Tuple;
        data.addChild(stack);
        emit refreshDataReceived(requestId, RefreshKind::FullStack, data);
    });
}

void QmlImpl::activateFrame(int index)
{
    m_currentFrameIndex = index;
}

void QmlImpl::selectThread(const QString &)
{
    // QML has no real thread concept either - same trivial no-op every
    // other backend's own selectThread() is (model bookkeeping belongs to
    // DebuggerEngine/GenericDebuggerEngine, not here).
}

void QmlImpl::executeDebuggerCommand(const QString &command, const WatchItemData &inspectorItem)
{
    // Mirrors QmlEngine::executeDebuggerCommand()'s own two branches. While the
    // VM runs there is no frame to evaluate in, so the expression goes to the
    // Inspector view's selected object instead (see the interface's own comment
    // on inspectorContextId, and queryObjectExpression() below).
    //
    // Not mirrored: real code's unpausedEvaluate branch, which uses
    // V8Debugger's own "evaluate" with a context id when the service reports
    // that capability. QmlImpl never negotiates that capability at all, so it
    // always takes the QmlDebugger route here - the same one real code falls
    // back to.
    if (m_inferiorRunning) {
        if (!inspectorItem.isInspect || inspectorItem.id == -1) {
            emit message(Tr::tr("The application has to be stopped in a breakpoint in order to "
                                "evaluate expressions."), ConsoleOutput);
            return;
        }
        queryObjectExpression(int(inspectorItem.id), command);
        return;
    }
    DebuggerCommand cmd(EVALUATE);
    cmd.arg(EXPRESSION, command);
    cmd.arg(FRAME, m_currentFrameIndex);
    runCommand(cmd, [this](const QVariantMap &resp) {
        const bool success = resp.value(QLatin1String(SUCCESS)).toBool();
        if (!success) {
            emit message(resp.value(QLatin1String(MESSAGE)).toString(), LogError);
            return;
        }
        const QVariantMap body = resp.value(QLatin1String(BODY)).toMap();
        // ConsoleOutput, like the running branch above: real
        // handleExecuteDebuggerCommand() prints the result into the debugger
        // console. (Its failure path builds a console *error* item, which has no
        // channel of its own here - LogError above is the closest this interface
        // offers.)
        emit message(body.value(QLatin1String(VALUE)).toString(), ConsoleOutput);
    });
}

// Not ported this slice - see the class comment (real QmlEngine has no
// equivalent at all; an interpreted JS engine has none of these concepts).
void QmlImpl::setRegisterValue(const QString &, const QString &) {}
void QmlImpl::accessMemory(MemoryOp, quint64, quint64, quint64, const QByteArray &) {}
void QmlImpl::fetchDisassembly(quint64, quint64, const QString &) {}
void QmlImpl::setPeripheralRegisterValue(quint64, quint64) {}
void QmlImpl::watchPoint(quint64, const QPoint &) {}
void QmlImpl::createSnapshot(quint64) {}
// Mirrors QmlEngine::assignValueInDebugger()'s non-inspect branch: v8 has no
// "set variable" command, so the assignment is just an expression evaluated in
// the current frame. The value has to be quoted according to the item's own
// type - a bare string would otherwise be parsed as an identifier.
void QmlImpl::assignValueInDebugger(const WatchItemData &item, const QString &expr,
                                    const QString &value)
{
    QString literal;
    if (item.type == "boolean") {
        literal = (value != "false" && value != "0") ? QStringLiteral("true")
                                                    : QStringLiteral("false");
    } else if (item.type == "number") {
        literal = value;
    } else {
        literal = '"' + QString(value).replace('"', QLatin1String("\\\"")) + '"';
    }
    const QString assignment = expr + " = " + literal + ';';

    // An Inspector item belongs to a live scene object, not to any frame, so it
    // is assigned through the QmlDebugger service - the isInspect() branch of
    // real QmlEngine::assignValueInDebugger(), which routes to
    // QmlInspectorAgent::assignValue() for exactly the same reason. The item's
    // own debug id came along on the item (see watchItem()'s debugId).
    if (item.isInspect) {
        if (item.id != -1)
            queryObjectExpression(int(item.id), assignment);
        return;
    }

    DebuggerCommand cmd(EVALUATE);
    cmd.arg(EXPRESSION, assignment);
    cmd.arg(FRAME, m_currentFrameIndex);
    runCommand(cmd, [this](const QVariantMap &resp) {
        if (!resp.value(QLatin1String(SUCCESS)).toBool()) {
            emit message(resp.value(QLatin1String(MESSAGE)).toString(), LogError);
            return;
        }
        // Real code follows up with updateLocals() so the view reflects the
        // new value; here that refresh is GenericDebuggerEngine's to trigger.
    });
}

} // namespace Debugger::Internal
