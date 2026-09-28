// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QObject;
class QString;
QT_END_NAMESPACE

namespace ProjectExplorer { class BuildConfiguration; }
namespace Utils { class CommandLine; }

namespace Zephyr::Internal {

void setupWestBuildSteps();

QString westBoard(const ProjectExplorer::BuildConfiguration *bc);
Utils::CommandLine westConfigureCommand(const ProjectExplorer::BuildConfiguration *bc);

#ifdef WITH_TESTS
QObject *createWestBoardsTest();
#endif

} // namespace Zephyr::Internal
