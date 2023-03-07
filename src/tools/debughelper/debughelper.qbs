import qbs 1.0

QtcTool {
    name: "qtc_debughelper"
    consoleApplication: true

    Depends { name: "Qt"; submodules: ["core", "network"]; }

    files: [ "debughelper.cpp" ]
}
