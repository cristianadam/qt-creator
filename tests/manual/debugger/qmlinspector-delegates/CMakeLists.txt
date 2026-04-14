cmake_minimum_required(VERSION 3.16)

project(qmlinspector-delegates VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(qmlinspector-delegates
    main.cpp
)

qt_add_qml_module(qmlinspector-delegates
    URI DelegateTest
    VERSION 1.0
    QML_FILES
        Main.qml
        MyItem.qml
)

target_link_libraries(qmlinspector-delegates
    PRIVATE Qt6::Quick
)

target_compile_definitions(qmlinspector-delegates PRIVATE QT_QML_DEBUG)
