add_qtc_test(tst_cxxfrontend
  CONDITION QTC_ENABLE_CXX_FRONTEND
  DEPENDS cxx-frontend
  DEFINES SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES tst_cxxfrontend.cpp
)

add_qtc_test(tst_cxxfrontendlexer
  CONDITION QTC_ENABLE_CXX_FRONTEND
  DEPENDS CxxFrontendBridge CPlusPlus
  DEFINES SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES tst_cxxfrontendlexer.cpp
)

add_qtc_test(tst_cxxfrontendpp
  CONDITION QTC_ENABLE_CXX_FRONTEND
  DEPENDS CxxFrontendBridge CPlusPlus
  DEFINES SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES tst_cxxfrontendpp.cpp
)
