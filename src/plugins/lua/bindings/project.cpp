// Copyright (C) The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "../luaengine.h"

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>

using namespace ProjectExplorer;

namespace Lua::Internal {

void setupProjectModule()
{
    registerProvider("Project", [](sol::state_view lua) -> sol::object {
        sol::table result = lua.create_table();

        result.new_usertype<Project>(
            "Project", sol::no_constructor, "directory", sol::property(&Project::projectDirectory));

        result["canRunStartupProject"] =
            [](const QString &mode) -> std::pair<bool, std::variant<QString, sol::lua_nil_t>> {
            auto result = ProjectExplorerPlugin::canRunStartupProject(Utils::Id::fromString(mode));
            if (result)
                return std::make_pair(true, sol::lua_nil);
            return std::make_pair(false, result.error());
        };

        result["RunMode"] = lua.create_table_with(
            "Normal", Constants::NORMAL_RUN_MODE, "Debug", Constants::DEBUG_RUN_MODE);

        return result;
    });

    // startupProjectChanged
    registerHook("projects.startupProjectChanged", [](sol::function func, QObject *guard) {
        QObject::connect(
            ProjectManager::instance(),
            &ProjectManager::startupProjectChanged,
            guard,
            [func](ProjectExplorer::Project *project) {
                Utils::expected_str<void> res = void_safe_call(func, project);
                QTC_CHECK_EXPECTED(res);
            });
    });

    // projectAdded
    registerHook("projects.projectAdded", [](sol::function func, QObject *guard) {
        QObject::connect(
            ProjectManager::instance(),
            &ProjectManager::projectAdded,
            guard,
            [func](ProjectExplorer::Project *project) {
                Utils::expected_str<void> res = void_safe_call(func, project);
                QTC_CHECK_EXPECTED(res);
            });
    });

    // projectRemoved
    registerHook("projects.projectRemoved", [](sol::function func, QObject *guard) {
        QObject::connect(
            ProjectManager::instance(),
            &ProjectManager::projectRemoved,
            guard,
            [func](ProjectExplorer::Project *project) {
                Utils::expected_str<void> res = void_safe_call(func, project);
                QTC_CHECK_EXPECTED(res);
            });
    });

    // aboutToRemoveProject
    registerHook("projects.aboutToRemoveProject", [](sol::function func, QObject *guard) {
        QObject::connect(
            ProjectManager::instance(),
            &ProjectManager::aboutToRemoveProject,
            guard,
            [func](ProjectExplorer::Project *project) {
                Utils::expected_str<void> res = void_safe_call(func, project);
                QTC_CHECK_EXPECTED(res);
            });
    });

    // runActionsUpdated
    registerHook("projects.runActionsUpdated", [](sol::function func, QObject *guard) {
        QObject::connect(
            ProjectExplorerPlugin::instance(),
            &ProjectExplorerPlugin::runActionsUpdated,
            guard,
            [func]() {
                Utils::expected_str<void> res = void_safe_call(func);
                QTC_CHECK_EXPECTED(res);
            });
    });
}

} // namespace Lua::Internal
