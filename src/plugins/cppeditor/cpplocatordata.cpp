// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpplocatordata.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"
#endif

#include <utils/stringtable.h>

#include <QThread>
#include <QtConcurrentMap>

#include <algorithm>

using namespace Utils;

namespace CppEditor {

using namespace Internal;

// How many files the cxx front end reads at once.
//
// Half of what the machine reports, not all of it and not one fewer, because
// more measured *worse*: reading the cplusplus project (118 files) took 15.4s
// on six and 17.6s on eleven. A read is allocation-heavy -- about a quarter
// of it is malloc -- so the readers contend rather than scale, and on a
// machine whose cores are not alike the slow half of them is a poor place to
// put one. Half also halves the memory a batch holds, which is what bounds
// this on a smaller machine.
static int cxxFrontendReaderCount()
{
    return std::max(1, QThread::idealThreadCount() / 2);
}

CppLocatorData::CppLocatorData()
{
    m_cxxFrontendPool.setMaxThreadCount(cxxFrontendReaderCount());
    // Measured as free (15.4s either way) and it keeps a project's worth of
    // reads from crowding out the work somebody is waiting on.
    m_cxxFrontendPool.setThreadPriority(QThread::LowPriority);

    connect(&m_cxxFrontendWatcher, &QFutureWatcher<ReadFile>::resultsReadyAt,
            this, &CppLocatorData::takeCxxFrontendResults);
    connect(&m_cxxFrontendWatcher, &QFutureWatcher<ReadFile>::finished,
            this, &CppLocatorData::readPendingWithCxxFrontend);
}

CppLocatorData::~CppLocatorData()
{
    // A read holds nothing of this object, but it delivers what it found to
    // it, so none may still be running when it goes.
    m_cxxFrontendWatcher.cancel();
    m_cxxFrontendWatcher.waitForFinished();
}

// What \a document declares, as the entries an index keeps.
//
// Worked out here rather than under the lock: this is the whole cost of
// keeping the index, and holding the lock through it would stop every
// locator query for as long as it takes.
static IndexItem::Ptr entriesFor(const CPlusPlus::Document::Ptr &document)
{
    // A searcher of this call's own, since two files may be indexed at once.
    SearchSymbols search;
    search.setSymbolsToSearchFor(SymbolType::Enums | SymbolType::Classes
                                 | SymbolType::Functions | SymbolType::TypeAliases);
    return search(document);
}

QList<IndexItem::Ptr> CppLocatorData::findSymbols(IndexItem::ItemType type,
                                                  const QString &symbolName) const
{
    QList<IndexItem::Ptr> matches;
    filterAllFiles([&](const IndexItem::Ptr &info) {
        if (info->type() & type) {
            if (info->symbolName() == symbolName || info->scopedSymbolName() == symbolName)
                matches << info;
        }
        if (info->type() & IndexItem::Enum)
            return IndexItem::Continue;
        return IndexItem::Recurse;
    });
    return matches;
}

void CppLocatorData::onDocumentUpdated(const CPlusPlus::Document::Ptr &document)
{
    if (document->filePath().suffix() == "moc")
        return;

    // Worked out here because this is the one moment the document has a tree:
    // the source processor lets go of its source and its AST as soon as this
    // returns.
    if (const IndexItem::Ptr forThisFile = entriesFor(document)) {
        QMutexLocker locker(&m_infosByFileMutex);
        m_infosByFile.insert(document->filePath().intern(), forThisFile);
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // And queued for the other model to read again and answer better, which
    // it does by parsing the file's whole include closure -- about a second,
    // where the walk above is a small fraction of one. So it must not happen
    // here: this is called for one file after another on the indexer's single
    // thread, which would then wait for every one of them, and that is what
    // made the model's index the cost it was. It goes to a pool instead, and
    // what comes back replaces the entries above. Until it does, the file has
    // the built-in reading's entries rather than none.
    if (!cxxFrontendModelRequested())
        return;

    {
        QMutexLocker locker(&m_pendingMutex);
        m_pending.insert(document->filePath());
        if (m_readScheduled)
            return;
        m_readScheduled = true;
    }
    QMetaObject::invokeMethod(this, [this] { readPendingWithCxxFrontend(); },
                              Qt::QueuedConnection);
#endif
}

void CppLocatorData::readPendingWithCxxFrontend()
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (m_cxxFrontendWatcher.isRunning()) {
        // Its finishing calls this again and takes whatever has accumulated
        // by then, so nothing more need be posted until it does -- and the
        // batch it is delivering is not replaced half-way through, which
        // would drop the answers it was about to give.
        QMutexLocker locker(&m_pendingMutex);
        m_readScheduled = true;
        return;
    }

    FilePaths batch;
    {
        QMutexLocker locker(&m_pendingMutex);
        m_readScheduled = false;
        // Whatever the last batch did not deliver -- it was cancelled, or a
        // file it held was removed -- is not owed any longer.
        m_beingRead = 0;
        // And what was removed while it ran has been kept out of the index
        // already; a file removed and then indexed again stands in the batch
        // below on its own account.
        m_removedSinceRead.clear();
        if (m_pending.isEmpty())
            return;
        batch = FilePaths(m_pending.cbegin(), m_pending.cend());
        m_pending.clear();
        m_beingRead = batch.size();
    }

    // Read once for the whole batch, and here rather than on the pool: this
    // is the thread the indexer reports to, and what these are read off is
    // built-in documents whose source it clears as it goes.
    const CxxFrontendIndexInputs inputs = cxxFrontendIndexInputs(CppModelManager::snapshot());

    m_cxxFrontendWatcher.setFuture(
        QtConcurrent::mapped(&m_cxxFrontendPool, batch, [inputs](const FilePath &filePath) {
            const std::optional<IndexItem::Ptr> entries
                = cxxFrontendIndexTreeFor(inputs, filePath);
            return ReadFile{filePath, entries ? *entries : IndexItem::Ptr()};
        }));
#endif
}

void CppLocatorData::takeCxxFrontendResults(int begin, int end)
{
    // Both locks, pending first. Nothing takes them the other way round: the
    // two are held one after the other everywhere else.
    QMutexLocker pending(&m_pendingMutex);
    m_beingRead -= end - begin;
    QMutexLocker infos(&m_infosByFileMutex);
    for (int i = begin; i < end; ++i) {
        const ReadFile &read = m_cxxFrontendWatcher.resultAt(i);

        // Nothing where that model declined the file -- Objective-C, or one
        // it could not read -- and then the built-in walk's entries, which
        // are already here, stand.
        if (!read.second)
            continue;

        // Waiting to be read again, so this is the older of the two answers
        // and the newer one is on its way.
        if (m_pending.contains(read.first))
            continue;

        // Taken out of the index since this reading began -- the project was
        // closed, or the file was. Putting the entries in now would name
        // things nothing can reach, and nothing would take them out again.
        if (m_removedSinceRead.contains(read.first))
            continue;

        m_infosByFile.insert(read.first.intern(), read.second);
    }
}

int CppLocatorData::cxxFrontendFilesOutstanding() const
{
    QMutexLocker locker(&m_pendingMutex);
    return m_pending.size() + m_beingRead;
}

void CppLocatorData::onAboutToRemoveFiles(const FilePaths &files)
{
    if (files.isEmpty())
        return;

    {
        QMutexLocker locker(&m_pendingMutex);
        for (const FilePath &file : files) {
            m_pending.remove(file);
            m_removedSinceRead.insert(file);
        }
    }

    QMutexLocker locker(&m_infosByFileMutex);

    for (const FilePath &file : files)
        m_infosByFile.remove(file);

    StringTable::scheduleGC();
}

} // namespace CppEditor
