// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cxxfrontendindexcache.h"

#include <coreplugin/icore.h>

#include <utils/qtcassert.h>

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QSaveFile>

#include <algorithm>

using namespace Utils;

namespace CppEditor::Internal {

// Bumped whenever what is written changes shape, so that a store written by
// an older Qt Creator is passed over rather than misread.
const quint32 kFormat = 5;
const quint32 kMagic = 0x43585849; // "CXXI"

// What the store may take on disk before the readings written longest ago
// are dropped.
//
// Measured over a 300-file slice of this project: 5.7 MB, or 19 kB a file,
// so this is a project of some twenty-seven thousand of them -- and a third
// of that where the files are as header-heavy as Qt's own. The same slice
// cost 8.2 MB before a shard stopped naming every path a second time and
// writing a length beside every digest.
//
// Little of it is what the files declare: that is 1.1 MB, shared between
// the units that read a header the same way. What the rest is, and this is
// the thing to know before trying to make it smaller, is digests rather
// than paths. A shard is nine tenths paths before compression, but a
// thousand paths sharing their first forty characters come to almost
// nothing after it, while a digest compresses to itself -- looking, as it
// does, like noise.
//
// A bound at all because nothing else drops anything: a store grows by a
// shard per file indexed, for every project ever opened, and a cache is a
// thing that may be thrown away rather than a thing to fill a disk with.
const qint64 kDefaultMaximumBytes = 512ll * 1024 * 1024;

// Enough of a digest to tell two files apart and short enough that a
// thousand of them per shard is not what makes the store big. A collision
// here would keep a reading that is no longer true, and eight bytes puts
// that far below the chance of the disk being wrong.
const int kDigestLength = 8;

static QByteArray digestOf(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1).left(kDigestLength);
}

// Enough of a digest to name what a file declares, and longer than the one
// above because the two answer different questions. A content digest is only
// ever compared with the digest of that same path's bytes, so a collision
// takes one file's two versions agreeing; this one *names* a description
// among every description in the store, which is the birthday problem over
// the lot of them. At twelve bytes a store of a million descriptions has
// about one chance in 10^17 of handing back the wrong file's.
const int kEntriesKeyLength = 12;

// A path as a shard holds it: UTF-8, rather than the UTF-16 a QString writes.
// Nine tenths of a shard is paths, and a path is very nearly ASCII, so this
// is half of what they cost before compression and a fifth of what is left
// after it.
//
// A name holding an unpaired surrogate -- which Windows permits and nothing
// much makes -- does not come back the same, and the file it names is then
// read afresh every session instead of being taken from the store. That is
// the safe way round for it to fail, and it costs the one file.
static void writePath(QDataStream &stream, const QString &path)
{
    stream << path.toUtf8();
}

static QString readPath(QDataStream &stream)
{
    QByteArray utf8;
    stream >> utf8;
    return QString::fromUtf8(utf8);
}

// A digest as a shard holds it: the bytes themselves, with no length written
// beside them, every one of them being of the one size. The lengths came to
// a third again of what the digests cost, and the digests are the part of a
// shard that no compression shrinks -- being digests, they look like noise.
//
// The length is given rather than taken from the digest, because it is what
// the reader will read: one of another size would put every field after it
// out of step. And a shard misread that way is worse than one that never
// matches -- keysOf() cannot parse it either, so the sweep that reclaims
// what the files declare gives up on finding it and goes on giving up, for
// this store, every session after. Hence the answer rather than an
// assertion alone: a shard this could not write is one not written at all.
[[nodiscard]] static bool writeDigest(QDataStream &stream, const QByteArray &digest, int length)
{
    QTC_ASSERT(digest.size() == length, return false);
    stream.writeRawData(digest.constData(), length);
    return true;
}

static QByteArray readDigest(QDataStream &stream, int length)
{
    QByteArray digest(length, Qt::Uninitialized);
    if (stream.readRawData(digest.data(), length) != length)
        return {};
    return digest;
}

