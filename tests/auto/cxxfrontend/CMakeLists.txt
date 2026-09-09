add_qtc_test(tst_cxxfrontend
  CONDITION QTC_ENABLE_CXX_FRONTEND
  DEPENDS cxx-frontend
  DEFINES SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES tst_cxxfrontend.cpp
)
