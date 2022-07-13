import qbs

QtcPlugin {
    name: "Squish"

    Depends { name: "Core" }
    Depends { name: "Utils" }

    Depends { name: "Qt.widgets" }

    files: [
        "squish.qrc",
        "squishplugin_global.h",
        "squishconstants.h",
        "squishplugin.cpp",
        "squishplugin.h",
        "squishsettings.cpp",
        "squishsettings.h",
        "squishnavigationwidget.cpp",
        "squishnavigationwidget.h",
        "squishoutputpane.cpp",
        "squishoutputpane.h",
        "squishtesttreemodel.cpp",
        "squishtesttreemodel.h",
        "squishtesttreeview.cpp",
        "squishtesttreeview.h",
        "squishfilehandler.cpp",
        "squishfilehandler.h",
        "opensquishsuitesdialog.cpp",
        "opensquishsuitesdialog.h",
        "squishutils.cpp",
        "squishutils.h",
        "squishtools.cpp",
        "squishtools.h",
        "squishtr.h",
        "squishxmloutputhandler.cpp",
        "squishxmloutputhandler.h",
        "testresult.cpp",
        "testresult.h",
        "squishresultmodel.cpp",
        "squishresultmodel.h",
        "deletesymbolicnamedialog.cpp",
        "deletesymbolicnamedialog.h",
        "objectsmapdocument.cpp",
        "objectsmapdocument.h",
        "objectsmaptreeitem.cpp",
        "objectsmaptreeitem.h",
        "propertytreeitem.cpp",
        "propertytreeitem.h",
        "objectsmapeditorwidget.cpp",
        "objectsmapeditorwidget.h",
        "objectsmapeditor.cpp",
        "objectsmapeditor.h",
        "propertyitemdelegate.cpp",
        "propertyitemdelegate.h",
        "symbolnameitemdelegate.cpp",
        "symbolnameitemdelegate.h"
    ]
}
