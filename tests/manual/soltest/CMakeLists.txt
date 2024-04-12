add_qtc_test(tst_soltest
  MANUALTEST
  DEPENDS lua546 sol2
  SOURCES
    main.cpp
)

target_include_directories(tst_soltest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/plugins/lua/)

