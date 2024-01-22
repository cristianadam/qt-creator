// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "dashboard/dashboardclient.h"

#include <QFuture>

#include <memory>

namespace ProjectExplorer { class Project; }

namespace Axivion::Internal {

QFuture<DashboardClient::RawProjectList> fetchProjectList();
void fetchProjectInfo(const QString &projectName);
std::shared_ptr<const DashboardClient::ProjectInfo> projectInfo();
bool handleCertificateIssue();

} // Axivion::Internal

