// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <QObject>

namespace Utils {
namespace Tasking {

class QTCREATOR_UTILS_EXPORT TaskInterface : public QObject
{
    Q_OBJECT

public:
    TaskInterface() = default;
    virtual void start() = 0;

signals:
    void done(bool success);
};

class QTCREATOR_UTILS_EXPORT TaskBase
{
public:
    // Internal, provided by QTC_DECLARE_CUSTOM_TASK
    using TaskCreateHandler = std::function<TaskInterface *(void)>;
    // Called prior to task start, just after createHandler
    using TaskSetupHandler = std::function<void(TaskInterface &)>;
    // Called on task done / error
    using TaskEndHandler = std::function<void(const TaskInterface &)>;
    // Called when sub tree entered / after sub three ended with success or failure
    using SubTreeHandler = std::function<void()>;

    struct TaskHandler {
        TaskCreateHandler m_createHandler;
        TaskSetupHandler m_setupHandler;
        TaskEndHandler m_doneHandler;
        TaskEndHandler m_errorHandler;
    }; // TODO: Make it protected

    enum class ExecuteMode {
        Parallel, // default
        Sequential
    };

    // 4 policies:
    // 1. When all children finished with done -> report done, otherwise:
    //    a) Report error on first error and stop executing other children (including their subtree)
    //    b) On first error - wait for all children to be finished and report error afterwards
    // 2. When all children finished with error -> report error, otherwise:
    //    a) Report done on first done and stop executing other children (including their subtree)
    //    b) On first done - wait for all children to be finished and report done afterwards

    enum class WorkflowPolicy {
        StopOnError,     // 1a - Will report error on any child error, otherwise done (if all children were done)
        ContinueOnError, // 1b - the same. When no children it reports done.
        StopOnDone,      // 2a - Will report done on any child done, otherwise error (if all children were error)
        ContinueOnDone,  // 2b - the same. When no children it reports done. (?)
        Optional         // Returns always done after all children finished
    };

    ExecuteMode executeMode() const { return m_executeMode; }
    WorkflowPolicy workflowPolicy() const { return m_workflowPolicy; }
    TaskHandler taskHandler() const { return m_taskHandler; }

    SubTreeHandler subTreeSetupHandler() const { return m_subTreeSetupHandler; }
    SubTreeHandler subTreeDoneHandler() const { return m_subTreeDoneHandler; }
    SubTreeHandler subTreeErrorHandler() const { return m_subTreeErrorHandler; }
    QList<TaskBase> children() const { return m_children; } // TODO: maybe QList<Task> ?

protected:
    enum class Type {
        Task,
        Mode,
        Policy,
        TaskHandler,
        SubTreeSetupHandler,
        SubTreeDoneHandler,
        SubTreeErrorHandler
        // TODO: Add Cond type (with CondHandler and True and False branches)?
    };
    enum class SubTreeHandlerType {
        Setup,
        Done,
        Error
    };

    static Type subTreeHandlerTypeToType(SubTreeHandlerType type) {
        switch (type) {
        case SubTreeHandlerType::Setup: return Type::SubTreeSetupHandler;
        case SubTreeHandlerType::Done: return Type::SubTreeDoneHandler;
        case SubTreeHandlerType::Error: return Type::SubTreeErrorHandler;
        }
        return Type::SubTreeDoneHandler;
    }

    TaskBase() = default;
    TaskBase(ExecuteMode mode)
        : m_type(Type::Mode)
        , m_executeMode(mode) {}
    TaskBase(WorkflowPolicy policy)
        : m_type(Type::Policy)
        , m_workflowPolicy(policy) {}
    TaskBase(const TaskHandler &taskHandler)
        : m_type(Type::TaskHandler)
        , m_taskHandler(taskHandler) {}
    TaskBase(SubTreeHandlerType type, const SubTreeHandler &handler)
        : m_type(subTreeHandlerTypeToType(type))
    {
        if (type == SubTreeHandlerType::Setup)
            m_subTreeSetupHandler = handler;
        else if (type == SubTreeHandlerType::Done)
            m_subTreeDoneHandler = handler;
        else if (type == SubTreeHandlerType::Error)
            m_subTreeErrorHandler = handler;
    }
    void addChildren(const QList<TaskBase> &children);

private:
    Type m_type = Type::Task;
    ExecuteMode m_executeMode = ExecuteMode::Sequential;
    WorkflowPolicy m_workflowPolicy = WorkflowPolicy::StopOnError;
    TaskHandler m_taskHandler;

    SubTreeHandler m_subTreeSetupHandler;
    SubTreeHandler m_subTreeDoneHandler;
    SubTreeHandler m_subTreeErrorHandler;

