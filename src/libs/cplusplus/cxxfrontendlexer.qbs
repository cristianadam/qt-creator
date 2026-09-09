import qbs

// SimpleLexer on top of the cxx-frontend scanner. A product of its own rather
// than part of CPlusPlus, so that the C++23 that cxx-frontend needs does not
// spread to everything that uses the front end. Kept in sync with the
// CxxFrontendLexer target in CMakeLists.txt.
QtcLibrary {
    name: "CxxFrontendLexer"
    type: "staticlibrary"

    // Mirrors the QTC_ENABLE_CXX_FRONTEND CMake option being off by default.
    builtByDefault: false

    Depends { name: "CPlusPlus" }
    Depends { name: "cxx-frontend" }

    files: [
        "CxxFrontendLexer.cpp",
        "CxxFrontendLexer.h",
    ]

    Export {
        Depends { name: "cpp" }
        Depends { name: "CPlusPlus" }
        cpp.includePaths: [project.ide_source_tree + "/src/libs"]
    }
}
