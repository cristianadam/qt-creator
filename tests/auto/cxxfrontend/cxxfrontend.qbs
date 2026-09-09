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

        Depends { name: "CxxFrontendBridge" }
        Depends { name: "CPlusPlus" }
        files: "tst_cxxfrontendlexer.cpp"

        cpp.defines: base.concat(['SRCDIR="' + path + '"'])
    }

    QtcAutotest {
        name: "cxx-frontend preprocessor autotest"
        builtByDefault: false

        Depends { name: "CxxFrontendBridge" }
        Depends { name: "CPlusPlus" }
        files: "tst_cxxfrontendpp.cpp"

        cpp.defines: base.concat(['SRCDIR="' + path + '"'])
    }

    QtcAutotest {
        name: "cxx-frontend overview autotest"
        builtByDefault: false

        Depends { name: "CxxFrontendBridge" }
        Depends { name: "CPlusPlus" }
        files: "tst_cxxfrontendoverview.cpp"

        cpp.defines: base.concat(['SRCDIR="' + path + '"'])
    }

    QtcAutotest {
        name: "cxx-frontend document autotest"
        builtByDefault: false

        Depends { name: "CxxFrontendBridge" }
        Depends { name: "CPlusPlus" }
        files: "tst_cxxfrontenddocument.cpp"

        cpp.defines: base.concat(['SRCDIR="' + path + '"'])
    }

    QtcAutotest {
        name: "cxx-frontend snapshot autotest"
        builtByDefault: false

        Depends { name: "CxxFrontendBridge" }
        Depends { name: "CPlusPlus" }
        files: "tst_cxxfrontendsnapshot.cpp"

        cpp.defines: base.concat(['SRCDIR="' + path + '"'])
    }
}
