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

} // namespace CppEditor::Internal
