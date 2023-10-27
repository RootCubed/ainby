add_executable(ainby)

file(GLOB_RECURSE ainby_SRC
    "*.cpp"
)

target_sources(ainby PUBLIC
    ${ainby_SRC}
)

target_include_directories(ainby
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${SDL3_DIR}/include
    PUBLIC ${TINYFILEDIALOGS_DIR}
)

add_definitions(-DIMGUI_USER_CONFIG="ainby_imgui_config.h")

if(WIN32)
    set_target_properties(ainby PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

target_link_libraries(ainby
    IMGUI TINYFILEDIALOGS
)
