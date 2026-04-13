import qbs 1.0

QtcPlugin {
    name: "InternalTests"
    condition: qtc.testsEnabled

    Depends { name: "Core" }
    Depends { name: "Debugger" }
    Depends { name: "Docker" }
    Depends { name: "ProjectExplorer" }

    files: [
        "dockerdebuggertest.h",
        "dockerdebuggertest.cpp",
        "internaltestsplugin.cpp",
    ]
}
