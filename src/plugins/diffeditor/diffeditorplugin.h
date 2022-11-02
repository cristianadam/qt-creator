// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "diffeditor_global.h"

#include <coreplugin/diffservice.h>
#include <extensionsystem/iplugin.h>

namespace Utils { class FutureSynchronizer; }

QT_BEGIN_NAMESPACE
template <typename T>
class QFuture;
QT_END_NAMESPACE

namespace DiffEditor {
namespace Internal {

class DiffEditorServiceImpl : public QObject, public Core::DiffService
{
    Q_OBJECT
    Q_INTERFACES(Core::DiffService)

public:
    DiffEditorServiceImpl();

    void diffFiles(const QString &leftFileName, const QString &rightFileName) override;
    void diffModifiedFiles(const QStringList &fileNames) override;
};

class DiffEditorPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "DiffEditor.json")

public:
    DiffEditorPlugin();
    ~DiffEditorPlugin();

    bool initialize(const QStringList &arguments, QString *errorMessage) final;

    static Utils::FutureSynchronizer *futureSynchronizer();

private:
    class DiffEditorPluginPrivate *d = nullptr;

#ifdef WITH_TESTS
private slots:
    void testMakePatch_data();
    void testMakePatch();
    void testReadPatch_data();
    void testReadPatch();
    void testFilterPatch_data();
    void testFilterPatch();
#endif // WITH_TESTS
};

} // namespace Internal
} // namespace DiffEditor
