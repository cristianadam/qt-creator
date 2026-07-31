add_qtc_plugin(Alien
  PLUGIN_DEPENDS Core LanguageClient TextEditor
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
    alientreeview.cpp alientreeview.h
    alienplugin.cpp
    aliensettings.cpp aliensettings.h
    alientr.h
    vscodemanifest.cpp vscodemanifest.h
    webviewrenderer.h
)

# Optional static-HTML webview backend (no QtWebEngine dependency).
extend_qtc_plugin(Alien
  CONDITION TARGET qlitehtml
  DEPENDS qlitehtml
  DEFINES ALIEN_WITH_LITEHTML
  SOURCES
    litehtmlwebviewrenderer.cpp litehtmlwebviewrenderer.h
)
