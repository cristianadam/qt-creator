// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "tasktree.h"

#include "guard.h"
#include "qtcassert.h"

namespace Utils {
namespace Tasking {

ExecuteInSequence sequential;
ExecuteInParallel parallel;
WorkflowPolicy stopOnError(TaskBase::WorkflowPolicy::StopOnError);
WorkflowPolicy continueOnError(TaskBase::WorkflowPolicy::ContinueOnError);
WorkflowPolicy stopOnDone(TaskBase::WorkflowPolicy::StopOnDone);
WorkflowPolicy continueOnDone(TaskBase::WorkflowPolicy::ContinueOnDone);
WorkflowPolicy optional(TaskBase::WorkflowPolicy::Optional);

void TaskBase::addChildren(const QList<TaskBase> &children)
{
    QTC_ASSERT(m_type == Type::Task, qWarning("Only Task may have children, skipping..."); return);
    for (const TaskBase &child : children) {
        switch (child.m_type) {
        case Type::Task:
            m_children.append(child);
            break;
        case Type::Mode:
            QTC_ASSERT(m_type == Type::Task,
                       qWarning("Mode may only be a child of Task, skipping..."); return);
            m_executeMode = child.m_executeMode; // TODO: Assert on redefinition?
            break;
        case Type::Policy:
            QTC_ASSERT(m_type == Type::Task,
                       qWarning("Mode may only be a child of Task, skipping..."); return);
            m_workflowPolicy = child.m_workflowPolicy; // TODO: Assert on redefinition?
            break;
        case Type::TaskHandler:
            QTC_ASSERT(child.m_taskHandler.m_createHandler,
                       qWarning("Task Create Handler can't be null, skipping..."); return);
            QTC_ASSERT(child.m_taskHandler.m_setupHandler,
                       qWarning("Task Setup Handler can't be null, skipping..."); return);
            m_children.append(child);
            break;
        case Type::SubTreeSetupHandler:
            QTC_ASSERT(m_type == Type::Task, qWarning("Sub Tree Setup Handler may only be a "
                       "child of Task, skipping..."); break);
            QTC_ASSERT(!m_subTreeSetupHandler, qWarning("Sub Tree Setup Handler redefinition, "
                                                       "overriding..."));
            m_subTreeSetupHandler = child.m_subTreeSetupHandler;
            break;
        case Type::SubTreeDoneHandler:
            QTC_ASSERT(m_type == Type::Task, qWarning("Sub Tree Done Handler may only be a "
                       "child of Task, skipping..."); break);
            QTC_ASSERT(!m_subTreeDoneHandler, qWarning("Sub Tree Done Handler redefinition, "
                                                       "overriding..."));
            m_subTreeDoneHandler = child.m_subTreeDoneHandler;
            break;
        case Type::SubTreeErrorHandler:
            QTC_ASSERT(m_type == Type::Task, qWarning("Sub Tree Error Handler may only be a "
                       "child of Task, skipping..."); break);
            QTC_ASSERT(!m_subTreeErrorHandler, qWarning("Sub Tree Error Handler redefinition, "
                                                        "overriding..."));
            m_subTreeErrorHandler = child.m_subTreeErrorHandler;
            break;
        }
    }
    if (m_type == Type::Task) {
        // TODO: Wrong case: !m_setupHandler && m_doneHandler
//        QTC_ASSERT(m_taskSetupHandler || !m_taskDoneHandler, qWarning("Specifying Done Handler "
//                                                              "without Setup Handler is no-op"));
    }
}

} // namespace Tasking

using namespace Tasking;

class TaskTreePrivate;
class TaskNode;

class TaskContainer
{
public:
    TaskContainer(TaskTreePrivate *taskTreePrivate, TaskContainer *parentContainer,
                 const TaskBase &task);
    ~TaskContainer();
    void start();
    void stop();
    bool isRunning() const;
    void childDone(bool success);
    void invokeSubTreeHandler(bool success);
    void resetSuccessBit();
    void updateSuccessBit(bool success);

