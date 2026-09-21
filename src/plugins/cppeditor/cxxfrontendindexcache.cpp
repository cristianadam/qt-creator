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
const quint32 kFormat = 4;
const quint32 kMagic = 0x43585849; // "CXXI"

// What the store may take on disk before the readings written longest ago
// are dropped.
//
// Measured over a 300-file slice of this project: 19.7 MB, which is 65 kB
// a file, so this is a project of some eight thousand files. Most of it is
// not what the files declare -- that is 2.3 MB, shared -- but the thousand
// paths each shard names as what it was read through.
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
    // the whole of why this is not kept in the shard. A digest of the full
    // twenty bytes, since a collision here would not lose a reading but
    // hand back another file's.
    const QByteArray key = QCryptographicHash::hash(raw, QCryptographicHash::Sha1);
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
    if (entryCount < 0)
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
}

int CxxFrontendIndexCache::hits() const
{
    QMutexLocker locker(&m_mutex);
    return m_hits;
}

int CxxFrontendIndexCache::misses() const
{
    QMutexLocker locker(&m_mutex);
    return m_misses;
}

std::optional<CxxFrontendIndexRead> CxxFrontendIndexCache::take(const FilePath &filePath,
                                                                const QByteArray &projectKey) const
{
    const auto miss = [this]() -> std::optional<CxxFrontendIndexRead> {
        QMutexLocker locker(&m_mutex);
        ++m_misses;
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
    if (fileCount < 0)
        return miss();

    CxxFrontendIndexRead read;
    read.includedFiles.reserve(fileCount - 1);
    for (qint32 i = 0; i < fileCount; ++i) {
        QString path;
        QByteArray digest;
        stream >> path >> digest;
        if (stream.status() != QDataStream::Ok)
            return miss();
        if (digest.isEmpty() || contentsOf(path) != digest)
            return miss();
        // The first is the file itself, which is not one of its own includes.
        if (i > 0)
            read.includedFiles.append(path);
    }

    // A reading is of a whole translation unit, so it says what each file
    // in it declares; the file read stands first.
    qint32 describedCount = 0;
    stream >> describedCount;
    if (describedCount <= 0)
        return miss();
    read.files.reserve(describedCount);
    for (qint32 f = 0; f < describedCount; ++f) {
        CxxFrontendIndexRead::File file;
        QString path;
        QByteArray key;
        stream >> path >> key;
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
    file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);

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
    for (const QString &path : std::as_const(files)) {
        const QByteArray digest = contentsOf(path);
        // A file that cannot be read now cannot be checked later, and a
        // shard that can never be used again is worse than none.
        if (digest.isEmpty())
            return;
        stream << path << digest;
    }

    // What each file declares goes beside the shard rather than in it, and
    // is named after itself: a header read into a thousand translation
    // units is described the same way by most of them, and the shard keeps
    // the digest rather than the description.
    stream << qint32(read.files.size());
    for (const CxxFrontendIndexRead::File &file : read.files) {
        const QByteArray key = writeEntries(file.entries);
        // Nothing written means nothing to point at, and a shard pointing
        // at what is not there is a shard that can never be used.
        if (key.isEmpty())
            return;
        stream << file.filePath.toFSPathString() << key;
    }

    // Written whole or not at all: a half-written shard read back next time
    // would be a reading of a file that was never made.
    QSaveFile out(shardFor(filePath).toFSPathString());
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(qCompress(raw));
    out.commit();
}

// Everything in \a directory and below it, each with what it costs and when
// it was last written.
static QList<std::pair<FilePath, QDateTime>> filesUnder(const FilePath &directory,
                                                        qint64 *totalSize)
{
    QList<std::pair<FilePath, QDateTime>> found;
    directory.iterateDirectory(
        [&found, totalSize](const FilePath &path) {
            found.append({path, path.lastModified()});
            *totalSize += path.fileSize();
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
    for (qint32 i = 0; i < fileCount; ++i) {
        QString path;
        QByteArray digest;
        stream >> path >> digest;
        if (stream.status() != QDataStream::Ok)
            return std::nullopt;
    }

    qint32 describedCount = 0;
    stream >> describedCount;
    QSet<QByteArray> keys;
    for (qint32 i = 0; i < describedCount; ++i) {
        QString path;
        QByteArray key;
        stream >> path >> key;
        // Half of what it points at is not an answer to what it points
        // at: read as the whole truth, the rest would be swept away.
        if (stream.status() != QDataStream::Ok)
            return std::nullopt;
        keys.insert(key);
    }
    return keys;
}

qint64 CxxFrontendIndexCache::sizeOnDisk() const
{
    qint64 total = 0;
    filesUnder(m_directory, &total);
    return total;
}

void CxxFrontendIndexCache::pruneToBound() const
{
    const FilePath entries = m_directory.pathAppended("entries");

    qint64 total = 0;
    QList<std::pair<FilePath, QDateTime>> shards;
    for (const auto &[path, written] : filesUnder(m_directory, &total)) {
        if (!path.isChildOf(entries))
            shards.append({path, written});
    }
    if (total <= m_maximumBytes)
        return;

    // Oldest first, which for a shard is the reading nobody has wanted
    // for longest: take() touches the one it hands back.
    std::sort(shards.begin(), shards.end(),
              [](const auto &left, const auto &right) { return left.second < right.second; });

    // Only what was there before this session began. A shard written a
    // moment ago may be one another worker is about to point at, and the
    // entries beside it are being written as this runs.
    for (const auto &[path, written] : std::as_const(shards)) {
        if (total <= m_maximumBytes / 2)
            break;
        if (written >= m_startedAt)
            continue;
        const qint64 size = path.fileSize();
        if (path.removeFile())
            total -= size;
    }

    // And then whatever nothing points at any longer, which is most of what
    // a dropped shard cost: its entries are shared, so they go only where
    // no other shard kept them.
    //
    // A shard this cannot read says nothing about what is wanted -- it was
    // written by another Qt Creator, whose format is not this one's, and
    // the store is shared between them. Reading it as "wants nothing"
    // would sweep away the entries only it points at, and that install
    // would find its whole store dangling. So one such shard stops the
    // sweep: the bound has been made by dropping shards already.
    QSet<QByteArray> wanted;
    bool readThemAll = true;
    m_directory.iterateDirectory(
        [&wanted, &readThemAll, &entries](const FilePath &path) {
            if (path.isChildOf(entries))
                return IterationPolicy::Continue;
            const std::optional<QSet<QByteArray>> keys = keysOf(path);
            if (!keys) {
                readThemAll = false;
                return IterationPolicy::Stop;
            }
            wanted.unite(*keys);
            return IterationPolicy::Continue;
        },
        {{}, DirFilterFlag::Files, DirIteratorFlag::Subdirectories});
    if (!readThemAll)
        return;

    // And what this session has put to use, whose shard may not be written
    // yet: the workers are storing while this runs.
    {
        QMutexLocker locker(&m_mutex);
        wanted.unite(m_keysUsed);
    }

    entries.iterateDirectory(
        [&wanted, this](const FilePath &path) {
            if (path.lastModified() >= m_startedAt)
                return IterationPolicy::Continue;
            const QByteArray key = QByteArray::fromHex(path.completeBaseName().toLatin1());
            if (!wanted.contains(key))
                path.removeFile();
            return IterationPolicy::Continue;
        },
        {{}, DirFilterFlag::Files, DirIteratorFlag::Subdirectories});
}

} // namespace CppEditor::Internal
