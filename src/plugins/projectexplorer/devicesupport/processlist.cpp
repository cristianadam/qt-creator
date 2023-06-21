// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "processlist.h"

#include "../projectexplorertr.h"

#include <utils/processinfo.h>
#include <utils/qtcassert.h>
#include <utils/treemodel.h>
#include <utils/processinfo.h>

#include <QTimer>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

using namespace Utils;

namespace ProjectExplorer {
namespace Internal {

enum State { Inactive, Listing, Killing };

class DeviceProcessTreeItem : public TreeItem
{
public:
    DeviceProcessTreeItem(const ProcessInfo &p, Qt::ItemFlags f) : process(p), fl(f) {}

    QVariant data(int column, int role) const final;
    Qt::ItemFlags flags(int) const final { return fl; }

    ProcessInfo process;
    Qt::ItemFlags fl;
};

class DeviceProcessListPrivate
{
public:
    DeviceProcessListPrivate(const IDevice::ConstPtr &device)
        : device(device)
    { }

    qint64 ownPid = -1;
    const IDevice::ConstPtr device;
    State state = Inactive;
    TreeModel<TypedTreeItem<DeviceProcessTreeItem>, DeviceProcessTreeItem> model;
};

} // namespace Internal

using namespace Internal;

ProcessList::ProcessList(const IDevice::ConstPtr &device, QObject *parent)
    : QObject(parent), d(std::make_unique<DeviceProcessListPrivate>(device))
{
    d->model.setHeader({Tr::tr("Process ID"), Tr::tr("Command Line")});

#if defined(Q_OS_UNIX)
    setOwnPid(getpid());
#elif defined(Q_OS_WIN)
    setOwnPid(GetCurrentProcessId());
#endif
}

ProcessList::~ProcessList() = default;

void ProcessList::update()
{
    QTC_ASSERT(d->state == Inactive, return);
    QTC_ASSERT(device(), return);

    d->model.clear();
    d->model.rootItem()->appendChild(
                new DeviceProcessTreeItem(
                    {0, Tr::tr("Fetching process list. This might take a while."), ""},
                    Qt::NoItemFlags));
    d->state = Listing;
    doUpdate();
}

void ProcessList::reportProcessListUpdated(const QList<ProcessInfo> &processes)
{
    QTC_ASSERT(d->state == Listing, return);
    setFinished();
    d->model.clear();
    for (const ProcessInfo &process : processes) {
        Qt::ItemFlags fl;
        if (process.processId != d->ownPid)
            fl = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        d->model.rootItem()->appendChild(new DeviceProcessTreeItem(process, fl));
    }

    emit processListUpdated();
}

void ProcessList::killProcess(int row)
{
    QTC_ASSERT(row >= 0 && row < d->model.rootItem()->childCount(), return);
    QTC_ASSERT(d->state == Inactive, return);
    QTC_ASSERT(device(), return);

    d->state = Killing;
    doKillProcess(at(row));
}

void ProcessList::setOwnPid(qint64 pid)
{
    d->ownPid = pid;
}

void ProcessList::reportProcessKilled()
{
    QTC_ASSERT(d->state == Killing, return);
    setFinished();
    emit processKilled();
}

ProcessInfo ProcessList::at(int row) const
{
    return d->model.rootItem()->childAt(row)->process;
}

QAbstractItemModel *ProcessList::model() const
{
    return &d->model;
}

QVariant DeviceProcessTreeItem::data(int column, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
        if (column == 0)
            return process.processId ? process.processId : QVariant();
        else
            return process.commandLine;
    }
    return QVariant();
}

void ProcessList::setFinished()
{
    d->state = Inactive;
}

IDevice::ConstPtr ProcessList::device() const
{
    return d->device;
}

void ProcessList::reportError(const QString &message)
{
    QTC_ASSERT(d->state != Inactive, return);
    setFinished();
    emit error(message);
}

void ProcessList::doKillProcess(const ProcessInfo &processInfo)
{
    m_signalOperation = device()->signalOperation();
    connect(m_signalOperation.data(),
            &DeviceProcessSignalOperation::finished,
            this,
            &ProcessList::reportDelayedKillStatus);
    m_signalOperation->killProcess(processInfo.processId);
}

void ProcessList::handleUpdate()
{
    reportProcessListUpdated(ProcessInfo::processInfoList(ProcessList::device()->rootPath()));
}

void ProcessList::doUpdate()
{
    QTimer::singleShot(0, this, &ProcessList::handleUpdate);
}

void ProcessList::reportDelayedKillStatus(const QString &errorMessage)
{
    if (errorMessage.isEmpty())
        reportProcessKilled();
    else
        reportError(errorMessage);

    m_signalOperation.reset();
}

} // ProjectExplorer
