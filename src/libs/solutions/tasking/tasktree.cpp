// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "tasktree.h"

#include <QDebug>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QPromise>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <atomic>

using namespace std::chrono;

namespace Tasking {

// That's cut down qtcassert.{c,h} to avoid the dependency.
#define QT_STRING(cond) qDebug("SOFT ASSERT: \"%s\" in %s: %s", cond,  __FILE__, QT_STRINGIFY(__LINE__))
#define QT_ASSERT(cond, action) if (Q_LIKELY(cond)) {} else { QT_STRING(#cond); action; } do {} while (0)
#define QT_CHECK(cond) if (cond) {} else { QT_STRING(#cond); } do {} while (0)

class Guard
{
    Q_DISABLE_COPY(Guard)
public:
    Guard() = default;
    ~Guard() { QT_CHECK(m_lockCount == 0); }
    bool isLocked() const { return m_lockCount; }
private:
    int m_lockCount = 0;
    friend class GuardLocker;
};

class GuardLocker
{
    Q_DISABLE_COPY(GuardLocker)
public:
    GuardLocker(Guard &guard) : m_guard(guard) { ++m_guard.m_lockCount; }
    ~GuardLocker() { --m_guard.m_lockCount; }
private:
    Guard &m_guard;
};

/*!
    \module TaskingSolution
    \title Tasking Solution
    \ingroup solutions-modules
    \brief Contains a general purpose Tasking solution.

    The Tasking solution depends on Qt only, and doesn't depend on any \QC specific code.
*/

/*!
    \namespace Tasking
    \inmodule TaskingSolution
    \brief The Tasking namespace encloses all classes and global functions of the Tasking solution.
*/

/*!
    \class Tasking::TaskInterface
    \inheaderfile solutions/tasking/tasktree.h
    \inmodule TaskingSolution
    \brief TaskInterface is the abstract base class for implementing custom task adapters.

    To implement a custom task adapter, derive your adapter from the
    \c TaskAdapter<Task> class template. TaskAdapter automatically creates and destroys
    the custom task instance and associates the adapter with a given \c Task type.
*/

/*!
    \fn virtual void TaskInterface::start()

    This method is called by the running TaskTree for starting the \c Task instance.
    Reimplement this method in \c TaskAdapter<Task>'s subclass in order to start the
    associated task.

    Use TaskAdapter::task() to access the associated \c Task instance.

    \sa done(), TaskAdapter::task()
*/

/*!
    \fn void TaskInterface::done(DoneResult result)

    Emit this signal from the \c TaskAdapter<Task>'s subclass, when the \c Task is finished.
    Pass \c DoneResult::Success as a \a result argument when the task finishes with success;
    otherwise, when an error occurs, pass \c DoneResult::Error.
*/

/*!
    \class Tasking::TaskAdapter
    \inheaderfile solutions/tasking/tasktree.h
    \inmodule TaskingSolution
    \brief A class template for implementing custom task adapters.

    The TaskAdapter class template is responsible for creating a task of the \c Task type,
    starting it, and reporting success or an error when the task is finished.
    It also associates the adapter with a given \c Task type.

    Reimplement this class with the actual \c Task type to adapt the task's interface
    into the general TaskTree's interface for managing the \c Task instances.

    Each subclass needs to provide a public default constructor,
    implement the start() method, and emit the done() signal when the task is finished.
    Use task() to access the associated \c Task instance.

    To use your task adapter inside the task tree, create an alias to the
    Tasking::CustomTask template passing your task adapter as a template parameter:
    \code
        // Defines actual worker
        class Worker {...};

        // Adapts Worker's interface to work with task tree
        class WorkerTaskAdapter : public TaskAdapter<Worker> {...};

        // Defines WorkerTask as a new task tree element
        using WorkerTask = CustomTask<WorkerTaskAdapter>;
    \endcode

    For more information on implementing the custom task adapters, refer to \l {Task Adapters}.

    \sa start(), done(), task()
*/

/*!
    \typealias TaskAdapter::Type

    Type alias for the \c Task type.
*/

/*!
    \fn template <typename Task> TaskAdapter<Task>::TaskAdapter<Task>()

    Creates a task adapter for the given \c Task type. Internally, it creates
    an instance of \c Task, which is accessible via the task() method.

    \sa task()
*/

/*!
    \fn template <typename Task> Task *TaskAdapter<Task>::task()

    Returns the pointer to the associated \c Task instance.
*/

/*!
    \fn template <typename Task> Task *TaskAdapter<Task>::task() const
    \overload

    Returns the const pointer to the associated \c Task instance.
*/

/*!
    \class Tasking::GroupItem
    \inheaderfile solutions/tasking/tasktree.h
    \inmodule TaskingSolution
    \brief GroupItem represents the basic element that may be a part of any
           \l {Tasking::Group} {Group}.

    GroupItem is a basic element that may be a part of any \l {Tasking::Group} {Group}.
    It encapsulates the functionality provided by any GroupItem's subclass.
    It is a value type and it is safe to copy the GroupItem instance,
    even when it is originally created via the subclass' constructor.

    There are four main kinds of GroupItem:
    \table
    \header
        \li GroupItem Kind
        \li Brief Description
    \row
        \li \l CustomTask
        \li Defines asynchronous task type and task's start, done, and error handlers.
            Aliased with a unique task name, such as, \c ConcurrentCallTask<ResultType>
            or \l NetworkQueryTask. Asynchronous tasks are the main reason for using a task tree.
    \row
        \li \l Group
        \li A container for other group items. Since the group is of the GroupItem type,
            it's possible to nest it inside another group. The group is seen by its parent
            as a single asynchronous task.
    \row
        \li \l Storage
        \li Enables the child tasks of a group to exchange data.
            When Storage is placed inside a group, the task tree instantiates
            the storage object just before the group is entered,
            and destroys it just after the group is finished.
    \row
        \li Other group control items
        \li The items returned by \l {Tasking::parallelLimit()} {parallelLimit()} or
            \l {Tasking::workflowPolicy()} {workflowPolicy()} influence the group's behavior.
            The items returned by \l {Tasking::onGroupSetup()} {onGroupSetup()},
            \l {Tasking::onGroupDone()} {onGroupDone()} or
            \l {Tasking::onGroupError()} {onGroupError()} define custom handlers called when
            the group starts or ends execution.
    \endtable
*/

/*!
    \class Tasking::Group
    \inheaderfile solutions/tasking/tasktree.h
    \inmodule TaskingSolution
    \brief Group represents the basic element for composing declarative recipes describing
           how to execute and handle a nested tree of asynchronous tasks.

    Group is a container for other group items. It encloses child tasks into one unit,
    which is seen by the group's parent as a single, asynchronous task.
    Since Group is of the GroupItem type, it may also be a child of Group.

    Insert child tasks into the group by using aliased custom task names, such as,
    \c ConcurrentCallTask<ResultType> or \c NetworkQueryTask:

    \code
        const Group group {
            NetworkQueryTask(...),
            ConcurrentCallTask<int>(...)
        };
    \endcode

    The group's behavior may be customized by inserting the items returned by
    \l {Tasking::parallelLimit()} {parallelLimit()} or
    \l {Tasking::workflowPolicy()} {workflowPolicy()} functions:

    \code
        const Group group {
            parallel,
            continueOnError,
            NetworkQueryTask(...),
            NetworkQueryTask(...)
        };
    \endcode

    The group may contain nested groups:

    \code
        const Group group {
            finishAllAndSuccess,
            NetworkQueryTask(...),
            Group {
                NetworkQueryTask(...),
                Group {
                    parallel,
                    NetworkQueryTask(...),
                    NetworkQueryTask(...),
                }
                ConcurrentCallTask<QString>(...)
            }
        };
    \endcode

    The group may dynamically instantiate a custom storage structure, which may be used for
    inter-task data exchange:

    \code
        struct MyCustomStruct { QByteArray data; };

        TreeStorage<MyCustomStruct> storage;

        const auto onFirstSetup = [](NetworkQuery &task) { ... };
        const auto onFirstDone = [storage](const NetworkQuery &task) {
            // storage-> gives a pointer to MyCustomStruct instance,
            // created dynamically by the running task tree.
            storage->data = task.reply()->readAll();
        };
        const auto onSecondSetup = [storage](ConcurrentCall<QImage> &task) {
            // storage-> gives a pointer to MyCustomStruct. Since the group is sequential,
            // the stored MyCustomStruct was already updated inside the onFirstDone handler.
            const QByteArray storedData = storage->data;
        };

        const Group group {
            // When the group is entered by a running task tree, it creates MyCustomStruct
            // instance dynamically. It is later accessible from all handlers via
            // the *storage or storage-> operators.
            sequential,
            Storage(storage),
            NetworkQueryTask(onFirstSetup, onFirstDone),
            ConcurrentCallTask<QImage>(onSecondSetup)
        };
    \endcode
*/

/*!
    \fn Group::Group(const QList<GroupItem> &children)

    Constructs a group with a given list of \a children.

    This constructor is useful when the child items of the group are not known at compile time,
    but later, at runtime:

    \code
        const QStringList sourceList = ...;

        QList<GroupItem> groupItems { parallel };

        for (const QString &source : sourceList) {
            const NetworkQueryTask task(...); // use source for setup handler
            groupItems << task;
        }

        const Group group(groupItems);
    \endcode
*/

/*!
    \fn Group::Group(std::initializer_list<GroupItem> children)

    Constructs a group from std::initializer_list given by \a children.

    This constructor is useful when all child items of the group are known at compile time:

    \code
        const Group group {
            finishAllAndSuccess,
            NetworkQueryTask(...),
            Group {
                NetworkQueryTask(...),
                Group {
                    parallel,
                    NetworkQueryTask(...),
                    NetworkQueryTask(...),
                }
                ConcurrentCallTask<QString>(...)
            }
        };
    \endcode
*/

/*!
    \fn GroupItem Group::withTimeout(std::chrono::milliseconds timeout, const GroupEndHandler &handler) const

    Attaches \c TimeoutTask to a copy of \c this group, elapsing after \a timeout in milliseconds,
    with an optionally provided timeout \a handler, and returns the coupled item.

    When the group finishes before \a timeout passes,
    the returned item finishes immediately with the group's result.
    Otherwise, the \a handler is invoked (if provided), the group is stopped,
    and the returned item finishes with an error.
*/

/*!
    \class Tasking::CustomTask
    \inheaderfile solutions/tasking/tasktree.h
    \inmodule TaskingSolution
    \brief A class template used for declaring task items and defining their setup,
           done, and error handlers.

    The CustomTask class template is used inside TaskTree for describing custom task items.

    Custom task names are aliased with unique names using the CustomTask template
    with a given TaskAdapter subclass as a template parameter.
    For example, \c ConcurrentCallTask<T> is an alias to the CustomTask that is defined
    to work with \c ConcurrentCall<T> as an associated task class.
    The following table contains all the built-in tasks and their associated task classes:

    \table
    \header
        \li Aliased Task Name (Tasking Namespace)
        \li Associated Task Class
        \li Brief Description
    \row
        \li ConcurrentCallTask<ReturnType>
        \li ConcurrentCall<ReturnType>
        \li Starts an asynchronous task. Runs in a separate thread.
    \row
        \li NetworkQueryTask
        \li NetworkQuery
        \li Sends a network query.
    \row
        \li TaskTreeTask
        \li TaskTree
        \li Starts a nested task tree.
    \row
        \li TimeoutTask
        \li \c std::chrono::milliseconds
        \li Starts a timer.
    \row
        \li WaitForBarrierTask
        \li MultiBarrier<Limit>
        \li Starts an asynchronous task waiting for the barrier to pass.
    \endtable
*/

/*!
    \typealias CustomTask::Task

    Type alias for \c Adapter::Type.

    This is the associated task's type.
*/

/*!
    \typealias CustomTask::EndHandler

    Type alias for \c std::function<void(const Task &)>.
*/

/*!
    \fn template <typename Adapter> template <typename SetupHandler> CustomTask<Adapter>::CustomTask<Adapter>(SetupHandler &&setup, const EndHandler &done, const EndHandler &error)

    Constructs the CustomTask instance and attaches the \a setup, \a done, and \a error
    handlers to the task. When the running task tree is about to start the task,
    it instantiates the associated \l Task object, invokes \a setup handler with a \e reference
    to the created task, and starts it. When the running task finishes with success or an error,
    the task tree invokes \a done or \a error handler, respectively,
    with a \e {const reference} to the created task.

    The passed \a setup handler is either of the \c std::function<SetupResult(Task &)> or
    \c std::function<void(Task &)> type. For example:

    \code
        static void parseAndLog(const QString &input);

        ...

        const QString input = ...;

        const auto onFirstSetup = [input](ConcurrentCall<void> &task) {
            if (input == "Skip")
                return SetupResult::StopWithSuccess; // This task won't start, the next one will
            if (input == "Error")
                return SetupResult::StopWithError; // This task and the next one won't start
            task.setConcurrentCallData(parseAndLog, input);
            // This task will start, and the next one will start after this one finished with success
            return SetupResult::Continue;
        };

        const auto onSecondSetup = [input](ConcurrentCall<void> &task) {
            task.setConcurrentCallData(parseAndLog, input);
        };

        const Group group {
            ConcurrentCallTask<void>(onFirstSetup),
            ConcurrentCallTask<void>(onSecondSetup)
        };
    \endcode

    When the passed \a setup handler is of the \c std::function<SetupResult(Task &)> type,
    the return value of the handler instructs the running tree on how to proceed after
    the handler's invocation is finished. The default return value of SetupResult::Continue
    instructs the tree to continue running, i.e. to execute the associated \c Task.
    The return value of SetupResult::StopWithSuccess or SetupResult::StopWithError instructs
    the tree to skip the task's execution and finish immediately with success or an error,
    respectively.
    When the return type is either SetupResult::StopWithSuccess or SetupResult::StopWithError,
    the task's \a done or \a error handler (even if provided) are not called afterwards.

    The \a setup handler may be of a shortened form of std::function<void(Task &)>,
    i.e. the return value is void. In this case it's assumed that the return value is
    SetupResult::Continue by default.

    When the running task finishes, one of \a done or \a error handlers is called,
    depending on whether it finished with success or an error, respectively.
    Both handlers are of std::function<void(const Task &)> type.
*/

/*!
    \fn template <typename Adapter> GroupItem CustomTask<Adapter>::withTimeout(std::chrono::milliseconds timeout, const GroupItem::GroupEndHandler &handler) const

    Attaches \c TimeoutTask to a copy of \c this task, elapsing after \a timeout in milliseconds,
    with an optionally provided timeout \a handler, and returns the coupled item.

    When the task finishes before \a timeout passes,
    the returned item finishes immediately with the task's result.
    Otherwise, the \a handler is invoked (if provided), the task is stopped,
    and the returned item finishes with an error.
*/

/*!
    \enum Tasking::WorkflowPolicy

    This enum describes the possible behavior of the Group element when any group's child task
    finishes its execution. It's also used when the running Group is stopped.

    \value StopOnError
        Default. Corresponds to the stopOnError global element.
        If any child task finishes with an error, the group stops and finishes with an error.
        If all child tasks finished with success, the group finishes with success.
        If a group is empty, it finishes with success.
    \value ContinueOnError
        Corresponds to the continueOnError global element.
        Similar to stopOnError, but in case any child finishes with an error,
        the execution continues until all tasks finish, and the group reports an error
        afterwards, even when some other tasks in the group finished with success.
        If all child tasks finish successfully, the group finishes with success.
        If a group is empty, it finishes with success.
    \value StopOnSuccess
        Corresponds to the stopOnSuccess global element.
        If any child task finishes with success, the group stops and finishes with success.
        If all child tasks finished with an error, the group finishes with an error.
        If a group is empty, it finishes with an error.
    \value ContinueOnSuccess
        Corresponds to the continueOnSuccess global element.
        Similar to stopOnSuccess, but in case any child finishes successfully,
        the execution continues until all tasks finish, and the group reports success
        afterwards, even when some other tasks in the group finished with an error.
        If all child tasks finish with an error, the group finishes with an error.
        If a group is empty, it finishes with an error.
    \value StopOnSuccessOrError
        Corresponds to the stopOnSuccessOrError global element.
        The group starts as many tasks as it can. When any task finishes,
        the group stops and reports the task's result.
        Useful only in parallel mode.
        In sequential mode, only the first task is started, and when finished,
        the group finishes too, so the other tasks are always skipped.
        If a group is empty, it finishes with an error.
    \value FinishAllAndSuccess
        Corresponds to the finishAllAndSuccess global element.
        The group executes all tasks and ignores their return results. When all
        tasks finished, the group finishes with success.
        If a group is empty, it finishes with success.
    \value FinishAllAndError
        Corresponds to the finishAllAndError global element.
        The group executes all tasks and ignores their return results. When all
        tasks finished, the group finishes with an error.
        If a group is empty, it finishes with an error.

    Whenever a child task's result causes the Group to stop,
    i.e. in case of StopOnError, StopOnSuccess, or StopOnSuccessOrError policies,
    the Group stops the other running child tasks (if any - for example in parallel mode),
    and skips executing tasks it has not started yet (for example, in the sequential mode -
    those, that are placed after the failed task). Both stopping and skipping child tasks
    may happen when parallelLimit() is used.

    The table below summarizes the differences between various workflow policies:

    \table
    \header
        \li \l WorkflowPolicy
        \li Executes all child tasks
        \li Result
        \li Result when the group is empty
    \row
        \li StopOnError
        \li Stops when any child task finished with an error and reports an error
        \li An error when at least one child task failed, success otherwise
        \li Success
    \row
        \li ContinueOnError
        \li Yes
        \li An error when at least one child task failed, success otherwise
        \li Success
    \row
        \li StopOnSuccess
        \li Stops when any child task finished with success and reports success
        \li Success when at least one child task succeeded, an error otherwise
        \li An error
    \row
        \li ContinueOnSuccess
        \li Yes
        \li Success when at least one child task succeeded, an error otherwise
        \li An error
    \row
        \li StopOnSuccessOrError
        \li Stops when any child task finished and reports child task's result
        \li Success or an error, depending on the finished child task's result
        \li An error
    \row
        \li FinishAllAndSuccess
        \li Yes
        \li Success
        \li Success
    \row
        \li FinishAllAndError
        \li Yes
        \li An error
        \li An error
    \endtable

    If a child of a group is also a group, the child group runs its tasks according to its own
    workflow policy. When a parent group stops the running child group because
    of parent group's workflow policy, i.e. when the StopOnError, StopOnSuccess,
    or StopOnSuccessOrError policy was used for the parent,
    the child group's result is reported according to the
    \b Result column and to the \b {child group's workflow policy} row in the table above.
*/

/*!
    \variable sequential
    A convenient global group's element describing the sequential execution mode.

    This is the default execution mode of the Group element.

    When a Group has no execution mode, it runs in the sequential mode.
    All the direct child tasks of a group are started in a chain, so that when one task finishes,
    the next one starts. This enables you to pass the results from the previous task
    as input to the next task before it starts. This mode guarantees that the next task
    is started only after the previous task finishes.

    \sa parallel, parallelLimit()
*/

/*!
    \variable parallel
    A convenient global group's element describing the parallel execution mode.

    All the direct child tasks of a group are started after the group is started,
    without waiting for the previous child tasks to finish.
    In this mode, all child tasks run simultaneously.

    \sa sequential, parallelLimit()
*/

/*!
    \variable stopOnError
    A convenient global group's element describing the StopOnError workflow policy.

    This is the default workflow policy of the Group element.
*/

/*!
    \variable continueOnError
    A convenient global group's element describing the ContinueOnError workflow policy.
*/

/*!
    \variable stopOnSuccess
    A convenient global group's element describing the StopOnSuccess workflow policy.
*/

/*!
    \variable continueOnSuccess
    A convenient global group's element describing the ContinueOnSuccess workflow policy.
*/

/*!
    \variable stopOnSuccessOrError
    A convenient global group's element describing the StopOnSuccessOrError workflow policy.
*/

/*!
    \variable finishAllAndSuccess
    A convenient global group's element describing the FinishAllAndSuccess workflow policy.
*/

/*!
    \variable finishAllAndError
    A convenient global group's element describing the FinishAllAndError workflow policy.
*/

/*!
    \enum Tasking::SetupResult

    This enum is optionally returned from the group's or task's setup handler function.
    It instructs the running task tree on how to proceed after the setup handler's execution
    finished.
    \value Continue
           Default. The group's or task's execution continues normally.
           When a group's or task's setup handler returns void, it's assumed that
           it returned Continue.
    \value StopWithSuccess
           The group's or task's execution stops immediately with success.
           When returned from the group's setup handler, all child tasks are skipped,
           and the group's onGroupDone() handler is invoked (if provided).
           The group reports success to its parent. The group's workflow policy is ignored.
           When returned from the task's setup handler, the task isn't started,
           its done handler isn't invoked, and the task reports success to its parent.
    \value StopWithError
           The group's or task's execution stops immediately with an error.
           When returned from the group's setup handler, all child tasks are skipped,
           and the group's onGroupError() handler is invoked (if provided).
           The group reports an error to its parent. The group's workflow policy is ignored.
           When returned from the task's setup handler, the task isn't started,
           its error handler isn't invoked, and the task reports an error to its parent.
*/

/*!
    \typealias GroupItem::GroupSetupHandler

    Type alias for \c std::function<SetupResult()>.

    The GroupSetupHandler is used when constructing the onGroupSetup() element.
    Any function with the above signature, when passed as a group setup handler,
    will be called by the running task tree when the group execution starts.

    The return value of the handler instructs the running group on how to proceed
    after the handler's invocation is finished. The default return value of SetupResult::Continue
    instructs the group to continue running, i.e. to start executing its child tasks.
    The return value of SetupResult::StopWithSuccess or SetupResult::StopWithError
    instructs the group to skip the child tasks' execution and finish immediately with
    success or an error, respectively.

    When the return type is either SetupResult::StopWithSuccess
    of SetupResult::StopWithError, the group's done or error handler (if provided)
    is called synchronously immediately afterwards.

    \note Even if the group setup handler returns StopWithSuccess or StopWithError,
    one of the group's done or error handlers is invoked. This behavior differs
    from that of task handlers and might change in the future.

    The onGroupSetup() accepts also functions in the shortened form of \c std::function<void()>,
    i.e. the return value is void. In this case it's assumed that the return value
    is SetupResult::Continue by default.

    \sa onGroupSetup()
*/

/*!
    \typealias GroupItem::GroupEndHandler

    Type alias for \c std::function<void()>.

    The GroupEndHandler is used when constructing the onGroupDone() and onGroupError() elements.
    Any function with the above signature, when passed as a group done or error handler,
    will be called by the running task tree when the group ends with success or an error,
    respectively.

    \sa onGroupDone(), onGroupError()
*/

// TODO: Fix docs

/*!
    \fn template <typename SetupHandler> GroupItem onGroupSetup(SetupHandler &&handler)

    Constructs a group's element holding the group setup handler.
    The \a handler is invoked whenever the group starts.

    The passed \a handler is either of \c std::function<SetupResult()> or \c std::function<void()>
    type. For more information on possible argument type, refer to
    \l {GroupItem::GroupSetupHandler}.

    When the \a handler is invoked, none of the group's child tasks are running yet.

    If a group contains the Storage elements, the \a handler is invoked
    after the storages are constructed, so that the \a handler may already
    perform some initial modifications to the active storages.

    \sa GroupItem::GroupSetupHandler, onGroupDone(), onGroupError()
*/

// TODO: Fix docs

/*!
    Constructs a group's element holding the group done handler.
    The \a handler is invoked whenever the group finishes with success.
    Depending on the group's workflow policy, this handler may also be called
    when the running group is stopped (e.g. when finishAllAndSuccess element was used).

    When the \a handler is invoked, all of the group's child tasks are already finished.

    If a group contains the Storage elements, the \a handler is invoked
    before the storages are destructed, so that the \a handler may still
    perform a last read of the active storages' data.

    \sa GroupItem::GroupEndHandler, onGroupSetup(), onGroupError()
*/

// TODO: Fix docs

/*!
    Constructs a group's element holding the group error handler.
    The \a handler is invoked whenever the group finishes with an error.
    Depending on the group's workflow policy, this handler may also be called
    when the running group is stopped (e.g. when stopOnError element was used).

    When the \a handler is invoked, all of the group's child tasks are already finished.

    If a group contains the Storage elements, the \a handler is invoked
    before the storages are destructed, so that the \a handler may still
    perform a last read of the active storages' data.

    \sa GroupItem::GroupEndHandler, onGroupSetup(), onGroupDone()
*/

/*!
    Constructs a group's element describing the \l{Execution Mode}{execution mode}.

    The execution mode element in a Group specifies how the direct child tasks of
    the Group are started.

    For convenience, when appropriate, the \l sequential or \l parallel global elements
    may be used instead.

    The \a limit defines the maximum number of direct child tasks running in parallel:

    \list
        \li When \a limit equals to 0, there is no limit, and all direct child tasks are started
        together, in the oder in which they appear in a group. This means the fully parallel
        execution, and the \l parallel element may be used instead.

        \li When \a limit equals to 1, it means that only one child task may run at the time.
        This means the sequential execution, and the \l sequential element may be used instead.
        In this case child tasks run in chain, so the next child task starts after
        the previous child task has finished.

        \li When other positive number is passed as \a limit, the group's child tasks run
        in parallel, but with a limited number of tasks running simultanously.
        The \e limit defines the maximum number of tasks running in parallel in a group.
        When the group is started, the first batch of tasks is started
        (the number of tasks in a batch equals to the passed \a limit, at most),
        while the others are kept waiting. When any running task finishes,
        the group starts the next remaining one, so that the \e limit of simultaneously
        running tasks inside a group isn't exceeded. This repeats on every child task's
        finish until all child tasks are started. This enables you to limit the maximum
        number of tasks that run simultaneously, for example if running too many processes might
        block the machine for a long time.
    \endlist

    In all execution modes, a group starts tasks in the oder in which they appear.

    If a child of a group is also a group, the child group runs its tasks according
    to its own execution mode.

    \sa sequential, parallel
*/
GroupItem parallelLimit(int limit)
{
    return Group::parallelLimit(qMax(limit, 0));
}

/*!
    Constructs a group's workflow policy element for a given \a policy.

    For convenience, global elements may be used instead.

    \sa stopOnError, continueOnError, stopOnSuccess, continueOnSuccess, stopOnSuccessOrError,
        finishAllAndSuccess, finishAllAndError, WorkflowPolicy
*/
GroupItem workflowPolicy(WorkflowPolicy policy)
{
    return Group::workflowPolicy(policy);
}

const GroupItem sequential = parallelLimit(1);
const GroupItem parallel = parallelLimit(0);

const GroupItem stopOnError = workflowPolicy(WorkflowPolicy::StopOnError);
const GroupItem continueOnError = workflowPolicy(WorkflowPolicy::ContinueOnError);
const GroupItem stopOnSuccess = workflowPolicy(WorkflowPolicy::StopOnSuccess);
const GroupItem continueOnSuccess = workflowPolicy(WorkflowPolicy::ContinueOnSuccess);
const GroupItem stopOnSuccessOrError = workflowPolicy(WorkflowPolicy::StopOnSuccessOrError);
const GroupItem finishAllAndSuccess = workflowPolicy(WorkflowPolicy::FinishAllAndSuccess);
const GroupItem finishAllAndError = workflowPolicy(WorkflowPolicy::FinishAllAndError);

DoneResult toDoneResult(bool success)
{
    return success ? DoneResult::Success : DoneResult::Error;
}

static SetupResult toSetupResult(bool success)
{
    return success ? SetupResult::StopWithSuccess : SetupResult::StopWithError;
}

static DoneResult toDoneResult(DoneWith doneWith)
{
    return doneWith == DoneWith::Success ? DoneResult::Success : DoneResult::Error;
}

static DoneWith toDoneWith(DoneResult result)
{
    return result == DoneResult::Success ? DoneWith::Success : DoneWith::Error;
}

class StorageThreadData
{
    Q_DISABLE_COPY_MOVE(StorageThreadData)

public:
    StorageThreadData(StorageData *storageData);
    ~StorageThreadData();