    QList<TaskBase> m_children;
};

class QTCREATOR_UTILS_EXPORT Task : public TaskBase
{
public:
    Task(const QList<TaskBase> &children) { addChildren(children); }
    Task(std::initializer_list<TaskBase> children)  { addChildren(children); }
};

class QTCREATOR_UTILS_EXPORT ExecuteInSequence : public TaskBase
{
public:
    ExecuteInSequence() : TaskBase(ExecuteMode::Sequential) {}
};

class QTCREATOR_UTILS_EXPORT ExecuteInParallel : public TaskBase
{
public:
    ExecuteInParallel() : TaskBase(ExecuteMode::Parallel) {}
};

class QTCREATOR_UTILS_EXPORT WorkflowPolicy : public TaskBase
{
public:
    WorkflowPolicy(TaskBase::WorkflowPolicy policy) : TaskBase(policy) {}
};

class QTCREATOR_UTILS_EXPORT OnSubTreeSetup : public TaskBase
{
public:
    OnSubTreeSetup(const SubTreeHandler &handler) : TaskBase(SubTreeHandlerType::Setup, handler) {}
};

class QTCREATOR_UTILS_EXPORT OnSubTreeDone : public TaskBase
{
public:
    OnSubTreeDone(const SubTreeHandler &handler) : TaskBase(SubTreeHandlerType::Done, handler) {}
};

class QTCREATOR_UTILS_EXPORT OnSubTreeError : public TaskBase
{
public:
    OnSubTreeError(const SubTreeHandler &handler) : TaskBase(SubTreeHandlerType::Error, handler) {}
};

// TODO: Add Conditional node and a handler OnCond that evaluates to true.
//       If the result is true, the True process is being run, otherwise False process
//       (and their respective possible subtrees).

QTCREATOR_UTILS_EXPORT extern ExecuteInSequence sequential;
QTCREATOR_UTILS_EXPORT extern ExecuteInParallel parallel;
QTCREATOR_UTILS_EXPORT extern WorkflowPolicy stopOnError;
QTCREATOR_UTILS_EXPORT extern WorkflowPolicy continueOnError;
QTCREATOR_UTILS_EXPORT extern WorkflowPolicy stopOnDone;
QTCREATOR_UTILS_EXPORT extern WorkflowPolicy continueOnDone;
QTCREATOR_UTILS_EXPORT extern WorkflowPolicy optional;

template <typename Task>
class TaskAdapter : public TaskInterface
{
public:
    using Type = Task;
    TaskAdapter() = default;
    Task *task() { return &m_task; }
    const Task *task() const { return &m_task; }
private:
    Task m_task;
};

template <typename Task>
class TaskCreator {};

template <typename Adapter>
class CustomTask : public TaskBase
{
public:
    using Task = typename Adapter::Type;
    using SetupHandler = std::function<void(Task &)>;
    using EndHandler = std::function<void(const Task &)>;
    static Adapter *createAdapter() { return new Adapter; }
    CustomTask(const SetupHandler &setup, const EndHandler &done = {}, const EndHandler &error = {})
        : TaskBase({&createAdapter, wrapSetup(setup),
                    wrapEnd(done), wrapEnd(error)}) {}

private:
    static TaskSetupHandler wrapSetup(SetupHandler handler) {
        if (!handler)
            return {};
        return [handler](TaskInterface &taskInterface) {
            Adapter &adapter = static_cast<Adapter &>(taskInterface);
            handler(*adapter.task());
        };
    };
    static TaskEndHandler wrapEnd(EndHandler handler) {
        if (!handler)
            return {};
        return [handler](const TaskInterface &taskInterface) {
            const Adapter &adapter = static_cast<const Adapter &>(taskInterface);
            handler(*adapter.task());
        };
    };
};

} // namespace Tasking

class TaskTreePrivate;

class QTCREATOR_UTILS_EXPORT TaskTree : public QObject
{
    Q_OBJECT

public:
    TaskTree(const Tasking::Task &root);
    ~TaskTree();

    void start();
    void stop();
    bool isRunning() const;

signals:
    void done();
    void errorOccurred();

private:
    TaskTreePrivate *d;
};

} // namespace Utils

#define QTC_DECLARE_CUSTOM_TASK(CustomTaskName, TaskAdapterClass)\
namespace Utils::Tasking { using CustomTaskName = CustomTask<TaskAdapterClass>; }

#define QTC_DECLARE_CUSTOM_TEMPLATE_TASK(CustomTaskName, TaskAdapterClass)\
namespace Utils::Tasking {\
template <typename ...Args>\
using CustomTaskName = CustomTask<TaskAdapterClass<Args...>>;\
} // namespace Utils::Tasking

