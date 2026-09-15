import qbs 1.0

QtcPlugin {
    name: "Alien"

    Depends { name: "AutoTest" }
    Depends { name: "Core" }
    Depends { name: "Debugger" }
    Depends { name: "LanguageClient" }
    Depends { name: "McpServerLib" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "TerminalLib" }
    Depends { name: "TextEditor" }
    Depends { name: "Utils" }
    Depends { name: "Qt"; submodules: ["widgets"] }
    Depends { name: "qlitehtml"; required: false }
    Depends { name: "Qt.webenginewidgets"; required: false }
    Depends { name: "Qt.webchannel"; required: false }

    property bool withWebEngine: Qt.webenginewidgets.present && Qt.webchannel.present

    cpp.defines: {
        var defines = base;
        if (qlitehtml.present)
            defines.push("ALIEN_WITH_LITEHTML");
        if (withWebEngine)
            defines.push("ALIEN_WITH_WEBENGINE");
        return defines;
    }

    Group {
        name: "long description"
        files: "Description.md"
        fileTags: "pluginjson.longDescription"
    }

    Group {
        name: "litehtml webview backend"
        condition: qlitehtml.present
        files: [
            "litehtmlwebviewrenderer.cpp",
            "litehtmlwebviewrenderer.h",
        ]
    }

    Group {
        name: "QtWebEngine webview backend"
        condition: withWebEngine
        files: [
            "webenginewebviewrenderer.cpp",
            "webenginewebviewrenderer.h",
        ]
    }

    QtcTestFiles {
        files: [
            "alien_test.cpp",
            "alien_test.h",
        ]
    }

    Group {
        name: "host"
        files: "host/host.js"
        fileTags: "qt.core.resource_data"
        Qt.core.resourcePrefix: "/alien"
        Qt.core.resourceSourceBase: sourceDirectory
    }

    Group {
        name: "test extensions"
        condition: qtc.withPluginTests
        prefix: "host/"
        files: [
            "completiontestextension/*",
            "diagnosticstestextension/*",
            "docsynctestextension/*",
            "hoverdeftestextension/*",
            "lsptestextension/*",
            "mockdapadapter/*",
            "mockserver/*",
            "quickpicktestextension/*",
            "statusbartestextension/*",
            "testextension/*",
            "treeviewtestextension/*",
            "webviewtestextension/*",
        ]
        fileTags: "qt.core.resource_data"
        Qt.core.resourcePrefix: "/alien"
        Qt.core.resourceSourceBase: sourceDirectory
    }

    files: [
        "extensionhost.cpp",
        "extensionhost.h",
        "extensionregistry.cpp",
        "extensionregistry.h",
        "hostconnection.cpp",
        "hostconnection.h",
        "alienclient.cpp",
        "alienclient.h",
        "aliencompletion.cpp",
        "aliencompletion.h",
        "alienconstants.h",
        "alienhover.cpp",
        "alienhover.h",
        "alienlocatorfilter.cpp",
        "alienlocatorfilter.h",
        "alienmcp.cpp",
        "alienmcp.h",
        "alienoutline.cpp",
        "alienoutline.h",
        "alienpanes.cpp",
        "alienpanes.h",
        "alientreeview.cpp",
        "alientreeview.h",
        "alienplugin.cpp",
        "alienplugin.h",
        "alientests.cpp",
        "alientests.h",
        "aliensettings.cpp",
        "aliensettings.h",
        "alientr.h",
        "autowebviewrenderer.cpp",
        "autowebviewrenderer.h",
        "codicons.cpp",
        "codicons.h",
        "editorprotocol.cpp",
        "hostprotocols.h",
        "runprotocol.cpp",
        "vscodemanifest.cpp",
        "vscodemanifest.h",
        "webviewrenderer.h",
        "windowprotocol.cpp",
    ]
}
