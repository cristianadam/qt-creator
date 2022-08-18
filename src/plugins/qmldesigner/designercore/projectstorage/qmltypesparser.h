// Copyright  (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "nonlockingmutex.h"
#include "qmltypesparserinterface.h"

namespace Sqlite {
class Database;
}

namespace QmlDesigner {

template<typename Database>
class ProjectStorage;

template<typename ProjectStorage, typename Mutex>
class SourcePathCache;

class QmlTypesParser : public QmlTypesParserInterface
{
public:
    using ProjectStorage = QmlDesigner::ProjectStorage<Sqlite::Database>;
    using PathCache = QmlDesigner::SourcePathCache<ProjectStorage, NonLockingMutex>;

    QmlTypesParser(PathCache &pathCache, ProjectStorage &storage)
        : m_pathCache{pathCache}
        , m_storage{storage}
    {}

    void parse(const QString &sourceContent,
               Storage::Synchronization::Imports &imports,
               Storage::Synchronization::Types &types,
               const Storage::Synchronization::ProjectData &projectData) override;

private:
    PathCache &m_pathCache;
    ProjectStorage &m_storage;
};
} // namespace QmlDesigner
