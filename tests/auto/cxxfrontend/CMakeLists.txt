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

add_qtc_test(tst_cxxfrontendoverview
  CONDITION QTC_ENABLE_CXX_FRONTEND
  DEPENDS CxxFrontendBridge CPlusPlus
  DEFINES SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES tst_cxxfrontendoverview.cpp
)

add_qtc_test(tst_cxxfrontenddocument
  CONDITION QTC_ENABLE_CXX_FRONTEND
  DEPENDS CxxFrontendBridge CPlusPlus
  DEFINES SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES tst_cxxfrontenddocument.cpp
)

add_qtc_test(tst_cxxfrontendsnapshot
  CONDITION QTC_ENABLE_CXX_FRONTEND
  DEPENDS CxxFrontendBridge CPlusPlus
  DEFINES SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES tst_cxxfrontendsnapshot.cpp
)

# Reads the syntax tree itself, so it depends on cxx-frontend directly: that
# is where the include paths and the C++23 those headers need come from. Every
# other test here asks a document questions instead and does not.
add_qtc_test(tst_cxxfrontendast
  CONDITION QTC_ENABLE_CXX_FRONTEND
  DEPENDS CxxFrontendBridge CPlusPlus cxx-frontend
  DEFINES SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
  SOURCES tst_cxxfrontendast.cpp
)
