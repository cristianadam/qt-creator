
add_qtc_executable(qtc_debughelper
  DEPENDS Qt::Core Qt::Network
  SOURCES
    debughelper.cpp
)

if (WIN32)
  extend_qtc_executable(qtc_debughelper
    DEFINES _UNICODE UNICODE _CRT_SECURE_NO_WARNINGS
  )
endif()
