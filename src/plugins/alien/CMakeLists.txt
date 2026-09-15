add_qtc_plugin(Alien
  DEPENDS TerminalLib
  PLUGIN_DEPENDS AutoTest Core Debugger LanguageClient ProjectExplorer TextEditor
  LONG_DESCRIPTION_MD Description.md
  SOURCES
    extensionhost.cpp extensionhost.h
    extensionregistry.cpp extensionregistry.h
    hostconnection.cpp hostconnection.h
    alienclient.cpp alienclient.h
    aliencompletion.cpp aliencompletion.h
    alienconstants.h
    alienhover.cpp alienhover.h
    alienlocatorfilter.cpp alienlocatorfilter.h
    alienoutline.cpp alienoutline.h
    alienpanes.cpp alienpanes.h
    alientreeview.cpp alientreeview.h
    alienplugin.cpp alienplugin.h
    alientests.cpp alientests.h
    aliensettings.cpp aliensettings.h
    alientr.h
    autowebviewrenderer.cpp autowebviewrenderer.h
    codicons.cpp codicons.h
    editorprotocol.cpp
    hostprotocols.h
    runprotocol.cpp
    vscodemanifest.cpp vscodemanifest.h
    webviewrenderer.h
    windowprotocol.cpp
)

extend_qtc_plugin(Alien
  CONDITION WITH_TESTS
  SOURCES
    alien_test.cpp alien_test.h
)

qtc_add_resources(Alien "alien_host"
  PREFIX "/alien"
  FILES host/host.js
)

qtc_add_resources(Alien alien_test_qrc
  CONDITION WITH_TESTS
  PREFIX "/alien"
  FILES
    host/completiontestextension/extension.js
    host/completiontestextension/package.json
    host/diagnosticstestextension/extension.js
    host/diagnosticstestextension/package.json
    host/docsynctestextension/extension.js
    host/docsynctestextension/package.json
    host/hoverdeftestextension/extension.js
    host/hoverdeftestextension/package.json
    host/lsptestextension/extension.js
    host/lsptestextension/package.json
    host/mockdapadapter/adapter.js
    host/mockserver/server.js
    host/quickpicktestextension/extension.js
    host/quickpicktestextension/package.json
    host/statusbartestextension/extension.js
    host/statusbartestextension/package.json
    host/testextension/extension.js
    host/testextension/package.json
    host/treeviewtestextension/extension.js
    host/treeviewtestextension/package.json
    host/webviewtestextension/extension.js
    host/webviewtestextension/package.json
)

# The panels an extension puts up are a web page, which the widget tools cannot
# reach into; these let one be looked at and driven.
extend_qtc_plugin(Alien
  SOURCES alienmcp.cpp alienmcp.h
  DEPENDS McpServerLib
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
