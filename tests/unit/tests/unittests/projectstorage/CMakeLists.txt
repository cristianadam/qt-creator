# qmldesigner/designercore/projectstorage
extend_qtc_test(unittest
  SOURCES
    directorypathcompressor-test.cpp
    filestatuscache-test.cpp
    modulescanner-test.cpp
    projectstorage-test.cpp
    projectstoragepathwatcher-test.cpp
    projectstorageupdater-test.cpp
    typeannotationreader-test.cpp
)

extend_qtc_test(unittest
  CONDITION IS_SUPPORTED_PROJECTSTORAGE_QT
  DEPENDS Qt6::QmlDomPrivate Qt6::QmlCompilerPrivate
  SOURCES
    qmldocumentparser-test.cpp
    qmltypesparser-test.cpp
)

extend_qtc_test(unittest
  CONDITION IS_SUPPORTED_PROJECTSTORAGE_QT
  SOURCES_PREFIX "${QML_DESIGNER_DIRECTORY}/libs/designercore"
  DEPENDS Qt6::QmlDomPrivate Qt6::QmlCompilerPrivate
  DEFINES QDS_BUILD_QMLPARSER
  SOURCES
    projectstorage/qmldocumentparser.cpp projectstorage/qmldocumentparser.h
    projectstorage/qmltypesparser.cpp projectstorage/qmltypesparser.h
)

unittest_copy_data_folder()
