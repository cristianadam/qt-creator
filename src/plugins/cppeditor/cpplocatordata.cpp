// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpplocatordata.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendindexcache.h"
#include "cxxfrontendmodel.h"
#endif

#include <utils/hostosinfo.h>
#include <utils/stringtable.h>

#include <QThread>
#include <QtConcurrentMap>

#include <algorithm>

using namespace Utils;

namespace CppEditor {

using namespace Internal;

// About what one reader needs while it works, which is a whole translation
// unit's worth of tokens and syntax tree.
//
// Measured over a 60-file slice of this project, where the peak went 2.9,
// 3.4, 4.8 and 7.5 GB on one, two, three and six readers: a fixed couple of
// gigabytes -- the built-in model's snapshot and the index itself, which are
// there either way -- and about 0.9 GB for each reader on top.
static constexpr quint64 cxxFrontendMemoryPerReader = 1024ull * 1024 * 1024;

// How many files the cxx front end reads at once.
//
// Bounded by the cores, and then by the memory, because a reader now reads a
// whole translation unit: it is the memory that binds first on any ordinary
// machine, and running out of it is worse for the person using the editor
// than an index that takes longer.
//
// Half the cores, not all of them and not one fewer, because more measured
// *worse*: reading the cplusplus project (118 files) took 15.4s on six and
// 17.6s on eleven. A read is allocation-heavy -- about a quarter of it is
// malloc -- so the readers contend rather than scale, and on a machine whose
// cores are not alike the slow half of them is a poor place to put one.
//
// And then no more than a quarter of what is installed, an index being
// something that happens while somebody is working rather than the work. On
// the machine this was written on that quarter is nine gigabytes, so the
// cores still decide; on a laptop with eight it is what decides.
static int cxxFrontendReaderCount()
{
    // Overridable, since what a machine can spare is not always what it has:
    // a build running beside this one wants the same memory.
    if (const int asked = qEnvironmentVariableIntValue("QTC_CXX_FRONTEND_READERS"); asked > 0)
        return asked;

    const int byCores = std::max(1, QThread::idealThreadCount() / 2);

    // Nothing known about the memory: the cores decide, as they used to.
    const std::optional<quint64> installed = HostOsInfo::totalMemoryInstalledInBytes();
    if (!installed)
        return byCores;

    const auto byMemory = int(*installed / 4 / cxxFrontendMemoryPerReader);
    return std::clamp(byMemory, 1, byCores);
}

// What a reader's stack has to hold.
//
// The front end walks a file by recursion and bounds itself by counting its
// own frames -- its constant evaluator allows 512 nested calls -- but one of
// its frames is a score of C++ ones, the visitors being large. That budget
// was written for the thread the editor reads on, which gets the eight
// megabytes a main thread gets; a pooled thread gets the half a megabyte the
// system hands a plain one, and the same file that reads fine in an editor
// then overruns the guard page while being indexed.
//
// So a reader is given what the code it runs was written against. It is
// address space, not memory: only the pages a read touches are committed.
static constexpr uint cxxFrontendReaderStackSize = 8 * 1024 * 1024;

CppLocatorData::CppLocatorData()
{
    m_cxxFrontendPool.setMaxThreadCount(cxxFrontendReaderCount());
    m_cxxFrontendPool.setStackSize(cxxFrontendReaderStackSize);
    // Measured as free (15.4s either way) and it keeps a project's worth of
    // reads from crowding out the work somebody is waiting on.
    m_cxxFrontendPool.setThreadPriority(QThread::LowPriority);

    connect(&m_cxxFrontendWatcher, &QFutureWatcher<ReadResult>::resultsReadyAt,
            this, &CppLocatorData::takeCxxFrontendResults);
    connect(&m_cxxFrontendWatcher, &QFutureWatcher<ReadResult>::finished,
            this, &CppLocatorData::readWhatWasNotCovered);
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
        // Nothing pending, nothing running, nothing scheduled: the indexer
        // has been quiet and this is the first file of a fresh run. What
        // was covered in the last one says nothing about this one -- the
        // files are being read again because something changed.
        if (m_pending.isEmpty() && m_awaitingCoverage.isEmpty() && m_beingRead == 0
            && !m_readScheduled) {
            m_coveredThisRun.clear();
            m_describedThisRun.clear();
        }
        m_indexerDone = false;
        m_objectiveCSwept = false;

        // Reported means the built-in model has just read it again, so
        // whatever a reading of some translation unit said of it before may
        // be about the file as it was. It is owed a fresh answer, covered
        // or not.
        m_coveredThisRun.remove(document->filePath());
        m_describedThisRun.remove(document->filePath());
        m_pending.insert(document->filePath());
        if (m_readScheduled)
            return;
        m_readScheduled = true;
    }
    QMetaObject::invokeMethod(this, [this] { readPendingWithCxxFrontend(); },
                              Qt::QueuedConnection);
#endif
}

