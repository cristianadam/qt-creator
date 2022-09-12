// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "dockersettings.h"

#include <utils/deviceshell.h>
#include <utils/filepath.h>
#include <utils/qtcprocess.h>


namespace Docker::Internal {

class ContainerShell : public Utils::DeviceShell
{
public:
    ContainerShell(DockerSettings *settings, const QString &containerId, const Utils::FilePath &devicePath)
        : m_settings(settings)
        , m_containerId(containerId)
        , m_devicePath(devicePath)
    {
    }

private:
    void setupShellProcess(Utils::QtcProcess *shellProcess) final
    {
        shellProcess->setCommand({m_settings->dockerBinaryPath.filePath(), {"container", "start", "-i", "-a", m_containerId}});
    }

    Utils::CommandLine createFallbackCommand(const Utils::CommandLine &cmdLine)
    {
        Utils::CommandLine result = cmdLine;
        result.setExecutable(cmdLine.executable().onDevice(m_devicePath));
        return result;
    }

private:
    DockerSettings *m_settings;
    QString m_containerId;
    Utils::FilePath m_devicePath;
};


} // namespace Docker::Internal
