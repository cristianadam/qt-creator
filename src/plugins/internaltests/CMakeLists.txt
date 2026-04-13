add_qtc_plugin(InternalTests
  CONDITION WITH_TESTS
  SKIP_TRANSLATION
  PLUGIN_DEPENDS Core Debugger Docker ProjectExplorer
  SOURCES
    internaltestsplugin.cpp
    dockerdebuggertest.h
    dockerdebuggertest.cpp
)
