
find_package(Qt6 COMPONENTS HttpServer)

add_qtc_plugin(QtcApi
  CONDITION TARGET Qt::HttpServer
  PLUGIN_DEPENDS Core
  DEPENDS Qt::HttpServer
  SOURCES
    qtcapiplugin.cpp qtcapiplugin.h
    qtcapitr.h
    qtcapi_global.h
    qtcserver.cpp qtcserver.h
)
