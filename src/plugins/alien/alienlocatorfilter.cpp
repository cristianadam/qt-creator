// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alienlocatorfilter.h"

#include "alientr.h"

#include <QtTaskTree/QBarrier>
#include <QtTaskTree/QTaskTree>

using namespace Core;
using namespace QtTaskTree;

namespace Alien::Internal {

AlienLocatorFilter::AlienLocatorFilter(const std::function<QList<AlienCommand>()> &commands,
                                       const std::function<void(const QString &id)> &execute)
    : m_commands(commands)
    , m_execute(execute)
{
    setId("Run VS Code extension command");
    setDisplayName(Tr::tr("Run Extension Command"));
    setDescription(Tr::tr("Runs a command contributed by one of the running VSIX "
                          "extensions."));
    setDefaultShortcutString("vsc");
    setPriority(Medium);
}

LocatorMatcherTasks AlienLocatorFilter::matchers()
{
    const auto onSetup = [commands = m_commands, execute = m_execute] {
        const LocatorStorage &storage = *LocatorStorage::storage();
        const QString input = storage.input();
        const Qt::CaseSensitivity sensitivity = caseSensitivity(input);

        // Same three-bucket ordering the other filters use: a prefix match
        // beats a substring match, which beats a match in the extra info.
        LocatorFilterEntries best;
        LocatorFilterEntries better;
        LocatorFilterEntries good;

        for (const AlienCommand &command : commands()) {
            const QString display = command.title.isEmpty() ? command.id : command.title;
            int index = display.indexOf(input, 0, sensitivity);
            auto dataType = LocatorFilterEntry::HighlightInfo::DisplayName;
            if (index < 0) {
                index = command.id.indexOf(input, 0, sensitivity);
                dataType = LocatorFilterEntry::HighlightInfo::ExtraInfo;
            }
            if (index < 0 && !input.isEmpty())
                continue;

            LocatorFilterEntry entry;
            entry.displayName = display;
            entry.extraInfo = command.source.isEmpty() ? command.id
                                                       : command.source + " - " + command.id;
            entry.acceptor = [execute, id = command.id] {
                execute(id);
                return AcceptResult();
            };
            if (index >= 0)
                entry.highlightInfo
                    = LocatorFilterEntry::HighlightInfo(index, input.size(), dataType);

            if (display.startsWith(input, sensitivity))
                best.append(entry);
            else if (display.contains(input, sensitivity))
                better.append(entry);
            else
                good.append(entry);
        }
        storage.reportOutput(best + better + good);
    };
    return {QSyncTask(onSetup)};
}

AlienSymbolLocatorFilter::AlienSymbolLocatorFilter(const SymbolSource &symbols)
    : m_symbols(symbols)
{
    setId("VS Code extension symbols");
    setDisplayName(Tr::tr("Extension Symbols"));
    setDescription(Tr::tr("Locates symbols reported by the running VSIX extensions."));
    setDefaultShortcutString("vss");
    setPriority(Medium);
}

LocatorMatcherTasks AlienSymbolLocatorFilter::matchers()
{
    // Asking the extension is a round trip. A barrier is what waits for it:
    // the request is sent when the task starts and the barrier is advanced
    // from the answer, so nothing blocks and no event loop is nested.
    Storage<QList<AlienSymbol>> resultStorage;

    const auto onQuerySetup = [symbols = m_symbols, resultStorage](QBarrier &barrier) {
        symbols(LocatorStorage::storage()->input(),
                [&barrier, resultStorage](const QList<AlienSymbol> &found) {
                    *resultStorage = found;
                    barrier.advance();
                });
    };

    const auto onReport = [resultStorage] {
        const LocatorStorage &storage = *LocatorStorage::storage();
        const QString input = storage.input();
        const Qt::CaseSensitivity sensitivity = caseSensitivity(input);

        LocatorFilterEntries entries;
        for (const AlienSymbol &symbol : *resultStorage) {
            LocatorFilterEntry entry;
            entry.displayName = symbol.name;
            entry.extraInfo = symbol.container.isEmpty()
                                  ? symbol.filePath.toUserOutput()
                                  : symbol.container + " - " + symbol.filePath.toUserOutput();
            entry.filePath = symbol.filePath;
            entry.linkForEditor = Utils::Link(symbol.filePath, symbol.line + 1, symbol.character);
            if (const int index = symbol.name.indexOf(input, 0, sensitivity); index >= 0) {
                entry.highlightInfo = LocatorFilterEntry::HighlightInfo(
                    index, input.size(), LocatorFilterEntry::HighlightInfo::DisplayName);
            }
            entries.append(entry);
        }
        storage.reportOutput(entries);
    };

    const Group root {
        resultStorage,
        QBarrierTask(onQuerySetup),
        QSyncTask(onReport),
    };
    return {root};
}

AlienTaskLocatorFilter::AlienTaskLocatorFilter(const TaskSource &tasks,
                                               const std::function<void(const QString &)> &run)
    : m_tasks(tasks)
    , m_run(run)
{
    setId("Run VS Code extension task");
    setDisplayName(Tr::tr("Run Extension Task"));
    setDescription(Tr::tr("Runs a task offered by one of the running VSIX extensions."));
    setDefaultShortcutString("vst");
    setPriority(Medium);
}

LocatorMatcherTasks AlienTaskLocatorFilter::matchers()
{
    Storage<QList<AlienTask>> resultStorage;

    const auto onQuerySetup = [tasks = m_tasks, resultStorage](QBarrier &barrier) {
        tasks([&barrier, resultStorage](const QList<AlienTask> &found) {
            *resultStorage = found;
            barrier.advance();
        });
    };

    const auto onReport = [resultStorage, run = m_run] {
        const LocatorStorage &storage = *LocatorStorage::storage();
        const QString input = storage.input();
        const Qt::CaseSensitivity sensitivity = caseSensitivity(input);

        LocatorFilterEntries entries;
        for (const AlienTask &task : *resultStorage) {
            const int index = task.name.indexOf(input, 0, sensitivity);
            if (index < 0 && !input.isEmpty())
                continue;
            LocatorFilterEntry entry;
            entry.displayName = task.name;
            entry.extraInfo = task.detail.isEmpty() ? task.source
                                                    : task.source + " - " + task.detail;
            entry.acceptor = [run, id = task.id] {
                run(id);
                return AcceptResult();
            };
            if (index >= 0) {
                entry.highlightInfo = LocatorFilterEntry::HighlightInfo(
                    index, input.size(), LocatorFilterEntry::HighlightInfo::DisplayName);
            }
            entries.append(entry);
        }
        storage.reportOutput(entries);
    };

    const Group root {resultStorage, QBarrierTask(onQuerySetup), QSyncTask(onReport)};
    return {root};
}

} // namespace Alien::Internal
