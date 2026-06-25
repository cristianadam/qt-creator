option(BUILD_EXECUTABLE_QTPROFILER_STANDALONE
       "Creates an install target for standalone qtprofiler"
       OFF)

add_qtc_executable(qtprofiler
  CONDITION TARGET Profiler
  DEPENDS app_version Profiler CommonTraceFormat Tracing
  SOURCES
    mainsidebar.cpp mainsidebar.h
    qtprofilerinit.cpp qtprofilerinit.h
    qtprofilermain.cpp
    qtprofilerrpc.cpp qtprofilerrpc.h
    qtprofilersettings.cpp qtprofilersettings.h
    qtprofilerwindow.h qtprofilerwindow.cpp
    recordingpage.cpp recordingpage.h
    welcomepage.cpp welcomepage.h
    schema/api.h
)

# qtprofiler now lives in its own bundle, so it can no longer fall back to
# "Qt Creator.app"'s resources at runtime (ICore::resourcePath() resolves to
# the bundle's Contents/Resources). Copy the resources it needs into the
# bundle for dev builds.
if (APPLE AND TARGET qtprofiler)
  qtc_output_binary_dir(_output_binary_dir)
  set(qtprofiler_resource_dest
    "${_output_binary_dir}/${IDE_APP_PATH}/${qtprofiler_bundle_name}.app/Contents/Resources")

  # Themes are copied straight from their source location. Translations are
  # generated at build time, so they are taken from the build tree instead.
  # Runs before the codesigning step below, so the signature seals the resources.
  add_custom_command(TARGET qtprofiler POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${PROJECT_SOURCE_DIR}/share/qtcreator/themes"
      "${qtprofiler_resource_dest}/themes"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${_output_binary_dir}/${IDE_DATA_PATH}/translations"
      "${qtprofiler_resource_dest}/translations"
    VERBATIM
    COMMENT "Copying qtprofiler resources into the app bundle"
  )

  # The generated translations must exist in the build tree before we copy them.
  if (TARGET generate_qm_files)
    add_dependencies(qtprofiler generate_qm_files)
  endif()
endif()

qtc_add_resources(qtprofiler "schema"
  PREFIX "/qtprofiler"
  BASE "."
  FILES
    schema/qtprofilerapi.json.schema
)

# The call-stack sampler uses task_for_pid(), which requires the binary to carry
# the debugger entitlement. Re-sign (ad-hoc) after each build so the freshly
# linked binary keeps it. Without this the sampler fails at attach time.
if (APPLE AND TARGET qtprofiler)
  add_custom_command(TARGET qtprofiler POST_BUILD
    COMMAND codesign --sign - --force
            --entitlements "${CMAKE_CURRENT_LIST_DIR}/qtprofiler.entitlements"
            "$<TARGET_FILE:qtprofiler>"
    VERBATIM
    COMMENT "Codesigning qtprofiler with the debugger entitlement"
  )
endif()

if (NOT TARGET qtprofiler OR NOT BUILD_EXECUTABLE_QTPROFILER_STANDALONE)
  return()
endif()

create_standalone_install_component(standalone_qtprofiler
  TARGET qtprofiler
  BUNDLE_PLIST_TEMPLATE ${CMAKE_CURRENT_LIST_DIR}/Info.plist.in
  PLUGINS
    platforminputcontexts
    platforms
    platformthemes
    styles
    wayland-decoration-client
    wayland-graphics-integration-client
    wayland-shell-integration
    xcbglintegrations
  IMPORTS
    Qt
    QtCore
    QtNetwork
    QtQml
    QtWayland
  RESOURCES
    themes
    translations
)
