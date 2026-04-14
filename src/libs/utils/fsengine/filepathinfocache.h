// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../filepath.h"

#include <QCache>
#include <QMutex>
#include <QMutexLocker>

namespace Utils::Internal {

class FilePathInfoCache
{
public:
    struct CachedData
    {
        FilePathInfo filePathInfo;
        QDateTime timeout;
    };

    using RetrievalFunction = CachedData (*)(const FilePath &);

public:
    FilePathInfoCache()
        : m_cache(50000)
    {}

    CachedData cached(const FilePath &filePath, const RetrievalFunction &retrievalFunction)
    {
        {
            QMutexLocker lk(&m_mutex);
            CachedData *data = m_cache.object(filePath);

            // If the cache entry is fresh, return a copy immediately.
            if (data && data->timeout >= QDateTime::currentDateTime())
                return *data;
        }

        // Retrieve without the lock: the retrieval function queries the
        // filesystem and must not hold m_mutex, because it may indirectly
        // call invalidate() (e.g. via a device file-access factory that
        // calls FSEngine::invalidateFileInfoCache()), which would deadlock.
        CachedData retrieved = retrievalFunction(filePath);

        QMutexLocker lk(&m_mutex);
        auto data = new CachedData(retrieved);
        if (Q_UNLIKELY(!m_cache.insert(filePath, data))) {
            // This path will never happen, but to silence coverity we
            // have to check it since insert in theory could delete
            // the object if a cost bigger than the cache size is
            // specified.
            // Note: data has been deleted by QCache::insert on failure.
            return retrieved;
        }

        // Return a copy of the data, so it cannot be deleted by the cache
        return *data;
    }

    void cache(const FilePath &path, CachedData *data)
    {
        QMutexLocker lk(&m_mutex);
        m_cache.insert(path, data);
    }

    void cache(const QList<QPair<FilePath, CachedData>> &fileDataList)
    {
        QMutexLocker lk(&m_mutex);
        for (const auto &[path, data] : fileDataList)
            m_cache.insert(path, new CachedData(data));
    }

    void invalidate(const FilePath &path)
    {
        QMutexLocker lk(&m_mutex);
        if (path.isEmpty()) {
            m_cache.clear();
            return;
        }

        m_cache.remove(path);
    }

private:
    QMutex m_mutex;
    QCache<FilePath, CachedData> m_cache;
};

} // namespace Utils::Internal
