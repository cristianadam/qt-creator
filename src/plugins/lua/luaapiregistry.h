// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "lua_global.h"

#include "luaengine.h"

#include "sol/sol.hpp"

bool LUA_EXPORT sol_lua_check(sol::types<QString>,
                              lua_State *L,
                              int index,
                              std::function<sol::check_handler_type> handler,
                              sol::stack::record &tracking);
QString LUA_EXPORT sol_lua_get(sol::types<QString>,
                               lua_State *L,
                               int index,
                               sol::stack::record &tracking);
int LUA_EXPORT sol_lua_push(sol::types<QString>, lua_State *L, const QString &qStr);

namespace Lua {

class LUA_EXPORT LuaApiRegistry
{
public:
    template<typename FuncType>
    static void createFunction(FuncType &&f, const QString &name)
    {
        LuaEngine::instance().qtc()[name.toLocal8Bit().constData()] = std::forward<FuncType>(f);
    }

    template<typename T>
    static sol::usertype<T> createClass(const QString &name)
    {
        return LuaEngine::instance().lua().new_usertype<T>(name.toLocal8Bit().constData());
    }

    static void registerUtils();
    static void registerFetch();
    static void registerWait();
    static void registerProcess();

    static Utils::expected_str<int> resumeImpl(sol::this_state s, int nargs);

    template<typename... Args>
    static Utils::expected_str<int> resume(sol::this_state s, Args &&...args)
    {
        sol::stack::push(s, std::forward<Args>(args)...);
        return resumeImpl(s, sizeof...(Args));
    }
};

} // namespace Lua