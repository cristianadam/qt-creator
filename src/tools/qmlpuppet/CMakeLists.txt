cmake_minimum_required(VERSION 3.20)

# standalone build
if (NOT QT_CREATOR_API_DEFINED)
    option(BUILD_DESIGNSTUDIO "Build QML Puppet with Qt Design Studio version in the binary name." OFF)
    if (BUILD_DESIGNSTUDIO)
        list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../dist/branding/qtdesignstudio")
    endif()
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake")

  project(qmlpuppet)

  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)
  set(CMAKE_AUTOUIC ON)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)

  set(DESTINATION DESTINATION .)
  include(QtCreatorIDEBranding)
  include(QtCreatorAPI)
  include(QtCreatorSbom)
  qtc_handle_compiler_cache_support()

  find_package(Qt6
    COMPONENTS Concurrent Core Gui Network PrintSupport Qml Quick Sql Widgets Xml
    REQUIRED
  )
  set(IS_STAND_ALONE_PUPPET_BUILD ON)

  qtc_setup_sbom()
  qtc_sbom_compute_cpe(project_cpe
      VENDOR "qt"
      PRODUCT "${PROJECT_NAME}"
      VERSION "${IDE_VERSION}"
  )
  set(QT_SBOM_LICENSE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../../LICENSES")
  qtc_sbom_begin_project(
    PURL_NAMESPACE "qt"
    PURL_NAME "${PROJECT_NAME}"
    CPE "${project_cpe}"
  )
endif()

add_qtc_executable(qmlpuppet
  ${DESTINATION}
  CONDITION TARGET Qt::Quick
  DEPENDS
    Qt::CorePrivate Qt::Widgets Qt::QmlPrivate
    Qt::QuickPrivate Qt::Network Qt::GuiPrivate
    QmlPuppetCommunication
  INCLUDES
    ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
  SOURCES
    qmlpuppet/qmlpuppetmain.cpp
    qmlpuppet/qmlbase.h
    qmlpuppet/qmlpuppet.h qmlpuppet/qmlpuppet.cpp
    qmlpuppet/configcrashpad.h
    qmlpuppet.qrc
  PROPERTIES
    OUTPUT_NAME qmlpuppet-${IDE_VERSION}
)

if (IS_STAND_ALONE_PUPPET_BUILD)
    include(../../libs/qmlpuppetcommunication/QmlPuppetCommunication.cmake)
    configure_file(../../app/app_version.h.cmakein app/app_version.h ESCAPE_QUOTES)
else()
    extend_qtc_executable(qmlpuppet
      DEPENDS app_version
    )
endif()

extend_qtc_executable(qmlpuppet
  CONDITION Qt6_VERSION
  SOURCES
    editor3d_qt6.qrc
)

extend_qtc_executable(qmlpuppet
  CONDITION UNIX AND (NOT APPLE)
  DEPENDS rt
)

extend_qtc_executable(qmlpuppet
  SOURCES_PREFIX instances
  SOURCES
    nodeinstanceclientproxy.cpp nodeinstanceclientproxy.h
)

find_package(Qt6 COMPONENTS Quick3D QUIET)
extend_qtc_executable(qmlpuppet
  CONDITION TARGET Qt::Quick3D
  FEATURE_INFO "Qt Quick 3D support"
  DEPENDS Qt::Quick3D Qt::Quick3DPrivate
  DEFINES QUICK3D_MODULE

  SOURCES_PREFIX qmlpuppet/editor3d
  SOURCES
    generalhelper.cpp generalhelper.h
    mousearea3d.cpp mousearea3d.h
    geometrybase.cpp geometrybase.h
    camerageometry.cpp camerageometry.h
    lightgeometry.cpp lightgeometry.h
    gridgeometry.cpp gridgeometry.h
    selectionboxgeometry.cpp selectionboxgeometry.h
    linegeometry.cpp linegeometry.h
    icongizmoimageprovider.cpp icongizmoimageprovider.h
    boxgeometry.cpp boxgeometry.h
    lookatgeometry.cpp lookatgeometry.h
)

find_package(Qt6 COMPONENTS Quick3DAssetImport QUIET)
if (Qt6_VERSION VERSION_GREATER_EQUAL 6.9.0)
  find_package(Qt6 COMPONENTS Quick3DAssetImportPrivate QUIET)
endif()
extend_qtc_executable(qmlpuppet
  CONDITION TARGET Qt::Quick3DAssetImportPrivate
  FEATURE_INFO "Qt Quick 3D asset import"
  DEPENDS Qt::Quick3DAssetImportPrivate
  DEFINES IMPORT_QUICK3D_ASSETS
)

find_package(Qt6 COMPONENTS Quick3DParticles QUIET)
if (Qt6_VERSION VERSION_GREATER_EQUAL 6.9.0)
  find_package(Qt6 COMPONENTS Quick3DParticlesPrivate QUIET)
endif()
extend_qtc_executable(qmlpuppet
  CONDITION TARGET Qt::Quick3DParticles AND TARGET Qt::Quick3DParticlesPrivate
  FEATURE_INFO "Qt Quick 3D particles"
  DEPENDS Qt::Quick3DParticles Qt::Quick3DParticlesPrivate
  DEFINES QUICK3D_PARTICLES_MODULE
)

# Quick3DAssetUtils optionally depends on QuickTimeline, so find also it to make the CI build work
find_package(Qt6 COMPONENTS Quick3DAssetUtils QuickTimeline QUIET)
if (Qt6_VERSION VERSION_GREATER_EQUAL 6.9.0)
  find_package(Qt6 COMPONENTS Quick3DAssetUtilsPrivate QUIET)
endif()
extend_qtc_executable(qmlpuppet
  CONDITION TARGET Qt::Quick3DAssetUtilsPrivate
  FEATURE_INFO "Qt Quick 3D asset utils"
  DEPENDS Qt::Quick3DAssetUtilsPrivate
  DEFINES QUICK3D_ASSET_UTILS_MODULE
)

extend_qtc_executable(qmlpuppet
  SOURCES_PREFIX qmlpuppet/import3d
  SOURCES
    import3d.cpp import3d.h
)

extend_qtc_executable(qmlpuppet
  SOURCES_PREFIX qmlpuppet/renderer
  SOURCES
    qmlrenderer.cpp qmlrenderer.h
)

extend_qtc_executable(qmlpuppet
  SOURCES_PREFIX qmlpuppet/instances
  SOURCES
    anchorchangesnodeinstance.cpp anchorchangesnodeinstance.h
    behaviornodeinstance.cpp behaviornodeinstance.h
    childrenchangeeventfilter.cpp childrenchangeeventfilter.h
    componentnodeinstance.cpp componentnodeinstance.h
    dummycontextobject.cpp dummycontextobject.h
    dummynodeinstance.cpp dummynodeinstance.h
    layoutnodeinstance.cpp layoutnodeinstance.h
    nodeinstanceserver.cpp nodeinstanceserver.h
    nodeinstancesignalspy.cpp nodeinstancesignalspy.h
    objectnodeinstance.cpp objectnodeinstance.h
    positionernodeinstance.cpp positionernodeinstance.h
    qmlpropertychangesnodeinstance.cpp qmlpropertychangesnodeinstance.h
    qmlstatenodeinstance.cpp qmlstatenodeinstance.h
    qmltransitionnodeinstance.cpp qmltransitionnodeinstance.h
    qt3dpresentationnodeinstance.cpp qt3dpresentationnodeinstance.h
    qt5bakelightsnodeinstanceserver.cpp qt5bakelightsnodeinstanceserver.h
    qt5import3dnodeinstanceserver.cpp qt5import3dnodeinstanceserver.h
    qt5informationnodeinstanceserver.cpp qt5informationnodeinstanceserver.h
    qt5nodeinstanceclientproxy.cpp qt5nodeinstanceclientproxy.h
    qt5nodeinstanceserver.cpp qt5nodeinstanceserver.h
    qt5previewnodeinstanceserver.cpp qt5previewnodeinstanceserver.h
    qt5rendernodeinstanceserver.cpp qt5rendernodeinstanceserver.h
    qt5testnodeinstanceserver.cpp qt5testnodeinstanceserver.h
    quick3dmaterialnodeinstance.cpp quick3dmaterialnodeinstance.h
    quick3dnodeinstance.cpp quick3dnodeinstance.h
    quick3drenderablenodeinstance.cpp quick3drenderablenodeinstance.h
    quick3dtexturenodeinstance.cpp quick3dtexturenodeinstance.h
    quickitemnodeinstance.cpp quickitemnodeinstance.h
    servernodeinstance.cpp servernodeinstance.h
    qt5capturepreviewnodeinstanceserver.cpp qt5capturepreviewnodeinstanceserver.h
    nodeinstanceserverdispatcher.cpp nodeinstanceserverdispatcher.h
    capturenodeinstanceserverdispatcher.cpp capturenodeinstanceserverdispatcher.h
    viewconfig.cpp viewconfig.h
    animationdriver.cpp animationdriver.h
)

extend_qtc_executable(qmlpuppet
  SOURCES_PREFIX qmlprivategate
  SOURCES
    qmlprivategate.cpp qmlprivategate.h
)

if (DEFINED MULTILANGUAGE_SUPPORT_SUBDIRECTORY AND Qt6_VERSION VERSION_GREATER_EQUAL 6.2.1)
  add_subdirectory(${MULTILANGUAGE_SUPPORT_SUBDIRECTORY} multilanguagesupport_static_build)
endif()

extend_qtc_executable(qmlpuppet
    CONDITION TARGET QtCreator::multilanguage-support
    DEPENDS QtCreator::multilanguage-support
    FEATURE_INFO "multilanguage-support in qmlpuppet"
)

extend_qtc_executable(qmlpuppet
  PUBLIC_INCLUDES src/libs
)

extend_qtc_executable(qmlpuppet
  CONDITION TARGET Nanotrace
  DEPENDS Nanotrace
)

if (Qt6_VERSION VERSION_GREATER_EQUAL 6.4.0)
    extend_qtc_executable(qmlpuppet
      # QT_QML_DEBUG is disabled in release builds, but is necessary to have the preview debug channel
      DEFINES ENABLE_INTERNAL_QML_RUNTIME QT_QML_DEBUG
      PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/qmlpuppet/runner
      SOURCES_PREFIX qmlpuppet/runner
      SOURCES
        qmlruntime.h qmlruntime.cpp
        qmlconfiguration.h loadwatcher.h
    )

    # Turn the tool into its own self-contained qml module
    qt_add_qml_module(qmlpuppet
        URI QmlRuntime.QmlConfiguration
        VERSION 1.0
        RESOURCE_PREFIX "/qt-project.org/imports"
    )
    if (QTC_STATIC_BUILD)
      qt_import_qml_plugins(qmlpuppet PATH_TO_SCAN ${SRCDIR})
    endif()
endif()


# Crashpad
# only windows requires separate crashpad client per process until client->SetHandlerIPCPipe()
# is implemented (check the TODO inside startCrashpad())
extend_qtc_executable(qmlpuppet
  CONDITION BUILD_WITH_CRASHPAD AND WIN32
  DEFINES
    CRASHPAD_BACKEND_URL="${CRASHPAD_BACKEND_URL}"
    ENABLE_CRASHPAD
  DEPENDS Crashpad::Crashpad
)

# add application icon
# IDE_LOGO_PATH in a default QtCreator build is empty, all icons are found by relative paths
# So we can not use the icon then.
if (TARGET qmlpuppet AND NOT "${IDE_LOGO_PATH}" STREQUAL "")
    if (WIN32)
      set(RC_APPLICATION_NAME "${IDE_DISPLAY_NAME}")
      set(RC_VERSION "${IDE_VERSION}.0")
      set(RC_VERSION_STRING "${IDE_VERSION_DISPLAY}")
      set(RC_COPYRIGHT "${IDE_COPYRIGHT}")
      set(RC_PUBLISHER "${IDE_PUBLISHER}")

      string(REPLACE " " "\\x20" RC_APPLICATION_NAME "${RC_APPLICATION_NAME}")
      string(REPLACE " " "\\x20" RC_PUBLISHER "${RC_PUBLISHER}")
      string(REPLACE " " "\\x20" RC_COPYRIGHT "${RC_COPYRIGHT}")
      string(REPLACE "." "," RC_VERSION "${RC_VERSION}")

      target_compile_definitions(qmlpuppet PRIVATE
        RC_PUBLISHER=${RC_PUBLISHER}
        RC_APPLICATION_NAME=${RC_APPLICATION_NAME}
        RC_VERSION=${RC_VERSION}
        RC_VERSION_STRING=${RC_VERSION_STRING}
        RC_COPYRIGHT=${RC_COPYRIGHT}
        RC_ICON_PATH=${IDE_ICON_PATH}
      )

      target_sources(qmlpuppet PRIVATE windows_application_icon/qmlpuppet.rc)
    endif()
endif()

if(IS_STAND_ALONE_PUPPET_BUILD)
  qtc_sbom_end_project()
endif()
