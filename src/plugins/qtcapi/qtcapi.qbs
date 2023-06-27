import qbs 1.0

QtcPlugin {
    name: "QtcApi"

    Depends { name: "Core" }
    Depends { name: "Qt"; submodules: ["widgets", "xml", "network", "httpserver"] }

    files: [
        "qtcapiplugin.cpp",
        "qtcapiplugin.h",
        "qtcapi_global.h",
        "qtcapitr.h",
        "qtcserver.cpp",
        "qtcserver.h",
    ]
}