// Marks every Objective-C file the model knows of, and everything each of
// them includes, as answered for.
//
// This front end does not read Objective-C, so nothing will ever cover what
// one includes -- and what one includes is a thousand of AppKit's headers,
// which would otherwise be read one at a time, each as a C++ file it is
// not. The built-in walk has described them and its description stands.
//
// Done in one sweep rather than as each file is reported, because a header
// of an Objective-C file is reported long before the file itself.
void CppLocatorData::coverWhatObjectiveCBrings()
{
#ifdef QTC_WITH_CXX_FRONTEND
    const CPlusPlus::Snapshot snapshot = CppModelManager::snapshot();
    QSet<FilePath> answeredFor;
    for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
        const FilePath &filePath = it.key();
        if (!ProjectFile::isObjC(filePath))
            continue;
        answeredFor.insert(filePath);
        answeredFor.unite(snapshot.allIncludesForDocument(filePath));
    }
    if (answeredFor.isEmpty())
        return;

    QMutexLocker locker(&m_pendingMutex);
    for (const FilePath &filePath : std::as_const(answeredFor)) {
        m_coveredThisRun.insert(filePath);
        m_awaitingCoverage.remove(filePath);
    }
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

    {
        // Before anything is promoted for want of a reading to cover it.
        bool sweep = false;
        {
            QMutexLocker locker(&m_pendingMutex);
            sweep = m_indexerDone && !m_objectiveCSwept;
            m_objectiveCSwept = m_objectiveCSwept || sweep;
        }
        if (sweep)
            coverWhatObjectiveCBrings();
    }

    FilePaths batch;
    // Files the model will not read at all, so that nothing is waiting for
    // a reading of them that will never come.
    FilePaths declined;
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
        if (m_pending.isEmpty() && m_awaitingCoverage.isEmpty())
            return;

        // Only the sources are read. A reading is of a whole translation
        // unit and says what every file in it declares, so the headers are
        // covered by whichever source reaches them -- and reading each of
        // them again on its own is the bulk of what indexing costs.
        //
        // The whole of what is pending is taken all the same, so that
        // anything in m_pending while the batch runs is by definition newly
        // reported, which is what the check on a result relies on. The
        // headers wait in m_awaitingCoverage instead.
        for (const FilePath &filePath : std::as_const(m_pending)) {
            // Read as a translation unit only what the project builds as
            // one: a file with no project part of its own is written into
            // another -- moc_foo.cpp lives inside mocs_compilation.cpp,
            // and there are hundreds of those -- so reading it here would
            // read it a second time. Headers likewise.
            //
            // Getting this wrong costs time and nothing else: a file no
            // reading covers is read on its own at the end either way.
            // Objective-C, which this front end does not read. Reading it
            // would be declined, and what it includes would then be read
            // one header at a time -- a thousand of AppKit's, parsed as
            // C++, for an answer worth nothing.
            if (ProjectFile::isObjC(filePath)) {
                declined.append(filePath);
                continue;
            }

            const bool isItsOwnUnit = !ProjectFile::isHeader(ProjectFile::classify(filePath))
                                      && !CppModelManager::projectPart(filePath).isEmpty();
            if (isItsOwnUnit) {
                batch.append(filePath);
                continue;
            }
            // Already answered for by a reading that includes it. The
            // indexer reports it all the same, and reading it again would
            // say what has just been said.
            if (!m_coveredThisRun.contains(filePath))
                m_awaitingCoverage.insert(filePath);
        }
        m_pending.clear();

        // No source left to cover them, so whatever is still waiting is
        // read as a translation unit of its own after all: a header no file
        // in the project includes, or one whose includers all came from the
        // store and so read nothing. They wait until here rather than being
        // taken as they arrive, because the indexer reports a project over
        // many batches and a header usually arrives before its source.
        if (batch.isEmpty()) {
            if (!m_indexerDone)
                return;
            batch = FilePaths(m_awaitingCoverage.cbegin(), m_awaitingCoverage.cend());
            m_awaitingCoverage.clear();
        }
        m_beingRead = batch.size();
    }

    // Nothing will read what an Objective-C file includes, so it is marked
    // as answered for here instead. The built-in walk has described those
    // files already and its description stands; what must not happen is
    // each of them being read on its own at the end, as a C++ file it is
    // not. The walk is over the built-in snapshot, which only reads.
    if (!declined.isEmpty()) {
        const CPlusPlus::Snapshot snapshot = CppModelManager::snapshot();
        QSet<FilePath> answeredFor;
        for (const FilePath &filePath : std::as_const(declined)) {
            answeredFor.insert(filePath);
            answeredFor.unite(snapshot.allIncludesForDocument(filePath));
        }
        QMutexLocker locker(&m_pendingMutex);
        for (const FilePath &filePath : std::as_const(answeredFor)) {
            m_coveredThisRun.insert(filePath);
            m_awaitingCoverage.remove(filePath);
        }
    }

    if (batch.isEmpty())
        return;

    // Read once for the whole batch, and here rather than on the pool: this
    // is the thread the indexer reports to, and what these are read off is
    // built-in documents whose source it clears as it goes.
    const CxxFrontendIndexInputs inputs = cxxFrontendIndexInputs(CppModelManager::snapshot());

    // The store is kept across batches so that a header reached by a
    // thousand files is still read once, and this is where what it
    // remembers of the files goes stale: anything written since the last
    // batch has to be seen afresh.
    if (!m_cxxFrontendCache)
        m_cxxFrontendCache = std::make_unique<CxxFrontendIndexCache>(inputs.predefinedMacros);
    m_cxxFrontendCache->forgetContents();
    CxxFrontendIndexCache * const cache = m_cxxFrontendCache.get();

    // Each file's project key worked out here, not on the pool: it is read
    // off the project's data, which belongs to this thread.
    QList<Request> requests;
    requests.reserve(batch.size());
    for (const FilePath &filePath : std::as_const(batch))
        requests.append({filePath, cxxFrontendProjectKey(filePath)});

    const auto resultOf = [](const FilePath &filePath, const CxxFrontendIndexRead &read) {
        ReadResult result;
        result.covered.reserve(read.includedFiles.size() + 1);
        result.covered.append(filePath);
        for (const QString &included : read.includedFiles)
            result.covered.append(FilePath::fromUserInput(included));
        result.withEntries.reserve(read.files.size());
        for (const CxxFrontendIndexRead::File &file : read.files) {
            if (!file.entries.isEmpty())
                result.withEntries.append({file.filePath, cxxFrontendIndexTreeFrom(file)});
        }
        return result;
    };

    m_cxxFrontendWatcher.setFuture(QtConcurrent::mapped(
        &m_cxxFrontendPool, requests, [inputs, cache, resultOf](const Request &request) {
            // The store first, since taking a reading from it is what makes
            // a second session cheap; reading the file is the fallback, not
            // the other way round.
            if (const std::optional<CxxFrontendIndexRead> stored
                = cache->take(request.filePath, request.projectKey)) {
                return resultOf(request.filePath, *stored);
            }

            const std::optional<CxxFrontendIndexRead> read
                = cxxFrontendReadForIndex(inputs, request.filePath);
            if (!read) {
                ReadResult declined;
                declined.covered.append(request.filePath);
                return declined;
            }

            cache->store(request.filePath, request.projectKey, *read);
            return resultOf(request.filePath, *read);
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
        const ReadResult &result = m_cxxFrontendWatcher.resultAt(i);

        // Every file the reading covers is answered for, whether or not it
        // had anything to say. Saying so is what keeps a header from being
        // read again on its own, which is the whole of what a reading of
        // the unit saves.
        for (const FilePath &covered : result.covered) {
            if (m_removedSinceRead.contains(covered))
                continue;
            m_coveredThisRun.insert(covered);
            m_awaitingCoverage.remove(covered);
        }

        for (const ReadFile &read : result.withEntries) {
            // Waiting to be read again, so this is the older of the two
            // answers and the newer one is on its way.
            if (m_pending.contains(read.first))
                continue;

            // Taken out of the index since this reading began -- the
            // project was closed, or the file was. Putting the entries in
            // now would name things nothing can reach, and nothing would
            // take them out again.
            if (m_removedSinceRead.contains(read.first))
                continue;

            // A header reached by two sources is described twice, once per
            // reading. The first stands, which is the rule the built-in
            // model follows too -- it reads a header once, in whichever
            // translation unit reaches it first -- and it keeps the index
            // from depending on the order a pool finishes in. The file the
            // reading was of is always described by it, so it is never the
            // one turned away here.
            if (m_describedThisRun.contains(read.first))
                continue;
            m_describedThisRun.insert(read.first);

            m_infosByFile.insert(read.first.intern(), read.second);
        }
    }
}

void CppLocatorData::readWhatWasNotCovered()
{
    {
        QMutexLocker locker(&m_pendingMutex);
        // What is still waiting stays waiting while sources keep coming:
        // the indexer reports a project's files over many batches, and a
        // header reported before the source that includes it would
        // otherwise be read on its own a moment before that source covered
        // it anyway. Only once nothing is left to read are the stragglers
        // taken on their own account, by the batch below.
        if (m_pending.isEmpty() && !m_awaitingCoverage.isEmpty())
            m_readScheduled = false;
    }
    readPendingWithCxxFrontend();
}

int CppLocatorData::cxxFrontendFilesOutstanding() const
{
    QMutexLocker locker(&m_pendingMutex);
    // What is waiting to be covered is still owed: it either comes back
    // with a source that includes it or is read on its own at the end.
    return m_pending.size() + m_awaitingCoverage.size() + m_beingRead;
}

int CppLocatorData::cxxFrontendCacheHits() const
{
#ifdef QTC_WITH_CXX_FRONTEND
    return m_cxxFrontendCache ? m_cxxFrontendCache->hits() : 0;
#else
    return 0;
#endif
}

int CppLocatorData::cxxFrontendCacheMisses() const
{
#ifdef QTC_WITH_CXX_FRONTEND
    return m_cxxFrontendCache ? m_cxxFrontendCache->misses() : 0;
#else
    return 0;
#endif
}

void CppLocatorData::onSourceFilesRefreshed()
{
#ifdef QTC_WITH_CXX_FRONTEND
    {
        QMutexLocker locker(&m_pendingMutex);
        m_indexerDone = true;
    }
    readPendingWithCxxFrontend();
#endif
}

void CppLocatorData::onAboutToRemoveFiles(const FilePaths &files)
{
    if (files.isEmpty())
        return;

    {
        QMutexLocker locker(&m_pendingMutex);
        for (const FilePath &file : files) {
            m_pending.remove(file);
            m_awaitingCoverage.remove(file);
            // Gone, so nothing stands for it any longer. Were it to come
            // back it would have to be read afresh, and saying it is
            // already answered for would see to it that it never was.
            m_coveredThisRun.remove(file);
            m_describedThisRun.remove(file);
            m_removedSinceRead.insert(file);
        }
    }

    QMutexLocker locker(&m_infosByFileMutex);

    for (const FilePath &file : files)
        m_infosByFile.remove(file);

    StringTable::scheduleGC();
}

} // namespace CppEditor
