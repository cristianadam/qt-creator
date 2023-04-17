// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "clangutils.h"

#include <extensionsystem/iplugin.h>

#include <utils/futuresynchronizer.h>
#include <utils/parameteraction.h>

#include <QFutureWatcher>

namespace ClangCodeModel::Internal {

class ClangCodeModelPlugin final: public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "ClangCodeModel.json")

public:
    ClangCodeModelPlugin();
    ~ClangCodeModelPlugin() override;
    void initialize() override;

    static Utils::FutureSynchronizer *futureSynchronizer();

private:
    void generateCompilationDB();
    void createCompilationDBAction();

    Utils::ParameterAction *m_generateCompilationDBAction = nullptr;
    QFutureWatcher<GenerateCompilationDbResult> m_generatorWatcher;
    Utils::FutureSynchronizer m_futureSynchronizer;
};

} // ClangCodeModel::Internal
