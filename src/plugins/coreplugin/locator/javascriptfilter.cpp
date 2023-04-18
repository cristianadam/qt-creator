// Copyright (C) 2018 Andre Hartmann <aha_1980@gmx.de>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "javascriptfilter.h"

#include "../coreplugintr.h"

#include <extensionsystem/pluginmanager.h>

#include <utils/asynctask.h>

#include <QClipboard>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJSEngine>

using namespace Core;
using namespace Core::Internal;
using namespace Utils;

static const char initData[] = R"(
    function abs(x) { return Math.abs(x); }
    function acos(x) { return Math.acos(x); }
    function asin(x) { return Math.asin(x); }
    function atan(x) { return Math.atan(x); }
    function atan2(x, y) { return Math.atan2(x, y); }
    function bin(x) { return '0b' + x.toString(2); }
    function ceil(x) { return Math.ceil(x); }
    function cos(x) { return Math.cos(x); }
    function exp(x) { return Math.exp(x); }
    function e() { return Math.E; }
    function floor(x) { return Math.floor(x); }
    function hex(x) { return '0x' + x.toString(16); }
    function log(x) { return Math.log(x); }
    function max() { return Math.max.apply(null, arguments); }
    function min() { return Math.min.apply(null, arguments); }
    function oct(x) { return '0' + x.toString(8); }
    function pi() { return Math.PI; }
    function pow(x, y) { return Math.pow(x, y); }
    function random() { return Math.random(); }
    function round(x) { return Math.round(x); }
    function sin(x) { return Math.sin(x); }
    function sqrt(x) { return Math.sqrt(x); }
    function tan(x) { return Math.tan(x); }
)";

// Created exclusively in a caller thread.
// Destructed in a caller or separate thread.
// Wrapped with std::shared_ptr when passed from a caller to a separate thread.
class JSEngineThread
{
public:
    // Called exclusively from a caller thread
    void cancel() {
        QMutexLocker locker(&m_mutex);
        QTC_ASSERT(!m_canceled, return);
        m_canceled = true;
        if (m_engine)
            m_engine->setInterrupted(true);
    }
    // Called exclusively from a separate thread
    void run(QPromise<QString> &promise, const QString &evaluateData) {
        {
            QMutexLocker locker(&m_mutex);
            QTC_ASSERT(!m_engine, return);
            if (m_canceled)
                return;
            m_engine.reset(new QJSEngine);
        }
        // The init evaluation may also timeout, when e.g. a new version of init script is wrong.
        const QJSValue initResult = m_engine->evaluate(initData);
        if (initResult.isError() || m_engine->hasError()) {
            QMutexLocker locker(&m_mutex);
            m_engine.reset();
            return;
        }
        const QJSValue result = m_engine->evaluate(evaluateData);
        {
            QMutexLocker locker(&m_mutex);
            if (m_canceled || m_engine->isInterrupted())
                return;
             // TODO: Should we check result.isError() and m_engine->hasError() ?
            promise.addResult(result.toString());
            m_engine.reset();
        }
    }

private:
    QMutex m_mutex;
    bool m_canceled = false;
    // Created and destructed exclusively in a separate thread, inside run()
    std::unique_ptr<QJSEngine> m_engine;
};

class JSEngineTask : public QObject
{
    Q_OBJECT

public:
    ~JSEngineTask();
    void setTimeout(int msecs) { m_timeout = msecs; }
    void setEvaluateData(const QString &data) { m_evaluateData = data; }
    void start();

    bool isRunning() const { return m_watcher.get(); }
    QString result() const { return m_result; }

signals:
    void done(bool success);

private:
    int m_timeout = 0;
    QString m_evaluateData;
    QString m_result;
    std::unique_ptr<QTimer> m_timer;
    std::unique_ptr<QFutureWatcher<QString>> m_watcher;
    std::shared_ptr<JSEngineThread> m_engineThread;
};

JSEngineTask::~JSEngineTask()
{
    if (!isRunning())
        return;

    m_engineThread->cancel();
    ExtensionSystem::PluginManager::futureSynchronizer()->addFuture(m_watcher->future());
}

