add_qtc_plugin(QmlFormat
  DEPENDS Utils Qt::Widgets
  PLUGIN_DEPENDS Core TextEditor
  SOURCES
    qmlformatconfigwidget.h qmlformatconfigwidget.cpp
    qmlformatplugin.cpp
    qmlformatsettings.h qmlformatsettings.cpp
)