import qbs

Project {
    // Both mirror the QTC_ENABLE_CXX_FRONTEND CMake option being off by
    // default.
    QtcAutotest {
        name: "cxx-frontend autotest"
        builtByDefault: false

        Depends { name: "cxx-frontend" }
        files: "tst_cxxfrontend.cpp"

        cpp.defines: base.concat(['SRCDIR="' + path + '"'])
    }

    QtcAutotest {
        name: "cxx-frontend lexer autotest"
        builtByDefault: false

        Depends { name: "CxxFrontendLexer" }
        Depends { name: "CPlusPlus" }
        files: "tst_cxxfrontendlexer.cpp"

        cpp.defines: base.concat(['SRCDIR="' + path + '"'])
    }
}