void JSEngineTask::start()
{
    QTC_ASSERT(!m_watcher, return);
    QTC_ASSERT(!isRunning(), return);

    m_result.clear();
    m_engineThread.reset(new JSEngineThread);
    m_watcher.reset(new QFutureWatcher<QString>);
    connect(m_watcher.get(), &QFutureWatcherBase::finished, this, [this] {
        const bool hasValue = m_watcher->future().resultCount();
        if (hasValue)
            m_result = m_watcher->result();
        m_engineThread.reset();
        m_watcher.release()->deleteLater();
        m_timer.reset();
        emit done(hasValue);
    });
    m_timer.reset();
    if (m_timeout > 0) {
        m_timer.reset(new QTimer);
        m_timer->setSingleShot(true);
        m_timer->setInterval(m_timeout);
        connect(m_timer.get(), &QTimer::timeout, this, [this] {
            m_engineThread->cancel();
            ExtensionSystem::PluginManager::futureSynchronizer()->addFuture(m_watcher->future());
            m_engineThread.reset();
            m_watcher.reset();
            m_timer.release()->deleteLater();
            emit done(false);
        });
        m_timer->start();
    }

    auto runThread = [](QPromise<QString> &promise, const QString &evaluateData,
                        const std::shared_ptr<JSEngineThread> &engineThread) {
        engineThread->run(promise, evaluateData);
    };
    m_watcher->setFuture(Utils::asyncRun(runThread, m_evaluateData, m_engineThread));
}

class JSEngineTaskAdapter : public Tasking::TaskAdapter<JSEngineTask>
{
public:
    JSEngineTaskAdapter() { connect(task(), &JSEngineTask::done, this, &TaskInterface::done); }
    void start() final { task()->start(); }
};

QTC_DECLARE_CUSTOM_TASK(JSEngine, JSEngineTaskAdapter);

