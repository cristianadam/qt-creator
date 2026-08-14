# Generate documentation

option(BUILD_DEVELOPER_DOCS "Include developer documentation" OFF)
add_feature_info("Include developer documentation" BUILD_DEVELOPER_DOCS "")

function(_find_all_includes _ret_includes _ret_framework_paths)
  set(_all_includes "${PROJECT_SOURCE_DIR}/src/plugins;${PROJECT_SOURCE_DIR}/src/libs")
  foreach(_target ${__QTC_PLUGINS} ${__QTC_LIBRARIES})
    if (NOT TARGET ${_target})
      continue()
    endif()
    get_target_property(_includes ${_target} INCLUDE_DIRECTORIES)
    foreach(_include ${_includes})
      string(FIND "${_include}" "/src/plugins/" _in_plugins)
      string(FIND "${_include}" "/src/libs/" _in_libs)
      string(FIND "${_include}" "${CMAKE_BINARY_DIR}" _in_build)
      string(REGEX MATCH "\\$<TARGET_PROPERTY:([^,]+).*>" _property_match "${_include}")
      set(_property_target "${CMAKE_MATCH_1}")
      if(_in_plugins LESS 0 AND _in_libs LESS 0 AND _in_build LESS 0 AND (NOT _property_target OR TARGET ${_property_target}))
        list(APPEND _all_includes ${_include})
      endif()
    endforeach()
  endforeach()
  list(APPEND _all_includes ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
  list(REMOVE_DUPLICATES _all_includes)
  set("${_ret_includes}" "${_all_includes}" PARENT_SCOPE)

  # framework path
  if (APPLE)
    get_target_property(_qt_type Qt::Core TYPE)
    if ("${_qt_type}" STREQUAL "INTERFACE_LIBRARY")
        # version-less target Qt::Core is an interface library that links to QtX::Core
        get_target_property(_qt_core Qt::Core INTERFACE_LINK_LIBRARIES)
    else()
        set(_qt_core "Qt::Core")
    endif()
    get_target_property(_qt_target ${_qt_core} LOCATION)
    # eg <fw_path>/QtCore.framework/QtCore
    # or <fw_path>/QtCore.framework/Versions/A/QtCore
    # cut off /QtCore.framework/*
    string(REGEX REPLACE "/QtCore.framework/.*" "" _qt_loc "${_qt_target}")
    set("${_ret_framework_paths}" "${_qt_loc}" PARENT_SCOPE)
  endif()
endfunction()

function(_add_doc _doc_file _dev_doc_file)
  set(GENERATED_ATTRIBUTIONS_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_attributions)
  add_qtc_documentation(${_doc_file}
    ENVIRONMENT_EXPORTS GENERATED_ATTRIBUTIONS_DIR
  )
  set(attribution_src "${CMAKE_CURRENT_SOURCE_DIR}/../qt_attributions.json")
  set(attribution_dest "${CMAKE_CURRENT_BINARY_DIR}/qt_attributions.json")
  qtc_prepare_attribution_file("${attribution_src}" "${attribution_dest}")
  add_qtc_doc_attribution(doc_attributions
    "${attribution_dest}"
    "${GENERATED_ATTRIBUTIONS_DIR}/creator-attributions.qdoc"
    ${_doc_file}
  )
  if (Qt6_VERSION VERSION_GREATER_EQUAL 6.8.0)
    set(litehtml_attributions "${CMAKE_CURRENT_SOURCE_DIR}/../src/libs/qlitehtml/src/3rdparty/qt_attribution.json")
    if(EXISTS "${litehtml_attributions}")
      add_qtc_doc_attribution(doc_attributions_litehtml
        "${litehtml_attributions}"
        "${GENERATED_ATTRIBUTIONS_DIR}/creator-attributions-litehtml.qdoc"
        ${_doc_file}
      )
    endif()
  else()
    message(WARNING "Disabling qt_attribution.json handling for qlitehtml for Qt < 6.8.0")
  endif()
  if (BUILD_DEVELOPER_DOCS)
    _find_all_includes(_all_includes _framework_paths)
    add_qtc_documentation(${_dev_doc_file}
      INCLUDE_DIRECTORIES ${_all_includes}
      FRAMEWORK_PATHS ${_framework_paths}
    )
  endif()
endfunction()

if (WITH_DOCS)
  _add_doc(${IDE_DOC_FILE} "qtcreatordev/qtcreator-dev.qdocconf")
endif()
if(WITH_ONLINE_DOCS)
  _add_doc(${IDE_DOC_FILE_ONLINE} "qtcreatordev/qtcreator-dev-online.qdocconf")
endif()

if (BUILD_DOCS_BY_DEFAULT)
set(EXCLUDE_DOCS_FROM_ALL "")
else()
set(EXCLUDE_DOCS_FROM_ALL "EXCLUDE_FROM_ALL")
endif()

install(DIRECTORY config
  DESTINATION ${IDE_DEVEL_HEADER_INSTALL_PATH}/doc
  COMPONENT Devel
  ${EXCLUDE_DOCS_FROM_ALL}
)

# The full MCP tool list is generated from the running server; the committed
# mcp-tools.qdocinc is only a placeholder. A committed snapshot goes stale
# whenever a tool is added in another commit, so the real list is produced at
# doc-build time: regenerate_mcp_tools_doc rewrites it from the built server
# and is made a prerequisite of the documentation targets. A doc build without
# the application keeps the placeholder.
#
# "-load all" force-enables every plugin, so tools from plugins that are
# disabled by default (the MCP server itself, and any future provider) are
# captured too; the list is meant to be exhaustive.
if (TARGET qtcreator AND TARGET mcpserver)
  add_custom_target(regenerate_mcp_tools_doc
    COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen
      $<TARGET_FILE:qtcreator> -load all
      -mcp-dump-tools ${CMAKE_CURRENT_SOURCE_DIR}/qtcreator/src/mcp/mcp-tools.qdocinc
      -settingspath ${CMAKE_CURRENT_BINARY_DIR}/mcp-dump-settings
    DEPENDS qtcreator mcpserver
    USES_TERMINAL
    VERBATIM
    COMMENT "Regenerating the MCP tool list from the MCP server"
  )
  foreach(_doc_target html_docs_qtcreator qch_docs_qtcreator
                      html_docs_qtcreator-online qch_docs_qtcreator-online)
    if (TARGET ${_doc_target})
      add_dependencies(${_doc_target} regenerate_mcp_tools_doc)
    endif()
  endforeach()
endif()
