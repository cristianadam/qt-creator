// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alienlocatorfilter.h"

#include "alientr.h"

using namespace Core;
using namespace QtTaskTree;

namespace Alien::Internal {

AlienLocatorFilter::AlienLocatorFilter(const std::function<QList<AlienCommand>()> &commands,
                                       const std::function<void(const QString &id)> &execute)
    : m_commands(commands)
    , m_execute(execute)
{
    setId("Run VS Code extension command");
    setDisplayName(Tr::tr("Run VS Code Extension Command"));
    setDescription(Tr::tr("Runs a command contributed by one of the running VS Code "
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

} // namespace Alien::Internal
