// Copyright (C) 2020 Alexis Jeandet.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "mesonprojectparser.h"
#include "kitdata.h"

#include <cppeditor/cppprojectupdater.h>

#include <projectexplorer/buildsystem.h>
#include <projectexplorer/target.h>

#include <utils/filesystemwatcher.h>

namespace MesonProjectManager {
namespace Internal {

class MesonBuildConfiguration;
class MesonBuildSystem final : public ProjectExplorer::BuildSystem
{
    Q_OBJECT
public:
    MesonBuildSystem(MesonBuildConfiguration *bc);
    ~MesonBuildSystem() final;

    void triggerParsing() final;
    QString name() const final { return QLatin1String("meson"); }

    inline const BuildOptionsList &buildOptions() const { return m_parser.buildOptions(); }
    inline const TargetsList &targets() const { return m_parser.targets(); }

    bool configure();
    bool setup();
    bool wipe();

    MesonBuildConfiguration *mesonBuildConfiguration();

    const QStringList &targetList() const { return m_parser.targetsNames(); }

    void setMesonConfigArgs(const QStringList &args) { m_pendingConfigArgs = args; }

private:
    void init();
    bool parseProject();
    void updateKit(ProjectExplorer::Kit *kit);
    bool needsSetup();
    void parsingCompleted(bool success);
    ProjectExplorer::Kit *kit();
    QStringList configArgs(bool isSetup);
    ProjectExplorer::BuildSystem::ParseGuard m_parseGuard;
    MesonProjectParser m_parser;
    CppEditor::CppProjectUpdater m_cppCodeModelUpdater;
    QStringList m_pendingConfigArgs;
    Utils::FileSystemWatcher m_IntroWatcher;
    KitData m_kitData;
};

} // namespace Internal
} // namespace MesonProjectManager
