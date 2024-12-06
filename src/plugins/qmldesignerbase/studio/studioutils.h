// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../qmldesignerbase_global.h"

#include <utils/fileutils.h>
#include <utils/id.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <QtQml/qqml.h>
#include <QLocale>

namespace QmlDesigner {

namespace StudioUtils {

QMLDESIGNERBASE_EXPORT void logError(
    const QString &message,
    Utils::Id category = ProjectExplorer::Constants::TASK_CATEGORY_BUILDSYSTEM,
    const Utils::FilePath &file = {},
    int line = -1);

QMLDESIGNERBASE_EXPORT void logWarning(
    const QString &message,
    Utils::Id category = ProjectExplorer::Constants::TASK_CATEGORY_BUILDSYSTEM,
    const Utils::FilePath &file = {},
    int line = -1);

QMLDESIGNERBASE_EXPORT void clearTasks(
    Utils::Id category = ProjectExplorer::Constants::TASK_CATEGORY_BUILDSYSTEM);

} // namespace StudioUtils

// namespace StudioUtils

} // namespace QmlDesigner
