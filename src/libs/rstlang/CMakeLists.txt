set(RSTLANG_PARSER_FILES
  rstparser.h
  rstparser.cpp
  rstparsertable_p.h
  rstparsertable.cpp
)

option(QTC_RSTLANG_AUTOGENERATE_PARSER
  "Regenerate the reStructuredText parser from rstlang.g with qlalr" OFF)

if(QTC_RSTLANG_AUTOGENERATE_PARSER)
  foreach(file IN LISTS RSTLANG_PARSER_FILES)
    list(APPEND RSTLANG_PARSER_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${file}")
  endforeach()

  # qlalr writes its output into the working directory and spells the grammar
  # the way it was passed into the #line directives, so it runs on a copy of the
  # grammar in the build directory.
  add_custom_command(
    OUTPUT ${RSTLANG_PARSER_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/rstlang.g" rstlang.g
    COMMAND Qt6::qlalr --qt --no-debug ./rstlang.g
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${RSTLANG_PARSER_FILES} "${CMAKE_CURRENT_SOURCE_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/rstlang.g" Qt6::qlalr
    COMMENT "Generating the reStructuredText parser"
    VERBATIM
  )
else()
  set(RSTLANG_PARSER_SOURCES ${RSTLANG_PARSER_FILES})
endif()

set_source_files_properties(rstlang.g PROPERTIES HEADER_FILE_ONLY ON)

add_qtc_library(RstLang
  DEPENDS Qt::Core
  SOURCES
    rstlang.g
    rstlang.h
    rstast.cpp rstast.h
    rstastvisitor.cpp rstastvisitor.h
    rstdocument.cpp rstdocument.h
    rstengine.cpp rstengine.h
    rstlexer.cpp rstlexer.h
    rstmarkdown.cpp rstmarkdown.h
    rstmemorypool.h
    ${RSTLANG_PARSER_SOURCES}
)
