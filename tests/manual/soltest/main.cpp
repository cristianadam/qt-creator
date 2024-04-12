// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

//* This is a test case for a crash that happens when dynamic_set is called from a coroutine

#include <iostream>
#include <sol/sol.hpp>

static bool didDestruct = false;

class MySubObject
{
public:
    ~MySubObject() { std::cout << "~MySubObject();" << std::endl; }
};

class MyDynamic
{
public:
    ~MyDynamic()
    {
        std::cout << "~MyDynamic();" << std::endl;
        m_entries.clear();
        std::cout << "Done" << std::endl;
        didDestruct = true;
    }

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

std::unique_ptr<MyDynamic> createMyDynamic()
{
    auto container = std::make_unique<MyDynamic>();
    return container;
}

int main()
{
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::coroutine);

    lua.new_usertype<MyDynamic>(
        "MyDynamic",
        "create",
        &createMyDynamic,
        sol::meta_function::index,
        &MyDynamic::dynamic_get,
        sol::meta_function::new_index,
        &MyDynamic::dynamic_set,
        sol::meta_function::length,
        &MyDynamic::size);

    lua.new_usertype<MySubObject>("MySubObject", "create", []() {
        return std::make_unique<MySubObject>();
    });

    while (!didDestruct) {
        auto result = lua.script(R"(
            local function test()
                local dyn = MyDynamic.create()
                dyn.test = MySubObject.create()
                print(".")
            end

            -- return not_crashing_function()
            --     test()
            -- end

            return crashing_function()
                coroutine.resume(coroutine.create(test))
            end
        )");

        sol::function t = result;
        t();
    }

    return 0;
}
