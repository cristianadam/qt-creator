// Copyright  (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "nonlockingmutex.h"
#include "qmldocumentparserinterface.h"

namespace Sqlite {
class Database;
}

namespace QmlDesigner {

template<typename Database>
class ProjectStorage;

template<typename ProjectStorage, typename Mutex>
class SourcePathCache;

class QmlDocumentParser : public QmlDocumentParserInterface
{
public:
    using ProjectStorage = QmlDesigner::ProjectStorage<Sqlite::Database>;
    using PathCache = QmlDesigner::SourcePathCache<ProjectStorage, NonLockingMutex>;

    QmlDocumentParser(ProjectStorage &storage, PathCache &pathCache)
        : m_storage{storage}
        , m_pathCache{pathCache}
    {}

    Storage::Synchronization::Type parse(const QString &sourceContent,
                                         Storage::Synchronization::Imports &imports,
                                         SourceId sourceId,
                                         Utils::SmallStringView directoryPath) override;

private:
    ProjectStorage &m_storage;
    PathCache &m_pathCache;
};
} // namespace QmlDesigner