namespace Core {
namespace Internal {

JavaScriptFilter::JavaScriptFilter()
{
    setId("JavaScriptFilter");
    setDisplayName(Tr::tr("Evaluate JavaScript"));
    setDescription(Tr::tr("Evaluates arbitrary JavaScript expressions and copies the result."));
    setDefaultIncludedByDefault(false);
    setDefaultShortcutString("=");
    m_abortTimer.setSingleShot(true);
    m_abortTimer.setInterval(1000);
    connect(&m_abortTimer, &QTimer::timeout, this, [this] {
        m_aborted = true;
        if (m_engine)
            m_engine->setInterrupted(true);
    });
}

LocatorMatcherTasks JavaScriptFilter::matchers()
{
    using namespace Tasking;

    TreeStorage<LocatorStorage> storage;

    const auto onGroupSetup = [storage] {
        if (storage->input().trimmed().isEmpty()) {
            LocatorFilterEntry entry;
            entry.displayName = Tr::tr("Type some JavaScript statement...");
            storage->reportOutput({entry});
            return TaskAction::StopWithDone;
        }
        return TaskAction::Continue;
    };

    const auto onSetup = [storage](JSEngineTask &engine) {
        engine.setTimeout(1000);
        engine.setEvaluateData(storage->input());
    };
    const auto onDone = [storage](const JSEngineTask &engine) {
        const auto acceptor = [](const QString &clipboardContents) {
            return [clipboardContents] {
                QGuiApplication::clipboard()->setText(clipboardContents);
                return AcceptResult();
            };
        };
        const QString input = storage->input();
        const QString result = engine.result();
        const QString expression = input + " = " + result;

        LocatorFilterEntry entry;
        entry.displayName = expression;

        LocatorFilterEntry copyResultEntry;
        copyResultEntry.displayName = Tr::tr("Copy to clipboard: %1").arg(result);
        copyResultEntry.acceptor = acceptor(result);

        LocatorFilterEntry copyExpressionEntry;
        copyExpressionEntry.displayName = Tr::tr("Copy to clipboard: %1").arg(expression);
        copyExpressionEntry.acceptor = acceptor(expression);

        storage->reportOutput({entry, copyResultEntry, copyExpressionEntry});
    };
    const auto onError = [storage](const JSEngineTask &engine) {
        Q_UNUSED(engine)
        const QString message = storage->input() + " = " + Tr::tr("Engine aborted after timeout.");
        LocatorFilterEntry entry;
        entry.displayName = message;
        storage->reportOutput({entry});
    };

    const Group root {
        OnGroupSetup(onGroupSetup),
        JSEngine(onSetup, onDone, onError)
    };

    return {{root, storage}};
}

JavaScriptFilter::~JavaScriptFilter() = default;

void JavaScriptFilter::prepareSearch(const QString &entry)
{
    Q_UNUSED(entry)

    if (!m_engine)
        setupEngine();
    m_engine->setInterrupted(false);
    m_aborted = false;
    m_abortTimer.start();
}

QList<LocatorFilterEntry> JavaScriptFilter::matchesFor(
        QFutureInterface<Core::LocatorFilterEntry> &future, const QString &entry)
{
    Q_UNUSED(future)

    QList<LocatorFilterEntry> entries;
    if (entry.trimmed().isEmpty()) {
        LocatorFilterEntry entry;
        entry.displayName = Tr::tr("Reset Engine");
        entry.acceptor = [this] {
            m_engine.reset();
            return AcceptResult();
        };
        entries.append(entry);
    } else {
        // Note, that evaluate may be interrupted from caller thread.
        // In this case m_aborted is set to true.
        const QString result = m_engine->evaluate(entry).toString();
        if (m_aborted) {
            const QString message = entry + " = " + Tr::tr("Engine aborted after timeout.");
            LocatorFilterEntry entry;
            entry.displayName = message;
            entry.acceptor = [] { return AcceptResult(); };
            entries.append(entry);
        } else {
            const auto acceptor = [](const QString &clipboardContents) {
                return [clipboardContents] {
                    QGuiApplication::clipboard()->setText(clipboardContents);
                    return AcceptResult();
                };
            };
            const QString expression = entry + " = " + result;

            LocatorFilterEntry entry;
            entry.displayName = expression;
            entry.acceptor = [] { return AcceptResult(); };
            entries.append(entry);

            LocatorFilterEntry resultEntry;
            resultEntry.displayName = Tr::tr("Copy to clipboard: %1").arg(result);
            resultEntry.acceptor = acceptor(result);
            entries.append(resultEntry);

            LocatorFilterEntry expressionEntry;
            expressionEntry.displayName = Tr::tr("Copy to clipboard: %1").arg(expression);
            expressionEntry.acceptor = acceptor(expression);
            entries.append(expressionEntry);
        }
    }
    return entries;
}

void JavaScriptFilter::setupEngine()
{
    m_engine.reset(new QJSEngine);
    m_engine->evaluate(
                "function abs(x) { return Math.abs(x); }\n"
                "function acos(x) { return Math.acos(x); }\n"
                "function asin(x) { return Math.asin(x); }\n"
                "function atan(x) { return Math.atan(x); }\n"
                "function atan2(x, y) { return Math.atan2(x, y); }\n"
                "function bin(x) { return '0b' + x.toString(2); }\n"
                "function ceil(x) { return Math.ceil(x); }\n"
                "function cos(x) { return Math.cos(x); }\n"
                "function exp(x) { return Math.exp(x); }\n"
                "function e() { return Math.E; }\n"
                "function floor(x) { return Math.floor(x); }\n"
                "function hex(x) { return '0x' + x.toString(16); }\n"
                "function log(x) { return Math.log(x); }\n"
                "function max() { return Math.max.apply(null, arguments); }\n"
                "function min() { return Math.min.apply(null, arguments); }\n"
                "function oct(x) { return '0' + x.toString(8); }\n"
                "function pi() { return Math.PI; }\n"
                "function pow(x, y) { return Math.pow(x, y); }\n"
                "function random() { return Math.random(); }\n"
                "function round(x) { return Math.round(x); }\n"
                "function sin(x) { return Math.sin(x); }\n"
                "function sqrt(x) { return Math.sqrt(x); }\n"
                "function tan(x) { return Math.tan(x); }\n");
}

} // namespace Internal
} // namespace Core

#include "javascriptfilter.moc"
