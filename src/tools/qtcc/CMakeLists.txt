find_package(Curses REQUIRED)

if (NOT Curses_FOUND)
  message(FATAL_ERROR "NCurses package could not be found")
  return()
endif()

add_executable(qtcc main.cpp)
target_include_directories(qtcc PRIVATE ${CURSES_INCLUDE_DIR})
target_link_libraries(qtcc PRIVATE ${CURSES_LIBRARIES})

