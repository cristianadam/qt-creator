// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "lua_global.h"

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginspec.h>

#include <utils/expected.h>
#include <utils/filepath.h>

#include <sol/sol.hpp>

// this needs to be included after sol/sol.hpp!
#include "luaqttypes.h"

#include <QJsonValue>

#include <memory>

namespace Lua {
class LuaEnginePrivate;
class LuaPluginSpec;

struct CoroutineState
{
    bool isMainThread;
};

class LUA_EXPORT LuaEngine
{
private:
    LuaEngine();

public:
    using PackageProvider = std::function<sol::object(sol::state_view)>;

    ~LuaEngine();
    static LuaEngine &instance();

    Utils::expected_str<LuaPluginSpec *> loadPlugin(const Utils::FilePath &path);

    static void registerProvider(const QString &packageName, const PackageProvider &provider);
    static void autoRegister(std::function<void(sol::state_view)> registerFunction);
    static void registerHook(QString name, std::function<void(sol::function)> hookProvider);

    static Utils::expected_str<void> connectHooks(sol::state_view lua, const sol::table &hookTable);

    static bool isCoroutine(lua_State *state);

    static sol::table toTable(sol::state_view lua, const QJsonValue &v);
    static QJsonValue toJson(const sol::table &t);

    static Utils::expected_str<int> resumeImpl(sol::this_state s, int nargs);

    template<typename... Args>
    static Utils::expected_str<int> resume(sol::this_state s, Args &&...args)
    {
        sol::stack::push(s, std::forward<Args>(args)...);
        return resumeImpl(s, sizeof...(Args));
    }

    template<typename R, typename... Args>
    static Utils::expected_str<R> safe_call(sol::protected_function function, Args &&...args)
    {
        sol::protected_function_result result = function(std::forward<Args>(args)...);
        if (!result.valid()) {
            sol::error err = result;
            return Utils::make_unexpected(QString::fromLocal8Bit(err.what()));
        }
        try {
            return result.get<R>();
        } catch (std::runtime_error &e) {
            return Utils::make_unexpected(QString::fromLocal8Bit(e.what()));
        }
    }

    template<typename... Args>
    static Utils::expected_str<void> void_safe_call(sol::protected_function function, Args &&...args)
    {
        sol::protected_function_result result = function(std::forward<Args>(args)...);
        if (!result.valid()) {
            sol::error err = result;
            return Utils::make_unexpected(QString::fromLocal8Bit(err.what()));
        }
        return {};
    }

protected:
    Utils::expected_str<void> connectHooks(
        sol::state_view lua, const sol::table &table, QString path);

private:
    std::unique_ptr<LuaEnginePrivate> d;
};

} // namespace Lua