    int createStorage();
    bool deleteStorage(int id); // Returns true if it was the last storage.

    void activateStorage(int id);

    void *activeStorageVoid() const;
    int activeStorageId() const { return m_activeStorage; }

private:
    StorageData *m_storageData = nullptr;
    QHash<int, void *> m_storageHash;
    int m_activeStorage = 0; // 0 means no active storage.
};

class StorageData
{
public:
    ~StorageData();
    StorageThreadData &threadData() {
        QMutexLocker lock(&m_threadDataMutex);
        return m_threadDataMap.emplace(QThread::currentThread(), this).first->second;
    }

    void deleteStorage(int id)
    {
        if (!threadData().deleteStorage(id))
            return;

        QMutexLocker lock(&m_threadDataMutex);
        m_threadDataMap.erase(
            m_threadDataMap.find(QThread::currentThread()));
    }

    const TreeStorageBase::StorageConstructor m_constructor = {};
    const TreeStorageBase::StorageDestructor m_destructor = {};
    QMutex m_threadDataMutex = {};
    // Use std::map on purpose, so that it doesn't invalidate references on modifications.
    // Don't optimize it by using std::unordered_map.
    std::map<QThread *, StorageThreadData> m_threadDataMap = {};
    std::atomic_int m_storageInstanceCounter = 0; // Bumped on each creation.
};

StorageThreadData::StorageThreadData(StorageData *storageData)
    : m_storageData(storageData)
{
    QT_CHECK(m_storageData->m_constructor);
    QT_CHECK(m_storageData->m_destructor);
}

StorageThreadData::~StorageThreadData()
{
    QT_CHECK(m_storageHash.isEmpty());
    // TODO: Issue a warning about the leak instead?
    for (void *ptr : std::as_const(m_storageHash))
        m_storageData->m_destructor(ptr);
}

int StorageThreadData::createStorage()
{
    QT_ASSERT(m_activeStorage == 0, return 0); // TODO: should be allowed?
    const int newId = m_storageData->m_storageInstanceCounter.fetch_add(1) + 1;
    m_storageHash.insert(newId, m_storageData->m_constructor());
    return newId;
}

bool StorageThreadData::deleteStorage(int id)
{
    QT_ASSERT(m_activeStorage == 0, return false); // TODO: should be allowed?
    const auto it = m_storageHash.constFind(id);
    QT_ASSERT(it != m_storageHash.constEnd(), return false);
    m_storageData->m_destructor(it.value());
    m_storageHash.erase(it);
    return m_storageHash.empty();
}

// passing 0 deactivates currently active storage
void StorageThreadData::activateStorage(int id)
{
    if (id == 0) {
        QT_ASSERT(m_activeStorage, return);
        m_activeStorage = 0;
        return;
    }
    QT_ASSERT(m_activeStorage == 0, return);
    // TODO: Unneeded check? OTOH, it's quite important...
    const auto it = m_storageHash.find(id);
    QT_ASSERT(it != m_storageHash.end(), return);
    m_activeStorage = id;
}

void *StorageThreadData::activeStorageVoid() const
{
    QT_ASSERT(m_activeStorage, qWarning(
        "The referenced storage is not reachable in the running tree. "
        "A nullptr will be returned which might lead to a crash in the calling code. "
        "It is possible that no storage was added to the tree, "
        "or the storage is not reachable from where it is referenced."); return nullptr);
    const auto it = m_storageHash.constFind(m_activeStorage);
    QT_ASSERT(it != m_storageHash.constEnd(), return nullptr);
    return it.value();
}

StorageData::~StorageData()
{
    QMutexLocker lock(&m_threadDataMutex);
    QT_CHECK(m_threadDataMap.empty());
}

bool TreeStorageBase::isValid() const
{
    return m_storageData && m_storageData->m_constructor && m_storageData->m_destructor;
}

TreeStorageBase::TreeStorageBase(StorageConstructor ctor, StorageDestructor dtor)
    : m_storageData(new StorageData{ctor, dtor})
{}

void *TreeStorageBase::activeStorageVoid() const
{
    return m_storageData->threadData().activeStorageVoid();
}

void GroupItem::addChildren(const QList<GroupItem> &children)
{
    QT_ASSERT(m_type == Type::Group || m_type == Type::List,
              qWarning("Only Group or List may have children, skipping..."); return);
    if (m_type == Type::List) {
        m_children.append(children);
        return;
    }
    for (const GroupItem &child : children) {
        switch (child.m_type) {
        case Type::List:
            addChildren(child.m_children);
            break;
        case Type::Group:
            m_children.append(child);
            break;
        case Type::GroupData:
            if (child.m_groupData.m_groupHandler.m_setupHandler) {
                QT_ASSERT(!m_groupData.m_groupHandler.m_setupHandler,
                          qWarning("Group setup handler redefinition, overriding..."));
                m_groupData.m_groupHandler.m_setupHandler
                    = child.m_groupData.m_groupHandler.m_setupHandler;
            }
            if (child.m_groupData.m_groupHandler.m_doneHandler) {
                QT_ASSERT(!m_groupData.m_groupHandler.m_doneHandler,
                          qWarning("Group done handler redefinition, overriding..."));
                m_groupData.m_groupHandler.m_doneHandler
                    = child.m_groupData.m_groupHandler.m_doneHandler;
            }
            if (child.m_groupData.m_parallelLimit) {
                QT_ASSERT(!m_groupData.m_parallelLimit,
                          qWarning("Group execution mode redefinition, overriding..."));
                m_groupData.m_parallelLimit = child.m_groupData.m_parallelLimit;
            }
            if (child.m_groupData.m_workflowPolicy) {
                QT_ASSERT(!m_groupData.m_workflowPolicy,
                          qWarning("Group workflow policy redefinition, overriding..."));
                m_groupData.m_workflowPolicy = child.m_groupData.m_workflowPolicy;
            }
            break;
        case Type::TaskHandler:
            QT_ASSERT(child.m_taskHandler.m_createHandler,
                      qWarning("Task create handler can't be null, skipping..."); return);
            m_children.append(child);
            break;
        case Type::Storage:
            // Check for duplicates, as can't have the same storage twice on the same level.
            for (const TreeStorageBase &storage : child.m_storageList) {
                if (m_storageList.contains(storage)) {
                    QT_ASSERT(false, qWarning("Can't add the same storage into one Group twice, "
                                              "skipping..."));
                    continue;
                }
                m_storageList.append(storage);
            }
            break;
        }
    }
}

GroupItem GroupItem::withTimeout(const GroupItem &item, milliseconds timeout,
                                 const std::function<void()> &handler)
{
    const auto onSetup = [timeout](milliseconds &timeoutData) { timeoutData = timeout; };
    return Group {
        parallel,
        stopOnSuccessOrError,
        Group {
            finishAllAndError,
            handler ? TimeoutTask(onSetup, [handler] { handler(); }, CallDoneIf::Success)
                    : TimeoutTask(onSetup)
        },
        item
    };
}

class TaskTreePrivate;
class TaskNode;
class TaskRuntimeNode;
class TaskRuntimeContainer;

class ExecutionContextActivator
{
public:
    ExecutionContextActivator(TaskRuntimeContainer *container) { activateContext(container); }
    ~ExecutionContextActivator() {
        for (int i = m_activeStorages.size() - 1; i >= 0; --i) // iterate in reverse order
            m_activeStorages[i].m_storageData->threadData().activateStorage(0);
    }

private:
    void activateContext(TaskRuntimeContainer *container);
    QList<TreeStorageBase> m_activeStorages;
};

class TaskContainer
{
    Q_DISABLE_COPY(TaskContainer)

public:
    TaskContainer(TaskContainer &&other) = default;
    TaskContainer(TaskTreePrivate *taskTreePrivate, const GroupItem &task);

