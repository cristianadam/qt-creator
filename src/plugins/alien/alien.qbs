import qbs 1.0

QtcPlugin {
    name: "Alien"

    Depends { name: "Core" }
    Depends { name: "LanguageClient" }
    Depends { name: "TextEditor" }
    Depends { name: "Utils" }
    Depends { name: "Qt"; submodules: ["widgets"] }

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
        "alienconstants.h",
        "alienplugin.cpp",
        "aliensettings.cpp",
        "aliensettings.h",
        "alientr.h",
        "vscodemanifest.cpp",
        "vscodemanifest.h",
    ]
}
