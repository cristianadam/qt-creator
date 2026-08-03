add_qtc_plugin(Alien
  PLUGIN_DEPENDS Core LanguageClient ProjectExplorer TextEditor
  LONG_DESCRIPTION_MD Description.md
  SOURCES
    alien.qrc
    extensionhost.cpp extensionhost.h
    extensionregistry.cpp extensionregistry.h
    hostconnection.cpp hostconnection.h
    alienclient.cpp alienclient.h
    aliencompletion.cpp aliencompletion.h
    alienconstants.h
    alienhover.cpp alienhover.h
    alienlocatorfilter.cpp alienlocatorfilter.h
    alientreeview.cpp alientreeview.h
    alienplugin.cpp
    aliensettings.cpp aliensettings.h
    alientr.h
    autowebviewrenderer.cpp autowebviewrenderer.h
    codicons.cpp codicons.h
    vscodemanifest.cpp vscodemanifest.h
    webviewrenderer.h
)

# Optional static-HTML webview backend (no QtWebEngine dependency).
extend_qtc_plugin(Alien
  CONDITION TARGET qlitehtml
  FEATURE_INFO "litehtml webview backend"
  DEPENDS qlitehtml
  DEFINES ALIEN_WITH_LITEHTML
  SOURCES
    litehtmlwebviewrenderer.cpp litehtmlwebviewrenderer.h
)

# Optional scriptable webview backend. Only booted when a panel turns out to
# need JavaScript, see AutoWebviewRenderer.
option(BUILD_ALIENBACKEND_QTWEBENGINE "Build the QtWebEngine webview backend for Alien." YES)
find_package(Qt6 COMPONENTS WebEngineWidgets WebChannel QUIET)
extend_qtc_plugin(Alien
  CONDITION BUILD_ALIENBACKEND_QTWEBENGINE AND TARGET Qt::WebEngineWidgets AND TARGET Qt::WebChannel
  FEATURE_INFO "QtWebEngine webview backend"
  DEPENDS Qt::WebEngineWidgets Qt::WebChannel
  DEFINES ALIEN_WITH_WEBENGINE
  SOURCES
    webenginewebviewrenderer.cpp webenginewebviewrenderer.h
)
