// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mcpsessionstate.h"

#include "debuggerengine.h"
#include "enginemanager.h"
#include "registerhandler.h"
#include "stackhandler.h"
#include "threadshandler.h"
#include "watchhandler.h"

#include <utils/qtcassert.h>

#include <QTimer>

namespace Debugger::Internal {

/*!
    \class Debugger::Internal::McpSessionState
    \internal

    Tracks one debug session on behalf of the MCP tools: which stop the
    inferior is at, and whether the stack, the locals and the registers that
    the engine holds belong to that stop, to the current thread and to the
    current frame.

    Every resumption of the inferior starts a new epoch. Data is current when
    it arrived in the current epoch, for the thread and frame that are
    current now. A stack can arrive before the engine reports the stop, so
    the epoch, not the stop, is what data is matched against.
*/

static int s_nextSessionNumber = 1;

void McpSessionState::startTracking()
{
    QObject::connect(EngineManager::instance(), &EngineManager::engineStateChanged,
                     EngineManager::instance(), [](DebuggerEngine *engine) {
        if (McpSessionState *state = forEngine(engine))
            state->handleStateChange();
    });
}

McpSessionState *McpSessionState::forEngine(DebuggerEngine *engine)
{
    if (!engine)
        return nullptr;
    if (auto state = engine->findChild<McpSessionState *>(QString(), Qt::FindDirectChildrenOnly))
        return state;
    return new McpSessionState(engine);
}

McpSessionState::McpSessionState(DebuggerEngine *engine)
    : QObject(engine)
    , m_engine(engine)
    , m_sessionId(QString("session-%1").arg(s_nextSessionNumber++))
    , m_lastState(engine->state())
{
    if (m_lastState == InferiorStopOk || m_lastState == InferiorUnrunnable)
        m_stopId = 1;

    StackHandler *stack = engine->stackHandler();
    connect(stack, &StackHandler::stackChanged, this, &McpSessionState::handleStackChanged);
    connect(stack, &StackHandler::currentIndexChanged, this, &McpSessionState::changed);

    connect(engine->threadsHandler(), &ThreadsHandler::currentThreadChanged, this, [this] {
        // A stack can arrive before the engine has reported any thread. It
        // belongs to the thread that stopped, which is what becomes current.
        if (m_stackEpoch == m_epoch && m_stackThreadId.isEmpty())
            m_stackThreadId = currentThreadId();
        if (m_localsEpoch == m_epoch && m_localsThreadId.isEmpty())
            m_localsThreadId = currentThreadId();
        emit changed();
    });

    WatchModelBase *watch = engine->watchHandler()->model();
    connect(watch, &WatchModelBase::updateStarted, this, [this] {
        m_localsEpoch = -1;
        emit changed();
    });
    connect(watch, &WatchModelBase::updateAborted, this, [this] { m_localsAborted = true; });
    connect(watch, &WatchModelBase::updateFinished, this, &McpSessionState::handleLocalsFinished);

    connect(engine->registerHandler(), &QAbstractItemModel::layoutChanged, this, [this] {
        if (!m_registersExpected)
            return;
        m_registersExpected = false;
        m_registersEpoch = m_epoch;
        m_registersStackGeneration = m_stackGeneration;
        m_registersFrameLevel = currentFrameLevel();
        emit changed();
    });
}

void McpSessionState::handleStateChange()
{
    QTC_ASSERT(m_engine, return);
    const int newState = m_engine->state();
    const bool wasStopped = m_lastState == InferiorStopOk;
    m_lastState = newState;

    if (wasStopped && (newState == InferiorRunRequested || newState == InferiorRunOk))
        ++m_epoch;
    if (!wasStopped && (newState == InferiorStopOk || newState == InferiorUnrunnable))
        ++m_stopId;
    emit changed();
}

void McpSessionState::handleStackChanged()
{
    m_stackEpoch = m_epoch;
    ++m_stackGeneration;
    m_stackThreadId = currentThreadId();
    emit changed();
}

void McpSessionState::handleLocalsFinished()
{
    const bool aborted = m_localsAborted;
    m_localsAborted = false;
    if (!aborted) {
        m_localsEpoch = m_epoch;
        m_localsStackGeneration = m_stackGeneration;
        m_localsFrameLevel = currentFrameLevel();
        m_localsThreadId = currentThreadId();
    }
    emit changed();
}

bool McpSessionState::isStopped() const
{
    return m_engine
           && (m_engine->state() == InferiorStopOk || m_engine->state() == InferiorUnrunnable);
}

bool McpSessionState::hasEnded() const
{
    if (!m_engine)
        return true;
    switch (m_engine->state()) {
    case EngineSetupFailed:
    case EngineRunFailed:
    case InferiorStopFailed:
    case InferiorShutdownRequested:
    case InferiorShutdownFinished:
    case EngineShutdownRequested:
    case EngineShutdownFinished:
    case DebuggerFinished:
        return true;
    default:
        return false;
    }
}

QString McpSessionState::currentThreadId() const
{
    if (!m_engine)
        return {};
    const Thread thread = m_engine->threadsHandler()->currentThread();
    return thread ? thread->id() : QString();
}

int McpSessionState::currentFrameLevel() const
{
    return m_engine ? m_engine->stackHandler()->currentIndex() : -1;
}

bool McpSessionState::isStackReady() const
{
    return isStopped() && m_stackEpoch == m_epoch && m_stackThreadId == currentThreadId()
           && m_engine->stackHandler()->isContentsValid();
}

bool McpSessionState::isLocalsReady() const
{
    return isStackReady() && m_localsEpoch == m_epoch
           && m_localsStackGeneration == m_stackGeneration
           && m_localsFrameLevel == currentFrameLevel()
           && m_localsThreadId == currentThreadId();
}

bool McpSessionState::isRegistersReady() const
{
    return isStackReady() && m_registersEpoch == m_epoch
           && m_registersStackGeneration == m_stackGeneration
           && m_registersFrameLevel == currentFrameLevel();
}

void McpSessionState::expectRegisters()
{
    m_registersExpected = true;
}

QJsonObject McpSessionState::context() const
{
    QJsonObject result{{"session_id", m_sessionId}, {"stop_id", m_stopId}};
    if (const QString thread = currentThreadId(); !thread.isEmpty())
        result["thread_id"] = thread;
    if (const int level = currentFrameLevel(); level >= 0)
        result["frame_level"] = level;
    return result;
}

QJsonObject McpSessionState::readiness() const
{
    return {{"stack", isStackReady()},
            {"locals", isLocalsReady()}};
}

class Waiter final : public QObject
{
public:
    Waiter(McpSessionState *state,
           int timeoutMs,
           const std::function<bool()> &ready,
           const std::function<void(WaitOutcome)> &done)
        : m_state(state)
        , m_ready(ready)
        , m_done(done)
    {
        connect(state, &McpSessionState::changed, this, &Waiter::check);
        connect(state, &QObject::destroyed, this, [this] { finish(WaitOutcome::SessionEnded); });
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] { finish(WaitOutcome::TimedOut); });
        m_timer.start(timeoutMs);
    }

    void check()
    {
        if (!m_state)
            finish(WaitOutcome::SessionEnded);
        else if (m_ready())
            finish(WaitOutcome::Ready);
        else if (m_state->hasEnded())
            finish(WaitOutcome::SessionEnded);
    }

private:
    void finish(WaitOutcome outcome)
    {
        if (m_finished)
            return;
        m_finished = true;
        m_timer.stop();
        const std::function<void(WaitOutcome)> done = m_done;
        deleteLater();
        done(outcome);
    }

    QPointer<McpSessionState> m_state;
    std::function<bool()> m_ready;
    std::function<void(WaitOutcome)> m_done;
    QTimer m_timer;
    bool m_finished = false;
};

/*!
    \internal

    Calls \a done once \a ready holds for \a state, when the session ends, or
    when \a timeoutMs have passed, whichever comes first. \a ready is
    evaluated right away and then whenever \a state may have changed, so no
    polling is involved.
*/
void waitUntil(McpSessionState *state,
               int timeoutMs,
               const std::function<bool()> &ready,
               const std::function<void(WaitOutcome)> &done)
{
    if (!state) {
        done(WaitOutcome::SessionEnded);
        return;
    }
    if (ready()) {
        done(WaitOutcome::Ready);
        return;
    }
    if (state->hasEnded()) {
        done(WaitOutcome::SessionEnded);
        return;
    }
    new Waiter(state, timeoutMs, ready, done);
}

} // namespace Debugger::Internal
