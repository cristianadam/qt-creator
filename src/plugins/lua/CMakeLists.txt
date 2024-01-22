add_subdirectory(lua-5.4.6)

add_qtc_plugin(Lua
  PLUGIN_DEPENDS Core
  PUBLIC_DEPENDS lua546
  PUBLIC_DEFINES LUA_AVAILABLE
  SOURCES
    luaapiregistry.cpp
    luaapiregistry.h
    luaaspectbindings.cpp
    luaengine.cpp
    luaengine.h
    lualayoutingbindings.cpp
    luaplugin.cpp
    luapluginloader.cpp
    luapluginloader.h
    luapluginspec.cpp
    luapluginspec.h
    luaqttypes.cpp
    luaqttypes.h
    luasettingsbindings.cpp
    luatr.h
    luauibindings.cpp
    # generateqtbindings.cpp # Use this if you need to generate some code.
    sol/config.hpp
    sol/forward.hpp
    sol/sol.hpp
)

set_property(SOURCE luauibindings.cpp PROPERTY SKIP_AUTOMOC ON)
set_property(SOURCE luauibindings.cpp PROPERTY SKIP_AUTOGEN ON)
