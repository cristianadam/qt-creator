import qbs.FileInfo

QtApplication {
    name: "qmlserver_inferior"
    condition: Qt.quick.present
    Depends { name: "Qt.quick" }
    Depends { name: "Qt.qml" }

    // Needed for QQmlDebuggingEnabler's static initializer (see main.cpp) to
    // actually run - without it, -qmljsdebugger is silently ignored.
    cpp.defines: ["QT_QML_DEBUG"]

    install: false
    destinationDirectory: project.buildDirectory + '/'
                          + FileInfo.relativePath(project.ide_source_tree, sourceDirectory)

    files: [
        "main.cpp",
        "main.qrc",
    ]
}
