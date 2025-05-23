// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "timelinenotesmodel.h"
#include "timelinetracefile.h"
#include "timelinetracemanager.h"
#include "tracingtr.h"

#include <utils/async.h>
#include <utils/qtcassert.h>

#include <QFile>

#include <memory>

namespace Timeline {

TraceEventTypeStorage::~TraceEventTypeStorage()
{
}

TraceEventStorage::~TraceEventStorage()
{
}

class TimelineTraceManager::TimelineTraceManagerPrivate
{
public:
    std::unique_ptr<TraceEventTypeStorage> typeStorage;
    std::unique_ptr<TraceEventStorage> eventStorage;

    TimelineNotesModel *notesModel = nullptr;

    int numEvents = 0;
    quint64 availableFeatures = 0;
    quint64 visibleFeatures = 0;
    quint64 recordedFeatures = 0;
    bool aggregateTraces = false;

    QHash<quint8, QList<TraceEventLoader>> eventLoaders;
    QList<Initializer> initializers;
    QList<Finalizer> finalizers;
    QList<Clearer> clearers;

    qint64 traceStart = -1;
    qint64 traceEnd = -1;

    void dispatch(const TraceEvent &event, const TraceEventType &type);
    void reset();
    void updateTraceTime(qint64 time);
};

TimelineTraceManager::TimelineTraceManager(std::unique_ptr<TraceEventStorage> &&eventStorage,
                                           std::unique_ptr<TraceEventTypeStorage> &&typeStorage,
                                           QObject *parent) :
    QObject(parent), d(new TimelineTraceManagerPrivate)
{
    d->eventStorage = std::move(eventStorage);
    d->typeStorage = std::move(typeStorage);
}

TimelineTraceManager::~TimelineTraceManager()
{
    delete d;
}

TimelineNotesModel *TimelineTraceManager::notesModel() const
{
    return d->notesModel;
}

bool TimelineTraceManager::isEmpty() const
{
    return d->eventStorage->size() == 0;
}

int TimelineTraceManager::numEvents() const
{
    return d->numEvents;
}

int TimelineTraceManager::numEventTypes() const
{
    return d->typeStorage->size();
}

void TimelineTraceManager::swapEventStorage(std::unique_ptr<TraceEventStorage> &other)
{
    d->eventStorage.swap(other);
}

const TraceEventStorage *TimelineTraceManager::eventStorage() const
{
    return d->eventStorage.get();
}

const TraceEventTypeStorage *TimelineTraceManager::typeStorage() const
{
    return d->typeStorage.get();
}

void TimelineTraceManager::appendEvent(TraceEvent &&event)
{
    d->dispatch(event, d->typeStorage->get(event.typeIndex()));
    d->eventStorage->append(std::move(event));
}

const TraceEventType &TimelineTraceManager::eventType(int typeId) const
{
    return d->typeStorage->get(typeId);
}

void TimelineTraceManager::setEventType(int typeId, TraceEventType &&type)
{
    d->recordedFeatures |= (1ull << type.feature());
    d->typeStorage->set(typeId, std::move(type));
}

int TimelineTraceManager::appendEventType(TraceEventType &&type)
{
    d->recordedFeatures |= (1ull << type.feature());
    return d->typeStorage->append(std::move(type));
}

void TimelineTraceManager::registerFeatures(quint64 features, TraceEventLoader eventLoader,
                                            Initializer initializer, Finalizer finalizer,
                                            Clearer clearer)
{
    if ((features & d->availableFeatures) != features) {
        d->availableFeatures |= features;
        emit availableFeaturesChanged(d->availableFeatures);
    }

    if ((features & d->visibleFeatures) != features) {
        d->visibleFeatures |= features;
        emit visibleFeaturesChanged(d->visibleFeatures);
    }

    for (quint8 feature = 0; feature != sizeof(features) * 8; ++feature) {
        if (features & (1ULL << feature)) {
            if (eventLoader)
                d->eventLoaders[feature].append(eventLoader);
        }
    }

    if (initializer)
        d->initializers.append(initializer);

    if (finalizer)
        d->finalizers.append(finalizer);

    if (clearer)
        d->clearers.append(clearer);
}

quint64 TimelineTraceManager::availableFeatures() const
{
    return d->availableFeatures;
}

quint64 TimelineTraceManager::visibleFeatures() const
{
    return d->visibleFeatures;
}

void TimelineTraceManager::setVisibleFeatures(quint64 features)
{
    if (d->visibleFeatures != features) {
        d->visibleFeatures = features;
        emit visibleFeaturesChanged(d->visibleFeatures);
    }
}

quint64 TimelineTraceManager::recordedFeatures() const
{
    return d->recordedFeatures;
}

void TimelineTraceManager::setRecordedFeatures(quint64 features)
{
    if (d->recordedFeatures != features) {
        d->recordedFeatures = features;
        emit recordedFeaturesChanged(d->recordedFeatures);
    }
}

bool TimelineTraceManager::aggregateTraces() const
{
    return d->aggregateTraces;
}

void TimelineTraceManager::setAggregateTraces(bool aggregateTraces)
{
    d->aggregateTraces = aggregateTraces;
}

void TimelineTraceManager::initialize()
{
    for (const Initializer &initializer : std::as_const(d->initializers))
        initializer();
}

void TimelineTraceManager::finalize()
{
    d->eventStorage->finalize();

    // Load notes after the timeline models have been initialized ...
    // which happens on stateChanged(Done).

    for (const Finalizer &finalizer : std::as_const(d->finalizers))
        finalizer();
}

QFuture<void> TimelineTraceManager::save(const QString &filename)
{
    TimelineTraceFile *writer = createTraceFile();
    writer->setTraceTime(traceStart(), traceEnd(), traceDuration());
    writer->setTraceManager(this);
    writer->setNotes(d->notesModel);

    connect(writer, &QObject::destroyed, this, &TimelineTraceManager::saveFinished);
    connect(writer, &TimelineTraceFile::error, this, &TimelineTraceManager::error);

    QFutureInterface<void> fi;
    fi.reportStarted();
    writer->setFuture(fi);

    Utils::asyncRun([filename, writer, fi] {
        QFile file(filename);

        if (file.open(QIODevice::WriteOnly))
            writer->save(&file);
        else
            writer->fail(Tr::tr("Could not open %1 for writing.").arg(filename));

        if (fi.isCanceled())
            file.remove();
        writer->deleteLater();
        QFutureInterface fiCopy = fi;
        fiCopy.reportFinished();
    });
    return fi.future();
}

QFuture<void> TimelineTraceManager::load(const QString &filename)
{
    clearAll();
    initialize();
    TimelineTraceFile *reader = createTraceFile();
    reader->setTraceManager(this);
    reader->setNotes(d->notesModel);

    connect(reader, &QObject::destroyed, this, &TimelineTraceManager::loadFinished);
    connect(reader, &TimelineTraceFile::error, this, &TimelineTraceManager::error);

    QFutureInterface<void> fi;
    fi.reportStarted();
    reader->setFuture(fi);
    Utils::asyncRun([filename, reader, fi] {
        QFile file(filename);

        if (file.open(QIODevice::ReadOnly))
            reader->load(&file);
        else
            reader->fail(Tr::tr("Could not open %1 for reading.").arg(filename));

        reader->deleteLater();
        QFutureInterface fiCopy = fi;
        fiCopy.reportFinished();
    });

    QFutureWatcher<void> *watcher = new QFutureWatcher<void>(reader);
    connect(watcher, &QFutureWatcherBase::canceled, this, &TimelineTraceManager::clearAll);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, reader] {
        if (!reader->isCanceled()) {
            if (reader->traceStart() >= 0)
                decreaseTraceStart(reader->traceStart());
            if (reader->traceEnd() >= 0)
                increaseTraceEnd(reader->traceEnd());
            finalize();
        }
    });
    watcher->setFuture(fi.future());
    return fi.future();
}

