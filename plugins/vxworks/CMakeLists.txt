find_package(QtCreator COMPONENTS Core REQUIRED)
find_package(Qt6 COMPONENTS Network Widgets REQUIRED)

add_qtc_plugin(VxWorks
  PLUGIN_DEPENDS
    QtCreator::Core QtCreator::ProjectExplorer QtCreator::QtSupport QtCreator::Debugger
    QtCreator::CMakeProjectManager
  DEPENDS Qt::Network Qt::Widgets QtCreator::ExtensionSystem QtCreator::Utils
  SOURCES
    addkitdialog.cpp addkitdialog.h
    vxworks.qrc
    vxworksconstants.h
    vxworksdevice.cpp vxworksdevice.h
    vxworksgcctoolchain.cpp vxworksgcctoolchain.h
    vxworksplugin.cpp
    vxworksqtversion.cpp vxworksqtversion.h
    vxworksrunconfiguration.cpp vxworksrunconfiguration.h
    vxworksruncontrol.cpp vxworksruncontrol.h
    vxworkssettingspage.cpp vxworkssettingspage.h
    vxworksdevicetester.h vxworksdevicetester.cpp
    vxworksprocessimpl.h vxworksprocessimpl.cpp
    vxworksdevicetestdialog.h vxworksdevicetestdialog.cpp
    vxworksdeploysupport.cpp vxworksdeploysupport.h
    vxworkscopystep.h vxworkscopystep.cpp
    vxworksdebuggerruncontrol.cpp vxworksdebuggerruncontrol.h
    vxworkstr.h
)
