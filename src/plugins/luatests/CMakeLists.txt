add_custom_target(luatests ALL
    SOURCES 
        luatests/luatests.lua
        luatests/tests.lua
        luatests/inspect.lua
        luatests/qtctest.lua
        luatests/tst_aspectcontainer.lua
        luatests/tst_fetch.lua
)


function (setup_copycommands TARGET)
    get_target_property(ALL_SOURCES ${TARGET} SOURCES)
    foreach(SOURCE ${ALL_SOURCES})
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}
                ${CMAKE_BINARY_DIR}/${IDE_PLUGIN_PATH}/lua-plugins/${SOURCE}
            COMMENT "Copying ${LUAFILE} to ${CMAKE_BINARY_DIR}/${IDE_PLUGIN_PATH}/lua-plugins/${LUAFILE}"
        )
    endforeach()
endfunction()

setup_copycommands(luatests)
