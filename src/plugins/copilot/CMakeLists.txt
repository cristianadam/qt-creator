add_qtc_plugin(CoPilot
  SKIP_TRANSLATION
  PLUGIN_DEPENDS Core LanguageClient
  SOURCES
    copilotplugin.cpp copilotplugin.h
    copilotclient.cpp copilotclient.h
    documentwatcher.cpp documentwatcher.h
)
