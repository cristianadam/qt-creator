if(COMMAND add_qtc_test)
  add_qtc_test(tst_nestedmodel
    MANUALTEST
    DEPENDS Qt::Widgets
    SOURCES
      main.cpp
  )
else()
  cmake_minimum_required(VERSION 3.16)
  project(tst_nestedmodel)

  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  qt_standard_project_setup()

  qt_add_executable(tst_nestedmodel main.cpp)
  target_link_libraries(tst_nestedmodel PRIVATE Qt6::Widgets)
endif()
