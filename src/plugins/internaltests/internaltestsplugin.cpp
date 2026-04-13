// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dockerdebuggertest.h"

#include <extensionsystem/iplugin.h>

namespace InternalTests::Internal {

class InternalTestsPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "InternalTests.json")

    void initialize() final
    {
        addTestCreator(createDockerDebuggerTest);
    }
};

} // namespace InternalTests::Internal

#include "internaltestsplugin.moc"
