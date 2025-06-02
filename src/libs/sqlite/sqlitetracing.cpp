// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "sqlitetracing.h"

namespace Sqlite {

#ifdef ENABLE_SQLITE_TRACING

std::shared_ptr<TraceFile> traceFile()
{
    static auto traceFile = std::make_shared<TraceFile>("tracing.json");

    return traceFile;
}

namespace {

thread_local NanotraceHR::EnabledEventQueueWithoutArguments eventQueue(traceFile());
thread_local NanotraceHR::EnabledEventQueueWithoutArguments eventQueueWithoutArguments(traceFile());

} // namespace

NanotraceHR::EnabledCategory &sqliteLowLevelCategory()
{
    thread_local NanotraceHR::EnabledCategory sqliteLowLevelCategory_{"sqlite low level",
                                                                      eventQueue,
                                                                      eventQueueWithoutArguments,
                                                                      sqliteLowLevelCategory};
    return sqliteLowLevelCategory_;
}

NanotraceHR::EnabledCategory &sqliteHighLevelCategory()
{
    thread_local NanotraceHR::EnabledCategory sqliteHighLevelCategory_{"sqlite high level",
                                                                       eventQueue,
                                                                       eventQueueWithoutArguments,
                                                                       sqliteHighLevelCategory};

    return sqliteHighLevelCategory_;
}

#endif

} // namespace Sqlite
