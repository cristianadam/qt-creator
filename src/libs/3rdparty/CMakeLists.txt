add_subdirectory(cplusplus)
add_subdirectory(syntax-highlighting)
add_subdirectory(libvterm)
if(EMSCRIPTEN)
    # libptyqt has no WebAssembly backend (no pseudo-terminals under Emscripten).
    # Provide a stub INTERFACE target so dependents (Utils) still resolve their link deps.
    add_library(ptyqt INTERFACE)
else()
    add_subdirectory(libptyqt)
endif()
add_subdirectory(qtkeychain)
add_subdirectory(lua)
add_subdirectory(sol2)

if(WIN32)
    add_subdirectory(winpty)
endif()

if(EMSCRIPTEN)
    # libarchive relies on POSIX disk-walking APIs unavailable under Emscripten.
    # Provide a stub INTERFACE target that tells its dependents to compile without
    # libarchive; unarchiver.cpp is guarded to a no-op there.
    add_library(QtCLibArchive INTERFACE)
    target_compile_definitions(QtCLibArchive INTERFACE QTC_WITHOUT_LIBARCHIVE)
else()
    find_package(LibArchive QUIET)

    if(LibArchive_FOUND)
      set(QTC_USE_SYSTEM_LIBARCHIVE_DEFAULT ON)
    else()
      set(QTC_USE_SYSTEM_LIBARCHIVE_DEFAULT OFF)
    endif()
    option(QTC_USE_SYSTEM_LIBARCHIVE "Use the system provided libarchive" ${QTC_USE_SYSTEM_LIBARCHIVE_DEFAULT})

    if (NOT QTC_USE_SYSTEM_LIBARCHIVE)
        add_subdirectory(xz)
        add_subdirectory(bzip2)
        add_subdirectory(zlib)

        set(ENABLE_WERROR OFF)
        set(ENABLE_TEST OFF)
        set(ENABLE_INSTALL OFF)
        set(BUILD_SHARED_LIBS OFF)

        add_subdirectory(libarchive)
        add_library(QtCLibArchive ALIAS archive_static)

        IF (NOT MSVC)
          # Disable warnings
          target_compile_options(archive_static PRIVATE "-w")
        ENDIF()
    else()
        # make IMPORTED_GLOBAL to be able to create ALIAS target
        set_property(TARGET LibArchive::LibArchive PROPERTY IMPORTED_GLOBAL TRUE)
        add_library(QtCLibArchive ALIAS LibArchive::LibArchive)
    endif()

    add_feature_info("Use system libarchive" QTC_USE_SYSTEM_LIBARCHIVE "")
endif()