CxxFrontendIndexCache::CxxFrontendIndexCache(const QStringList &macros,
                                             const FilePath &directory,
                                             qint64 maximumBytes)
    : m_directory(directory.isEmpty() ? Core::ICore::cacheResourcePath("cxx-index") : directory)
    , m_macrosKey(digestOf(macros.join('\n').toUtf8()))
    , m_maximumBytes(maximumBytes > 0 ? maximumBytes : kDefaultMaximumBytes)
    , m_startedAt(QDateTime::currentDateTime())
{
}

Utils::FilePath CxxFrontendIndexCache::shardFor(const FilePath &filePath) const
{
    // Named after the file so that the store can be read by a person, and
    // after a digest of its whole path so that two files of one name in two
    // directories do not write over each other.
    const QByteArray path = filePath.toFSPathString().toUtf8();
    return m_directory.pathAppended(filePath.fileName() + '.'
                                    + QString::fromLatin1(digestOf(path).toHex()) + ".idx");
}

FilePath CxxFrontendIndexCache::entriesFor(const QByteArray &key) const
{
    // Under the first byte of the digest, so that a project's worth of them
    // is a couple of hundred directories rather than one that every listing
    // of the store has to walk.
    const QString hex = QString::fromLatin1(key.toHex());
    return m_directory.pathAppended("entries") / hex.left(2) / (hex + ".ent");
}

