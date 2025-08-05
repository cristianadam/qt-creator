// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/idevicewidget.h>

#include <tasking/tasktreerunner.h>

#include <utils/pathchooser.h>
#include <utils/pathlisteditor.h>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QLineEdit;
class QTextBrowser;
class QToolButton;
QT_END_NAMESPACE

namespace Docker::Internal {

class Logger
{
public:
    std::function<void(const QString &message)> log;
    std::function<void()> clear;
    std::function<void()> onDone;
};

class DockerDeviceWidget final : public ProjectExplorer::IDeviceWidget
{
public:
    explicit DockerDeviceWidget(const ProjectExplorer::IDevice::Ptr &device);

    void updateDeviceFromUi() final {}
    void updateDaemonStateTexts();

private:
    QLabel *m_daemonState;
    QToolButton *m_daemonReset;
    QTextBrowser *m_logView = nullptr;
    Logger m_logger;
};

} // Docker::Internal
