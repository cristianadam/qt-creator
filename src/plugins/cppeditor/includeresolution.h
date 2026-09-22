// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/headerpath.h>

#include <utils/filepath.h>

#include <QHash>
#include <QMutex>

#include <functional>

namespace CppEditor::Internal {

// Where an #include is to be found among a project part's header paths.
//
// Shared because two models read the same code and must reach the same
// files: the built-in source processor, and the cxx front end's index,
// which used to answer an include by looking the including file up in the
// built-in model's snapshot -- and so could not run until that model had
// read the file first.

// The header paths as a resolver must walk them, which is not quite the
// list a project part holds: a framework path is followed by the private
// frameworks nested inside it, since a framework's own headers include
// those without saying where they are. The directories are listed here, so
// this is worth doing once for a project part rather than once per include.
ProjectExplorer::HeaderPaths preparedHeaderPaths(const ProjectExplorer::HeaderPaths &headerPaths);

// Where \a name stands among \a headerPaths, searched in order from \a from,
// or an empty path where none of them holds it. \a isThere says what counts
// as present, a caller having files in hand that the disk does not -- the
// ones being edited.
//
// A framework path answers `QtCore/qstring.h` as
// `<path>/QtCore.framework/Headers/qstring.h`, which is why a name with no
// directory in it cannot come from one.
Utils::FilePath resolveAmongHeaderPaths(
    const QString &name,
    const ProjectExplorer::HeaderPaths &headerPaths,
    const std::function<bool(const Utils::FilePath &)> &isThere,
    int from = 0);

// What names have already been found among one list of header paths.
//
// Worth sharing between the readings of a batch, and this is the whole
// reason the class exists: where the paths hold a name does not depend on
// who asked, the files of a project part ask about the same few thousand
// names, and one reading of this project asks some seventeen thousand
// times. A walk that finds nothing has asked the disk about every one of
// fifty-odd paths before it says so, which is why nowhere is remembered
// too.
//
// Shared between the workers of a batch, so it locks. The walk itself runs
// outside the lock: two workers may then do one name twice, which costs a
// walk and cannot differ, where holding the lock across it would put every
// worker behind whichever is reading the disk.
class ResolvedNames
{
public:
    Utils::FilePath resolve(const QString &name,
                            const ProjectExplorer::HeaderPaths &headerPaths,
                            const std::function<bool(const Utils::FilePath &)> &isThere);

private:
    QMutex m_mutex;
    QHash<QString, Utils::FilePath> m_known;
};

// The text of the headers a batch reads.
//
// A header is handed over once per *inclusion*, not once per file and not
// once per reading: the front end asks for it again every time an #include
// names it, since whether an include guard makes that a no-op is something
// only the preprocessor knows. Measured over a twenty-file slice of this
// project, one batch made **320,000 reads of 4.9 GB against 1,801 distinct
// files holding 12 MB** -- the same bytes some four hundred times over, and
// a third of the reader's time in open() and the UTF-8 decode behind it.
//
// So the text is kept for as long as the batch is, which is the same view
// of the disk the store's content digests are memoized against. A file
// written while a batch runs is read again on the next pass, the way one
// changed between passes is.
//
// Bounded, because a large project's header set is not: past the bound
// nothing more is kept and the readers go back to the disk for what is not
// already there. Whatever is read first stays, which is the hot set --
// every translation unit reaches much the same headers.
//
// Shared between the workers of a batch, so it locks; and as with
// ResolvedNames the disk read happens outside the lock, since holding it
// across one would put every reader behind whichever is reading.
class HeaderContents
{
public:
    // \a maximumBytes counts the files' own bytes rather than what they
    // take as QString, which is about twice that for text.
    explicit HeaderContents(qint64 maximumBytes = 256 * 1024 * 1024);

    // What \a filePath says, or nothing where it cannot be read. The same
    // QString every time, so handing it over costs a reference rather than
    // a copy of the file.
    std::optional<QString> textOf(const Utils::FilePath &filePath);

    // How many asks were answered without going to the disk, and how many
    // were not: the one outward sign that this does anything.
    int hits() const;
    int misses() const;

private:
    mutable QMutex m_mutex;
    QHash<Utils::FilePath, QString> m_known;
    qint64 m_bytes = 0;
    const qint64 m_maximumBytes;
    int m_hits = 0;
    int m_misses = 0;
};

} // namespace CppEditor::Internal
