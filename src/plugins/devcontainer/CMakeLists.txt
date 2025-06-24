add_qtc_plugin(DevContainerPlugin
  PLUGIN_DEPENDS Core ProjectExplorer
  DEPENDS DevContainer
  SOURCES
    devcontainerplugin.cpp

    devcontainerplugin_global.h
    devcontainerplugintr.h
    
    devcontainerdevice.cpp
    devcontainerdevice.h
)
