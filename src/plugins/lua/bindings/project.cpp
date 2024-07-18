// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "../luaengine.h"

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

namespace Lua::Internal {

void addProjectModule()
{
    LuaEngine::registerProvider("Project", [](sol::state_view lua) -> sol::object {
        sol::table result = lua.create_table();

        result.new_usertype<ProjectExplorer::Project>(
            "Project",
            sol::no_constructor,
            "displayName",
            &ProjectExplorer::Project::displayName,
            "path",
            &ProjectExplorer::Project::projectFilePath);

        result["forFile"] = [](const Utils::FilePath &path) {
            return ProjectExplorer::ProjectManager::projectForFile(path);
        };

        return result;
    });
}

} // namespace Lua::Internal