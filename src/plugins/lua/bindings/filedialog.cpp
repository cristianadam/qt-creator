// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#include "../luaengine.h"

#include <utils/fileutils.h>

using namespace Utils;

namespace Lua::Internal {

void setupFileDialogModule()
{
    registerProvider("FileDialog", [](sol::state_view lua) -> sol::object {
        sol::table fileDialog = lua.create_table();
        fileDialog["selectFile"] = []() -> FilePath {
            return FileUtils::getOpenFilePath("Select File ...");
        };

        return fileDialog;
    });
}

} // namespace Lua::Internal
