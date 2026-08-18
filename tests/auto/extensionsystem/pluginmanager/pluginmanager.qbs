import qbs

Project {
    name: "PluginManager autotests"
    references: [
        "circularplugins/circularplugins.qbs",
        "correctplugins1/correctplugins1.qbs",
        "softloadableplugins/softloadableplugins.qbs",
        "test.qbs"
    ]
}
