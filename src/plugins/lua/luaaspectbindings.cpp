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
                            return func.call<Layouting::LayoutItem>();
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
    } else if (key == "enabler")
        aspect->setEnabler(value.as<BoolAspect *>());
}

template<class T>
void typedAspectCreate(T *aspect, const std::string &key, sol::object value)
{
    if (key == "defaultValue")
        aspect->setDefaultValue(value.as<typename T::valueType>());
    else if (key == "value")
        aspect->setValue(value.as<typename T::valueType>());
    else
        baseAspectCreate(aspect, key, value);
}

template<class T>
std::unique_ptr<T> createAspectFromTable(
    sol::table options, const std::function<void(T *, const std::string &, sol::object)> &f)
{
    auto aspect = std::make_unique<T>();

    for (const auto &[k, v] : options) {
        if (k.is<std::string>()) {
            std::string key = k.as<std::string>();
            f(aspect.get(), key, v);
        }
    }

    return aspect;
}

template<class T>
void addTypedAspectBaseBindings(sol::state &lua)
{
    lua.new_usertype<TypedAspect<T>>("TypedAspect<bool>",
                                     "value",
                                     sol::property(&TypedAspect<T>::value,
                                                   [](TypedAspect<T> *a, const T &v) {
                                                       a->setValue(v);
                                                   }),
                                     "volatileValue",
                                     sol::property(&TypedAspect<T>::volatileValue,
                                                   [](TypedAspect<T> *a, const T &v) {
                                                       a->setVolatileValue(v);
                                                   }),
                                     "defaultValue",
                                     sol::property(&TypedAspect<T>::defaultValue),
                                     sol::base_classes,
                                     sol::bases<BaseAspect>());
}

template<class T>
sol::usertype<T> addTypedAspect(sol::state &lua, const QString &name)
{
    addTypedAspectBaseBindings<typename T::valueType>(lua);

    return lua.new_usertype<T>(
        name,
        "create",
        [](sol::table options) { return createAspectFromTable<T>(options, &typedAspectCreate<T>); },
        sol::base_classes,
        sol::bases<TypedAspect<typename T::valueType>, BaseAspect>());
}

