// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "autotest_global.h"

#include <extensionsystem/iplugin.h>

namespace ProjectExplorer {
class Project;
class RunConfiguration;
}

namespace Autotest {
namespace Internal {

class TestProjectSettings;
struct TestSettings;

struct ChoicePair
{
    explicit ChoicePair(const QString &name = QString(), const QString &exe = QString())
        : displayName(name), executable(exe) {}
    bool matches(const ProjectExplorer::RunConfiguration *rc) const;

    QString displayName;
    QString executable;
};

class AutotestPlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "AutoTest.json")

public:
    AutotestPlugin();
    ~AutotestPlugin() override;

    bool initialize(const QStringList &arguments, QString *errorString) override;
    void extensionsInitialized() override;
    ShutdownFlag aboutToShutdown() override;

    static TestSettings *settings();
    static TestProjectSettings *projectSettings(ProjectExplorer::Project *project);
    static void updateMenuItemsEnabledState();
    static void cacheRunConfigChoice(const QString &buildTargetKey, const ChoicePair &choice);
    static ChoicePair cachedChoiceFor(const QString &buildTargetKey);
    static void clearChoiceCache();
    static void popupResultsPane();

private:
    QVector<QObject *> createTestObjects() const override;
};

} // namespace Internal
} // namespace Autotest
