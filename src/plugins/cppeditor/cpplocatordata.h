// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"
#include "cppmodelmanager.h"
#include "searchsymbols.h"

#include "includeresolution.h"

#include <cplusplus/CppDocument.h>

#include <projectexplorer/headerpath.h>

#include <QFutureWatcher>
#include <QHash>
#include <QPromise>
#include <QSet>
#include <QThreadPool>

#include <memory>
#include <optional>
#include <utility>

namespace CppEditor {

namespace Internal {

class CxxFrontendIndexCache;

// Destroying the cache needs its definition, and that is only built with the
// front end. This header is included from outside the plugin, where the
// front end's own define is not set, so the member below cannot be left out
// of the class there without giving it two layouts -- and std::unique_ptr's
// own deleter insists on a complete type.
//
// So the deleting is done out of line, in the one file that knows whether
// there is a type to delete. Stateless, so the member stays the size of the
// pointer it holds.
struct CxxFrontendIndexCacheDeleter
{
    void operator()(CxxFrontendIndexCache *cache) const;
};

} // namespace Internal

class CPPEDITOR_EXPORT CppLocatorData : public QObject
{
    Q_OBJECT

    // Only one instance, created by the CppModelManager.
    CppLocatorData();
    friend class Internal::CppModelManagerPrivate;

public:
    ~CppLocatorData() override;

    void filterAllFiles(IndexItem::Visitor func) const
    {
        QMutexLocker locker(&m_infosByFileMutex);
        QHash<Utils::FilePath, IndexItem::Ptr> infosByFile = m_infosByFile;
        locker.unlock();
        for (auto i = infosByFile.constBegin(), ei = infosByFile.constEnd(); i != ei; ++i)
            if (i.value()->visitAllChildren(func) == IndexItem::Break)
                return;
    }

    QList<IndexItem::Ptr> findSymbols(IndexItem::ItemType type, const QString &symbolName) const;

    // Every file the index holds a description of, which is every file the
    // code model has read -- a project's own and whatever else has been
    // opened or included. What a search for a definition has to look
    // through, beyond the files a project lists.
    Utils::FilePaths filesWithEntries() const;

    // How many files the cxx front end still has to read: waiting and being
    // read together. Zero once every file the indexer has reported has that
    // model's entries rather than the built-in reading's, and always zero
    // where that model is not in use.
    //
    // What somebody timing the index waits on, there being no other sign of
    // it: the indexer's own signal comes long before these are done.
    int cxxFrontendFilesOutstanding() const;

    // How many files the index took from its store rather than reading, and
    // how many it had to read. Zero and zero before anything is indexed, and
    // where that model is not in use. The store is otherwise invisible, so
    // this is what says whether a second session is getting the benefit of
    // the first.
    int cxxFrontendCacheHits() const;
    int cxxFrontendCacheMisses() const;

    // How many include closures came out of the store rather than being read
    // for. Counted apart from the two above, which are the index's own.
    int cxxFrontendClosuresServed() const;

    // Every file \a filePath reaches through its includes, as the index has
    // it and without reading anything. Nothing where the index knows no such
    // file.
    //
    // Answered by walking the include graph the index keeps -- a node per
    // file with the files it includes itself, which is clangd's IncludeGraph
    // and is kept for the same reason. A *header* can be asked about that
    // way and not otherwise: it is never read on its own, so nothing has a
    // reading of it, and asking a front end means parsing the file and every
    // header it reaches. Where the graph has nothing, the store's own list
    // for a file it holds a reading of still answers.
    //
    // As stale as the index is, which is to say: it names what the file
    // included when it was last read, and a file written since is read
    // again. A file being edited is not asked about here -- what is on disk
    // is not what it says.
    std::optional<Utils::FilePaths> indexedIncludesFor(const Utils::FilePath &filePath) const;

private:
    // The store, or nothing where none has been made yet.
    Internal::CxxFrontendIndexCache *storeIfMade() const;

public:

public slots:
    // Called where the document was parsed, which is a worker thread: what a
    // file declares is worked out there rather than handed to the GUI thread
    // to work out. The document is freshly parsed at that moment, and the
    // source processor is about to let go of its source and its tree.
    void onDocumentUpdated(const CPlusPlus::Document::Ptr &document);
    void onAboutToRemoveFiles(const Utils::FilePaths &files);