    TaskTreePrivate *const m_taskTreePrivate = nullptr;

    const int m_parallelLimit = 1;
    const WorkflowPolicy m_workflowPolicy = WorkflowPolicy::StopOnError;
    const GroupItem::GroupHandler m_groupHandler;
    const QList<TreeStorageBase> m_storageList;
    std::vector<TaskNode> m_children;
    const int m_taskCount = 0;
};

class TaskNode
{
    Q_DISABLE_COPY(TaskNode)

public:
    TaskNode(TaskNode &&other) = default;
    TaskNode(TaskTreePrivate *taskTreePrivate, const GroupItem &task)
        : m_taskHandler(task.m_taskHandler)
        , m_container(taskTreePrivate, task)
    {}

    bool isTask() const { return bool(m_taskHandler.m_createHandler); }
    int taskCount() const { return isTask() ? 1 : m_container.m_taskCount; }

    const GroupItem::TaskHandler m_taskHandler;
    TaskContainer m_container;
};

class TaskTreePrivate
{
    Q_DISABLE_COPY_MOVE(TaskTreePrivate)

public:
    TaskTreePrivate(TaskTree *taskTree)
        : q(taskTree) {}

    void start();
    void stop();
    void advanceProgress(int byValue);
    void emitStartedAndProgress();
    void emitProgress();
    void emitDone(DoneWith result);
    void callSetupHandler(TreeStorageBase storage, int storageId) {
        callStorageHandler(storage, storageId, &StorageHandler::m_setupHandler);
    }
    void callDoneHandler(TreeStorageBase storage, int storageId) {
        callStorageHandler(storage, storageId, &StorageHandler::m_doneHandler);
    }
    struct StorageHandler {
        TreeStorageBase::StorageVoidHandler m_setupHandler = {};
        TreeStorageBase::StorageVoidHandler m_doneHandler = {};
    };
    typedef TreeStorageBase::StorageVoidHandler StorageHandler::*HandlerPtr; // ptr to class member
    void callStorageHandler(TreeStorageBase storage, int storageId, HandlerPtr ptr)
    {
        const auto it = m_storageHandlers.constFind(storage);
        if (it == m_storageHandlers.constEnd())
            return;
        GuardLocker locker(m_guard);
        const StorageHandler storageHandler = *it;
        // TODO: we don't necessarily need to activate the storage here, it's enough to
        // get a pointer to the relevant storage instance.
        auto &threadData = storage.m_storageData->threadData();
        threadData.activateStorage(storageId);
        if (storageHandler.*ptr)
            (storageHandler.*ptr)(threadData.activeStorageVoid());
        threadData.activateStorage(0);
    }

