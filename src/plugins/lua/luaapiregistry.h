// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "lua_global.h"

#include "luaengine.h"
#include "luaqttypes.h"

#include "sol/sol.hpp"

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
