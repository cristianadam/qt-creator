import qbs

QtcAutotest {
    name: "cxx-frontend autotest"

    // Mirrors the QTC_ENABLE_CXX_FRONTEND CMake option being off by default.
    builtByDefault: false

    Depends { name: "cxx-frontend" }
    files: "tst_cxxfrontend.cpp"

    cpp.defines: base.concat(['SRCDIR="' + path + '"'])
}
