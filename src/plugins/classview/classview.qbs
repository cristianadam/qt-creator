import qbs 1.0

QtcPlugin {
    name: "ClassView"

    Depends { name: "Qt.widgets" }
    Depends { name: "CPlusPlus" }
    Depends { name: "Utils" }

    Depends { name: "Core" }
    Depends { name: "CppEditor" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "TextEditor" }

    files: [
        "classviewconstants.h",
        "classviewmanager.cpp",
        "classviewmanager.h",
        "classviewnavigationwidget.cpp",
        "classviewnavigationwidget.h",
        "classviewnavigationwidgetfactory.cpp",
        "classviewnavigationwidgetfactory.h",
        "classviewparser.cpp",
        "classviewparser.h",
        "classviewparsertreeitem.cpp",
        "classviewparsertreeitem.h",
        "classviewplugin.cpp",
        "classviewsymbolinformation.cpp",
        "classviewsymbolinformation.h",
        "classviewsymbollocation.cpp",
        "classviewsymbollocation.h",
        "classviewtr.h",
        "classviewtreeitemmodel.cpp",
        "classviewtreeitemmodel.h",
        "classviewutils.cpp",
        "classviewutils.h",
    ]

    QtcTestFiles {
        files: [
            "classviewparser_test.cpp",
            "classviewparsertreeitem_test.cpp",
        ]

        // Only so that the test can say which front end answered its
        // questions: the pane itself asks in places and is none the wiser.
        Properties {
            condition: qtc.enableCxxFrontend
            cpp.defines: outer.concat("QTC_WITH_CXX_FRONTEND")
        }
    }
}

