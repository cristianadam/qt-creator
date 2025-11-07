// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "remotelinux_export.h"

#include <projectexplorer/devicesupport/idevice.h>

QT_BEGIN_NAMESPACE
namespace QtTaskTree { class GroupItem; }
QT_END_NAMESPACE

namespace RemoteLinux {

namespace Internal { class SshDeviceTesterPrivate; }

class REMOTELINUX_EXPORT SshDeviceTester : public ProjectExplorer::DeviceTester
{
    Q_OBJECT

public:
    explicit SshDeviceTester(const ProjectExplorer::IDevice::Ptr &device);
    ~SshDeviceTester() override;

    void setExtraCommandsToTest(const QStringList &extraCommands);
    void setExtraTests(const QList<QtTaskTree::GroupItem> &extraTests);
    void testDevice() override;
    void stopTest() override;

private:
    std::unique_ptr<Internal::SshDeviceTesterPrivate> d;
};

} // namespace RemoteLinux
