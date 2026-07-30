import qbs
import qbs.FileInfo

Project {
    QtcAutotest {
        name: "backends autotest"
        Depends { name: "Debugger" }
        Depends { name: "Utils" }
        Depends { name: "Qt.network" } // For QHostAddress
        Depends { name: "qmlstack_inferior"; required: false }
        Depends { name: "qmlserver_inferior"; required: false }
        Depends { name: "qmlmix_inferior"; required: false }
        Group {
            name: "Sources from Debugger plugin"
            prefix: project.debuggerDir
            // GdbMi/DisassemblerLines aren't DEBUGGER_EXPORT, so their methods
            // aren't visible across the libDebugger.so boundary - compiled
            // directly into this test binary instead, same reason
            // dumpers.qbs/disassembler.qbs do the same.
            files: [
                "debuggerprotocol.h", "debuggerprotocol.cpp",
                "disassemblerlines.h", "disassemblerlines.cpp"
            ]
        }
        Group {
            name: "Test sources"
            files: [
                "tst_backends.cpp"
            ]
        }

        cpp.defines: {
            var defines = base.concat([
                'DUMPERDIR="' + path + '/../../../share/qtcreator/debugger"',
                // Keep in sync with CMakeLists.txt - see the reasoning there on
                // why the .qml inferiors' source dir is needed at all.
                'BACKENDS_TEST_SOURCE_DIR="' + path + '"'
            ]);
            if (Qt.quick.present) {
                defines.push('QMLSTACK_INFERIOR_EXECUTABLE="'
                             + FileInfo.joinPaths(destinationDirectory, "qmlstack_inferior") + '"');
                defines.push('QMLSERVER_INFERIOR_EXECUTABLE="'
                             + FileInfo.joinPaths(destinationDirectory, "qmlserver_inferior") + '"');
                defines.push('QMLMIX_INFERIOR_EXECUTABLE="'
                             + FileInfo.joinPaths(destinationDirectory, "qmlmix_inferior") + '"');
            }
            return defines;
        }
        cpp.includePaths: base.concat([project.debuggerDir])
    }
    references: [
        "qmlstack_inferior/qmlstack_inferior.qbs",
        "qmlserver_inferior/qmlserver_inferior.qbs",
        "qmlmix_inferior.qbs",
    ]
}
