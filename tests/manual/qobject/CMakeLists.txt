if (TARGET Qt::Widgets)
    add_qtc_test(tst_manual_qobject
      MANUALTEST
      DEPENDS Qt::Widgets
      SOURCES
        tst_manual_qobject.cpp
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(tst_manual_qobject)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()
    add_executable(tst_manual_qobject tst_manual_qobject.cpp)
    target_link_libraries(tst_manual_qobject PRIVATE Qt6::Widgets)
endif()