    // Node related methods

    // If returned value != Continue, childDone() needs to be called in parent container (in caller)
    // in order to unwind properly.
    SetupResult start(TaskRuntimeNode *node);
    void stop(TaskRuntimeNode *node);
    bool invokeDoneHandler(TaskRuntimeNode *node, DoneWith doneWith);

    // Container related methods

    SetupResult start(TaskRuntimeContainer *container);
    SetupResult continueStart(TaskRuntimeContainer *container, SetupResult startAction, int nextChild);
    SetupResult startChildren(TaskRuntimeContainer *container, int nextChild);
    SetupResult childDone(TaskRuntimeContainer *container, bool success);
    void stop(TaskRuntimeContainer *container);
    bool invokeDoneHandler(TaskRuntimeContainer *container, DoneWith doneWith);

    template <typename Handler, typename ...Args,
              typename ReturnType = typename std::invoke_result_t<Handler, Args...>>
    ReturnType invokeHandler(TaskRuntimeContainer *container, Handler &&handler, Args &&...args)
    {
        ExecutionContextActivator activator(container);
        GuardLocker locker(m_guard);
        return std::invoke(std::forward<Handler>(handler), std::forward<Args>(args)...);
    }

    TaskTree *q = nullptr;
    Guard m_guard;
    int m_progressValue = 0;
    QSet<TreeStorageBase> m_storages;
    QHash<TreeStorageBase, StorageHandler> m_storageHandlers;
    std::optional<TaskNode> m_root;
    std::unique_ptr<TaskRuntimeNode> m_runtimeRoot; // Keep me last in order to destruct first
};

static bool initialSuccessBit(WorkflowPolicy workflowPolicy)
{
    switch (workflowPolicy) {
    case WorkflowPolicy::StopOnError:
    case WorkflowPolicy::ContinueOnError:
    case WorkflowPolicy::FinishAllAndSuccess:
        return true;
    case WorkflowPolicy::StopOnSuccess:
    case WorkflowPolicy::ContinueOnSuccess:
    case WorkflowPolicy::StopOnSuccessOrError:
    case WorkflowPolicy::FinishAllAndError:
        return false;
    }
    QT_CHECK(false);
    return false;
}

class TaskRuntimeContainer
{
    Q_DISABLE_COPY(TaskRuntimeContainer)

public:
    TaskRuntimeContainer(const TaskContainer &taskContainer, TaskRuntimeNode *parentNode)
        : m_taskContainer(taskContainer)
        , m_parentNode(parentNode)
        , m_storageIdList(createStorages(taskContainer))
        , m_successBit(initialSuccessBit(taskContainer.m_workflowPolicy))
    {}

    ~TaskRuntimeContainer()
    {
        for (int i = m_taskContainer.m_storageList.size() - 1; i >= 0; --i) { // iterate in reverse order
            const TreeStorageBase storage = m_taskContainer.m_storageList[i];
            const int storageId = m_storageIdList.value(i);
            if (m_callStorageDoneHandlersOnDestruction)
                m_taskContainer.m_taskTreePrivate->callDoneHandler(storage, storageId);
            storage.m_storageData->deleteStorage(storageId);
        }
    }

    static QList<int> createStorages(const TaskContainer &container);
    bool isStarting() const { return m_startGuard.isLocked(); }
    int currentLimit() const;
    TaskRuntimeContainer *parentContainer() const;
    bool updateSuccessBit(bool success);
    void deleteChild(TaskRuntimeNode *node);

    const TaskContainer &m_taskContainer; // Not owning.
    TaskRuntimeNode *m_parentNode = nullptr; // Not owning.
    const QList<int> m_storageIdList;

    std::vector<std::unique_ptr<TaskRuntimeNode>> m_children; // Owning.
    bool m_successBit = true;
    bool m_callStorageDoneHandlersOnDestruction = false;
    int m_doneCount = 0;
    Guard m_startGuard;
};

class TaskRuntimeNode
{
    Q_DISABLE_COPY(TaskRuntimeNode)

public:
    TaskRuntimeNode(const TaskNode &taskNode, TaskRuntimeContainer *parentContainer)
        : m_taskNode(taskNode)
        , m_parentContainer(parentContainer)
    {}

