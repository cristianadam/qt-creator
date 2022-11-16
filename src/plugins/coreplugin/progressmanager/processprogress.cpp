// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "processprogress.h"

#include "progressmanager.h"
#include "../coreplugintr.h"

#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QFutureWatcher>

using namespace Utils;

namespace Core {

class ProcessProgressPrivate : public QObject
{
public:
    explicit ProcessProgressPrivate(ProcessProgress *progress, QtcProcess *process);
    ~ProcessProgressPrivate();

    QString displayName() const;
    void parseProgress(const QString &inputText);

    QtcProcess *m_process = nullptr;
    ProgressParser m_parser = {};
    QFutureWatcher<void> m_watcher;
    QFutureInterface<void> m_futureInterface;
    QString m_displayName;
};

ProcessProgressPrivate::ProcessProgressPrivate(ProcessProgress *progress, QtcProcess *process)
    : QObject(progress)
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
    QTC_ASSERT(!result.isEmpty(), result = Tr::tr("Unknown"));
    result[0] = result[0].toTitleCase();
    if (!commandLine.arguments().isEmpty())
        result += ' ' + commandLine.splitArguments().at(0);
    return result;
}

void ProcessProgressPrivate::parseProgress(const QString &inputText)
{
    QTC_ASSERT(m_parser, return);
    m_parser(m_futureInterface, inputText);
}

/*!
    \class Core::ProcessProgress

    \brief The ProcessProgress class is responsible for showing progress of the running process.

    It's possible to cancel the running process automatically after pressing a small 'x'
    indicator on progress panel. In this case QtcProcess::stop() method is being called.
*/

ProcessProgress::ProcessProgress(QtcProcess *process)
    : QObject(process)
    , d(new ProcessProgressPrivate(this, process))
{
    connect(&d->m_watcher, &QFutureWatcher<void>::canceled, this, [this] {
        d->m_process->stop(); // TODO: should we have different cancel policies?
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

void ProcessProgress::setDisplayName(const QString &name)
{
    d->m_displayName = name;
}

void ProcessProgress::setProgressParser(const ProgressParser &parser)
{
    if (d->m_parser) {
        disconnect(d->m_process, &QtcProcess::textOnStandardOutput,
                   d, &ProcessProgressPrivate::parseProgress);
        disconnect(d->m_process, &QtcProcess::textOnStandardError,
                   d, &ProcessProgressPrivate::parseProgress);
    }
    d->m_parser = parser;
    if (!d->m_parser)
        return;

    QTC_ASSERT(d->m_process->textChannelMode(Channel::Output) != TextChannelMode::Off,
               qWarning() << "Setting progress parser on a process without changing process' "
               "text channel mode is no-op.");

    connect(d->m_process, &QtcProcess::textOnStandardOutput,
            d, &ProcessProgressPrivate::parseProgress);
    connect(d->m_process, &QtcProcess::textOnStandardError,
            d, &ProcessProgressPrivate::parseProgress);
}

} // namespace Core
