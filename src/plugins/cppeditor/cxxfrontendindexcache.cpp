// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cxxfrontendindexcache.h"

#include <coreplugin/icore.h>

#include <utils/qtcassert.h>

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QSaveFile>

using namespace Utils;

namespace CppEditor::Internal {

// Bumped whenever what is written changes shape, so that a store written by
// an older Qt Creator is passed over rather than misread.
const quint32 kFormat = 1;
const quint32 kMagic = 0x43585849; // "CXXI"

// Enough of a digest to tell two files apart and short enough that a
// thousand of them per shard is not what makes the store big. A collision
// here would keep a reading that is no longer true, and eight bytes puts
// that far below the chance of the disk being wrong.
const int kDigestLength = 8;

static QByteArray digestOf(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1).left(kDigestLength);
}

CxxFrontendIndexCache::CxxFrontendIndexCache(const QStringList &macros, const FilePath &directory)
    : m_directory(directory.isEmpty() ? Core::ICore::cacheResourcePath("cxx-index") : directory)
    , m_macrosKey(digestOf(macros.join('\n').toUtf8()))
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
    if (magic != kMagic || format != kFormat)
        return miss();

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

    qint32 entryCount = 0;
    stream >> entryCount;
    if (entryCount < 0)
        return miss();
    read.entries.reserve(entryCount);
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
            return miss();
        entry.itemType = itemType;
        entry.line = line;
        entry.column = column;
        entry.icon = icon;
        // An entry hangs under one written before it. A store saying
        // otherwise was not written by this, and building a tree from it
        // would hang an entry under itself.
        if (parent >= i)
            return miss();
        entry.parent = parent;
        read.entries.append(entry);
    }
    if (stream.status() != QDataStream::Ok)
        return miss();

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

    stream << qint32(read.entries.size());
    for (const CxxFrontendIndexEntry &entry : read.entries) {
        stream << entry.name << entry.extra << entry.scope << qint32(entry.itemType)
               << qint32(entry.line) << qint32(entry.column) << qint32(entry.icon)
               << entry.isFunctionDefinition << qint32(entry.parent);
    }

    // Written whole or not at all: a half-written shard read back next time
    // would be a reading of a file that was never made.
    QSaveFile out(shardFor(filePath).toFSPathString());
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(qCompress(raw));
    out.commit();
}

} // namespace CppEditor::Internal
