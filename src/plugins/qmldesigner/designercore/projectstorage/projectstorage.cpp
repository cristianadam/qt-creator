// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectstorage.h"

#include <tracing/qmldesignertracing.h>

#include <sqlitedatabase.h>

namespace QmlDesigner {

NanotraceHR::StringCategory<projectStorageTracingStatus()> &projectStorageCategory()
{
    thread_local NanotraceHR::StringCategory<projectStorageTracingStatus()> projectStorageCategory_{
        "project storage"_t, Tracing::stringEventQueue(), projectStorageCategory};

    return projectStorageCategory_;
}

} // namespace QmlDesigner

template class QmlDesigner::ProjectStorage<Sqlite::Database>;
