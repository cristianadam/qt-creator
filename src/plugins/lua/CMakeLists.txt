add_subdirectory(lua-5.4.6)

add_qtc_plugin(Lua
  PLUGIN_DEPENDS Core
  PUBLIC_DEPENDS lua546
  PUBLIC_DEFINES LUA_AVAILABLE
  SOURCES
    luaapiregistry.cpp
    luaapiregistry.h
    luaengine.cpp
    luaengine.h
    luaplugin.cpp
    luapluginloader.cpp
    luapluginloader.h
    luapluginspec.cpp
    luapluginspec.h
    luatr.h
    sol/config.hpp
    sol/forward.hpp
    sol/sol.hpp
)