    TaskTreePrivate *m_taskTreePrivate = nullptr;
    TaskContainer *m_parentContainer = nullptr;
    const TaskBase::ExecuteMode m_executeMode = TaskBase::ExecuteMode::Parallel;
    TaskBase::WorkflowPolicy m_workflowPolicy = TaskBase::WorkflowPolicy::StopOnError;
    const TaskBase::SubTreeHandler m_subTreeSetupHandler;
    const TaskBase::SubTreeHandler m_subTreeDoneHandler;
    const TaskBase::SubTreeHandler m_subTreeErrorHandler;
    QList<TaskNode *> m_children;
    int m_currentIndex = -1;
    bool m_successBit = true;
};

class TaskNode : public QObject
{
public:
    TaskNode(TaskTreePrivate *taskTreePrivate, TaskContainer *parentContainer,
            const TaskBase &task)
        : m_taskHandler(task.taskHandler())
        , m_container(taskTreePrivate, parentContainer, task)
    {
    }

    bool start();
    void stop();
    bool isRunning();

private:
    const TaskBase::TaskHandler m_taskHandler;
    TaskContainer m_container;
    std::unique_ptr<TaskInterface> m_task;
};

class TaskTreePrivate
{
public:
    TaskTreePrivate(TaskTree *taskTree, const Task &root)
        : q(taskTree)
        , m_root(this, nullptr, root) {}

    void emitDone() {
        GuardLocker locker(m_guard);
        emit q->done();
    }
    void emitError() {
        GuardLocker locker(m_guard);
        emit q->errorOccurred();
    }

