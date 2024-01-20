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
    luatr.h
    luauibindings.cpp
    sol/config.hpp
    sol/forward.hpp
    sol/sol.hpp
)

