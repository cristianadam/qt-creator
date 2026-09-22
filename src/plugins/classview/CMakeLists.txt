add_qtc_plugin(ClassView
  PLUGIN_DEPENDS Core CppEditor ProjectExplorer TextEditor
  SOURCES
    classviewconstants.h
    classviewmanager.cpp classviewmanager.h
    classviewnavigationwidget.cpp classviewnavigationwidget.h
    classviewnavigationwidgetfactory.cpp classviewnavigationwidgetfactory.h
    classviewparser.cpp classviewparser.h
    classviewparsertreeitem.cpp classviewparsertreeitem.h
    classviewplugin.cpp
    classviewsymbolinformation.cpp classviewsymbolinformation.h
    classviewsymbollocation.cpp classviewsymbollocation.h
    classviewtr.h
    classviewtreeitemmodel.cpp classviewtreeitemmodel.h
    classviewutils.cpp classviewutils.h
)

extend_qtc_plugin(ClassView
  CONDITION WITH_TESTS
  SOURCES
    classviewparser_test.cpp
    classviewparsertreeitem_test.cpp
)

# Only so that the test can say which front end answered its questions: the
# pane itself asks in places and is none the wiser.
extend_qtc_plugin(ClassView
  CONDITION QTC_ENABLE_CXX_FRONTEND AND WITH_TESTS
  DEFINES QTC_WITH_CXX_FRONTEND
)
