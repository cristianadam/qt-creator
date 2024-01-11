// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luaengine.h"

// We need this include so that the sol_lua_* functions are visible
#include "luaapiregistry.h"
#include "luapluginspec.h"

#include <utils/algorithm.h>

#include <sol/sol.hpp>

#include <QJsonArray>
#include <QJsonObject>

using namespace Utils;

namespace Lua {

class LuaEnginePrivate
{
public:
    LuaEnginePrivate()
    {
        m_qtc = m_lua.create_table("qtc");
    }

    sol::state &lua() { return m_lua; }

    lua_State *state() { return m_lua.lua_state(); }

    sol::state m_lua;
    sol::table m_qtc;
};

LuaEngine &LuaEngine::instance()
{
    static LuaEngine luaEngine;
    return luaEngine;
}

LuaEngine::LuaEngine()
    : d(new LuaEnginePrivate())
{
    d->m_lua.open_libraries(sol::lib::base,
                            sol::lib::package,
                            sol::lib::coroutine,
                            sol::lib::string,
                            sol::lib::os,
                            sol::lib::math,
                            sol::lib::table,
                            sol::lib::debug,
                            sol::lib::bit32,
                            sol::lib::io);
}

LuaEngine::~LuaEngine() = default;

expected_str<LuaPluginSpec *> LuaEngine::loadPlugin(const Utils::FilePath &path)
{
    auto contents = path.fileContents();
    if (!contents)
        return make_unexpected(contents.error());

    sol::environment pluginEnvironment(d->lua(), sol::create);

    // We copy over all symbols from the main lua state to the plugin environment
    // We could whitelist certain symbols here, and it allows us to inject custom
    // functions where needed.
    for (const auto &[k, v] : d->lua()) {
        if (k.is<std::string>() && k.as<std::string>() == "require") {
            // We override the "require" function here. This is necessary as we found no way to
            // to set the "package.path" variable for each plugin in such a way that the
            // "package.searchers" would use it. So instead we create a function that sets the
            // correct path before we call the original search function.
            auto originalRequire = v.as<sol::function>();
            QString searchPath = (path.parentDir() / "?.lua").toUserOutput();

            pluginEnvironment.set(k,
                                  [searchPath, originalRequire](sol::this_environment e,
                                                                const std::string &name) {
                                      sol::table packageTable = e.env->get<sol::table>("package");
                                      packageTable.set("path", searchPath);

                                      sol::protected_function_result result = originalRequire.call(
                                          name);
                                      if (!result.valid()) {
                                          throw result.get<sol::error>();
                                      }
                                      return result;
                                  });
            continue;
        }

        pluginEnvironment.set(k, v);
    }

    auto result = d->lua().safe_script(std::string_view(contents->data(), contents->size()),
                                       pluginEnvironment,
                                       sol::script_pass_on_error,
                                       path.fileName().toUtf8().constData());

    if (!result.valid()) {
        sol::error err = result;
        return make_unexpected(QString(QString::fromUtf8(err.what())));
    }

    if (result.get_type() != sol::type::table)
        return make_unexpected(QString("Script did not return a table"));

    sol::table pluginInfo = result.get<sol::table>();
    if (!pluginInfo.valid())
        return make_unexpected(QString("Script did not return a table with plugin info"));

    return LuaPluginSpec::create(path, pluginInfo, pluginEnvironment);
}

sol::state &LuaEngine::lua()
{
    return d->lua();
}

sol::table &LuaEngine::qtc()
{
    return d->m_qtc;
}

bool LuaEngine::isCoroutine(lua_State *state)
{
    bool ismain = lua_pushthread(state) == 1;
    return !ismain;
}

template<typename KeyType>
static void setFromJson(sol::table &t, KeyType k, const QJsonValue &v)
{
    if (v.isDouble())
        t[k] = v.toDouble();
    else if (v.isBool())
        t[k] = v.toBool();
    else if (v.isString())
        t[k] = v.toString();
    else if (v.isObject())
        t[k] = LuaEngine::toTable(v);
    else if (v.isArray())
        t[k] = LuaEngine::toTable(v);
}

sol::table LuaEngine::toTable(const QJsonValue &v)
{
    sol::table table(::Lua::LuaEngine::instance().lua(), sol::create);

    if (v.isObject()) {
        QJsonObject o = v.toObject();

        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            setFromJson(table, it.key().toStdString(), it.value());
        }

    } else if (v.isArray()) {
        int i = 1;
        for (const auto &v : v.toArray()) {
            setFromJson(table, i++, v);
        }
    }

    return table;
}

QJsonValue toJsonValue(sol::object object);

QJsonValue toJsonValue(sol::table table)
{
    if (table.get<std::optional<sol::object>>(1)) {
        // Is Array
        QJsonArray arr;

        for (size_t i = 0; i < table.size(); ++i) {
            std::optional<sol::object> v = table.get<std::optional<sol::object>>(i + 1);
            if (!v)
                continue;
            arr.append(toJsonValue(*v));
        }

        return arr;
    }

    // Is Object
    QJsonObject obj;
    for (const auto &[k, v] : table)
        obj[k.as<QString>()] = toJsonValue(v);

    return obj;
}

QJsonValue toJsonValue(sol::object object)
{
    switch (object.get_type()) {
    case sol::type::lua_nil:
        return {};
    case sol::type::boolean:
        return object.as<bool>();
    case sol::type::number:
        return object.as<double>();
    case sol::type::string:
        return object.as<QString>();
    case sol::type::table:
        return toJsonValue(object.as<sol::table>());
    default:
        return {};
    }
}

QJsonValue LuaEngine::toJson(const sol::table &table)
{
    return toJsonValue(table);
}

} // namespace Lua
