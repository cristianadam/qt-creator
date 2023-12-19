#cmake_minimum_required(VERSION 3.16)

#project(miniyaml VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#find_package(Qt6 REQUIRED COMPONENTS Core)
#qt_standard_project_setup()

qt_add_library(miniyaml STATIC
#    main.cpp
    qtyaml.h qtyaml.cpp
    exception.h exception.cpp
    packagescanner.h
    yamlpackagescanner.h yamlpackagescanner.cpp
    packageinfo.h packageinfo.cpp
    applicationinfo.h applicationinfo.cpp
    global.h global.cpp

    libyaml/api.c
    libyaml/dumper.c
    libyaml/emitter.c
    libyaml/loader.c
    libyaml/parser.c
    libyaml/reader.c
    libyaml/scanner.c
    libyaml/writer.c
)

target_include_directories(miniyaml PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    libyaml
)

target_compile_definitions(miniyaml PUBLIC
    HAVE_CONFIG_H
    YAML_DECLARE_STATIC
    YAML_DECLARE_EXPORT
)

target_link_libraries(miniyaml PRIVATE Qt6::Core)
