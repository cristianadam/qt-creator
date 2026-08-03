import qbs 1.0

QtcPlugin {
    name: "Alien"

    Depends { name: "Core" }
    Depends { name: "LanguageClient" }
    Depends { name: "ProjectExplorer" }
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

    files: [
        "alien.qrc",
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
        "alientreeview.cpp",
        "alientreeview.h",
        "alienplugin.cpp",
        "aliensettings.cpp",
        "aliensettings.h",
        "alientr.h",
        "autowebviewrenderer.cpp",
        "autowebviewrenderer.h",
        "codicons.cpp",
        "codicons.h",
        "vscodemanifest.cpp",
        "vscodemanifest.h",
        "webviewrenderer.h",
    ]
}
