add_qtc_plugin(Docker
  DEPENDS Utils
  PLUGIN_DEPENDS Core ProjectExplorer QtSupport
  SOURCES
    containershell.h
    docker_global.h
    dockertr.h
    dockerapi.cpp dockerapi.h
    dockerconstants.h
    dockerdevice.cpp dockerdevice.h dockerdevice_p.h
    dockerdevicedata.h
    dockerdevicefactory.h
    dockerdevicesetupwizard.h
    dockerprocessimpl_p.h
    dockerdevicewidget.cpp dockerdevicewidget.h
    dockerplugin.cpp dockerplugin.h
    dockersettings.cpp dockersettings.h
    kitdetector.cpp kitdetector.h
)
