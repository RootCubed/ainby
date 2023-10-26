# SDL3
set(SDL3_DIR "SDL")
add_subdirectory(${SDL3_DIR})

# OpenGL
find_package(OpenGL REQUIRED)

# ImGui
set(IMGUI_DIR "imgui")
add_library(IMGUI STATIC)

target_sources(IMGUI
    PRIVATE
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui.cpp

    PRIVATE
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
)

target_include_directories(IMGUI
    PUBLIC ${IMGUI_DIR}
    PUBLIC ${IMGUI_DIR}/backends
    PUBLIC ${SDL3_DIR}/include
)

target_link_libraries(IMGUI PRIVATE SDL3::SDL3)

# tinyfiledialogs
set(TINYFILEDIALOGS_DIR "tinyfd")

add_library(TINYFILEDIALOGS STATIC
    ${TINYFILEDIALOGS_DIR}/tinyfiledialogs.c
)

target_include_directories(TINYFILEDIALOGS
    PUBLIC ${TINYFILEDIALOGS_DIR}
)
