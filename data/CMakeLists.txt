if (MSVC)
    set(ACTUAL_CMAKE_BINARY_DIR ${CMAKE_BINARY_DIR}/bin/$<$<CONFIG:Debug>:Debug>$<$<CONFIG:Release>:Release>$<$<CONFIG:MinSizeRel>:MinSizeRel>)
else()
    set(ACTUAL_CMAKE_BINARY_DIR ${CMAKE_BINARY_DIR}/bin)
endif()

add_custom_target(copyAssets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_LIST_DIR}/sarasa-term-j-regular.ttf ${ACTUAL_CMAKE_BINARY_DIR}/sarasa-term-j-regular.ttf
)
