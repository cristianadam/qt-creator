// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luaapiregistry.h"
#include "luaengine.h"

#include <utils/aspects.h>
#include <utils/layoutbuilder.h>

using namespace Utils;

namespace Lua::Internal {

/*

``` lua

local container = AspectContainer.create({
    autoApply = false
    enableCopilot = BoolAspect.create({
        settingsKey = "Copilot.EnableCopilot",
        displayName = "Enable Copilot",
        labelText = "Enable Copilot",
        toolTip = "Enables the Copilot integration.",
        defaultValue = false,
    }),
    autoComplete = BoolAspect.create({
        settingsKey = "Copilot.AutoComplete",
        displayName = "Auto Complete",
        labelText = "Auto Complete",
        toolTip = "Automatically request suggestions for the current text cursor position after changes to the document.",
        defaultValue = true,
    }),
}) 

```
*/

class LuaAspectContainer : public AspectContainer
{
public:
    using AspectContainer::AspectContainer;

    sol::object dynamic_get(std::string key)
    {
        auto it = m_entries.find(key);
        if (it == m_entries.cend()) {
            return sol::lua_nil;
        }
        return it->second;
    }

    void dynamic_set(std::string key, sol::stack_object value)
    {
        if (!value.is<BaseAspect>()) {
            throw std::runtime_error("AspectContainer can only contain BaseAspect instances");
        }

        registerAspect(value.as<BaseAspect *>());

        auto it = m_entries.find(key);
        if (it == m_entries.cend()) {
            m_entries.insert(it, {std::move(key), std::move(value)});
        } else {
            std::pair<const std::string, sol::object> &kvp = *it;
            sol::object &entry = kvp.second;
            entry = sol::object(std::move(value));
        }
    }

    size_t size() const { return m_entries.size(); }

public:
    std::unordered_map<std::string, sol::object> m_entries;
};

std::unique_ptr<LuaAspectContainer> aspectContainerCreate(sol::table options)
{
    auto container = std::make_unique<LuaAspectContainer>();

    for (const auto &[k, v] : options) {
        if (k.is<std::string>()) {
            std::string key = k.as<std::string>();
            if (key == "autoApply") {
                container->setAutoApply(v.as<bool>());
            } else if (key == "layouter") {
                if (v.is<sol::function>())
                    container->setLayouter(
                        [func = v.as<sol::function>()]() -> Layouting::LayoutItem {
                            func.call();
                            return Layouting::Column{};
                        });
            } else {
                container->m_entries[key] = v;
                if (v.is<BaseAspect>()) {
                    qDebug() << "Yay!";
                    container->registerAspect(v.as<BaseAspect *>());
                }
            }
        }
    }

    container->readSettings();

    return container;
}

void baseAspectCreate(BaseAspect *aspect, const std::string &key, sol::object value)
{
    if (key == "settingsKey")
        aspect->setSettingsKey(keyFromString(value.as<QString>()));
    else if (key == "displayName")
        aspect->setDisplayName(value.as<QString>());
    else if (key == "labelText")
        aspect->setLabelText(value.as<QString>());
    else if (key == "toolTip")
        aspect->setToolTip(value.as<QString>());
    else if (key == "onValueChanged") {
        QObject::connect(aspect, &BaseAspect::changed, aspect, [value]() {
            value.as<sol::function>().call();
        });
    } else if (key == "onVolatileValueChanged") {
        QObject::connect(aspect, &BaseAspect::volatileValueChanged, aspect, [value]() {
            value.as<sol::function>().call();
        });
    }
}

template<class T>
void typedAspectCreate(TypedAspect<T> *aspect, const std::string &key, sol::object value)
{
    if (key == "defaultValue")
        aspect->setDefaultValue(value.as<T>());
    else if (key == "value")
        aspect->setValue(value.as<T>());
    else
        baseAspectCreate(aspect, key, value);
}

std::unique_ptr<BoolAspect> boolAspectCreate(sol::table options)
{
    auto aspect = std::make_unique<BoolAspect>();

    for (const auto &[k, v] : options) {
        if (k.is<std::string>()) {
            std::string key = k.as<std::string>();
            typedAspectCreate(aspect.get(), key, v);
        }
    }

    return aspect;
}

void registerAspectBindings()
{
    sol::state &lua = LuaEngine::instance().lua();

    lua.new_usertype<BaseAspect>("Aspect");

    lua.new_usertype<TypedAspect<bool>>("TypedAspectBool",
                                        sol::base_classes,
                                        sol::bases<BaseAspect>());

    lua.new_usertype<BoolAspect>("BoolAspect",
                                 "create",
                                 &boolAspectCreate,
                                 "value",
                                 sol::property(&BoolAspect::value,
                                               [](BoolAspect *a, bool v) { a->setValue(v); }),
                                 "volatileValue",
                                 sol::property(&BoolAspect::volatileValue,
                                               [](BoolAspect *a, bool v) {
                                                   a->setVolatileValue(v);
                                               }),
                                 "defaultValue",
                                 sol::property(&BoolAspect::defaultValue),
                                 "apply",
                                 &BoolAspect::apply,
                                 sol::base_classes,
                                 sol::bases<TypedAspect<bool>, BaseAspect>());

    lua.new_usertype<LuaAspectContainer>("AspectContainer",
                                         "create",
                                         &aspectContainerCreate,
                                         "apply",
                                         &LuaAspectContainer::apply,
                                         sol::meta_function::index,
                                         &LuaAspectContainer::dynamic_get,
                                         sol::meta_function::new_index,
                                         &LuaAspectContainer::dynamic_set,
                                         sol::meta_function::length,
                                         &LuaAspectContainer::size,
                                         sol::base_classes,
                                         sol::bases<BaseAspect>());
}

} // namespace Lua::Internal