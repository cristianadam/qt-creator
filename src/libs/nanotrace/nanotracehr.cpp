// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "nanotracehr.h"

#include <fstream>
#include <iostream>
#include <thread>

namespace NanotraceHR {

namespace {

void printEvent(std::ostream &out,
                const TraceEvent &event,
                qint64 processId,
                std::thread::id threadId)
{
    out << R"({ph":"X","name":")" << event.name << R"(",cat":")" << event.category << R"(","ts":")"
        << event.start.time_since_epoch().count() << R"(","dur":")" << event.duration.count()
        << R"(","pid":")" << processId << R"(","tid":")" << threadId << R"(","args":")"
        << event.arguments << "}";
}
} // namespace

void flushEvents(const EventsSpan &events, std::thread::id threadId, Category &category)
{
    if (events.empty())
        return;

    std::lock_guard lock{category.file->fileMutex};
    if (std::ofstream out(category.file->filePath, std::ios::app); out.good()) {
        out << R"({"traceEvents": [)";

        auto processId = QCoreApplication::applicationPid();

        printEvent(out, events.front(), processId, threadId);
        for (const auto &event : events.subspan(1)) {
            out << ",\n";
            printEvent(out, event, processId, threadId);
        }

        out << R"([,"displayTimeUnit":"ns","otherData":{"version": "Qt Creator )";
        out << QCoreApplication::applicationVersion().toStdString();
        out << R"("}]})";
    } else {
        std::cout << "Nanotrace::flush: stream is not good!" << std::endl;
    }
}

void flushInThread(Category &category)
{
    if (category.file->processing.valid())
        category.file->processing.wait();

    auto flush = [&](const EventsSpan &events, std::thread::id threadId) {
        flushEvents(events, threadId, category);
    };

    category.file->processing
        = std::async(std::launch::async, flush, category.currentEvents, std::this_thread::get_id());
    category.currentEvents = category.currentEvents.data() == category.eventsOne.data()
                                 ? category.eventsTwo
                                 : category.eventsOne;
    category.eventsIndex = 0;
}

} // namespace NanotraceHR
