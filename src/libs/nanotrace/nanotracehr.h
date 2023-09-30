// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "nanotraceglobals.h"

#include <utils/span.h>

#include <array>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace NanotraceHR {
using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point< Clock >;
using Duration = std::chrono::duration<long long, std::nano>;

constexpr bool isTracerActive()
{
#ifdef NANOTRACE_ENABLED
    return true;
#else
    return false;
#endif
}

template<std::size_t size>
std::string_view toStringView(Utils::span<const char, size> string)
{
    return {string.data(), string.size()};
}

struct TraceEvent
{
    TraceEvent(const TraceEvent &) = delete;
    TraceEvent(TraceEvent &&) = delete;
    TraceEvent &operator=(const TraceEvent &) = delete;
    TraceEvent &operator=(TraceEvent &&) = delete;
    ~TraceEvent() = default;

    std::string_view name;
    std::string_view category;
    std::string_view arguments;
    TimePoint start;
    Duration duration;
};

using EventsSpan = Utils::span<TraceEvent>;
enum class IsEnabled { No, Yes };

void flushEvents(const EventsSpan &events, std::thread::id threadId, class Category &category);
void flushInThread(class Category &category);

class TraceFile
{
public:
    TraceFile([[maybe_unused]] std::string_view filePath)
        : filePath{filePath}
    {}
    std::string filePath;
    std::mutex fileMutex;
    std::future<void> processing;
};

class Category
{
public:
    Category() = default;
    ~Category()
    {
        if (isEnabled == IsEnabled::Yes)
            flushEvents(currentEvents, std::this_thread::get_id(), *this);
    }

    Category(const Category &) = delete;
    Category(TraceEvent &&) = delete;
    Category &operator=(const Category &) = delete;
    Category &operator=(Category &&) = delete;

    std::string_view name;
    TraceFile *file = nullptr;
    EventsSpan eventsOne;
    EventsSpan eventsTwo;
    EventsSpan currentEvents;
    std::size_t eventsIndex = 0;
    IsEnabled isEnabled = IsEnabled::No;
};

template<std::size_t eventCount>
class CategoryData
{
    using Events = std::array<TraceEvent, eventCount>;

public:
    CategoryData(std::string_view name, TraceFile &file)
        : name{name}
        , file{file}
    {}

    std::string name;
    TraceFile &file;
    Events eventsOne;
    Events eventsTwo;
};

template<std::size_t eventCount>
struct CategoryDataPointer
{
    operator Category() const
    {
        if constexpr (isTracerActive()) {
            return Category{data->name,
                            &data->file,
                            data->eventsOne,
                            data->eventsTwo,
                            data->eventsOne,
                            0,
                            IsEnabled::Yes};
        } else {
            return {};
        }
    }

    std::unique_ptr<CategoryData<eventCount>> data;
};

template<std::size_t eventCount>
CategoryDataPointer<eventCount> makeCategoryData(std::string_view name, TraceFile &file)
{
    if constexpr (isTracerActive()) {
        return std::make_unique<CategoryData<eventCount>>(name, file);
    } else {
        return {};
    }
}

extern thread_local Category fooCategory;

inline TraceEvent &getTraceEvent(Category &category)
{
    if (category.eventsIndex == category.currentEvents.size())
        flushInThread(category);

    return category.currentEvents[category.eventsIndex++];
}
template<std::size_t sizeName, std::size_t sizeArguments>
class Tracer
{
public:
    constexpr Tracer(const char (&name)[sizeName],
                     Category &category,
                     const char (&arguments)[sizeArguments])
        : m_name{name}
        , m_category{category}
        , m_arguments{arguments}
    {
        if constexpr (isTracerActive()) {
            if (m_category.isEnabled == IsEnabled::Yes)
                m_start = Clock::now();
        }
    }

    constexpr Tracer(const char (&name)[sizeName], Category &category)
        : Tracer{name, category, "{}"}
    {}

    std::string_view name() const { return toStringView(m_name); }
    std::string_view arguments() const { return toStringView(m_arguments); }

    ~Tracer()
    {
        if constexpr (isTracerActive()) {
            if (m_category.isEnabled == IsEnabled::Yes) {
                auto duration = Clock::now() - m_start;
                auto &traceEvent = getTraceEvent(m_category);
                traceEvent.name = toStringView(m_name);
                traceEvent.category = m_category.name;
                traceEvent.arguments = toStringView(m_arguments);
                traceEvent.start = m_start;
                traceEvent.duration = duration;
            }
        }
    }

private:
    TimePoint m_start;
    Utils::span<const char, sizeName> m_name;
    Category &m_category;
    Utils::span<const char, sizeArguments> m_arguments;
};

template<std::size_t sizeName, std::size_t sizeArguments>
Tracer(const char (&name)[sizeName], Category &category, const char (&arguments)[sizeArguments])
    -> Tracer<sizeName, sizeArguments>;

} // namespace NanotraceHR
