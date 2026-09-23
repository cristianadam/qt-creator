// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/locator/ilocatorfilter.h>

#include <utils/filepath.h>

#include <QFuture>

#include <unordered_set>

namespace CppEditor {
class CodeModelQueries;

namespace Internal {

// Every file the files in \a inputFilePaths reach through their includes,
// once each and themselves left out -- what this filter offers.
//
// Only out of what has already been read: \a queries answers off the
// built-in reading where a pass left one and off the cxx index's include
// graph otherwise, and says nothing for a file neither has. Reading a file
// to find out would be a parse of it and every header it reaches, once per
// file of every open project.
//
// A free function, rather than the body of the generator, so that a test
// can hand it a reading of its own.
Utils::FilePaths filesIncludedBy(const CodeModelQueries &queries,
                                 const std::unordered_set<Utils::FilePath> &inputFilePaths,
                                 const QFuture<void> &future);

class CppIncludesFilter final : public Core::ILocatorFilter
{
public:
    CppIncludesFilter();

private:
    Core::LocatorMatcherTasks matchers() final { return {m_cache.matcher()}; }
    Core::LocatorFileCache m_cache;
};

} // namespace Internal
} // namespace CppEditor
