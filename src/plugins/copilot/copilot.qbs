import qbs 1.0

QtcPlugin {
    name: "CoPilot"

    Depends { name: "Core" }
    Depends { name: "Qt"; submodules: ["widgets", "xml", "network"] }

    files: [
        "copilotplugin.cpp",
        "copilotplugin.h",
    ]
}

