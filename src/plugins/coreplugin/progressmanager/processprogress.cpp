// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "processprogress.h"

#include "coreplugintr.h"
#include "progressmanager.h"

#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QFutureWatcher>

using namespace Utils;

namespace Core {

class ProcessProgressPrivate
{
public:
    explicit ProcessProgressPrivate(ProcessProgress *progress, QtcProcess *process);
    ~ProcessProgressPrivate();

    QString displayName() const;

    ProcessProgress *q = nullptr;
    QtcProcess *m_process = nullptr;
    ProgressParser m_parser = {};
    QFutureWatcher<void> m_watcher;
    QFutureInterface<void> m_futureInterface;
    QString m_displayName;
};

ProcessProgressPrivate::ProcessProgressPrivate(ProcessProgress *progress, QtcProcess *process)
    : q(progress)
    , m_process(process)
{
}

ProcessProgressPrivate::~ProcessProgressPrivate()
{
    if (m_futureInterface.isRunning()) {
        m_futureInterface.reportCanceled();
        m_futureInterface.reportFinished();
        // TODO: should we stop the process? Or just mark the process canceled?
        // What happens to task in progress manager?
    }
}

QString ProcessProgressPrivate::displayName() const
{
    if (!m_displayName.isEmpty())
        return m_displayName;
    const CommandLine commandLine = m_process->commandLine();
    QString result = commandLine.executable().baseName();
    if (!result.isEmpty())
        result[0] = result[0].toTitleCase();
    else
        result = Tr::tr("Unknown");
    if (!commandLine.arguments().isEmpty())
        result += ' ' + commandLine.splitArguments().at(0);
    return result;
}

/*!
    \class Core::ProcessProgress

    \brief The ProcessProgress class is responsible for showing progress of the running process.

    It's able to cancel the running process automatically after pressing a small 'x' indicator on
    progress panel. In this case the QtcProcess::stop() method is being called.
*/

ProcessProgress::ProcessProgress(QtcProcess *process)
    : QObject(process)
    , d(new ProcessProgressPrivate(this, process))
{
    connect(&d->m_watcher, &QFutureWatcher<void>::canceled, this, [this] {
        d->m_futureInterface.reportCanceled(); // TODO: is it needed? Should be reported back on done.
        d->m_process->stop(); // TODO: should we have different cancel policies?
        // TODO: should we wait for finished?
    });
    connect(d->m_process, &QtcProcess::starting, this, [this] {
        d->m_futureInterface = QFutureInterface<void>();
        d->m_futureInterface.setProgressRange(0, 1);
        d->m_watcher.setFuture(d->m_futureInterface.future());
        d->m_futureInterface.reportStarted();

        const QString name = d->displayName();
        const auto id = Id::fromString(name + ".action");
        if (d->m_parser) {
            ProgressManager::addTask(d->m_futureInterface.future(), name, id);
        } else {
            ProgressManager::addTimedTask(d->m_futureInterface, name, id,
                                          qMax(2, d->m_process->timeoutS() / 5));
        }
    });
    connect(d->m_process, &QtcProcess::done, this, [this] {
        if (d->m_process->result() != ProcessResult::FinishedWithSuccess)
            d->m_futureInterface.reportCanceled();
        d->m_futureInterface.reportFinished();
    });
}

ProcessProgress::~ProcessProgress()
{
    delete d;
}

void ProcessProgress::setDisplayName(const QString &name)
{
    d->m_displayName = name;
}

void ProcessProgress::setProgressParser(const ProgressParser &parser)
{
    d->m_parser = parser;
    QTC_ASSERT(d->m_process->textChannelMode(Channel::Output) != TextChannelMode::Off,
               qWarning() << "Setting progress parser on a process without changing process' "
               "text channel mode is no-op.");

    auto parseProgress = [this](const QString &inputText) {
        if (d->m_parser)
            d->m_parser(d->m_futureInterface, inputText);
    };
    connect(d->m_process, &QtcProcess::textOnStandardOutput, this, parseProgress);
    connect(d->m_process, &QtcProcess::textOnStandardError, this, parseProgress);
}

} // namespace Core
