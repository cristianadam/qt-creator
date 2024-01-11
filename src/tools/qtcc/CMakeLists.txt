find_package(Curses REQUIRED)

if (NOT Curses_FOUND)
  message(FATAL_ERROR "NCurses package could not be found")
  return()
endif()

add_executable(qtcc main.cpp)
target_include_directories(qtcc PRIVATE ${CURSES_INCLUDE_DIR})
target_link_libraries(qtcc PRIVATE ${CURSES_LIBRARIES} ExtensionSystem Core Qt6::Widgets ProjectExplorer CMakeProjectManager)
target_include_directories(qtcc PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugins
    ${CMAKE_SOURCE_DIR}/src/libs
)
target_compile_definitions(qtcc PRIVATE PLUGINS_DIR=\"${CMAKE_BINARY_DIR}/lib/qtcreator/plugins\")
set_target_properties(qtcc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
