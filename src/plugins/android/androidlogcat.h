// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "androiddevice.h"

#include <QString>

#include <functional>

namespace ProjectExplorer {
class RunControl;
}

namespace Android::Internal {

void showLogcatTab(const AndroidDevice::ConstPtr &device);

void prepareForLogcatTab(ProjectExplorer::RunControl *runControl);
void bindRunningAppToLogcat(
    ProjectExplorer::RunControl *runControl, qint64 pid, const QString &packageName);
void unbindRunningAppFromLogcat(ProjectExplorer::RunControl *runControl);

using JdbCallback = std::function<void()>;

void setJdbCallbacksForLogcat(
    ProjectExplorer::RunControl *runControl,
    JdbCallback onWaitChunk,
    JdbCallback onSettled);
void clearJdbCallbacksForLogcat(ProjectExplorer::RunControl *runControl);
} // namespace Android::Internal