    const TaskNode &m_taskNode; // Not owning.
    TaskRuntimeContainer *m_parentContainer = nullptr; // Not owning.
    std::optional<TaskRuntimeContainer> m_container; // Owning.
    std::unique_ptr<TaskInterface> m_task; // Owning.
};

void ExecutionContextActivator::activateContext(TaskRuntimeContainer *container)
{
    const TaskContainer &taskContainer = container->m_taskContainer;
    for (int i = 0; i < taskContainer.m_storageList.size(); ++i) {
        const TreeStorageBase &storage = taskContainer.m_storageList[i];
        auto &threadData = storage.m_storageData->threadData();
        if (threadData.activeStorageId())
            continue; // Storage shadowing: The storage is already active, skipping it...
        m_activeStorages.append(storage);
        threadData.activateStorage(container->m_storageIdList.value(i));
    }
    // Go to the parent after activating this storages so that storage shadowing works
    // in the direction from child to parent root.
    if (container->parentContainer())
        activateContext(container->parentContainer());
}

void TaskTreePrivate::start()
{
    QT_ASSERT(m_root, return);
    QT_ASSERT(!m_runtimeRoot, return);
    m_progressValue = 0;
    emitStartedAndProgress();
    // TODO: check storage handlers for not existing storages in tree
    for (auto it = m_storageHandlers.cbegin(); it != m_storageHandlers.cend(); ++it) {
        QT_ASSERT(m_storages.contains(it.key()), qWarning("The registered storage doesn't "
                  "exist in task tree. Its handlers will never be called."));
    }
    m_runtimeRoot.reset(new TaskRuntimeNode(*m_root, nullptr));
    start(m_runtimeRoot.get());
}

void TaskTreePrivate::stop()
{
    QT_ASSERT(m_root, return);
    if (!m_runtimeRoot)
        return;
    stop(m_runtimeRoot.get());
    emitDone(DoneWith::Cancel);
}

void TaskTreePrivate::advanceProgress(int byValue)
{
    if (byValue == 0)
        return;
    QT_CHECK(byValue > 0);
    QT_CHECK(m_progressValue + byValue <= m_root->taskCount());
    m_progressValue += byValue;
    emitProgress();
}

void TaskTreePrivate::emitStartedAndProgress()
{
    GuardLocker locker(m_guard);
    emit q->started();
    emit q->progressValueChanged(m_progressValue);
}

void TaskTreePrivate::emitProgress()
{
    GuardLocker locker(m_guard);
    emit q->progressValueChanged(m_progressValue);
}

void TaskTreePrivate::emitDone(DoneWith result)
{
    QT_CHECK(m_progressValue == m_root->taskCount());
    GuardLocker locker(m_guard);
    emit q->done(result);
}

static std::vector<TaskNode> createChildren(TaskTreePrivate *taskTreePrivate,
                                            const QList<GroupItem> &children)
{
    std::vector<TaskNode> result;
    result.reserve(children.size());
    for (const GroupItem &child : children)
        result.emplace_back(taskTreePrivate, child);
    return result;
}

TaskContainer::TaskContainer(TaskTreePrivate *taskTreePrivate, const GroupItem &task)
    : m_taskTreePrivate(taskTreePrivate)
    , m_parallelLimit(task.m_groupData.m_parallelLimit.value_or(1))
    , m_workflowPolicy(task.m_groupData.m_workflowPolicy.value_or(WorkflowPolicy::StopOnError))
    , m_groupHandler(task.m_groupData.m_groupHandler)
    , m_storageList(task.m_storageList)
    , m_children(createChildren(taskTreePrivate, task.m_children))
    , m_taskCount(std::accumulate(m_children.cbegin(), m_children.cend(), 0,
                                  [](int r, const TaskNode &n) { return r + n.taskCount(); }))
{
    for (const TreeStorageBase &storage : m_storageList)
        m_taskTreePrivate->m_storages << storage;
}

QList<int> TaskRuntimeContainer::createStorages(const TaskContainer &container)
{
    QList<int> storageIdList;
    for (const TreeStorageBase &storage : container.m_storageList) {
        const int storageId = storage.m_storageData->threadData().createStorage();
        storageIdList.append(storageId);
        container.m_taskTreePrivate->callSetupHandler(storage, storageId);
    }
    return storageIdList;
}

int TaskRuntimeContainer::currentLimit() const
{
    // TODO: Handle children well
    const int childCount = m_taskContainer.m_children.size();
    return m_taskContainer.m_parallelLimit
               ? qMin(m_doneCount + m_taskContainer.m_parallelLimit, childCount) : childCount;
}

TaskRuntimeContainer *TaskRuntimeContainer::parentContainer() const
{
    return m_parentNode->m_parentContainer;
}

bool TaskRuntimeContainer::updateSuccessBit(bool success)
{
    if (m_taskContainer.m_workflowPolicy == WorkflowPolicy::FinishAllAndSuccess
        || m_taskContainer.m_workflowPolicy == WorkflowPolicy::FinishAllAndError
        || m_taskContainer.m_workflowPolicy == WorkflowPolicy::StopOnSuccessOrError) {
        if (m_taskContainer.m_workflowPolicy == WorkflowPolicy::StopOnSuccessOrError)
            m_successBit = success;
        return m_successBit;
    }

    const bool donePolicy = m_taskContainer.m_workflowPolicy == WorkflowPolicy::StopOnSuccess
                         || m_taskContainer.m_workflowPolicy == WorkflowPolicy::ContinueOnSuccess;
    m_successBit = donePolicy ? (m_successBit || success) : (m_successBit && success);
    return m_successBit;
}

void TaskRuntimeContainer::deleteChild(TaskRuntimeNode *node)
{
    std::remove_if(m_children.begin(), m_children.end(), [node](const auto &ptr) {
        return ptr.get() == node;
    });
}

SetupResult TaskTreePrivate::start(TaskRuntimeContainer *container)
{
    SetupResult startAction = SetupResult::Continue;
    if (container->m_taskContainer.m_groupHandler.m_setupHandler) {
        startAction = invokeHandler(container, container->m_taskContainer.m_groupHandler.m_setupHandler);
        if (startAction != SetupResult::Continue) {
            // TODO: Handle progress well.
            advanceProgress(container->m_taskContainer.m_taskCount);
            // Non-Continue SetupResult takes precedence over the workflow policy.
            container->m_successBit = startAction == SetupResult::StopWithSuccess;
        }
    }
    if (startAction == SetupResult::Continue) {
        if (container->m_taskContainer.m_children.empty())
            startAction = toSetupResult(container->m_successBit);
    } else { // TODO: Check if repeater exists, call its handler.

    }
    return continueStart(container, startAction, 0);
}

SetupResult TaskTreePrivate::continueStart(TaskRuntimeContainer *container, SetupResult startAction, int nextChild)
{
    const SetupResult groupAction = startAction == SetupResult::Continue ? startChildren(container, nextChild)
                                                                         : startAction;
    if (groupAction != SetupResult::Continue) {
        const bool bit = container->updateSuccessBit(groupAction == SetupResult::StopWithSuccess);
        TaskRuntimeContainer *parentContainer = container->parentContainer();
        TaskRuntimeNode *parentNode = container->m_parentNode;
        QT_CHECK(parentNode);
        const bool result = invokeDoneHandler(container, bit ? DoneWith::Success : DoneWith::Error);
        if (parentContainer) {
            parentContainer->deleteChild(parentNode);
            if (!parentContainer->isStarting())
                childDone(parentContainer, result);
        } else {
            QT_CHECK(m_runtimeRoot.get() == parentNode);
            m_runtimeRoot.reset();
            emitDone(result ? DoneWith::Success : DoneWith::Error);
        }
    }
    return groupAction;
}

SetupResult TaskTreePrivate::startChildren(TaskRuntimeContainer *container, int nextChild)
{
    GuardLocker locker(container->m_startGuard);
    for (int i = nextChild; i < int(container->m_taskContainer.m_children.size()); ++i) {
        const int limit = container->currentLimit();
        if (i >= limit)
            break;

        TaskRuntimeNode *newTask = new TaskRuntimeNode(container->m_taskContainer.m_children.at(i), container);
        container->m_children.emplace_back(newTask);

        const SetupResult startAction = start(newTask);
        if (startAction == SetupResult::Continue)
            continue;

        const SetupResult finalizeAction = childDone(container, startAction == SetupResult::StopWithSuccess);
        if (finalizeAction == SetupResult::Continue)
            continue;

        int skippedTaskCount = 0;
        // Skip scheduled but not run yet. The current (i) was already notified.
        for (int j = i + 1; j < limit; ++j)
            skippedTaskCount += container->m_taskContainer.m_children.at(j).taskCount();
        // TODO: Handle progress well
        advanceProgress(skippedTaskCount);
        return finalizeAction;
    }
    return SetupResult::Continue;
}

SetupResult TaskTreePrivate::childDone(TaskRuntimeContainer *container, bool success)
{
    const int limit = container->currentLimit(); // Read before bumping m_doneCount and stop()
    const WorkflowPolicy &workflowPolicy = container->m_taskContainer.m_workflowPolicy;
    const bool shouldStop = workflowPolicy == WorkflowPolicy::StopOnSuccessOrError
                        || (workflowPolicy == WorkflowPolicy::StopOnSuccess && success)
                        || (workflowPolicy == WorkflowPolicy::StopOnError && !success);
    if (shouldStop)
        stop(container);

    ++container->m_doneCount;
    const bool updatedSuccess = container->updateSuccessBit(success);
    const SetupResult startAction
        = (shouldStop || container->m_doneCount == int(container->m_taskContainer.m_children.size()))
        ? toSetupResult(updatedSuccess) : SetupResult::Continue;

    if (container->isStarting())
        return startAction;
    return continueStart(container, startAction, limit);
}

void TaskTreePrivate::stop(TaskRuntimeContainer *container)
{
    for (auto &child : container->m_children) {
        if (child)
            stop(child.get());
    }

    int skippedTaskCount = 0;
    for (int i = container->currentLimit(); i < int(container->m_taskContainer.m_children.size()); ++i)
        skippedTaskCount += container->m_taskContainer.m_children.at(i).taskCount();

    // TODO: Handle progress well
    advanceProgress(skippedTaskCount);
}

static bool shouldCall(CallDoneIf callDoneIf, DoneWith result)
{
    if (result == DoneWith::Success)
        return callDoneIf != CallDoneIf::Error;
    return callDoneIf != CallDoneIf::Success;
}

bool TaskTreePrivate::invokeDoneHandler(TaskRuntimeContainer *container, DoneWith doneWith)
{
    DoneResult result = toDoneResult(doneWith);
    const GroupItem::GroupHandler &groupHandler = container->m_taskContainer.m_groupHandler;
    if (groupHandler.m_doneHandler && shouldCall(groupHandler.m_callDoneIf, doneWith))
        result = invokeHandler(container, groupHandler.m_doneHandler, doneWith);
    container->m_callStorageDoneHandlersOnDestruction = true;
    container->m_parentNode->m_container.reset();
    return result == DoneResult::Success;
}

SetupResult TaskTreePrivate::start(TaskRuntimeNode *node)
{
    if (!node->m_taskNode.isTask()) {
        node->m_container.emplace(node->m_taskNode.m_container, node);
        return start(&*node->m_container);
    }

    const GroupItem::TaskHandler &handler = node->m_taskNode.m_taskHandler;
    node->m_task.reset(handler.m_createHandler());
    const SetupResult startAction = handler.m_setupHandler
        ? invokeHandler(node->m_parentContainer, handler.m_setupHandler, *node->m_task.get())
        : SetupResult::Continue;
    if (startAction != SetupResult::Continue) {
        // TODO: Handle progress well
        advanceProgress(1);
        node->m_task.reset();
        return startAction;
    }
    const std::shared_ptr<SetupResult> unwindAction
        = std::make_shared<SetupResult>(SetupResult::Continue);
    QObject::connect(node->m_task.get(), &TaskInterface::done,
                     q, [this, node, unwindAction](DoneResult doneResult) {
        const bool result = invokeDoneHandler(node, toDoneWith(doneResult));
        QObject::disconnect(node->m_task.get(), &TaskInterface::done, q, nullptr);
        node->m_task.release()->deleteLater();
        TaskRuntimeContainer *parentContainer = node->m_parentContainer;
        parentContainer->deleteChild(node);
        if (parentContainer->isStarting())
            *unwindAction = toSetupResult(result);
        else
            childDone(parentContainer, result);
    });

    node->m_task->start();
    return *unwindAction;
}

void TaskTreePrivate::stop(TaskRuntimeNode *node)
{
    if (!node->m_task) {
        if (!node->m_container)
            return;
        stop(&*node->m_container);
        node->m_container->updateSuccessBit(false);
        invokeDoneHandler(&*node->m_container, DoneWith::Cancel);
        return;
    }

    invokeDoneHandler(node, DoneWith::Cancel);
    node->m_task.reset();
}

bool TaskTreePrivate::invokeDoneHandler(TaskRuntimeNode *node, DoneWith doneWith)
{
    DoneResult result = toDoneResult(doneWith);
    const GroupItem::TaskHandler &handler = node->m_taskNode.m_taskHandler;
    if (handler.m_doneHandler && shouldCall(handler.m_callDoneIf, doneWith)) {
        result = invokeHandler(node->m_parentContainer,
                               handler.m_doneHandler, *node->m_task.get(), doneWith);
    }
    // TODO: Handle progress well
    advanceProgress(1);
    return result == DoneResult::Success;
}

/*!
    \class Tasking::TaskTree
    \inheaderfile solutions/tasking/tasktree.h
    \inmodule TaskingSolution
    \brief The TaskTree class runs an async task tree structure defined in a declarative way.

    Use the Tasking namespace to build extensible, declarative task tree
    structures that contain possibly asynchronous tasks, such as Process,
    FileTransfer, or ConcurrentCall<ReturnType>. TaskTree structures enable you
    to create a sophisticated mixture of a parallel or sequential flow of tasks
    in the form of a tree and to run it any time later.

    \section1 Root Element and Tasks

    The TaskTree has a mandatory Group root element, which may contain
    any number of tasks of various types, such as ProcessTask, FileTransferTask,
    or ConcurrentCallTask<ReturnType>:

    \code
        using namespace Tasking;

        const Group root {
            ProcessTask(...),
            ConcurrentCallTask<int>(...),
            FileTransferTask(...)
        };

        TaskTree *taskTree = new TaskTree(root);
        connect(taskTree, &TaskTree::done, ...);          // a successfully finished handler
        connect(taskTree, &TaskTree::errorOccurred, ...); // an erroneously finished handler
        taskTree->start();
    \endcode

    The task tree above has a top level element of the Group type that contains
    tasks of the ProcessTask, FileTransferTask, and ConcurrentCallTask<int> type.
    After taskTree->start() is called, the tasks are run in a chain, starting
    with ProcessTask. When the ProcessTask finishes successfully, the ConcurrentCallTask<int>
    task is started. Finally, when the asynchronous task finishes successfully, the
    FileTransferTask task is started.

    When the last running task finishes with success, the task tree is considered
    to have run successfully and the TaskTree::done() signal is emitted.
    When a task finishes with an error, the execution of the task tree is stopped
    and the remaining tasks are skipped. The task tree finishes with an error and
    sends the TaskTree::errorOccurred() signal.

    \section1 Groups

    The parent of the Group sees it as a single task. Like other tasks,
    the group can be started and it can finish with success or an error.
    The Group elements can be nested to create a tree structure:

    \code
        const Group root {
            Group {
                parallel,
                ProcessTask(...),
                ConcurrentCallTask<int>(...)
            },
            FileTransferTask(...)
        };
    \endcode

    The example above differs from the first example in that the root element has
    a subgroup that contains the ProcessTask and ConcurrentCallTask<int>. The subgroup is a
    sibling element of the FileTransferTask in the root. The subgroup contains an
    additional \e parallel element that instructs its Group to execute its tasks
    in parallel.

    So, when the tree above is started, the ProcessTask and ConcurrentCallTask<int> start
    immediately and run in parallel. Since the root group doesn't contain a
    \e parallel element, its direct child tasks are run in sequence. Thus, the
    FileTransferTask starts when the whole subgroup finishes. The group is
    considered as finished when all its tasks have finished. The order in which
    the tasks finish is not relevant.

    So, depending on which task lasts longer (ProcessTask or ConcurrentCallTask<int>), the
    following scenarios can take place:

    \table
    \header
        \li Scenario 1
        \li Scenario 2
    \row
        \li Root Group starts
        \li Root Group starts
    \row
        \li Sub Group starts
        \li Sub Group starts
    \row
        \li ProcessTask starts
        \li ProcessTask starts
    \row
        \li ConcurrentCallTask<int> starts
        \li ConcurrentCallTask<int> starts
    \row
        \li ...
        \li ...
    \row
        \li \b {ProcessTask finishes}
        \li \b {ConcurrentCallTask<int> finishes}
    \row
        \li ...
        \li ...
    \row
        \li \b {ConcurrentCallTask<int> finishes}
        \li \b {ProcessTask finishes}
    \row
        \li Sub Group finishes
        \li Sub Group finishes
    \row
        \li FileTransferTask starts
        \li FileTransferTask starts
    \row
        \li ...
        \li ...
    \row
        \li FileTransferTask finishes
        \li FileTransferTask finishes
    \row
        \li Root Group finishes
        \li Root Group finishes
    \endtable

    The differences between the scenarios are marked with bold. Three dots mean
    that an unspecified amount of time passes between previous and next events
    (a task or tasks continue to run). No dots between events
    means that they occur synchronously.

    The presented scenarios assume that all tasks run successfully. If a task
    fails during execution, the task tree finishes with an error. In particular,
    when ProcessTask finishes with an error while ConcurrentCallTask<int> is still being executed,
    the ConcurrentCallTask<int> is automatically stopped, the subgroup finishes with an error,
    the FileTransferTask is skipped, and the tree finishes with an error.

    \section1 Task Types

    Each task type is associated with its corresponding task class that executes
    the task. For example, a ProcessTask inside a task tree is associated with
    the Process class that executes the process. The associated objects are
    automatically created, started, and destructed exclusively by the task tree
    at the appropriate time.

    If a root group consists of five sequential ProcessTask tasks, and the task tree
    executes the group, it creates an instance of Process for the first
    ProcessTask and starts it. If the Process instance finishes successfully,
    the task tree destructs it and creates a new Process instance for the
    second ProcessTask, and so on. If the first task finishes with an error, the task
    tree stops creating Process instances, and the root group finishes with an
    error.

    The following table shows examples of task types and their corresponding task
    classes:

    \table
    \header
        \li Task Type (Tasking Namespace)
        \li Associated Task Class
        \li Brief Description
    \row
        \li ProcessTask
        \li Utils::Process
        \li Starts process.
    \row
        \li ConcurrentCallTask<ReturnType>
        \li Tasking::ConcurrentCall<ReturnType>
        \li Starts asynchronous task, runs in separate thread.
    \row
        \li TaskTreeTask
        \li Tasking::TaskTree
        \li Starts a nested task tree.
    \row
        \li FileTransferTask
        \li ProjectExplorer::FileTransfer
        \li Starts file transfer between different devices.
    \endtable

    \section1 Task Handlers

    Use Task handlers to set up a task for execution and to enable reading
    the output data from the task when it finishes with success or an error.

    \section2 Task's Start Handler

    When a corresponding task class object is created and before it's started,
    the task tree invokes an optionally user-provided setup handler. The setup
    handler should always take a \e reference to the associated task class object:

    \code
        const auto onSetup = [](Process &process) {
            process.setCommand({"sleep", {"3"}});
        };
        const Group root {
            ProcessTask(onSetup)
        };
    \endcode

    You can modify the passed Process in the setup handler, so that the task
    tree can start the process according to your configuration.
    You should not call \e {process.start();} in the setup handler,
    as the task tree calls it when needed. The setup handler is optional. When used,
    it must be the first argument of the task's constructor.

    Optionally, the setup handler may return a SetupResult. The returned
    SetupResult influences the further start behavior of a given task. The
    possible values are:

    \table
    \header
        \li SetupResult Value
        \li Brief Description
    \row
        \li Continue
        \li The task will be started normally. This is the default behavior when the
            setup handler doesn't return SetupResult (that is, its return type is
            void).
    \row
        \li StopWithSuccess
        \li The task won't be started and it will report success to its parent.
    \row
        \li StopWithError
        \li The task won't be started and it will report an error to its parent.
    \endtable

    This is useful for running a task only when a condition is met and the data
    needed to evaluate this condition is not known until previously started tasks
    finish. In this way, the setup handler dynamically decides whether to start the
    corresponding task normally or skip it and report success or an error.
    For more information about inter-task data exchange, see \l Storage.

    \section2 Task's Done and Error Handlers

    When a running task finishes, the task tree invokes an optionally provided
    done or error handler. Both handlers should always take a \e {const reference}
    to the associated task class object:

    \code
        const auto onSetup = [](Process &process) {
            process.setCommand({"sleep", {"3"}});
        };
        const auto onDone = [](const Process &process) {
            qDebug() << "Success" << process.cleanedStdOut();
        };
        const auto onError = [](const Process &process) {
            qDebug() << "Failure" << process.cleanedStdErr();
        };
        const Group root {
            ProcessTask(onSetup, onDone, onError)
        };
    \endcode

    The done and error handlers may collect output data from Process, and store it
    for further processing or perform additional actions. The done handler is optional.
    When used, it must be the second argument of the task's constructor.
    The error handler is also optional. When used, it must always be the third argument.
    You can omit the handlers or substitute the ones that you do not need with curly braces ({}).

    \note If the task setup handler returns StopWithSuccess or StopWithError,
    neither the done nor error handler is invoked.

    \section1 Group Handlers

    Similarly to task handlers, group handlers enable you to set up a group to
    execute and to apply more actions when the whole group finishes with
    success or an error.

    \section2 Group's Start Handler

    The task tree invokes the group start handler before it starts the child
    tasks. The group handler doesn't take any arguments:

    \code
        const auto onSetup = [] {
            qDebug() << "Entering the group";
        };
        const Group root {
            onGroupSetup(onSetup),
            ProcessTask(...)
        };
    \endcode

    The group setup handler is optional. To define a group setup handler, add an
    onGroupSetup() element to a group. The argument of onGroupSetup() is a user
    handler. If you add more than one onGroupSetup() element to a group, an assert
    is triggered at runtime that includes an error message.

    Like the task's start handler, the group start handler may return SetupResult.
    The returned SetupResult value affects the start behavior of the
    whole group. If you do not specify a group start handler or its return type
    is void, the default group's action is SetupResult::Continue, so that all
    tasks are started normally. Otherwise, when the start handler returns
    SetupResult::StopWithSuccess or SetupResult::StopWithError, the tasks are not
    started (they are skipped) and the group itself reports success or failure,
    depending on the returned value, respectively.

    \code
        const Group root {
            onGroupSetup([] { qDebug() << "Root setup"; }),
            Group {
                onGroupSetup([] { qDebug() << "Group 1 setup"; return SetupResult::Continue; }),
                ProcessTask(...) // Process 1
            },
            Group {
                onGroupSetup([] { qDebug() << "Group 2 setup"; return SetupResult::StopWithSuccess; }),
                ProcessTask(...) // Process 2
            },
            Group {
                onGroupSetup([] { qDebug() << "Group 3 setup"; return SetupResult::StopWithError; }),
                ProcessTask(...) // Process 3
            },
            ProcessTask(...) // Process 4
        };
    \endcode

    In the above example, all subgroups of a root group define their setup handlers.
    The following scenario assumes that all started processes finish with success:

    \table
    \header
        \li Scenario
        \li Comment
    \row
        \li Root Group starts
        \li Doesn't return SetupResult, so its tasks are executed.
    \row
        \li Group 1 starts
        \li Returns Continue, so its tasks are executed.
    \row
        \li Process 1 starts
        \li
    \row
        \li ...
        \li ...
    \row
        \li Process 1 finishes (success)
        \li
    \row
        \li Group 1 finishes (success)
        \li
    \row
        \li Group 2 starts
        \li Returns StopWithSuccess, so Process 2 is skipped and Group 2 reports
            success.
    \row
        \li Group 2 finishes (success)
        \li
    \row
        \li Group 3 starts
        \li Returns StopWithError, so Process 3 is skipped and Group 3 reports
            an error.
    \row
        \li Group 3 finishes (error)
        \li
    \row
        \li Root Group finishes (error)
        \li Group 3, which is a direct child of the root group, finished with an
            error, so the root group stops executing, skips Process 4, which has
            not started yet, and reports an error.
    \endtable

    \section2 Groups's Done and Error Handlers

    A Group's done or error handler is executed after the successful or failed
    execution of its tasks, respectively. The final value reported by the
    group depends on its \l {Workflow Policy}. The handlers can apply other
    necessary actions. The done and error handlers are defined inside the
    onGroupDone() and onGroupError() elements of a group, respectively. They do not
    take arguments:

    \code
        const Group root {
            onGroupSetup([] { qDebug() << "Root setup"; }),
            ProcessTask(...),
            onGroupDone([] { qDebug() << "Root finished with success"; }),
            onGroupError([] { qDebug() << "Root finished with an error"; })
        };
    \endcode

    The group done and error handlers are optional. If you add more than one
    onGroupDone() or onGroupError() each to a group, an assert is triggered at
    runtime that includes an error message.

    \note Even if the group setup handler returns StopWithSuccess or StopWithError,
    one of the group's done or error handlers is invoked. This behavior differs
    from that of task handlers and might change in the future.

    \section1 Other Group Elements

    A group can contain other elements that describe the processing flow, such as
    the execution mode or workflow policy. It can also contain storage elements
    that are responsible for collecting and sharing custom common data gathered
    during group execution.

    \section2 Execution Mode

    The execution mode element in a Group specifies how the direct child tasks of
    the Group are started. The most common execution modes are \l sequential and
    \l parallel. It's also possible to specify the limit of tasks running
    in parallel by using the parallelLimit() function.

    In all execution modes, a group starts tasks in the oder in which they appear.

    If a child of a group is also a group, the child group runs its tasks
    according to its own execution mode.

    \section2 Workflow Policy

    The workflow policy element in a Group specifies how the group should behave
    when any of its \e direct child's tasks finish. For a detailed description of possible
    policies, refer to WorkflowPolicy.

    If a child of a group is also a group, the child group runs its tasks
    according to its own workflow policy.

    \section2 Storage

    Use the Storage element to exchange information between tasks. Especially,
    in the sequential execution mode, when a task needs data from another,
    already finished task, before it can start. For example, a task tree that copies data by reading
    it from a source and writing it to a destination might look as follows:

    \code
        static QByteArray load(const QString &fileName) { ... }
        static void save(const QString &fileName, const QByteArray &array) { ... }

        static Group copyRecipe(const QString &source, const QString &destination)
        {
            struct CopyStorage { // [1] custom inter-task struct
                QByteArray content; // [2] custom inter-task data
            };

            // [3] instance of custom inter-task struct manageable by task tree
            const TreeStorage<CopyStorage> storage;

            const auto onLoaderSetup = [source](ConcurrentCall<QByteArray> &async) {
                async.setConcurrentCallData(&load, source);
            };
            // [4] runtime: task tree activates the instance from [7] before invoking handler
            const auto onLoaderDone = [storage](const ConcurrentCall<QByteArray> &async) {
                storage->content = async.result(); // [5] loader stores the result in storage
            };

            // [4] runtime: task tree activates the instance from [7] before invoking handler
            const auto onSaverSetup = [storage, destination](ConcurrentCall<void> &async) {
                const QByteArray content = storage->content; // [6] saver takes data from storage
                async.setConcurrentCallData(&save, destination, content);
            };
            const auto onSaverDone = [](const ConcurrentCall<void> &async) {
                qDebug() << "Save done successfully";
            };

            const Group root {
                // [7] runtime: task tree creates an instance of CopyStorage when root is entered
                Storage(storage),
                ConcurrentCallTask<QByteArray>(onLoaderSetup, onLoaderDone),
                ConcurrentCallTask<void>(onSaverSetup, onSaverDone)
            };
            return root;
        }

        const QString source = ...;
        const QString destination = ...;
        TaskTree taskTree(copyRecipe(source, destination));
        connect(&taskTree, &TaskTree::done,
                &taskTree, [] { qDebug() << "The copying finished successfully."; });
        tasktree.start();
    \endcode

    In the example above, the inter-task data consists of a QByteArray content
    variable [2] enclosed in a CopyStorage custom struct [1]. If the loader
    finishes successfully, it stores the data in a CopyStorage::content
    variable [5]. The saver then uses the variable to configure the saving task [6].

    To enable a task tree to manage the CopyStorage struct, an instance of
    TreeStorage<CopyStorage> is created [3]. If a copy of this object is
    inserted as group's child task [7], an instance of CopyStorage struct is
    created dynamically when the task tree enters this group. When the task
    tree leaves this group, the existing instance of CopyStorage struct is
    destructed as it's no longer needed.

    If several task trees that hold a copy of the common TreeStorage<CopyStorage>
    instance run simultaneously, each task tree contains its own copy of the
    CopyStorage struct.

    You can access CopyStorage from any handler in the group with a storage object.
    This includes all handlers of all descendant tasks of the group with
    a storage object. To access the custom struct in a handler, pass the
    copy of the TreeStorage<CopyStorage> object to the handler (for example, in
    a lambda capture) [4].

    When the task tree invokes a handler in a subtree containing the storage [7],
    the task tree activates its own CopyStorage instance inside the
    TreeStorage<CopyStorage> object. Therefore, the CopyStorage struct may be
    accessed only from within the handler body. To access the currently active
    CopyStorage from within TreeStorage<CopyStorage>, use the TreeStorage::operator->(),
    TreeStorage::operator*() or TreeStorage::activeStorage() method.

    The following list summarizes how to employ a Storage object into the task
    tree:
    \list 1
        \li Define the custom structure MyStorage with custom data [1], [2]
        \li Create an instance of TreeStorage<MyStorage> storage [3]
        \li Pass the TreeStorage<MyStorage> instance to handlers [4]
        \li Access the MyStorage instance in handlers [5], [6]
        \li Insert the TreeStorage<MyStorage> instance into a group [7]
    \endlist

    \note The current implementation assumes that all running task trees
    containing copies of the same TreeStorage run in the same thread. Otherwise,
    the behavior is undefined.

    \section1 TaskTree

    TaskTree executes the tree structure of asynchronous tasks according to the
    recipe described by the Group root element.

    As TaskTree is also an asynchronous task, it can be a part of another TaskTree.
    To place a nested TaskTree inside another TaskTree, insert the TaskTreeTask
    element into other tree's Group element.

    TaskTree reports progress of completed tasks when running. The progress value
    is increased when a task finishes or is skipped or stopped.
    When TaskTree is finished and the TaskTree::done() or TaskTree::errorOccurred()
    signal is emitted, the current value of the progress equals the maximum
    progress value. Maximum progress equals the total number of tasks in a tree.
    A nested TaskTree is counted as a single task, and its child tasks are not
    counted in the top level tree. Groups themselves are not counted as tasks,
    but their tasks are counted.

    To set additional initial data for the running tree, modify the storage
    instances in a tree when it creates them by installing a storage setup
    handler:

    \code
        TreeStorage<CopyStorage> storage;
        const Group root = ...; // storage placed inside root's group and inside handlers
        TaskTree taskTree(root);
        auto initStorage = [](CopyStorage &storage){
            storage.content = "initial content";
        };
        taskTree.onStorageSetup(storage, initStorage);
        taskTree.start();
    \endcode

    When the running task tree creates a CopyStorage instance, and before any
    handler inside a tree is called, the task tree calls the initStorage handler,
    to enable setting up initial data of the storage, unique to this particular
    run of taskTree.

    Similarly, to collect some additional result data from the running tree,
    read it from storage instances in the tree when they are about to be
    destroyed. To do this, install a storage done handler:

    \code
        TreeStorage<CopyStorage> storage;
        const Group root = ...; // storage placed inside root's group and inside handlers
        TaskTree taskTree(root);
        auto collectStorage = [](const CopyStorage &storage){
            qDebug() << "final content" << storage.content;
        };
        taskTree.onStorageDone(storage, collectStorage);
        taskTree.start();
    \endcode

    When the running task tree is about to destroy a CopyStorage instance, the
    task tree calls the collectStorage handler, to enable reading the final data
    from the storage, unique to this particular run of taskTree.

    \section1 Task Adapters

    To extend a TaskTree with a new task type, implement a simple adapter class
    derived from the TaskAdapter class template. The following class is an
    adapter for a single shot timer, which may be considered as a new asynchronous task:

    \code
        class TimerTaskAdapter : public TaskAdapter<QTimer>
        {
        public:
            TimerTaskAdapter() {
                task()->setSingleShot(true);
                task()->setInterval(1000);
                connect(task(), &QTimer::timeout, this, [this] { emit done(DoneResult::Success); });
            }
        private:
            void start() final { task()->start(); }
        };

        using TimerTask = CustomTask<TimerTaskAdapter>;
    \endcode

    You must derive the custom adapter from the TaskAdapter class template
    instantiated with a template parameter of the class implementing a running
    task. The code above uses QTimer to run the task. This class appears
    later as an argument to the task's handlers. The instance of this class
    parameter automatically becomes a member of the TaskAdapter template, and is
    accessible through the TaskAdapter::task() method. The constructor
    of TimerTaskAdapter initially configures the QTimer object and connects
    to the QTimer::timeout signal. When the signal is triggered, TimerTaskAdapter
    emits the \c done(DoneResult::Success) signal to inform the task tree that the task finished
    successfully. If it emits \c done(DoneResult::Error), the task finished with an error.
    The TaskAdapter::start() method starts the timer.

    To make QTimer accessible inside TaskTree under the \e TimerTask name,
    define TimerTask to be an alias to the Tasking::CustomTask<TimerTaskAdapter>.
    TimerTask becomes a new task type, using TimerTaskAdapter.

    The new task type is now registered, and you can use it in TaskTree:

    \code
        const auto onSetup = [](QTimer &task) { task.setInterval(2000); };
        const auto onDone = [] { qDebug() << "timer triggered"; };
        const Group root {
            TimerTask(onSetup, onDone)
        };
    \endcode

    When a task tree containing the root from the above example is started, it
    prints a debug message within two seconds and then finishes successfully.

    \note The class implementing the running task should have a default constructor,
    and objects of this class should be freely destructible. It should be allowed
    to destroy a running object, preferably without waiting for the running task
    to finish (that is, safe non-blocking destructor of a running task).
*/

/*!
    Constructs an empty task tree. Use setRecipe() to pass a declarative description
    on how the task tree should execute the tasks and how it should handle the finished tasks.

    Starting an empty task tree is no-op and the relevant warning message is issued.

    \sa setRecipe(), start()
*/
TaskTree::TaskTree()
    : d(new TaskTreePrivate(this))
{}

/*!
    Constructs a task tree with a given \a recipe. After the task tree is started,
    it executes the tasks contained inside the \a recipe and
    handles finished tasks according to the passed description.

    \sa setRecipe(), start()
*/
TaskTree::TaskTree(const Group &recipe) : TaskTree()
{
    setRecipe(recipe);
}

/*!
    Destroys the task tree.

    When the task tree is running while being destructed, it stops all the running tasks
    immediately. In this case, no handlers are called, not even the groups' and
    tasks' error handlers or onStorageDone() handlers. The task tree also doesn't emit any
    signals from the destructor, not even errorOccurred() or progressValueChanged() signals.
    This behavior may always be relied on.
    It is completely safe to destruct the running task tree.

    It's a usual pattern to destruct the running task tree, even from the main thread.
    It's guaranteed that the destruction will run quickly, without having to wait for
    the currently running tasks to finish, provided that the used tasks implement
    their destructors in a non-blocking way.

    \note Do not call the destructor directly from any of the running task's handlers
          or task tree's signals. In these cases, use \l deleteLater() instead.

    \sa stop()
*/
TaskTree::~TaskTree()
{
    QT_ASSERT(!d->m_guard.isLocked(), qWarning("Deleting TaskTree instance directly from "
              "one of its handlers will lead to a crash!"));
    // TODO: delete storages explicitly here?
    delete d;
}

/*!
    Sets a given \a recipe for the task tree. After the task tree is started,
    it executes the tasks contained inside the \a recipe and
    handles finished tasks according to the passed description.

    \note When called for a running task tree, the call is ignored.

    \sa TaskTree(const Tasking::Group &recipe), start()
*/
void TaskTree::setRecipe(const Group &recipe)
{
    QT_ASSERT(!isRunning(), qWarning("The TaskTree is already running, ignoring..."); return);
    QT_ASSERT(!d->m_guard.isLocked(), qWarning("The setRecipe() is called from one of the"
                                               "TaskTree handlers, ignoring..."); return);
    // TODO: Should we clear the m_storageHandlers, too?
    d->m_storages.clear();
    d->m_root.emplace(d, recipe);
}

/*!
    Starts the task tree.

    Use setRecipe() or the constructor to set the declarative description according to which
    the task tree will execute the contained tasks and handle finished tasks.

    When the task tree is empty, that is, constructed with a default constructor,
    a call to \e start is no-op and the relevant warning message is issued.

    Otherwise, when the task tree is already running, a call to \e start is ignored and the
    relevant warning message is issued.

    Otherwise, the task tree is started.

    The started task tree may finish synchronously,
    for example when the main group's start handler returns SetupResult::StopWithError.
    For this reason, the connections to the done and errorOccurred signals should be
    established before calling start. Use isRunning() in order to detect whether
    the task tree is still running after a call to start().

    The task tree implementation relies on the running event loop for listening to the tasks'
    done signals. Make sure you have a QEventLoop or QCoreApplication or one of its
    subclasses running (or about to be run) when calling this method.

    \sa TaskTree(const Tasking::Group &recipe), setRecipe(), isRunning(), stop()
*/
void TaskTree::start()
{
    QT_ASSERT(!isRunning(), qWarning("The TaskTree is already running, ignoring..."); return);
    QT_ASSERT(!d->m_guard.isLocked(), qWarning("The start() is called from one of the"
                                               "TaskTree handlers, ignoring..."); return);
    d->start();
}

/*!
    \fn void TaskTree::started()

    This signal is emitted when the task tree is started. The emission of this signal is
    followed synchronously by the progressValueChanged() signal with an initial \c 0 value.

    \sa start(), done(), errorOccurred()
*/

/*!
    \fn void TaskTree::done()

    This signal is emitted when the task tree finished with success.
    The task tree neither calls any handler, nor emits any signal anymore after this signal
    was emitted.

    Don't delete the task tree directly from this signal's handler. Use deleteLater() instead.

    \sa started(), errorOccurred()
*/

/*!
    \fn void TaskTree::errorOccurred()

    This signal is emitted when the task tree finished with an error.
    The task tree neither calls any handler, nor emits any signal anymore after this signal
    was emitted.

    Don't delete the task tree directly from this signal's handler. Use deleteLater() instead.

    \sa started(), done()
*/

/*!
    Stops the running task tree.

    Stops all the running tasks immediately.
    All running tasks finish with an error, invoking their error handlers.
    All running groups dispatch their handlers according to their workflow policies,
    invoking one of their end handlers. The storages' onStorageDone() handlers are invoked, too.
    The \l progressValueChanged signals are also being sent.
    This behavior may always be relied on.

    The \l stop is executed synchronously, so that after a call to \e stop
    all running tasks are finished and the tree is already stopped.
    It's guaranteed that the stop will run quickly, without any blocking wait for
    the currently running tasks to finish, provided the used tasks implement their destructors
    in a non-blocking way.

    When the task tree is empty, that is, constructed with a default constructor,
    a call to \e stop is no-op and the relevant warning message is issued.

    Otherwise, when the task tree wasn't started, a call to stop is ignored.

    \note Do not call this function directly from any of the running task's handlers
          or task tree's signals.

    \sa ~TaskTree()
*/
void TaskTree::stop()
{
    QT_ASSERT(!d->m_guard.isLocked(), qWarning("The stop() is called from one of the"
                                               "TaskTree handlers, ignoring..."); return);
    d->stop();
}

/*!
    Returns \c true if the task tree is currently running; otherwise returns \c false.

    \sa start(), stop()
*/
bool TaskTree::isRunning() const
{
    return bool(d->m_runtimeRoot);
}

/*!
    Executes a local event loop with QEventLoop::ExcludeUserInputEvents and starts the task tree.

    Returns \c true if the task tree finished successfully; otherwise returns \c false.

    \note Avoid using this method from the main thread. Use asynchronous start() instead.
          This method is to be used in non-main threads or in auto tests.

    \sa start()
*/
DoneWith TaskTree::runBlocking()
{
    QPromise<void> dummy;
    dummy.start();
    return runBlocking(dummy.future());
}

/*!
    \overload runBlocking()

    The passed \a future is used for listening to the cancel event.
    When the task tree is canceled, this method cancels the passed \a future.
*/
DoneWith TaskTree::runBlocking(const QFuture<void> &future)
{
    if (future.isCanceled())
        return DoneWith::Cancel;

    DoneWith doneWith = DoneWith::Cancel;
    QEventLoop loop;
    connect(this, &TaskTree::done, &loop, [&loop, &doneWith](DoneWith result) {
        doneWith = result;
        // Otherwise, the tasks from inside the running tree that were deleteLater()
        // will be leaked. Refer to the QObject::deleteLater() docs.
        QMetaObject::invokeMethod(&loop, [&loop] { loop.quit(); }, Qt::QueuedConnection);
    });
    QFutureWatcher<void> watcher;
    connect(&watcher, &QFutureWatcherBase::canceled, this, &TaskTree::stop);
    watcher.setFuture(future);

    QTimer::singleShot(0, this, &TaskTree::start);

    loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (doneWith == DoneWith::Cancel) {
        auto nonConstFuture = future;
        nonConstFuture.cancel();
    }
    return doneWith;
}

/*!
    Constructs a temporary task tree using the passed \a recipe and runs it blocking.

    The optionally provided \a timeout is used to stop the tree automatically after
    \a timeout milliseconds have passed.

    Returns \c true if the task tree finished successfully; otherwise returns \c false.

    \note Avoid using this method from the main thread. Use asynchronous start() instead.
          This method is to be used in non-main threads or in auto tests.

    \sa start()
*/
DoneWith TaskTree::runBlocking(const Group &recipe, milliseconds timeout)
{
    QPromise<void> dummy;
    dummy.start();
    return TaskTree::runBlocking(recipe, dummy.future(), timeout);
}

/*!
    \overload runBlocking(const Group &recipe, milliseconds timeout)

    The passed \a future is used for listening to the cancel event.
    When the task tree is canceled, this method cancels the passed \a future.
*/
DoneWith TaskTree::runBlocking(const Group &recipe, const QFuture<void> &future, milliseconds timeout)
{
    const Group root = timeout == milliseconds::max() ? recipe
                                                      : Group { recipe.withTimeout(timeout) };
    TaskTree taskTree(root);
    return taskTree.runBlocking(future);
}

/*!
    Returns the number of asynchronous tasks contained in the stored recipe.

    \note The returned number doesn't include Sync tasks.
    \note Any task or group that was set up using withTimeout() increases the total number of
          tasks by \c 1.

    \sa setRecipe(), progressMaximum()
*/
int TaskTree::taskCount() const
{
    return d->m_root ? d->m_root->taskCount() : 0;
}

/*!
    \fn void TaskTree::progressValueChanged(int value)

    This signal is emitted when the running task tree finished, stopped, or skipped some tasks.
    The \a value gives the current total number of finished, stopped or skipped tasks.
    When the task tree is started, and after the started() signal was emitted,
    this signal is emitted with an initial \a value of \c 0.
    When the task tree is about to finish, and before the done() or errorOccurred() signal
    is emitted, this signal is emitted with the final \a value of progressMaximum().

    \sa progressValue(), progressMaximum()
*/

/*!
    \fn int TaskTree::progressMaximum() const

    Returns the maximum progressValue().

    \note Currently, it's the same as taskCount(). This might change in the future.

    \sa progressValue()
*/

/*!
    Returns the current progress value, which is between the \c 0 and progressMaximum().

    The returned number indicates how many tasks have been already finished, stopped, or skipped
    while the task tree is running.
    When the task tree is started, this number is set to \c 0.
    When the task tree is finished, this number always equals progressMaximum().

    \sa progressMaximum()
*/
int TaskTree::progressValue() const
{
    return d->m_progressValue;
}

/*!
    \fn template <typename StorageStruct, typename StorageHandler> void TaskTree::onStorageSetup(const TreeStorage<StorageStruct> &storage, StorageHandler &&handler)

    Installs a storage setup \a handler for the \a storage to pass the initial data
    dynamically to the running task tree.

    The \c StorageHandler takes a reference to the \c StorageStruct instance:

    \code
        static void save(const QString &fileName, const QByteArray &array) { ... }

        TreeStorage<QByteArray> storage;

        const auto onSaverSetup = [storage](ConcurrentCall<void> &concurrent) {
            concurrent.setConcurrentCallData(&save, "foo.txt", *storage);
        };

        const Group root {
            Storage(storage),
            ConcurrentCallTask(onSaverSetup)
        };

        TaskTree taskTree(root);
        auto initStorage = [](QByteArray &storage){
            storage = "initial content";
        };
        taskTree.onStorageSetup(storage, initStorage);
        taskTree.start();
    \endcode

    When the running task tree enters a Group where the \a storage is placed in,
    it creates a \c StorageStruct instance, ready to be used inside this group.
    Just after the \c StorageStruct instance is created, and before any handler of this group
    is called, the task tree invokes the passed \a handler. This enables setting up
    initial content for the given storage dynamically. Later, when any group's handler is invoked,
    the task tree activates the created and initialized storage, so that it's available inside
    any group's handler.

    \sa onStorageDone()
*/

/*!
    \fn template <typename StorageStruct, typename StorageHandler> void TaskTree::onStorageDone(const TreeStorage<StorageStruct> &storage, StorageHandler &&handler)

    Installs a storage done \a handler for the \a storage to retrieve the final data
    dynamically from the running task tree.

    The \c StorageHandler takes a const reference to the \c StorageStruct instance:

    \code
        static QByteArray load(const QString &fileName) { ... }

        TreeStorage<QByteArray> storage;

        const auto onLoaderSetup = [storage](ConcurrentCall<void> &concurrent) {
            concurrent.setConcurrentCallData(&load, "foo.txt");
        };
        const auto onLoaderDone = [storage](const ConcurrentCall<void> &concurrent) {
            *storage = concurrent.result();
        };

        const Group root {
            Storage(storage),
            ConcurrentCallTask(onLoaderDone, onLoaderDone)
        };

        TaskTree taskTree(root);
        auto collectStorage = [](const QByteArray &storage){
            qDebug() << "final content" << storage;
        };
        taskTree.onStorageDone(storage, collectStorage);
        taskTree.start();
    \endcode

    When the running task tree is about to leave a Group where the \a storage is placed in,
    it destructs a \c StorageStruct instance.
    Just before the \c StorageStruct instance is destructed, and after all possible handlers from
    this group were called, the task tree invokes the passed \a handler. This enables reading
    the final content of the given storage dynamically and processing it further outside of
    the task tree.

    This handler is called also when the running tree is stopped. However, it's not called
    when the running tree is destructed.

    \sa onStorageSetup()
*/

void TaskTree::setupStorageHandler(const TreeStorageBase &storage,
                                   TreeStorageBase::StorageVoidHandler setupHandler,
                                   TreeStorageBase::StorageVoidHandler doneHandler)
{
    auto it = d->m_storageHandlers.find(storage);
    if (it == d->m_storageHandlers.end()) {
        d->m_storageHandlers.insert(storage, {setupHandler, doneHandler});
        return;
    }
    if (setupHandler) {
        QT_ASSERT(!it->m_setupHandler,
                  qWarning("The storage has its setup handler defined, overriding..."));
        it->m_setupHandler = setupHandler;
    }
    if (doneHandler) {
        QT_ASSERT(!it->m_doneHandler,
                  qWarning("The storage has its done handler defined, overriding..."));
        it->m_doneHandler = doneHandler;
    }
}

TaskTreeTaskAdapter::TaskTreeTaskAdapter()
{
    connect(task(), &TaskTree::done, this,
            [this](DoneWith result) { emit done(toDoneResult(result)); });
}

void TaskTreeTaskAdapter::start()
{
    task()->start();
}

using TimeoutCallback = std::function<void()>;

struct TimerData
{
    system_clock::time_point m_deadline;
    QPointer<QObject> m_context;
    TimeoutCallback m_callback;
};

struct TimerThreadData
{
    Q_DISABLE_COPY_MOVE(TimerThreadData)