    // Files have finished being parsed. Where that is the indexer reaching
    // the end of its pass, no source is coming that could cover the headers
    // still waiting and whatever is left is read on its own; where it is an
    // editor having reparsed the one document somebody is typing in, there
    // is nothing to do, that reparse having reported nothing to this.
    void onSourceFilesRefreshed(const QSet<Utils::FilePath> &files,
                                CppModelManager::RefreshOrigin origin);

    // Reads every translation unit \a project builds, taken from the
    // project's own data rather than waited for a file at a time.
    //
    // The index otherwise rides on the built-in indexer: it is that model
    // reporting a parsed document which puts a file here at all, so the
    // index cannot run without a pass that parses the whole project first
    // -- which is the thing it is meant to make unnecessary. A project's
    // parts already say which files it builds and how, so nothing has to be
    // parsed to find out.
    //
    // Behind QTC_CXX_FRONTEND_DRIVER while what else needs the built-in
    // model is still being unpicked. Where it is set, a reported document
    // queues nothing and this is the only way in.
    void readProjectWithCxxFrontend(ProjectExplorer::Project *project);

private:
    // One file to read, with what its project part contributes to reading
    // it -- worked out where the project's data belongs and carried to the
    // worker, which must not go asking for it.
    class Request
    {
    public:
        Utils::FilePath filePath;
        QByteArray projectKey;
        // Where this file's includes are to be looked for, prepared: a
        // reading finds them itself rather than asking the built-in model
        // what it resolved.
        ProjectExplorer::HeaderPaths headerPaths;
        // And what the files before it already found among those paths.
        // Shared by every request resolving against the same ones, a name's
        // answer depending on the paths and nothing else.
        std::shared_ptr<Internal::ResolvedNames> resolvedNames;
    };

    // What one file's entries came back as. The path travels with them
    // because a batch is read out of order and finishes out of order.
    class ReadFile
    {
    public:
        Utils::FilePath filePath;
        IndexItem::Ptr entries;
        // How many, so that the fullest reading of a file is the one kept.
        int count = 0;
    };

    // One reading's worth. A reading is of a whole translation unit, so it
    // answers for every file in it -- which is what \a covered lists -- but
    // only some of them declare anything the index keeps, and only those
    // carry entries. Naming the rest is what keeps them from being read
    // again one by one on the chance that they do.
    class ReadResult
    {
    public:
        Utils::FilePaths covered;
        QList<ReadFile> withEntries;
        // Which files of the unit include which, kept so that what a header
        // reaches can be answered without reading it. Turned into paths here
        // rather than where it lands, this being the pool's work.
        QList<std::pair<Utils::FilePath, Utils::FilePaths>> includes;
    };

    // Reads the files waiting for the cxx front end, as many at a time as
    // the machine has threads for. Runs on this object's own thread; only
    // the reading is on the pool.
    //
    // Does nothing while a batch is running -- that one's finishing takes
    // whatever has accumulated meanwhile, so the indexer is never waited for
    // and a batch is never replaced half-delivered.
    void readPendingWithCxxFrontend();
    // Puts back whatever the batch did not cover, and starts the next one.
    void readWhatWasNotCovered();
    void coverWhatObjectiveCBrings();
    void takeCxxFrontendResults(int begin, int end);

