// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "filestatuscache.h"
#include "filesystem.h"
#include "projectstoragetracing.h"

#include <utils/algorithm.h>
#include <utils/set_algorithm.h>

#include <QDateTime>
#include <QFileInfo>

namespace QmlDesigner {

using NanotraceHR::keyValue;
using ProjectStorageTracing::category;

void FileStatusCache::update(SourceId sourceId)
{
    auto found = std::ranges::lower_bound(m_cacheEntries, sourceId, {}, &FileStatus::sourceId);

    if (found != m_cacheEntries.end() && found->sourceId == sourceId)
        *found = m_fileSystem.fileStatus(sourceId);
}

void FileStatusCache::update(SourceIds sourceIds)
{
    NanotraceHR::Tracer tracer{"file status cache update", category()};

    auto getFileStatus = [&](auto &entry) {
        NanotraceHR::Tracer tracer{"get file status", category()};

        entry = m_fileSystem.fileStatus(entry.sourceId);

        tracer.end(keyValue("entry", entry));
    };

    Utils::set_greedy_intersection(m_cacheEntries, sourceIds, getFileStatus, {}, &FileStatus::sourceId);
}

SourceIds FileStatusCache::modified(SourceIds sourceIds) const
{
    NanotraceHR::Tracer tracer{"file status cache modified", category()};

    SourceIds modifiedSourceIds;
    modifiedSourceIds.reserve(sourceIds.size());

    Utils::set_greedy_intersection(
        m_cacheEntries,
        sourceIds,
        [&](auto &entry) {
            auto fileStatus = m_fileSystem.fileStatus(entry.sourceId);
            if (fileStatus != entry) {
                modifiedSourceIds.push_back(entry.sourceId);
                entry = fileStatus;
                tracer.tick("update entry", keyValue("entry", entry));
            }
        },
        {},
        &FileStatus::sourceId);

    FileStatuses newEntries;
    newEntries.reserve(sourceIds.size());

    Utils::set_greedy_difference(
        sourceIds,
        m_cacheEntries,
        [&](SourceId newSourceId) {
            const auto &entry = newEntries.emplace_back(m_fileSystem.fileStatus(newSourceId));
            modifiedSourceIds.push_back(newSourceId);
            tracer.tick("new entry", keyValue("entry", entry));
        },
        {},
        {},
        &FileStatus::sourceId);

    if (newEntries.size()) {
        FileStatuses mergedEntries;
        mergedEntries.reserve(m_cacheEntries.size() + newEntries.size());

        std::ranges::set_union(newEntries, m_cacheEntries, std::back_inserter(mergedEntries));

        m_cacheEntries = std::move(mergedEntries);
    }

    std::ranges::sort(modifiedSourceIds);

    return modifiedSourceIds;
}

FileStatusCache::size_type FileStatusCache::size() const
{
    NanotraceHR::Tracer tracer{"file status cache size", category()};

    return m_cacheEntries.size();
}

const FileStatus &FileStatusCache::find(SourceId sourceId) const
{
    NanotraceHR::Tracer tracer{"file status cache find", category(), keyValue("source id", sourceId)};

    auto found = std::ranges::lower_bound(m_cacheEntries, sourceId, {}, &FileStatus::sourceId);

    if (found != m_cacheEntries.end() && found->sourceId == sourceId) {
        const auto &entry = *found;

        tracer.tick("found entry", keyValue("entry", entry));

        return entry;
    }

    auto inserted = m_cacheEntries.insert(found, m_fileSystem.fileStatus(sourceId));
    const auto &entry = *inserted;

    tracer.tick("inserted entry", keyValue("entry", entry));

    return entry;
}

} // namespace QmlDesigner
