// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "futuresynchronizer.h"
#include "qtcassert.h"
#include "runextensions.h"
#include "tasktree.h"

#include <QFutureWatcher>
#include <QtConcurrent>

namespace Utils {

QTCREATOR_UTILS_EXPORT QThreadPool *asyncThreadPool();

template <typename...>
struct FutureArgType;

template <typename Arg>
struct FutureArgType<QFuture<Arg>>
{
    using Type = Arg;
};

template <typename...>
struct ConcurrentResultType;

template<typename Function, typename ...Args>
struct ConcurrentResultType<Function, Args...>
{
    using Type = typename FutureArgType<decltype(QtConcurrent::run(
        std::declval<Function>(), std::declval<Args>()...))>::Type;
};

template <typename Function, typename ...Args>
auto asyncWrapper(QThread::Priority priority, Function &&function, Args &&...args)
{
    bool constexpr isMember = std::is_member_function_pointer<std::decay_t<Function>>::value;
    bool constexpr isPromise = QtPrivate::ArgResolver<std::decay_t<Function>>::IsPromise::value;
    using ResultType = typename ConcurrentResultType<Function, Args...>::Type;

    const auto priorityModifier = [](QThread::Priority priority) {
        auto cleanup = qScopeGuard([oldPriority = QThread::currentThread()->priority()] {
            if (oldPriority != QThread::InheritPriority)
                QThread::currentThread()->setPriority(oldPriority);
        });
        if (priority != QThread::InheritPriority)
            QThread::currentThread()->setPriority(priority);
        return cleanup;
    };

    if constexpr (!isPromise) {
        std::decay_t<Function> fun = std::forward<Function>(function);
        std::tuple<std::decay_t<Args>...> tuple{std::forward<Args>(args)...};
        return [=] {
            auto cleanup = priorityModifier(priority);
            return std::apply(fun, tuple);
        };
    } else if constexpr (!isMember) {
        std::decay_t<Function> fun = std::forward<Function>(function);
        std::tuple<std::decay_t<Args>...> tuple{std::forward<Args>(args)...};
        return [=](QPromise<ResultType> &promise) {
            auto cleanup = priorityModifier(priority);
            return std::apply(fun, std::tuple_cat(std::tuple(std::ref(promise)), tuple));
        };
    } else {
        std::decay_t<Function> fun = std::forward<Function>(function);
        std::tuple<std::decay_t<Args>...> tuple{std::forward<Args>(args)...};
        return [=](QPromise<ResultType> &promise) {
            auto cleanup = priorityModifier(priority);
            auto [head, tail] = std::apply([](auto h, auto ...t) {
                return std::pair{std::tuple{h}, std::tuple{t...}};
            }, tuple);
            return std::apply(fun, std::tuple_cat(head, std::tuple(std::ref(promise)), tail));
        };
    }
}

template <typename Function, typename ...Args>
auto asyncRun(QThreadPool *threadPool, QThread::Priority priority,
              Function &&function, Args &&...args)
{
    QThreadPool *pool = threadPool ? threadPool : asyncThreadPool();
    return QtConcurrent::run(pool, asyncWrapper(priority, std::forward<Function>(function),
                                                std::forward<Args>(args)...));
}

template <typename Function, typename ...Args>
auto asyncRun(QThread::Priority priority, Function &&function, Args &&...args)
{
    return asyncRun(nullptr, priority, std::forward<Function>(function),
                    std::forward<Args>(args)...);
}

template <typename Function, typename ...Args>
auto asyncRun(QThreadPool *threadPool, Function &&function, Args &&...args)
{
    return asyncRun(threadPool, QThread::InheritPriority, std::forward<Function>(function),
                    std::forward<Args>(args)...);
}

template <typename Function, typename ...Args>
auto asyncRun(Function &&function, Args &&...args)
{
    return asyncRun(nullptr, QThread::InheritPriority, std::forward<Function>(function),
                    std::forward<Args>(args)...);
}

class QTCREATOR_UTILS_EXPORT AsyncTaskBase : public QObject
{
    Q_OBJECT

signals:
    void started();
    void done();
    void resultReadyAt(int index);
};

template <typename ResultType>
class AsyncTask : public AsyncTaskBase
{
public:
    AsyncTask() {
        connect(&m_watcher, &QFutureWatcherBase::finished, this, &AsyncTaskBase::done);
        connect(&m_watcher, &QFutureWatcherBase::resultReadyAt,
                this, &AsyncTaskBase::resultReadyAt);
    }
    ~AsyncTask()
    {
        if (isDone())
            return;

        m_watcher.cancel();
        if (!m_synchronizer)
            m_watcher.waitForFinished();
    }

    template <typename Function, typename ...Args>
    void setConcurrentCallData(Function &&function, Args &&...args)
    {
        return wrapConcurrent(std::forward<Function>(function), std::forward<Args>(args)...);
    }

    template <typename Function, typename ...Args>
    void setAsyncCallData(const Function &function, const Args &...args)
    {
        m_startHandler = [=] {
            return Utils::runAsync(m_threadPool, m_priority, function, args...);
        };
    }
    void setFutureSynchronizer(FutureSynchronizer *synchorizer) { m_synchronizer = synchorizer; }
    void setThreadPool(QThreadPool *pool) { m_threadPool = pool; }
    void setPriority(QThread::Priority priority) { m_priority = priority; }

    void start()
    {
        QTC_ASSERT(m_startHandler, qWarning("No start handler specified."); return);
        m_watcher.setFuture(m_startHandler());
        emit started();
        if (m_synchronizer)
            m_synchronizer->addFuture(m_watcher.future());
    }

    bool isDone() const { return m_watcher.isFinished(); }
    bool isCanceled() const { return m_watcher.isCanceled(); }

    QFuture<ResultType> future() const { return m_watcher.future(); }
    ResultType result() const { return m_watcher.result(); }
    ResultType resultAt(int index) const { return m_watcher.resultAt(index); }
    QList<ResultType> results() const { return future().results(); }
    bool isResultAvailable() const { return future().resultCount(); }

private:
    template <typename Function, typename ...Args>
    void wrapConcurrent(Function &&function, Args &&...args)
    {
        m_startHandler = [=] {
            return asyncRun(m_threadPool, m_priority, function, args...);
        };
    }

    template <typename Function, typename ...Args>
    void wrapConcurrent(std::reference_wrapper<const Function> &&wrapper, Args &&...args)
    {
        m_startHandler = [=] {
            return asyncRun(m_threadPool, m_priority, std::forward<const Function>(wrapper.get()),
                            args...);
        };
    }

    using StartHandler = std::function<QFuture<ResultType>()>;
    StartHandler m_startHandler;
    FutureSynchronizer *m_synchronizer = nullptr;
    QThreadPool *m_threadPool = nullptr;
    QThread::Priority m_priority = QThread::InheritPriority;
    QFutureWatcher<ResultType> m_watcher;
};

template <typename ResultType>
class AsyncTaskAdapter : public Tasking::TaskAdapter<AsyncTask<ResultType>>
{
public:
    AsyncTaskAdapter() {
        this->connect(this->task(), &AsyncTaskBase::done, this, [this] {
            emit this->done(!this->task()->isCanceled());
        });
    }
    void start() final { this->task()->start(); }
};

} // namespace Utils

QTC_DECLARE_CUSTOM_TEMPLATE_TASK(Async, AsyncTaskAdapter);