void registerAspectBindings()
{
    sol::state &lua = LuaEngine::instance().lua();

    lua.new_usertype<BaseAspect>("Aspect", "apply", &BaseAspect::apply);

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
                                         sol::bases<AspectContainer, BaseAspect>());

    addTypedAspect<BoolAspect>(lua, "BoolAspect");
    addTypedAspect<ColorAspect>(lua, "ColorAspect");
    addTypedAspect<SelectionAspect>(lua, "SelectionAspect");
    addTypedAspect<MultiSelectionAspect>(lua, "MultiSelectionAspect");
    addTypedAspect<StringAspect>(lua, "StringAspect");
    addTypedAspect<FilePathAspect>(lua, "FilePathAspect");
    addTypedAspect<IntegerAspect>(lua, "IntegerAspect");
    addTypedAspect<DoubleAspect>(lua, "DoubleAspect");
    addTypedAspect<StringListAspect>(lua, "StringListAspect");
    addTypedAspect<FilePathListAspect>(lua, "FilePathListAspect");
    addTypedAspect<IntegersAspect>(lua, "IntegersAspect");
    addTypedAspect<StringSelectionAspect>(lua, "StringSelectionAspect");

    lua.new_usertype<ToggleAspect>(
        "ToggleAspect",
        "create",
        [](sol::table options) {
            return createAspectFromTable<ToggleAspect>(
                options, [](ToggleAspect *aspect, const std::string &key, sol::object value) {
                    if (key == "offIcon")
                        aspect->setOffIcon(QIcon(value.as<QString>()));
                    else if (key == "offTooltip")
                        aspect->setOffTooltip(value.as<QString>());
                    else if (key == "onIcon")
                        aspect->setOnIcon(QIcon(value.as<QString>()));
                    else if (key == "onTooltip")
                        aspect->setOnTooltip(value.as<QString>());
                    else if (key == "onText")
                        aspect->setOnText(value.as<QString>());
                    else if (key == "offText")
                        aspect->setOffText(value.as<QString>());
                    else
                        typedAspectCreate(aspect, key, value);
                });
        },
        "action",
        &ToggleAspect::action,
        sol::base_classes,
        sol::bases<BoolAspect, TypedAspect<bool>, BaseAspect>());

    static auto triStateFromString = [](const QString &str) -> TriState {
        const QString l = str.toLower();
        if (l == "enabled")
            return TriState::Enabled;
        else if (l == "disabled")
            return TriState::Disabled;
        else if (l == "default")
            return TriState::Default;
        else
            return TriState::Default;
    };

    static auto triStateToString = [](TriState state) -> QString {
        if (state == TriState::Enabled)
            return "enabled";
        else if (state == TriState::Disabled)
            return "disabled";
        return "default";
    };

    lua.new_usertype<TriStateAspect>(
        "TriStateAspect",
        "create",
        [](sol::table options) {
            return createAspectFromTable<TriStateAspect>(
                options, [](TriStateAspect *aspect, const std::string &key, sol::object value) {
                    if (key == "defaultValue")
                        aspect->setDefaultValue(triStateFromString(value.as<QString>()));
                    else if (key == "value")
                        aspect->setValue(triStateFromString(value.as<QString>()));
                    else
                        baseAspectCreate(aspect, key, value);
                });
        },
        "value",
        sol::property([](TriStateAspect *a) { return triStateToString(a->value()); },
                      [](TriStateAspect *a, const QString &v) {
                          a->setValue(triStateFromString(v));
                      }),
        "volatileValue",
        sol::property([](TriStateAspect *
                             a) { return triStateToString(TriState::fromInt(a->volatileValue())); },
                      [](TriStateAspect *a, const QString &v) {
                          a->setVolatileValue(triStateFromString(v).toInt());
                      }),
        "defaultValue",
        sol::property([](TriStateAspect *a) { return triStateToString(a->defaultValue()); }),
        sol::base_classes,
        sol::bases<SelectionAspect, TypedAspect<int>, BaseAspect>());

    lua.new_usertype<TextDisplay>(
        "TextDisplay",
        "create",
        [](sol::table options) {
            return createAspectFromTable<TextDisplay>(
                options, [](TextDisplay *aspect, const std::string &key, sol::object value) {
                    if (key == "text") {
                        aspect->setText(value.as<QString>());
                    } else if (key == "iconType") {
                        const QString type = value.as<QString>().toLower();

                        if (type.isEmpty() || type == "None")
                            aspect->setIconType(Utils::InfoLabel::InfoType::None);
                        else if (type == "information")
                            aspect->setIconType(Utils::InfoLabel::InfoType::Information);
                        else if (type == "warning")
                            aspect->setIconType(Utils::InfoLabel::InfoType::Warning);
                        else if (type == "error")
                            aspect->setIconType(Utils::InfoLabel::InfoType::Error);
                        else if (type == "ok")
                            aspect->setIconType(Utils::InfoLabel::InfoType::Ok);
                        else if (type == "notok")
                            aspect->setIconType(Utils::InfoLabel::InfoType::NotOk);
                        else
                            aspect->setIconType(Utils::InfoLabel::InfoType::None);
                    } else {
                        baseAspectCreate(aspect, key, value);
                    }
                });
        },
        sol::base_classes,
        sol::bases<BaseAspect>());

    lua.new_usertype<AspectList>(
        "AspectList",
        "create",
        [](sol::table options) {
            return createAspectFromTable<AspectList>(
                options, [](AspectList *aspect, const std::string &key, sol::object value) {
                    if (key == "createItemFunction") {
                        aspect->setCreateItemFunction([func = value.as<sol::function>()]() {
                            return func.call<std::shared_ptr<BaseAspect>>();
                        });
                    } else if (key == "onItemAdded") {
                        aspect->setItemAddedCallback(
                            [func = value.as<sol::function>()](std::shared_ptr<BaseAspect> item) {
                                func.call(item);
                            });
                    } else if (key == "onItemRemoved") {
                        aspect->setItemRemovedCallback(
                            [func = value.as<sol::function>()](std::shared_ptr<BaseAspect> item) {
                                func.call(item);
                            });
                    } else {
                        baseAspectCreate(aspect, key, value);
                    }
                });
        },
        "createAndAddItem",
        &AspectList::createAndAddItem,
        "foreach",
        [](AspectList *a, sol::function clbk) {
            a->forEachItem<BaseAspect>(
                [clbk](std::shared_ptr<BaseAspect> item) { clbk.call(item); });
        },
        "enumerate",
        [](AspectList *a, sol::function clbk) {
            a->forEachItem<BaseAspect>(
                [clbk](std::shared_ptr<BaseAspect> item, int idx) { clbk.call(item, idx); });
        },
        sol::base_classes,
        sol::bases<BaseAspect>());
}

} // namespace Lua::Internal