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
    alienplugin.cpp
    aliensettings.cpp aliensettings.h
    alientr.h
    vscodemanifest.cpp vscodemanifest.h
)
