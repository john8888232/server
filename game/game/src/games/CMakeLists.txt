# 收集games目录下的源文件（不包括子目录）
file(GLOB GAMES_SRC
    "*.cpp"
)

# 创建games库
add_library(games STATIC ${GAMES_SRC})

target_include_directories(games PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(games PUBLIC
    core
)

# 添加子目录
add_subdirectory(mines_pro)

# 新游戏添加的位置
# add_subdirectory([new_game]) 