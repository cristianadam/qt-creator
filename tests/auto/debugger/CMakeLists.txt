cmake_minimum_required(VERSION 3.16)
if (NOT QT_CREATOR_API_DEFINED)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake")

  project(debugger)

  enable_testing()

  # Needed for pch
  set(QtCreator_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../")
  option(BUILD_WITH_PCH "Build with precompiled headers" ON)

  set(PROJECT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../")

  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)
  set(CMAKE_AUTOUIC ON)

  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)

  set(IMPLICIT_DEPENDS Qt::Test)

  include(QtCreatorIDEBranding)
  include(QtCreatorAPI)
  qtc_handle_compiler_cache_support()

  set(WITH_TESTS ON)

  find_package(Qt6
    COMPONENTS
      Gui Core Widgets Network Qml Concurrent Test Xml
    OPTIONAL_COMPONENTS
      Quick
    MODULE)
  find_package(Threads)

  add_subdirectory(${QtCreator_SOURCE_DIR}/src/libs/utils ${CMAKE_CURRENT_BINARY_DIR}/utils)
endif()

set(DEBUGGERDIR "${PROJECT_SOURCE_DIR}/src/plugins/debugger")

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES "") # Ensure no external link dependencies interfere with the probe
check_cxx_source_compiles("
    typedef __int128_t int128_t;
    int main() {
        volatile int128_t v = 12345678901234567890ULL;
        unsigned int base = 10;
        unsigned int rem = (unsigned int)(v % base);
        v = v / base;
        (void)rem;
        (void)v;
        return 0;
    }
" HAVE_INT128)

if(HAVE_INT128)
  add_definitions(-DHAVE_INT128)
endif()

add_qtc_test(tst_debugger_disassembler
  INCLUDES "${DEBUGGERDIR}"
  SOURCES
    tst_disassembler.cpp
    "${DEBUGGERDIR}/disassemblerlines.cpp" "${DEBUGGERDIR}/disassemblerlines.h"
)

option(WITH_DEBUGGER_DUMPERS "Include tests for debugger pretty printers" ON)
if (WITH_DEBUGGER_DUMPERS)
  get_target_property(qmake_binary Qt::qmake IMPORTED_LOCATION)

  add_qtc_test(tst_debugger_dumpers
    EXCLUDE_FROM_PRECHECK
    TIMEOUT 0
    DEPENDS Qt::Network Utils Debugger
    DEFINES
      DUMPERDIR="${PROJECT_SOURCE_DIR}/share/qtcreator/debugger"
      DEFAULT_QMAKE_BINARY="${qmake_binary}"
    INCLUDES
      "${DEBUGGERDIR}"
    SOURCES
      "${DEBUGGERDIR}/debuggertr.h"
      "${DEBUGGERDIR}/debuggerprotocol.cpp" "${DEBUGGERDIR}/debuggerprotocol.h"
      "${DEBUGGERDIR}/simplifytype.cpp" "${DEBUGGERDIR}/simplifytype.h"
      "${DEBUGGERDIR}/watchutils.cpp" "${DEBUGGERDIR}/watchutils.h"
      tst_dumpers.cpp
  )
endif()

# Off by default: needs a real gdb/lldb plus a Qt with QML debugging and the
# qmldbg_native plugin, so it is not run automatically on CI. Enable explicitly.
option(WITH_DEBUGGER_NATIVE_MIXED "Include test for native combined C++/QML debugging" OFF)
if (WITH_DEBUGGER_NATIVE_MIXED)
  get_target_property(qmake_binary Qt::qmake IMPORTED_LOCATION)

  add_qtc_test(tst_debugger_nativemixed
    EXCLUDE_FROM_PRECHECK
    TIMEOUT 0
    DEPENDS Qt::Test
    DEFINES
      DUMPERDIR="${PROJECT_SOURCE_DIR}/share/qtcreator/debugger"
      DEFAULT_QMAKE_BINARY="${qmake_binary}"
      NATIVEMIXED_DRIVER="${CMAKE_CURRENT_SOURCE_DIR}/nativemixed_driver.py"
      QMLMIX_SOURCE_DIR="${PROJECT_SOURCE_DIR}/tests/manual/debugger/qmlmix"
    SOURCES
      tst_nativemixed.cpp
  )
endif()

add_qtc_test(tst_debugger_gdb
  DEPENDS Qt::Network Utils
  INCLUDES "${DEBUGGERDIR}"
  SOURCES
    tst_gdb.cpp
    "${DEBUGGERDIR}/debuggertr.h"
    "${DEBUGGERDIR}/debuggerprotocol.cpp" "${DEBUGGERDIR}/debuggerprotocol.h"
)

# Real gdb + a real compiled test binary, driving DebuggerEngineInterface
# backend implementations (GdbImpl, and future ones) directly - see
# project_debugger_redesign_proposal.md's testing strategy. No build-time
# opt-in gate: initTestCase() below already QSKIPs the whole suite at
# runtime if gdb, a C++ compiler, or the dumper scripts aren't found
# (same self-contained "degrade gracefully" shape as tst_debugger_dumpers,
# which has never needed one either).
set(BACKENDS_TEST_DEFINES DUMPERDIR="${PROJECT_SOURCE_DIR}/share/qtcreator/debugger")
# The .qml inferiors are checked in rather than generated, so their breakpoint
# lines are found by scanning for marker comments (same as the generated C++/
# Python ones) instead of being hardcoded - that needs the source dir, since
# only the compiled executable lives in the build dir. Mirrors
# tst_debugger_nativemixed's own QMLMIX_SOURCE_DIR.
list(APPEND BACKENDS_TEST_DEFINES
  "BACKENDS_TEST_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

if (TARGET Qt::Quick)
  qt_add_executable(qmlstack_inferior
    qmlstack_inferior/qmlentrypoint.h qmlstack_inferior/qmlentrypoint.cpp
    qmlstack_inferior/main.cpp qmlstack_inferior/main.qrc
  )
  target_link_libraries(qmlstack_inferior PRIVATE Qt::Quick Qt::Qml)
  # Needed for QQmlDebuggingEnabler's static initializer (see main.cpp) to
  # actually run - without it, -qmljsdebugger is silently ignored.
  target_compile_definitions(qmlstack_inferior PRIVATE QT_QML_DEBUG)
  list(APPEND BACKENDS_TEST_DEFINES
    "QMLSTACK_INFERIOR_EXECUTABLE=\"$<TARGET_FILE_DIR:qmlstack_inferior>\"")

  # A local copy of tests/manual/debugger/qmlmix's sources - a Window-
  # rooted app with non-trivial QML initialization before its C++ call,
  # unlike qmlstack_inferior's bare QtObject with a zero-delay startup
  # call. Needed specifically for
  # stepsWithinQmlFrameAfterNativeMixedStepOut(): qmlstack_inferior could
  # never reliably reach a second stop after chaining a step-out with a
  # further step, while this inferior does so reliably both through real
  # GdbEngine and through GdbImpl - see project_debugger_redesign_proposal.md's
  # 4d entry.
  qt_add_executable(qmlmix_inferior qmlmix_inferior/main.cpp)
  # Without this, the resource path keeps the qmlmix_inferior/ subdirectory
  # prefix, so the auto-generated QML type ends up named something other
  # than "Main" - main.cpp's loadFromModule("MixTest", "Main") needs it
  # to be exactly "Main.qml" at the module's resource root.
  set_source_files_properties(qmlmix_inferior/Main.qml PROPERTIES
    QT_RESOURCE_ALIAS "Main.qml")
  qt_add_qml_module(qmlmix_inferior
    URI MixTest
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES qmlmix_inferior/Main.qml
    SOURCES qmlmix_inferior/qmlentrypoint.h qmlmix_inferior/qmlentrypoint.cpp
  )
  target_compile_definitions(qmlmix_inferior PRIVATE QT_QML_DEBUG)
  target_link_libraries(qmlmix_inferior PRIVATE Qt::Quick)
  # The generated qml type registration file does
  # "#if __has_include(<qmlentrypoint.h>)".
  target_include_directories(qmlmix_inferior PRIVATE qmlmix_inferior)
  list(APPEND BACKENDS_TEST_DEFINES
    "QMLMIX_INFERIOR_EXECUTABLE=\"$<TARGET_FILE_DIR:qmlmix_inferior>\"")
endif()

add_qtc_test(tst_debugger_backends
  TIMEOUT 0
  DEPENDS Qt::Network Utils Debugger
  DEFINES ${BACKENDS_TEST_DEFINES}
  INCLUDES
    "${DEBUGGERDIR}"
  SOURCES
    # GdbMi/DisassemblerLines aren't DEBUGGER_EXPORT, so their methods
    # aren't visible across the libDebugger.so boundary - compiled
    # directly into this test binary instead, same reason
    # tst_debugger_dumpers/tst_debugger_disassembler do the same.
    "${DEBUGGERDIR}/debuggerprotocol.cpp" "${DEBUGGERDIR}/debuggerprotocol.h"
    "${DEBUGGERDIR}/disassemblerlines.cpp" "${DEBUGGERDIR}/disassemblerlines.h"
    tst_backends.cpp
)

add_qtc_test(tst_debugger_protocol
  DEPENDS Qt::Network Utils
  INCLUDES
    "${DEBUGGERDIR}"
  SOURCES
    "${DEBUGGERDIR}/debuggertr.h"
    "${DEBUGGERDIR}/debuggerprotocol.cpp" "${DEBUGGERDIR}/debuggerprotocol.h"
    tst_protocol.cpp
)

add_qtc_test(tst_debugger_offsets
  DEPENDS Qt::CorePrivate
  INCLUDES "${DEBUGGERDIR}"
  SOURCES tst_offsets.cpp
)

add_qtc_test(tst_debugger_simplifytypes
  INCLUDES "${DEBUGGERDIR}"
  DEPENDS Utils
  DEFINES DUMPERDIR="${PROJECT_SOURCE_DIR}/share/qtcreator/debugger"
  SOURCES
    tst_simplifytypes.cpp
    "${DEBUGGERDIR}/debuggertr.h"
    "${DEBUGGERDIR}/simplifytype.cpp" "${DEBUGGERDIR}/simplifytype.h"
)

add_qtc_test(tst_debugger_pdb
  DEFINES
    DUMPERDIR="${PROJECT_SOURCE_DIR}/share/qtcreator/debugger"
    PDBDUMPER_DRIVER="${CMAKE_CURRENT_SOURCE_DIR}/pdbdumper.py"
  SOURCES
    tst_pdb.cpp
)
