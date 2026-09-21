// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cxxfrontendmodel.h"

#include <utils/filepath.h>

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QSet>

#include <optional>

namespace CppEditor::Internal {

// What reading each file for the index found, kept between sessions so that
// a file unchanged since last time is not read again.
//
// Reading one file with this model costs about half a second and a project
// has thousands, so without this every session pays for the whole index. The
// shape is clangd's: one file on disk per indexed file, holding that file's
// entries and what they were read through, and nothing shared between them
// that two writers could fight over.
//
// Every worker of the index's pool uses one of these at once, so all of it
// is safe to call from several threads.
class CxxFrontendIndexCache
{
public:
    // \a macros is what was defined before the first line of every file --
    // one string per macro, as CxxFrontendIndexInputs carries them. It is
    // part of what a stored reading is checked against.
    //
    // \a directory is where the store lives; the one shared by every project
    // this Qt Creator indexes where it is empty, which is what a test gives
    // to keep out of.
    //
    // \a maximumBytes is what the store may take on disk; the default where
    // it is zero. Exceeding it costs the readings written longest ago, not
    // the ones being written now.
    //
    // Constructed on the thread that owns the settings, since the shared
    // place is read from them.
    explicit CxxFrontendIndexCache(const QStringList &macros,
                                   const Utils::FilePath &directory = {},
                                   qint64 maximumBytes = 0);

    // What was stored for \a filePath, where the file and every file read
    // into it are unchanged and \a projectKey is the one it was read under.
    // Nothing otherwise, and then the caller reads the file.
    [[nodiscard]] std::optional<CxxFrontendIndexRead> take(const Utils::FilePath &filePath,
                                                           const QByteArray &projectKey) const;

    // Stores what reading \a filePath found. Overwrites whatever was there.
    void store(const Utils::FilePath &filePath,
               const QByteArray &projectKey,
               const CxxFrontendIndexRead &read);

    // Forgets what each file's contents were, which is remembered only so
    // that a header reached by a thousand files is read once while a batch
    // is checked. Called when a batch begins, so that a file written since
    // the last one is seen to have changed.
    void forgetContents();

    // Where the store is, for a test that wants to look or to start empty.
    [[nodiscard]] Utils::FilePath directory() const { return m_directory; }

    // What it takes on disk, shards and entries together. The one outward
    // sign that the bound is doing anything.
    [[nodiscard]] qint64 sizeOnDisk() const;

    // How many readings were taken from the store and how many were stored,
    // since the counts are the only outward sign that any of this works.
    [[nodiscard]] int hits() const;
    [[nodiscard]] int misses() const;

private:
    // A file's contents as a short digest, read once per batch. Digests
    // rather than a timestamp because a checkout rewrites timestamps without
    // changing a line, and because a file written twice within the clock's
    // resolution would otherwise keep a reading that is no longer true.
    [[nodiscard]] QByteArray contentsOf(const QString &filePath) const;
    [[nodiscard]] Utils::FilePath shardFor(const Utils::FilePath &filePath) const;

    // Where one file's entries live, under a digest of the entries
    // themselves. A header is described the same way by most of the
    // translation units that read it, so the copies are one file.
    [[nodiscard]] Utils::FilePath entriesFor(const QByteArray &key) const;
    [[nodiscard]] QByteArray writeEntries(const QList<CxxFrontendIndexEntry> &entries) const;
    [[nodiscard]] std::optional<QList<CxxFrontendIndexEntry>> readEntries(
        const QByteArray &key) const;

    // Brings the store back under its bound, oldest reading first, and then
    // drops the entries nothing refers to any longer. Run once a session,
    // and on the thread that writes rather than the one that draws.
    void pruneToBound() const;

    Utils::FilePath m_directory;
    QByteArray m_macrosKey;
    qint64 m_maximumBytes = 0;
    // What was on disk before this session began, which is all the pruning
    // may remove: a file written since may belong to a shard another worker
    // has not finished writing.
    QDateTime m_startedAt;

    mutable QMutex m_mutex;
    mutable QHash<QString, QByteArray> m_contents;
    mutable QSet<QString> m_directoriesMade;
    // The entries files this session has put to use, written now or found
    // already there. What the bound may not take: the shard pointing at
    // one of them may still be on its way to disk.
    mutable QSet<QByteArray> m_keysUsed;
    mutable bool m_pruned = false;
    mutable int m_hits = 0;
    mutable int m_misses = 0;
};

} // namespace CppEditor::Internal
