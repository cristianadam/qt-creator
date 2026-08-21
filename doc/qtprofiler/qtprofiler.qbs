// qbs counterpart of doc/qtprofiler/CMakeLists.txt.
//
// {TODO: decide where these products belong. doc/doc.qbs declares its
// QtcDocumentation items inline rather than referencing per-directory
// .qbs files, so the two Qt Profiler products below may be better placed
// directly in doc/doc.qbs. Whichever way it goes, keep it in sync with
// doc/qtprofiler/CMakeLists.txt per CLAUDE.md}

import qbs

Project {
    name: "qtprofiler documentation"

    QtcDocumentation {
        name: "qtprofiler doc online"
        isOnlineDoc: true
        mainDocConfFile: "qtprofiler/online/qtprofiler.qdocconf"

        files: [
            "qtprofiler/src/**/*",
        ]
    }

    QtcDocumentation {
        name: "qtprofiler doc offline"
        isOnlineDoc: false
        mainDocConfFile: "qtprofiler/qtprofiler.qdocconf"

        files: [
            "qtprofiler/src/**/*",
        ]
    }
}
