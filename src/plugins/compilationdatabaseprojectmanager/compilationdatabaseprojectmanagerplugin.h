// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <extensionsystem/iplugin.h>

namespace CompilationDatabaseProjectManager {
namespace Internal {

class CompilationDatabaseProjectManagerPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "CompilationDatabaseProjectManager.json")

    ~CompilationDatabaseProjectManagerPlugin();

    bool initialize(const QStringList &arguments, QString *errorMessage) final;
    QVector<QObject *> createTestObjects() const final;

    class CompilationDatabaseProjectManagerPluginPrivate *d = nullptr;
};

} // namespace Internal
} // namespace CompilationDatabaseProjectManager
