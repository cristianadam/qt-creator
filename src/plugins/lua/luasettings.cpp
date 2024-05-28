// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luatr.h"

#include <utils/aspects.h>

#include <coreplugin/dialogs/ioptionspage.h>

using namespace Utils;

namespace Lua::Internal {

class LuaSettings : public AspectContainer
{
public:
};

LuaSettings &settings()
{
    static LuaSettings settings;
    return settings;
}

class LuaOptionsPage : public Core::IOptionsPage
{
public:
    LuaOptionsPage()
    {
        setId("AA.Lua.General");
        setDisplayName(Tr::tr("General"));
        setCategory("ZY.Lua");
        setDisplayCategory("Lua");
        setCategoryIconPath(":/lua/images/settingscategory_lua.png");
        setSettingsProvider([] { return &settings(); });
    }
};

//const LuaOptionsPage settingsPage;

} // namespace Lua::Internal
