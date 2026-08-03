// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/locator/ilocatorfilter.h>

#include <QString>

#include <functional>

namespace Alien::Internal {

// One command contributed by a running extension, as the locator shows it.
class AlienCommand
{
public:
    QString id;      // "qdocPreview.showPreview"
    QString title;   // "Open Preview"
    QString source;  // contributing extension, for the extra info column
};

// Puts the commands of the running extensions into the locator, so they are
// reachable the way any other Creator action is. Without it an extension's
// commands can only be run through a generic "Execute Alien Extension
// Command..." dialog, which means they are effectively undiscoverable.
class AlienLocatorFilter final : public Core::ILocatorFilter
{
public:
    AlienLocatorFilter(const std::function<QList<AlienCommand>()> &commands,
                       const std::function<void(const QString &id)> &execute);

private:
    Core::LocatorMatcherTasks matchers() final;

    std::function<QList<AlienCommand>()> m_commands;
    std::function<void(const QString &id)> m_execute;
};

} // namespace Alien::Internal