    TaskTree *q = nullptr;
    TaskNode m_root;
    Guard m_guard;
};

TaskContainer::TaskContainer(TaskTreePrivate *taskTreePrivate, TaskContainer *parentContainer,
                           const TaskBase &task)
    : m_taskTreePrivate(taskTreePrivate)
    , m_parentContainer(parentContainer)
    , m_executeMode(task.executeMode())
    , m_workflowPolicy(task.workflowPolicy())
    , m_subTreeSetupHandler(task.subTreeSetupHandler())
    , m_subTreeDoneHandler(task.subTreeDoneHandler())
    , m_subTreeErrorHandler(task.subTreeErrorHandler())
{
    const QList<TaskBase> &children = task.children();
    for (const TaskBase &child : children)
        m_children.append(new TaskNode(m_taskTreePrivate, this, child));
}

TaskContainer::~TaskContainer()
{
    qDeleteAll(m_children);
}

void TaskContainer::start()
{
    if (m_subTreeSetupHandler) {
        GuardLocker locker(m_taskTreePrivate->m_guard);
        m_subTreeSetupHandler();
    }

    if (m_children.isEmpty()) {
        invokeSubTreeHandler(true);
        return;
    }

    m_currentIndex = 0;
    resetSuccessBit();

    if (m_executeMode == TaskBase::ExecuteMode::Sequential) {
        m_children.at(m_currentIndex)->start();
        return;
    }

    // Parallel case
    for (TaskNode *child : std::as_const(m_children)) {
        if (!child->start())
            return;
    }
}

void TaskContainer::stop()
{
    m_currentIndex = -1;
    for (TaskNode *child : std::as_const(m_children))
        child->stop();
}

bool TaskContainer::isRunning() const
{
    return m_currentIndex >= 0;
}

void TaskContainer::childDone(bool success)
{
    if ((m_workflowPolicy == TaskBase::WorkflowPolicy::StopOnDone && success)
            || (m_workflowPolicy == TaskBase::WorkflowPolicy::StopOnError && !success)) {
        stop();
        invokeSubTreeHandler(success);
        return;
    }

    ++m_currentIndex;
    updateSuccessBit(success);

    if (m_currentIndex == m_children.size()) {
        invokeSubTreeHandler(m_successBit);
        return;
    }

    if (m_executeMode == TaskBase::ExecuteMode::Sequential)
        m_children.at(m_currentIndex)->start();
}

void TaskContainer::invokeSubTreeHandler(bool success)
{
    m_currentIndex = -1;
    m_successBit = success;
    if (success && m_subTreeDoneHandler) {
        GuardLocker locker(m_taskTreePrivate->m_guard);
        m_subTreeDoneHandler();
    } else if (!success && m_subTreeErrorHandler) {
        GuardLocker locker(m_taskTreePrivate->m_guard);
        m_subTreeErrorHandler();
    }
    if (m_parentContainer) {
        m_parentContainer->childDone(success);
        return;
    }
    if (success)
        m_taskTreePrivate->emitDone();
    else
        m_taskTreePrivate->emitError();
}

void TaskContainer::resetSuccessBit()
{
    if (m_children.isEmpty())
        m_successBit = true;

    if (m_workflowPolicy == TaskBase::WorkflowPolicy::StopOnDone
            || m_workflowPolicy == TaskBase::WorkflowPolicy::ContinueOnDone) {
        m_successBit = false;
    } else {
        m_successBit = true;
    }
}

void TaskContainer::updateSuccessBit(bool success)
{
    if (m_workflowPolicy == TaskBase::WorkflowPolicy::Optional)
        return;
    if (m_workflowPolicy == TaskBase::WorkflowPolicy::StopOnDone
            || m_workflowPolicy == TaskBase::WorkflowPolicy::ContinueOnDone) {
        m_successBit = m_successBit || success;
    } else {
        m_successBit = m_successBit && success;
    }
}


bool TaskNode::start()
{
    if (!m_taskHandler.m_createHandler || !m_taskHandler.m_setupHandler) {
        m_container.start();
        return true;
    }
    m_task.reset(m_taskHandler.m_createHandler());
    {
        GuardLocker locker(m_container.m_taskTreePrivate->m_guard);
        m_taskHandler.m_setupHandler(*m_task.get());
    }
    connect(m_task.get(), &TaskInterface::done, this, [this](bool success) {
        if (success && m_taskHandler.m_doneHandler) {
            GuardLocker locker(m_container.m_taskTreePrivate->m_guard);
            m_taskHandler.m_doneHandler(*m_task.get());
        } else if (!success && m_taskHandler.m_errorHandler) {
            GuardLocker locker(m_container.m_taskTreePrivate->m_guard);
            m_taskHandler.m_errorHandler(*m_task.get());
        }

        m_task.release()->deleteLater();

        QTC_CHECK(m_container.m_parentContainer);
        m_container.m_parentContainer->childDone(success);
    });

    m_task->start();
    return m_task.get(); // In case of failed to start, done handler already released process
}

void TaskNode::stop()
{
    m_task.reset();
    m_container.stop();
}

bool TaskNode::isRunning()
{
    return m_task || m_container.isRunning();
}

/*!
    \class Utils::TaskTree

    \brief The TaskTree class is responsible for runnig process tree structure defined in a
           declarative way.

    The Tasking namespace (similar to Layouting) is designer for building declarative process
    tree structure. It's possible to form sophisticated mixtures of parallel or sequential
    processing.

    The TaskTree class is able to run processes in parallel, sequentially or in a combined way.

    Instead of creating QtcProcesses directly by the user, he just specifies how to
    setup the process (OnSetup handlers) and how to collect output data from the successfully
    finished process (OnDone handlers). The processes themselves are created and managed by
    TaskTree class.

    After each process finishes successfully the TaskTree continues to run its child processes
    (either in parallel or sequentially), depending on Execute policy. In this way a child process
    has a chance to take the output data received by a parent process into account when it's being
    started (since it's guaranteed that parent's OnDone handler is called before child's OnSetup
    handler). The same happens with sequential children - it's guaranteed that children will run
    in chain, i.e. next child's OnSetup handler will be called after previous child's OnDone
    handler was already called.

    When all processes in TaskTree have finished their execution (including nested processes
    and processes run in parallel) the top level OnSubTreeDone handler is being called.

    Whenever any process doesn't finish successfully, the top level OnSubTreeError handler
    is being called and TaskTree stops execution.
*/

TaskTree::TaskTree(const Task &root)
    : d(new TaskTreePrivate(this, root))
{
}

TaskTree::~TaskTree()
{
    QTC_ASSERT(!d->m_guard.isLocked(), qWarning("Deleting TaskTree instance directly from "
               "one of its handlers will lead to crash!"));
    delete d;
}

void TaskTree::start()
{
    QTC_ASSERT(!isRunning(), qWarning("The TaskTree is already running, ignoring..."); return);
    QTC_ASSERT(!d->m_guard.isLocked(), qWarning("The start() is called from one of the"
                                                "TaskTree handlers, ingoring..."); return);
    d->m_root.start();
}

void TaskTree::stop()
{
    QTC_ASSERT(!d->m_guard.isLocked(), qWarning("The stop() is called from one of the"
                                                "TaskTree handlers, ingoring..."); return);
    d->m_root.stop();
}

bool TaskTree::isRunning() const
{
    return d->m_root.isRunning();
}

} // namespace Utils