    QHash<int, TimerData> m_timerIdToTimerData = {};
    QMultiMap<system_clock::time_point, int> m_deadlineToTimerId = {};
    int m_timerIdCounter = 0;
};

// Please note the thread_local keyword below guarantees a separate instance per thread.
static thread_local TimerThreadData s_threadTimerData = {};

static void removeTimerId(int timerId)
{
    const auto it = s_threadTimerData.m_timerIdToTimerData.constFind(timerId);
    QT_ASSERT(it != s_threadTimerData.m_timerIdToTimerData.cend(),
              qWarning("Removing active timerId failed."); return);

    const system_clock::time_point deadline = it->m_deadline;
    s_threadTimerData.m_timerIdToTimerData.erase(it);

    const int removedCount = s_threadTimerData.m_deadlineToTimerId.remove(deadline, timerId);
    QT_ASSERT(removedCount == 1, qWarning("Removing active timerId failed."); return);
}

static void handleTimeout(int timerId)
{
    const auto itData = s_threadTimerData.m_timerIdToTimerData.constFind(timerId);
    if (itData == s_threadTimerData.m_timerIdToTimerData.cend())
        return; // The timer was already activated.

    const auto deadline = itData->m_deadline;
    while (true) {
        const auto itMap = s_threadTimerData.m_deadlineToTimerId.cbegin();
        if (itMap == s_threadTimerData.m_deadlineToTimerId.cend())
            return;

        if (itMap.key() > deadline)
            return;

        const auto it = s_threadTimerData.m_timerIdToTimerData.constFind(itMap.value());
        if (it == s_threadTimerData.m_timerIdToTimerData.cend()) {
            s_threadTimerData.m_deadlineToTimerId.erase(itMap);
            QT_CHECK(false);
            return;
        }

        const TimerData timerData = it.value();
        s_threadTimerData.m_timerIdToTimerData.erase(it);
        s_threadTimerData.m_deadlineToTimerId.erase(itMap);

        if (timerData.m_context)
            timerData.m_callback();
    }
}

static int scheduleTimeout(milliseconds timeout, QObject *context, const TimeoutCallback &callback)
{
    const int timerId = ++s_threadTimerData.m_timerIdCounter;
    const system_clock::time_point deadline = system_clock::now() + timeout;
    QTimer::singleShot(timeout, context, [timerId] { handleTimeout(timerId); });
    s_threadTimerData.m_timerIdToTimerData.emplace(timerId, TimerData{deadline, context, callback});
    s_threadTimerData.m_deadlineToTimerId.insert(deadline, timerId);
    return timerId;
}

TimeoutTaskAdapter::TimeoutTaskAdapter()
{
    *task() = std::chrono::milliseconds::zero();
}

TimeoutTaskAdapter::~TimeoutTaskAdapter()
{
    if (m_timerId)
        removeTimerId(*m_timerId);
}

void TimeoutTaskAdapter::start()
{
    m_timerId = scheduleTimeout(*task(), this, [this] {
        m_timerId = {};
        emit done(DoneResult::Success);
    });
}

} // namespace Tasking
