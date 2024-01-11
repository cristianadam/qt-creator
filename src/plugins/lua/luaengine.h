// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "lua_global.h"

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginspec.h>

#include <utils/expected.h>
#include <utils/filepath.h>

#include "sol/forward.hpp"

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
    ~LuaEngine();
    static LuaEngine &instance();

    Utils::expected_str<LuaPluginSpec *> loadPlugin(const Utils::FilePath &path);

    sol::state &lua();

    sol::table &qtc();

    static bool isCoroutine(lua_State *state);

    static sol::table toTable(const QJsonValue &v);
    static QJsonValue toJson(const sol::table &t);

private:
    std::unique_ptr<LuaEnginePrivate> d;
};

} // namespace Lua
