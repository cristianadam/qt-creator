// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/locator/ilocatorfilter.h>

#include <utils/filepath.h>

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

// One symbol an extension found somewhere in the workspace.
class AlienSymbol
{
public:
    QString name;
    QString container;
    Utils::FilePath filePath;
    int line = 0;      // zero-based, as the extension reports it
    int character = 0;
};

// Puts what an extension knows about the workspace where Qt Creator keeps the
// same thing for the languages it understands itself. The extension does the
// matching: it is asked with what was typed, because only it knows what its
// names look like.
class AlienSymbolLocatorFilter final : public Core::ILocatorFilter
{
public:
    using SymbolSource = std::function<void(const QString &query,
                                            const std::function<void(const QList<AlienSymbol> &)> &)>;

    explicit AlienSymbolLocatorFilter(const SymbolSource &symbols);

private:
    Core::LocatorMatcherTasks matchers() final;

    SymbolSource m_symbols;
};

// One task an extension offers to run.
class AlienTask
{
public:
    QString id;
    QString name;
    QString source;
    QString detail;
};

// The tasks the running extensions provide, where Qt Creator's other run
// entries are. Without this they are registered and unreachable: only another
// extension could ever ask for them.
class AlienTaskLocatorFilter final : public Core::ILocatorFilter
{
public:
    using TaskSource = std::function<void(const std::function<void(const QList<AlienTask> &)> &)>;

    AlienTaskLocatorFilter(const TaskSource &tasks,
                           const std::function<void(const QString &id)> &run);

private:
    Core::LocatorMatcherTasks matchers() final;

    TaskSource m_tasks;
    std::function<void(const QString &id)> m_run;
};

} // namespace Alien::Internal
