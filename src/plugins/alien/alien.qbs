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

    cpp.defines: {
        var defines = base;
        if (qlitehtml.present)
            defines.push("ALIEN_WITH_LITEHTML");
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
        "alientreeview.cpp",
        "alientreeview.h",
        "alienplugin.cpp",
        "aliensettings.cpp",
        "aliensettings.h",
        "alientr.h",
        "vscodemanifest.cpp",
        "vscodemanifest.h",
        "webviewrenderer.h",
    ]
}