qint64 TimelineTraceManager::traceStart() const
{
    return d->traceStart;
}

qint64 TimelineTraceManager::traceEnd() const
{
    return d->traceEnd;
}

qint64 TimelineTraceManager::traceDuration() const
{
    return traceEnd() - traceStart();
}

void TimelineTraceManager::decreaseTraceStart(qint64 start)
{
    QTC_ASSERT(start >= 0, return);
    if (d->traceStart > start || d->traceStart == -1) {
        d->traceStart = start;
        if (d->traceEnd == -1)
            d->traceEnd = d->traceStart;
        else
            QTC_ASSERT(d->traceEnd >= d->traceStart, d->traceEnd = d->traceStart);
    }
}

void TimelineTraceManager::increaseTraceEnd(qint64 end)
{
    QTC_ASSERT(end >= 0, return);
    if (d->traceEnd < end || d->traceEnd == -1) {
        d->traceEnd = end;
        if (d->traceStart == -1)
            d->traceStart = d->traceEnd;
        else
            QTC_ASSERT(d->traceEnd >= d->traceStart, d->traceStart = d->traceEnd);
    }
}

void TimelineTraceManager::TimelineTraceManagerPrivate::updateTraceTime(qint64 time)
{
    QTC_ASSERT(time >= 0, return);
    if (traceStart > time || traceStart == -1)
        traceStart = time;
    if (traceEnd < time || traceEnd == -1)
        traceEnd = time;
    QTC_ASSERT(traceEnd >= traceStart, traceStart = traceEnd);
}

void TimelineTraceManager::setNotesModel(TimelineNotesModel *notesModel)
{
    d->notesModel = notesModel;
}

void TimelineTraceManager::clearEventStorage()
{
    d->reset();
    if (d->notesModel)
        d->notesModel->clear();
    setRecordedFeatures(0);
    d->eventStorage->clear();
}

void TimelineTraceManager::clearTypeStorage()
{
    d->typeStorage->clear();
    d->recordedFeatures = 0;
}

void TimelineTraceManager::clear()
{
    clearEventStorage();
}

void TimelineTraceManager::clearAll()
{
    clearEventStorage();
    clearTypeStorage();
}

void TimelineTraceManager::restrictByFilter(TraceEventFilter filter)
{
    if (d->notesModel)
        d->notesModel->stash();

    d->reset();

    QFutureInterface<void> future;
    replayEvents(filter(std::bind(&TimelineTraceManagerPrivate::dispatch, d,
                                  std::placeholders::_1, std::placeholders::_2)), [this] {
        initialize();
    }, [this] {
        if (d->notesModel)
            d->notesModel->restore();
        finalize();
    }, [this](const QString &message) {
        if (!message.isEmpty()) {
            emit error(Tr::tr("Could not re-read events from temporary trace file: %1\n"
                              "The trace data is lost.").arg(message));
        }
        clearAll();
    }, future);
}

void TimelineTraceManager::TimelineTraceManagerPrivate::dispatch(const TraceEvent &event,
                                                                 const TraceEventType &type)
{
    for (const TraceEventLoader &loader : eventLoaders[type.feature()])
        loader(event, type);

    if (event.timestamp() >= 0)
        updateTraceTime(event.timestamp());
    ++numEvents;
}

void TimelineTraceManager::TimelineTraceManagerPrivate::reset()
{
    traceStart = -1;
    traceEnd = -1;

    for (const Clearer &clearer : std::as_const(clearers))
        clearer();

    numEvents = 0;
}

} // namespace Timeline
