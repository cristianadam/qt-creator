// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "studioutils.h"

#include <coreplugin/icore.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/taskhub.h>

namespace {

void logIssue(
    ProjectExplorer::Task::TaskType type,
    const QString &message,
    Utils::Id category = ProjectExplorer::Constants::TASK_CATEGORY_BUILDSYSTEM,
    const Utils::FilePath &file = {},
    int line = -1)
{
    ProjectExplorer::Task task(type, message, file, line, category);
    ProjectExplorer::TaskHub::addTask(task);
    ProjectExplorer::TaskHub::requestPopup();
}
} // namespace

namespace QmlDesigner {

namespace StudioUtils {

void logError(const QString &message, Utils::Id category, const Utils::FilePath &file, int line)
{
    logIssue(ProjectExplorer::Task::Warning, message, category, file, line);
}

void logWarning(const QString &message, Utils::Id category, const Utils::FilePath &file, int line)
{
    logIssue(ProjectExplorer::Task::Error, message, category, file, line);
}

void clearTasks(Utils::Id category)
{
    ProjectExplorer::TaskHub::clearTasks(category);
}

} // namespace StudioUtils

} // namespace QmlDesigner
