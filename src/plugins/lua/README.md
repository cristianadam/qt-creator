# Lua Plugin

## Introduction

The Lua plugin provides support for writing plugins using the Lua scripting language.

## Compile

To compile the Lua plugin you need to have Lua installed and visible to "find_package(Lua)".

* **macOS**: `brew install lua`
* **Linux**: `sudo apt install liblua5.4-dev`
* **Windows**: `winget install --id=DEVCOM.Lua -e`

> **_On Windows_** you will probably need to tell cmake where to find Lua. 
You can do this by setting the `LUA_DIR` **environment** variable to the location where Lua is installed. 
For example: `LUA_DIR=C:/Users/<username>/AppData/Local/Programs/Lua`. 
This seems to be the default location where winget installs Lua.

## Usage

The plugin scans the sub-folder `lua-plugins` in the standard plugin paths returned by
`ExtensionSystem::PluginManager::pluginPaths()`. It loads scripts from any folder that contains
a .lua script named the same as the folder. Whether or not the script is enabled is determined
by the `disabledByDefault` field in the plugin table and the settings of "About Plugins" in Qt Creator.

## Basic Lua plugin

A Lua script needs to provide the following table to be considered a plugin:

```lua
-- lua-plugins/myluaplugin/myluaplugin.lua
return {
    name = "MyLuaPlugin",
    version = "1.0.0",
    compatVersion = "1.0.0",
    vendor = "The Qt Company",
    category = "Language Client",
    setup = function() print("Hello World!") end,

    --- The following fields are optional
    description = "My first lua plugin",
    longDescription = [[
A long description.
Can contain newlines.
    ]],
    url = "https://www.qt.io",
    license = "MIT",
    revision = "rev1",
    copyright = "2024",
    experimental = true,
    disabledByDefault = false,

    dependencies = {
        { name="Core", version = "12.0.0" }
    },
} --[[@as QtcPlugin]]
```

Your base file needs to be named the same as the folder its contained in. `lua-plugins` folder. 
It must only return the plugin specification table and not execute or require any other code. 
Use `require` to load other files from within the setup function.

```lua 
-- lua-plugins/myluaplugin/myluaplugin.lua
return {
    -- ... required fields omitted ..
    setup = function() require 'init'.setup() end,
} --[[@as QtcPlugin]]

-- lua-plugins/myluaplugin/init.lua
local function setup()
    print("Hello from Lua!")
end

return {
    setup = setup,
}
```

The `require` function will search for files as such:

```
my-lua-plugin/?.lua
```

## Lua <=> C++ bindings

The Lua plugin provides the [sol2](https://github.com/ThePhD/sol2) library to bind C++ code to Lua.
sol2 is well [documented here](https://sol2.rtfd.io).

To export a function to Lua, use the `LuaApiRegistry::createFunction(f, name)` method.
This will register a function in lua as `qtc.name()`.

To export a class you can use the `LuaApiRegistry::createClass<T>(name)` method,
or call `::Lua::LuaEngine::instance().lua().new_usertype<T>(name)` directly.

## Lua Language Server

To make developing plugins easier, we provide a meta file [qtc.lua](meta/qtc.lua) that describes
what functions and classes are available in the Lua plugin. If you add bindings yourself
please add them to this file. The [.luarc.json](../../.luarc.json) file contains the configuration
for the [Lua Language Server](https://luals.github.io/) and will automatically load the `qtc.lua` file.

## Coroutines

A lot of Qt Creator functions will take some time to complete. To not block the main thread during
that time, we make heavy use of lua coroutines. Functions that need a coroutine to work are documented
as such, lets take for example `qtc.waitms(ms)`. This function will wait for `ms` milliseconds and
then return. To use it you need to call it from a running coroutine:

```lua
local function myFunction()
    qtc.waitms(1000)
    print("Hello from Lua!")
end

local function setup()
    local co = coroutine.create(myFunction)
    coroutine.resume(co)
end
```

The waitms function will immediately yield, which will suspend the execution of `myFunction` **AND**
make the `coroutine.resume(co)` return.

Once the internal timer is triggered, the C++ code will resume `myFunction` and it will continue to
print the message. `myFunction` will then return and the coroutine will be "dead", meaning it cannot
be resumed again. You can of course create a new coroutine and call `myFunction` again.

## Contributing

Contributions to the Lua plugin are welcome. Please read the contributing guide for more information.
