file(GLOB_RECURSE MINES_PRO_SRC
    "application/*.cpp"
    "domain/*.cpp"
    "infrastructure/*.cpp"
    "interfaces/*.cpp"
)

add_library(mines_pro STATIC ${MINES_PRO_SRC})

target_include_directories(mines_pro PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(mines_pro PUBLIC
    core
    games
) 