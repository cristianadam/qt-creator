// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpplocatordata.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"
#endif

#include <utils/stringtable.h>

using namespace Utils;

namespace CppEditor {

using namespace Internal;

CppLocatorData::CppLocatorData() = default;

// What \a document declares, as the entries an index keeps.
//
// Worked out here rather than under the lock: this is the whole cost of
// keeping the index, and holding the lock through it would stop every
// locator query for as long as it takes.
static IndexItem::Ptr entriesFor(const CPlusPlus::Document::Ptr &document)
{
#ifdef QTC_WITH_CXX_FRONTEND
    // The other model reads the file itself -- a second parse of every file
    // a project has, which is what running the index on it costs.
    if (const std::optional<IndexItem::Ptr> fromTheModel
        = Internal::cxxFrontendIndexTreeFor(CppModelManager::snapshot(), document->filePath())) {
        return *fromTheModel;
    }
#endif

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

    const IndexItem::Ptr forThisFile = entriesFor(document);
    if (!forThisFile)
        return;

    QMutexLocker locker(&m_infosByFileMutex);
    m_infosByFile.insert(document->filePath().intern(), forThisFile);
}

void CppLocatorData::onAboutToRemoveFiles(const FilePaths &files)
{
    if (files.isEmpty())
        return;

    QMutexLocker locker(&m_infosByFileMutex);

    for (const FilePath &file : files)
        m_infosByFile.remove(file);

    StringTable::scheduleGC();
}

} // namespace CppEditor
