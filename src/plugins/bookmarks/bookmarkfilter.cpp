// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bookmarkfilter.h"

#include "bookmark.h"
#include "bookmarkmanager.h"
#include "bookmarkstr.h"

#include <utils/algorithm.h>

using namespace Core;
using namespace Utils;

namespace Bookmarks::Internal {

BookmarkFilter::BookmarkFilter(BookmarkManager *manager)
    : m_manager(manager)
{
    setId("Bookmarks");
    setDisplayName(Tr::tr("Bookmarks"));
    setDescription(Tr::tr("Locates bookmarks. Filter by file name, by the text on the line of the "
                          "bookmark, or by the bookmark's note text."));
    setPriority(Medium);
    setDefaultShortcutString("b");
}

LocatorMatcherTasks BookmarkFilter::matchers()
{
    using namespace Tasking;

    TreeStorage<LocatorStorage> storage;

    const auto onSetup = [=] {
        if (m_manager->rowCount() == 0)
            return true;

        const auto match = [this](const QString &name, BookmarkManager::Roles role) {
            return m_manager->match(m_manager->index(0, 0), role, name, -1,
                                    Qt::MatchContains | Qt::MatchWrap);
        };

        const QString input = storage->input();
        const int colonIndex = input.lastIndexOf(':');
        QModelIndexList fileNameLineNumberMatches;
        if (colonIndex >= 0) {
            // Filter by "fileName:lineNumber" pattern
            const QString fileName = input.left(colonIndex);
            const QString lineNumber = input.mid(colonIndex + 1);
            fileNameLineNumberMatches = match(fileName, BookmarkManager::Filename);
            fileNameLineNumberMatches = Utils::filtered(fileNameLineNumberMatches,
                                                        [lineNumber](const QModelIndex &index) {
                return index.data(BookmarkManager::LineNumber).toString().contains(lineNumber);
            });
        }

        const QModelIndexList matches = filteredUnique(fileNameLineNumberMatches
                                                       + match(input, BookmarkManager::Filename)
                                                       + match(input, BookmarkManager::LineNumber)
                                                       + match(input, BookmarkManager::Note)
                                                       + match(input, BookmarkManager::LineText));
        LocatorFilterEntries entries;
        for (const QModelIndex &idx : matches) {
            const Bookmark *bookmark = m_manager->bookmarkForIndex(idx);
            const QString filename = bookmark->filePath().fileName();
            LocatorFilterEntry entry;
            entry.displayName = QString("%1:%2").arg(filename).arg(bookmark->lineNumber());
            // TODO: according to QModelIndex docs, we shouldn't store model indexes:
            // Model indexes should be used immediately and then discarded.
            // You should not rely on indexes to remain valid after calling model functions
            // that change the structure of the model or delete items.
            entry.acceptor = [manager = m_manager, idx] {
                if (Bookmark *bookmark = manager->bookmarkForIndex(idx))
                    manager->gotoBookmark(bookmark);
                return AcceptResult();
            };
            if (!bookmark->note().isEmpty())
                entry.extraInfo = bookmark->note();
            else if (!bookmark->lineText().isEmpty())
                entry.extraInfo = bookmark->lineText();
            else
                entry.extraInfo = bookmark->filePath().toString();
            int highlightIndex = entry.displayName.indexOf(input, 0, Qt::CaseInsensitive);
            if (highlightIndex >= 0) {
                entry.highlightInfo = {highlightIndex, int(input.length())};
            } else  {
                highlightIndex = entry.extraInfo.indexOf(input, 0, Qt::CaseInsensitive);
                if (highlightIndex >= 0) {
                    entry.highlightInfo = {highlightIndex, int(input.length()),
                                                 LocatorFilterEntry::HighlightInfo::ExtraInfo};
                } else if (colonIndex >= 0) {
                    const QString fileName = input.left(colonIndex);
                    const QString lineNumber = input.mid(colonIndex + 1);
                    highlightIndex = entry.displayName.indexOf(fileName, 0,
                                                                     Qt::CaseInsensitive);
                    if (highlightIndex >= 0) {
                        entry.highlightInfo = {highlightIndex, int(fileName.length())};
                        highlightIndex = entry.displayName.indexOf(
                            lineNumber, highlightIndex, Qt::CaseInsensitive);
                        if (highlightIndex >= 0) {
                            entry.highlightInfo.startsDisplay += highlightIndex;
                            entry.highlightInfo.lengthsDisplay += lineNumber.length();
                        }
                    }
                }
            }
            entry.displayIcon = bookmark->icon();
            entries.append(entry);
        }
        storage->reportOutput(entries);
        return true;
    };
    return {{Sync(onSetup), storage}};
}

void BookmarkFilter::prepareSearch(const QString &entry)
{
    m_results = {};
    if (m_manager->rowCount() == 0)
        return;

    auto match = [this](const QString &name, BookmarkManager::Roles role) {
        return m_manager->match(m_manager->index(0, 0), role, name, -1,
                                Qt::MatchContains | Qt::MatchWrap);
    };

    int colonIndex = entry.lastIndexOf(':');
    QModelIndexList fileNameLineNumberMatches;
    if (colonIndex >= 0) {
        // Filter by "fileName:lineNumber" pattern
        const QString fileName = entry.left(colonIndex);
        const QString lineNumber = entry.mid(colonIndex + 1);
        fileNameLineNumberMatches = match(fileName, BookmarkManager::Filename);
        fileNameLineNumberMatches =
                Utils::filtered(fileNameLineNumberMatches, [lineNumber](const QModelIndex &index) {
                    return index.data(BookmarkManager::LineNumber).toString().contains(lineNumber);
                });
    }

    const QModelIndexList matches = filteredUnique(fileNameLineNumberMatches
                                                   + match(entry, BookmarkManager::Filename)
                                                   + match(entry, BookmarkManager::LineNumber)
                                                   + match(entry, BookmarkManager::Note)
                                                   + match(entry, BookmarkManager::LineText));

    for (const QModelIndex &idx : matches) {
        const Bookmark *bookmark = m_manager->bookmarkForIndex(idx);
        const QString filename = bookmark->filePath().fileName();
        LocatorFilterEntry filterEntry;
        filterEntry.displayName = QString("%1:%2").arg(filename).arg(bookmark->lineNumber());
        // TODO: according to QModelIndex docs, we shouldn't store model indexes:
        // Model indexes should be used immediately and then discarded.
        // You should not rely on indexes to remain valid after calling model functions that change
        // the structure of the model or delete items.
        filterEntry.acceptor = [manager = m_manager, idx] {
            if (Bookmark *bookmark = manager->bookmarkForIndex(idx))
                manager->gotoBookmark(bookmark);
            return AcceptResult();
        };
        if (!bookmark->note().isEmpty())
            filterEntry.extraInfo = bookmark->note();
        else if (!bookmark->lineText().isEmpty())
            filterEntry.extraInfo = bookmark->lineText();
        else
            filterEntry.extraInfo = bookmark->filePath().toString();
        int highlightIndex = filterEntry.displayName.indexOf(entry, 0, Qt::CaseInsensitive);
        if (highlightIndex >= 0) {
            filterEntry.highlightInfo = {highlightIndex, int(entry.length())};
        } else  {
            highlightIndex = filterEntry.extraInfo.indexOf(entry, 0, Qt::CaseInsensitive);
            if (highlightIndex >= 0) {
                filterEntry.highlightInfo = {highlightIndex, int(entry.length()),
                                             LocatorFilterEntry::HighlightInfo::ExtraInfo};
            } else if (colonIndex >= 0) {
                const QString fileName = entry.left(colonIndex);
                const QString lineNumber = entry.mid(colonIndex + 1);
                highlightIndex = filterEntry.displayName.indexOf(fileName, 0, Qt::CaseInsensitive);
                if (highlightIndex >= 0) {
                    filterEntry.highlightInfo = {highlightIndex, int(fileName.length())};
                    highlightIndex = filterEntry.displayName.indexOf(
                        lineNumber, highlightIndex, Qt::CaseInsensitive);
                    if (highlightIndex >= 0) {
                        filterEntry.highlightInfo.startsDisplay += highlightIndex;
                        filterEntry.highlightInfo.lengthsDisplay += lineNumber.length();
                    }
                }
            }
        }

        filterEntry.displayIcon = bookmark->icon();
        m_results.append(filterEntry);
    }
}

QList<LocatorFilterEntry> BookmarkFilter::matchesFor(QFutureInterface<LocatorFilterEntry> &future,
                                                     const QString &entry)
{
    Q_UNUSED(future)
    Q_UNUSED(entry)
    return m_results;
}

} // Bookmarks::Internal
