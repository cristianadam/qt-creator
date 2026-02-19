import qbs

QtcPlugin {
    name: "mcpserver"

    Depends { name: "app_version_header" }
    Depends { name: "Core" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "TextEditor" }
    Depends { name: "Utils" }
    Depends { name: "Qt"; submodules: ["network", "widgets"] }

    files: [
        "issuesmanager.cpp",
        "issuesmanager.h",
        "mcpcommands.cpp",
        "mcpcommands.h",
        "mcpserverplugin.cpp",
        "mcpserverconstants.h",
        "mcpservertr.h",
    ]

    Group {
        name: "images"
        prefix: "images/"
        files: [
            "mcpicon.png",
            "mcpicon@2x.png",
        ]
        fileTags: "qt.core.resource_data"
    }

    Group {
        name: "schemas"
        prefix: "schemas/"
        files: [
            "issues-schema.json",
            "search-results-schema.json",
        ]
        fileTags: "qt.core.resource_data"
    }
}