QByteArray CxxFrontendIndexCache::writeEntries(const QList<CxxFrontendIndexEntry> &entries) const
{
    QByteArray raw;
    QDataStream stream(&raw, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << qint32(entries.size());
    for (const CxxFrontendIndexEntry &entry : entries) {
        stream << entry.name << entry.extra << entry.scope << qint32(entry.itemType)
               << qint32(entry.line) << qint32(entry.column) << qint32(entry.icon)
               << entry.isFunctionDefinition << qint32(entry.parent);
    }

    // The digest is of what a file declares, so two translation units that
    // read a header the same way write one file between them -- which is
    // the whole of why this is not kept in the shard.
    const QByteArray key
        = QCryptographicHash::hash(raw, QCryptographicHash::Sha1).left(kEntriesKeyLength);
    const FilePath path = entriesFor(key);

    // Used by this session, whether it is written below or was already
    // there. The pruning must not take it: the shard that points at it
    // may not be on disk yet, and then the reading would come back to a
    // file that is not there.
    {
        QMutexLocker locker(&m_mutex);
        m_keysUsed.insert(key);
    }

    // Already written, by this session or a previous one. The contents
    // cannot differ: they are what the name is made of.
    if (path.exists())
        return key;

    const QString directory = path.parentDir().toFSPathString();
    bool made = false;
    {
        QMutexLocker locker(&m_mutex);
        made = m_directoriesMade.contains(directory);
    }
    if (!made) {
        if (!path.parentDir().ensureWritableDir())
            return {};
        QMutexLocker locker(&m_mutex);
        m_directoriesMade.insert(directory);
    }

    QSaveFile out(path.toFSPathString());
    if (!out.open(QIODevice::WriteOnly))
        return {};
    out.write(qCompress(raw));
    if (!out.commit())
        return {};
    return key;
}

std::optional<QList<CxxFrontendIndexEntry>> CxxFrontendIndexCache::readEntries(
    const QByteArray &key) const
{
    // Asked for once per file a reading describes, which over a project is
    // far more often than there are answers: the units of this project
    // between them describe some three hundred thousand files out of five
    // thousand descriptions, so one of these was being opened,
    // decompressed and deserialized sixty-odd times over.
    //
    // Safe to remember for as long as the session lasts, and this is the
    // one reason it needs no invalidating: the key is a digest of the very
    // bytes it names, so what a key answers cannot change. A shard whose
    // files have changed is a miss on the digests above, not a stale
    // description here.
    //
    // The list is copy-on-write, so handing one back costs a reference.
    {
        QMutexLocker locker(&m_mutex);
        const auto known = m_entriesByKey.constFind(key);
        if (known != m_entriesByKey.constEnd())
            return *known;
    }

    QFile file(entriesFor(key).toFSPathString());
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray raw = qUncompress(file.readAll());
    if (raw.isEmpty())
        return std::nullopt;

    QDataStream stream(raw);
    stream.setVersion(QDataStream::Qt_6_0);
    qint32 entryCount = 0;
    stream >> entryCount;

    // Read as something a disk wrote, the way a shard's counts are. An
    // entry costs at least 33 bytes -- four for each of three string
    // lengths, four for each of five numbers, one for the flag -- so the
    // bytes in hand say how many there can be, and a count past that is a
    // file this did not write. Without the bound a corrupt one asks QList
    // for gigabytes and takes a reader down with bad_alloc where a miss was
    // the answer.
    if (entryCount < 0 || entryCount > raw.size() / 33)
        return std::nullopt;

    QList<CxxFrontendIndexEntry> entries;
    entries.reserve(entryCount);
    for (qint32 i = 0; i < entryCount; ++i) {
        CxxFrontendIndexEntry entry;
        qint32 itemType = 0;
        qint32 line = 0;
        qint32 column = 0;
        qint32 icon = 0;
        qint32 parent = 0;
        stream >> entry.name >> entry.extra >> entry.scope >> itemType >> line >> column >> icon
            >> entry.isFunctionDefinition >> parent;
        if (stream.status() != QDataStream::Ok)
            return std::nullopt;
        entry.itemType = itemType;
        entry.line = line;
        entry.column = column;
        entry.icon = icon;
        // An entry hangs under one written before it. A store saying
        // otherwise was not written by this, and building a tree from it
        // would hang an entry under itself.
        if (parent >= i)
            return std::nullopt;
        entry.parent = parent;
        entries.append(entry);
    }
    if (stream.status() != QDataStream::Ok)
        return std::nullopt;
    // Read outside the lock, so two workers may read one of these twice;
    // it cannot differ, being named after its own contents.
    QMutexLocker locker(&m_mutex);
    m_entriesByKey.insert(key, entries);
    return entries;
}

QByteArray CxxFrontendIndexCache::contentsOf(const QString &filePath) const
{
    {
        QMutexLocker locker(&m_mutex);
        const auto known = m_contents.constFind(filePath);
        if (known != m_contents.constEnd())
            return *known;
    }

    // Read outside the lock: a header is reached by a thousand files and the
    // point of remembering it is that the reading happens once, not that
    // every other worker waits while it does.
    const Result<QByteArray> contents = FilePath::fromUserInput(filePath).fileContents();
    const QByteArray digest = contents ? digestOf(*contents) : QByteArray();

    QMutexLocker locker(&m_mutex);
    m_contents.insert(filePath, digest);
    return digest;
}

void CxxFrontendIndexCache::forgetContents()
{
    QMutexLocker locker(&m_mutex);
    m_contents.clear();
    // The descriptions go with them, not because one could ever be wrong --
    // a key is a digest of its own bytes -- but because they are the index
    // over again, a second copy beside the one the locator keeps. Held for
    // the batch, which is where the sharing is: the units read together are
    // the ones reading the same headers.
    m_entriesByKey.clear();
}

int CxxFrontendIndexCache::hits() const
{
    QMutexLocker locker(&m_mutex);
    return m_hits;
}

int CxxFrontendIndexCache::closuresServed() const
{
    QMutexLocker locker(&m_mutex);
    return m_closuresServed;
}

int CxxFrontendIndexCache::misses() const
{
    QMutexLocker locker(&m_mutex);
    return m_misses;
}

std::optional<CxxFrontendIndexRead> CxxFrontendIndexCache::take(const FilePath &filePath,
                                                                const QByteArray &projectKey) const
{
    return readShard(filePath, projectKey, Wanted::Everything);
}

std::optional<QStringList> CxxFrontendIndexCache::includedFilesOf(
    const FilePath &filePath, const QByteArray &projectKey) const
{
    const std::optional<CxxFrontendIndexRead> read
        = readShard(filePath, projectKey, Wanted::TheFilesOnly);
    if (!read)
        return std::nullopt;
    return read->includedFiles;
}

std::optional<CxxFrontendIndexRead> CxxFrontendIndexCache::readShard(
    const FilePath &filePath, const QByteArray &projectKey, Wanted wanted) const
{
    const bool counts = wanted == Wanted::Everything;
    const auto miss = [this, counts]() -> std::optional<CxxFrontendIndexRead> {
        if (counts) {
            QMutexLocker locker(&m_mutex);
            ++m_misses;
        }
        return std::nullopt;
    };

    QFile file(shardFor(filePath).toFSPathString());
    if (!file.open(QIODevice::ReadOnly))
        return miss();

    // Read whole and uncompressed in one go: what makes the store small is
    // that a thousand paths sharing their first forty characters compress to
    // almost nothing, and that only works over the lot of them.
    const QByteArray raw = qUncompress(file.readAll());
    if (raw.isEmpty())
        return miss();

    QDataStream stream(raw);
    stream.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0;
    quint32 format = 0;
    stream >> magic >> format;
    if (magic != kMagic || format != kFormat) {
        // Written by an older Qt Creator, and nothing here will ever read
        // it again: the reading it holds is about to be made afresh and
        // written over it. Taken off the disk now rather than left for the
        // bound to notice, since a format going up otherwise leaves a
        // project's whole store standing as dead weight.
        //
        // A *newer* store is left alone. This is the older Qt Creator in
        // that case, and throwing away what the newer one will want next
        // time is no way to behave.
        if (magic == kMagic && format < kFormat)
            file.remove();
        return miss();
    }

    QByteArray storedMacrosKey;
    QByteArray storedProjectKey;
    stream >> storedMacrosKey >> storedProjectKey;
    if (storedMacrosKey != m_macrosKey || storedProjectKey != projectKey)
        return miss();

    // The file itself first, then everything read into it. Any one of them
    // different and the entries may be too, so they are all checked -- and
    // the first difference ends it, since one is enough.
    qint32 fileCount = 0;
    stream >> fileCount;
    // The file read stands first, so a shard naming none of them is one this
    // did not write -- and no more of them than the bytes in hand could
    // hold. The count comes off the disk, and reserving for a corrupt one
    // would ask for gigabytes and bring the reader down with a bad_alloc
    // where it should simply have missed. A checked file costs twelve bytes
    // at the very least: four for the length of its path and eight of
    // digest.
    if (fileCount <= 0 || fileCount > raw.size() / 12)
        return miss();

    CxxFrontendIndexRead read;
    // Kept whole, the file itself and all, because what the reading
    // describes is named by its place in this list.
    QStringList checked;
    checked.reserve(fileCount);
    for (qint32 i = 0; i < fileCount; ++i) {
        const QString path = readPath(stream);
        const QByteArray digest = readDigest(stream, kDigestLength);
        if (stream.status() != QDataStream::Ok || path.isEmpty() || digest.isEmpty())
            return miss();
        if (contentsOf(path) != digest)
            return miss();
        checked.append(path);
    }
    // The first is the file itself, which is not one of its own includes.
    read.includedFiles = checked.mid(1);

    // And that is the whole answer for a caller asking what the file
    // includes. Reading the descriptions as well would deserialize the
    // index of every file in the unit and keep it in m_entriesByKey, which
    // is a second copy of what the locator already holds -- kept until a
    // batch forgets it, and a query is no batch.
    if (wanted == Wanted::TheFilesOnly) {
        QMutexLocker locker(&m_mutex);
        ++m_closuresServed;
        return read;
    }

    // A reading is of a whole translation unit, so it says what each file
    // in it declares; the file read stands first.
    qint32 describedCount = 0;
    stream >> describedCount;
    // Bounded the same way, a described file costing sixteen bytes at the
    // very least: four for where it stands and twelve of key.
    if (describedCount <= 0 || describedCount > raw.size() / 16)
        return miss();
    read.files.reserve(describedCount);
    for (qint32 f = 0; f < describedCount; ++f) {
        CxxFrontendIndexRead::File file;
        // Where it stands among the files checked above, or -1 and then the
        // path, for the one that is no file and so was never checked.
        qint32 stands = 0;
        stream >> stands;
        QString path;
        if (stands >= 0) {
            if (stands >= checked.size())
                return miss();
            path = checked.at(stands);
        } else {
            path = readPath(stream);
        }
        const QByteArray key = readDigest(stream, kEntriesKeyLength);
        if (stream.status() != QDataStream::Ok || path.isEmpty() || key.isEmpty())
            return miss();
        // What the file declares is kept apart, under a digest of itself,
        // so that the hundred units that read a header the same way share
        // the one copy. Gone means the bound took it; the whole reading is
        // then a miss, the way it is when a file has changed.
        const std::optional<QList<CxxFrontendIndexEntry>> entries = readEntries(key);
        if (!entries)
            return miss();
        file.filePath = FilePath::fromUserInput(path);
        file.entries = *entries;
        read.files.append(file);
    }
    if (stream.status() != QDataStream::Ok)
        return miss();

    // Used now, which is what the bound goes by when it has to drop
    // something. Nothing else says so: a reading that comes back from the
    // store is read and not written, so without this a shard's age is the
    // age of the last time it was *missed* -- and a project that is fully
    // stored, which is the one worth keeping, would look like the stalest
    // thing there.
    //
    // Through a handle of its own, opened for writing. Setting the time
    // of a file opened read-only is allowed to the owner on Unix and
    // refused on Windows, where it needs the attributes to be writable --
    // and it fails silently, which would leave the bound dropping exactly
    // the readings it is meant to keep.
    QFile touch(shardFor(filePath).toFSPathString());
    if (touch.open(QIODevice::ReadWrite))
        touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);

    QMutexLocker locker(&m_mutex);
    ++m_hits;
    return read;
}