    // The progress of a run, shown and -- more to the point -- published
    // under the same task id the built-in pass uses, so that everything
    // waiting for "the index is being built" hears about this one too.
    // Counted over the whole run rather than a task per batch: a run is many
    // batches, and a consumer that reacts to the last of them ending would
    // start while the next is about to begin.
    void showIndexingProgress(int queued);
    void advanceIndexingProgress(int read);
    void finishIndexingProgress();
    // Whether the run has been called off -- by the progress item's own
    // button, or by Qt Creator closing, which cancels every task of that id
    // to stop the indexing. Stops it here too, rather than leaving the item
    // on screen until a run nobody wants finishes by itself.
    bool indexingWasCalledOff();

    mutable QMutex m_infosByFileMutex;
    QHash<Utils::FilePath, IndexItem::Ptr> m_infosByFile;

    // The include graph of everything read: a file and the files it includes
    // itself, merged from every reading. Asked from whatever thread a
    // question is on, so under a lock of its own -- and the smaller half of
    // what the index costs, an edge being two paths already held.
    mutable QMutex m_includeGraphMutex;
    QHash<Utils::FilePath, Utils::FilePaths> m_includeGraph;

    // The files the cxx front end has yet to read, written from the indexer's
    // thread and read from this one, so under a lock of their own rather than
    // the one the entries are under. Unused where that model is not built in.
    mutable QMutex m_pendingMutex;
    QSet<Utils::FilePath> m_pending;
    // Files taken out of the index since the batch being read began. A
    // reading of one of them was already under way when it went, and must
    // not put it back.
    QSet<Utils::FilePath> m_removedSinceRead;
    // Files of the batch that are nobody's translation unit -- headers --
    // and are waiting to be covered by the reading of a source that
    // includes one of them. Whatever is left uncovered when the batch ends
    // goes back to be read on its own account.
    QSet<Utils::FilePath> m_awaitingCoverage;
    // Files already answered for since the indexer began reporting, so
    // that a header reached by several sources is read once -- and, more
    // to the point, so that one reported after the source that covered it
    // is not read again on its own. The indexer reports each file once per
    // run, so anything already covered in this run is covered; a run of its
    // own starts the set again.
    QSet<Utils::FilePath> m_coveredThisRun;
    // Of those, the ones some reading had entries for, and how many. A
    // header is read into every file that includes it, and what each of
    // them makes of it differs -- a translation unit that excludes most of
    // it describes little. The fullest description is the one kept.
    QHash<Utils::FilePath, int> m_describedThisRun;
    // Whether the indexer has finished reporting. Until it has, a header
    // waits to be covered rather than being read as a unit of its own: it
    // is reported before the source that includes it, that being the order
    // a translation unit is read in.
    bool m_indexerDone = false;
    // Whether the sweep above has been made for this pass.
    bool m_objectiveCSwept = false;
    // Of the batch being read, how many have yet to come back.
    int m_beingRead = 0;
    bool m_readScheduled = false;
    QThreadPool m_cxxFrontendPool;
    QFutureWatcher<ReadResult> m_cxxFrontendWatcher;

    // What the progress of a run is reported through. Not behind the front
    // end's define, so that this class is one size wherever it is compiled --
    // the header is included from outside the plugin.
    QPromise<void> m_indexingProgress;
    // What says it has been cancelled: a promise has no signal of its own.
    QFutureWatcher<void> m_indexingCancelled;
    bool m_indexingShown = false;
    int m_filesQueuedThisRun = 0;
    int m_filesReadThisRun = 0;

    // What each file's reading came to last time. Made when the first batch
    // runs, since where it lives and what it is checked against are only
    // known then. Held by pointer so that this header, which is included
    // outside the plugin, does not need the type -- and so that the class
    // is the same size whether or not the model is built in, which is what
    // the deleter above is for.
    std::unique_ptr<Internal::CxxFrontendIndexCache, Internal::CxxFrontendIndexCacheDeleter>
        m_cxxFrontendCache;
    // Guards the pointer above, not what it points at -- the store is safe to
    // use from several threads, but it is made on the thread that owns the
    // project's data while a query may be asking for it on a pool.
    mutable QMutex m_cxxFrontendCacheMutex;
};

} // namespace CppEditor
