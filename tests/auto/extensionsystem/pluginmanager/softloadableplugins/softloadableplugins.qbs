import qbs

Project {
    name: "ExtensionSystem soft-loadable plugins autotests"
    references: [
        "plugin1/plugin1.qbs",
        "plugin2/plugin2.qbs"
    ]
}