void CxxFrontendIndexCache::store(const FilePath &filePath,
                                  const QByteArray &projectKey,
                                  const CxxFrontendIndexRead &read)
{
    if (!m_directory.ensureWritableDir())
        return;

    // Here rather than where this is made: making it is done on the thread
    // the editor draws on, and this is a walk of every file in the store.
    // Only a session that writes can have made the store too big, so a
    // warm one -- every reading taken from disk -- does not pay for it.
    bool prune = false;
    {
        QMutexLocker locker(&m_mutex);
        prune = !m_pruned;
        m_pruned = true;
    }
    if (prune)
        pruneToBound();

    QByteArray raw;
    QDataStream stream(&raw, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kMagic << kFormat << m_macrosKey << projectKey;

    // The file itself stands first among the files checked, so that reading
    // this back has one loop rather than two.
    QStringList files{filePath.toFSPathString()};
    files += read.includedFiles;
    stream << qint32(files.size());
    // Where each of them stands, so that what the reading describes can be
    // named by its place below rather than written out a second time.
    QHash<QString, qint32> placeOf;
    placeOf.reserve(files.size());
    qint32 place = 0;
    for (const QString &path : std::as_const(files)) {
        const QByteArray digest = contentsOf(path);
        // A file that cannot be read now cannot be checked later, and a
        // shard that can never be used again is worse than none.
        if (digest.size() != kDigestLength)
            return;
        placeOf.insert(path, place++);
        writePath(stream, path);
        if (!writeDigest(stream, digest, kDigestLength))
            return;
    }

    // What each file declares goes beside the shard rather than in it, and
    // is named after itself: a header read into a thousand translation
    // units is described the same way by most of them, and the shard keeps
    // the digest rather than the description.
    //
    // The file it describes is named by its place in the list above, where
    // all but one of them stand -- what a reading describes is what was
    // read into it. Written out only for the one that is no file at all,
    // <builtins>, which is the whole of why the place may be missing.
    stream << qint32(read.files.size());
    for (const CxxFrontendIndexRead::File &file : read.files) {
        const QByteArray key = writeEntries(file.entries);
        // Nothing written means nothing to point at, and a shard pointing
        // at what is not there is a shard that can never be used.
        if (key.size() != kEntriesKeyLength)
            return;
        const QString path = file.filePath.toFSPathString();
        const auto stands = placeOf.constFind(path);
        if (stands != placeOf.constEnd()) {
            stream << *stands;
        } else {
            stream << qint32(-1);
            writePath(stream, path);
        }
        if (!writeDigest(stream, key, kEntriesKeyLength))
            return;
    }

    // Written whole or not at all: a half-written shard read back next time
    // would be a reading of a file that was never made.
    QSaveFile out(shardFor(filePath).toFSPathString());
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(qCompress(raw));
    out.commit();
}

// What a shard is called. Everything else beside them -- the half-written
// temporaries a QSaveFile leaves while the workers store, whatever the
// desktop drops in a directory -- is none of this code's business, and
// reading one as a shard that says nothing would stop the sweep below.
static const char kShardSuffix[] = ".idx";

// And what a file declares, kept beside the shards under a digest of
// itself. Named here because the pruning has to weigh the two apart.
static const char kEntriesSuffix[] = ".ent";

// One file of the store, with what it costs and when it was last written.
class StoredFile
{
public:
    FilePath path;
    QDateTime written;
    qint64 size = 0;
};

// Everything in \a directory and below it. What each costs comes back with
// it: the pruning has to weigh the shards against the rest, and asking the
// disk a second time for what it has just been asked is a stat per file of
// the store.
static QList<StoredFile> filesUnder(const FilePath &directory)
{
    QList<StoredFile> found;
    directory.iterateDirectory(
        [&found](const FilePath &path) {
            found.append({path, path.lastModified(), path.fileSize()});
            return IterationPolicy::Continue;
        },
        {{}, DirFilterFlag::Files, DirIteratorFlag::Subdirectories});
    return found;
}

// The digests a shard points at, which is what says an entries file is
// still wanted. Read without checking anything else about it: a shard whose
// files have changed is still a shard whose entries must not be dropped
// from under another one.
static std::optional<QSet<QByteArray>> keysOf(const FilePath &shard)
{
    QFile file(shard.toFSPathString());
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray raw = qUncompress(file.readAll());
    if (raw.isEmpty())
        return std::nullopt;

    QDataStream stream(raw);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint32 format = 0;
    QByteArray macrosKey;
    QByteArray projectKey;
    stream >> magic >> format >> macrosKey >> projectKey;
    if (magic != kMagic || format != kFormat)
        return std::nullopt;

    qint32 fileCount = 0;
    stream >> fileCount;
    // Judged exactly as take() judges it. A count it would refuse is a shard
    // nothing will ever read again, and calling that "wants no entries"
    // would let the sweep take the entries only it points at -- which is
    // the very thing the giving up below is there to prevent.
    if (fileCount <= 0 || fileCount > raw.size() / 12)
        return std::nullopt;
    // Read past, not kept: what is wanted here is the keys below, and a
    // described file is named by its place among these only where it has
    // one.
    for (qint32 i = 0; i < fileCount; ++i) {
        readPath(stream);
        readDigest(stream, kDigestLength);
        if (stream.status() != QDataStream::Ok)
            return std::nullopt;
    }

    qint32 describedCount = 0;
    stream >> describedCount;
    if (describedCount <= 0 || describedCount > raw.size() / 16)
        return std::nullopt;
    QSet<QByteArray> keys;
    for (qint32 i = 0; i < describedCount; ++i) {
        qint32 stands = 0;
        stream >> stands;
        if (stands < 0)
            readPath(stream);
        const QByteArray key = readDigest(stream, kEntriesKeyLength);
        // Half of what it points at is not an answer to what it points
        // at: read as the whole truth, the rest would be swept away.
        if (stream.status() != QDataStream::Ok || key.isEmpty())
            return std::nullopt;
        keys.insert(key);
    }
    return keys;
}

qint64 CxxFrontendIndexCache::sizeOnDisk() const
{
    qint64 total = 0;
    for (const StoredFile &file : filesUnder(m_directory))
        total += file.size;
    return total;
}

void CxxFrontendIndexCache::pruneToBound() const
{
    const FilePath entries = m_directory.pathAppended("entries");

    qint64 total = 0;
    qint64 shardBytes = 0;
    QList<StoredFile> shards;
    QHash<QByteArray, StoredFile> entryFiles;
    for (const StoredFile &file : filesUnder(m_directory)) {
        total += file.size;
        if (file.path.fileName().endsWith(QLatin1String(kShardSuffix))) {
            shards.append(file);
            shardBytes += file.size;
        } else if (file.path.fileName().endsWith(QLatin1String(kEntriesSuffix))) {
            entryFiles.insert(QByteArray::fromHex(file.path.completeBaseName().toLatin1()), file);
        }
    }
    if (total <= m_maximumBytes)
        return;

    // What this session has put to use, whose shard may not be written yet:
    // the workers are storing while this runs, so neither the sweep below
    // nor the reckoning above it may count such an entries file as going.
    QSet<QByteArray> inUse;
    {
        QMutexLocker locker(&m_mutex);
        inUse = m_keysUsed;
    }

    // What each shard points at, and how many point at each entries file.
    // Dropping a shard frees the entries that were only its, and that is
    // most of what it cost: a target that counts the shard alone is a
    // target the store cannot be brought down to, and one that counts the
    // whole store without crediting what goes with a shard cannot be
    // reached at all. Either way the loop runs to the end and empties the
    // store -- the lately used along with the rest -- leaving the session a
    // cache that answers nothing. Counting what each drop really frees is
    // what makes the bound both honest and reachable.
    //
    // A shard this cannot read says nothing about what is wanted -- it was
    // written by another Qt Creator, whose format is not this one's, and
    // the store is shared between them. Reading it as "wants nothing" would
    // sweep away the entries only it points at, and that install would find
    // its whole store dangling. So one such shard leaves the entries alone
    // altogether: the shards are still dropped, counted on their own, and
    // what they pointed at is left for a session that can read the lot.
    QHash<FilePath, QSet<QByteArray>> pointedAtBy;
    QHash<QByteArray, int> pointingAt;
    bool readThemAll = true;
    for (const StoredFile &shard : std::as_const(shards)) {
        const std::optional<QSet<QByteArray>> keys = keysOf(shard.path);
        if (!keys) {
            readThemAll = false;
            break;
        }
        pointedAtBy.insert(shard.path, *keys);
        for (const QByteArray &key : *keys)
            ++pointingAt[key];
    }

    // Oldest first, which for a shard is the reading nobody has wanted
    // for longest: take() touches the one it hands back.
    std::sort(shards.begin(), shards.end(), [](const StoredFile &left, const StoredFile &right) {
        return left.written < right.written;
    });

    // Down to half the bound, so that a session which has just reached it
    // is not pruning again on the next file it stores.
    //
    // Weighed against the whole store where what each drop frees is known,
    // and against the shards alone where it is not: unable to say which
    // entries go with a shard, this would charge itself for every one of
    // them and never reach a target counted over the lot -- dropping the
    // whole store to chase it, which is the very thing the reckoning above
    // exists to prevent. The shards on their own are always reachable.
    //
    // Only what was there before this session began, besides. A shard
    // written a moment ago may be one another worker is about to point at,
    // and the entries beside it are being written as this runs.
    for (const StoredFile &shard : std::as_const(shards)) {
        if ((readThemAll ? total : shardBytes) <= m_maximumBytes / 2)
            break;
        if (shard.written >= m_startedAt)
            continue;
        if (!shard.path.removeFile())
            continue;
        total -= shard.size;
        shardBytes -= shard.size;
        if (!readThemAll)
            continue;

        // And what went with it, which is every entries file this was the
        // last shard to point at. Counted only where the sweep below will
        // really take it: one written this session, or one this session has
        // read, stays whatever points at it.
        for (const QByteArray &key : std::as_const(pointedAtBy[shard.path])) {
            if (--pointingAt[key] > 0 || inUse.contains(key))
                continue;
            const auto it = entryFiles.constFind(key);
            if (it != entryFiles.constEnd() && it->written < m_startedAt)
                total -= it->size;
        }
    }
    if (!readThemAll)
        return;

    // Asked for again, because the workers have been storing while the
    // above ran: one that has taken up an entries file since is a worker
    // whose shard is not on disk yet, and dropping what it points at would
    // leave that reading pointing at nothing. Crediting the loop above with
    // what it turns out it may not have freed only stops it dropping a
    // little early, which the bound can afford.
    {
        QMutexLocker locker(&m_mutex);
        inUse.unite(m_keysUsed);
    }

    // And then whatever nothing points at any longer.
    entries.iterateDirectory(
        [&pointingAt, &inUse, this](const FilePath &path) {
            if (path.lastModified() >= m_startedAt)
                return IterationPolicy::Continue;
            const QByteArray key = QByteArray::fromHex(path.completeBaseName().toLatin1());
            if (pointingAt.value(key) <= 0 && !inUse.contains(key))
                path.removeFile();
            return IterationPolicy::Continue;
        },
        {{}, DirFilterFlag::Files, DirIteratorFlag::Subdirectories});
}

} // namespace CppEditor::Internal
