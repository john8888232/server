cmake_minimum_required(VERSION 3.10)
project(game_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项，解决位置无关代码(PIE)的问题
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")

# 添加第三方库目录到包含路径
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# 添加nlohmann库
add_subdirectory(third_party/nlohmann)

# 查找系统依赖库
find_package(Threads REQUIRED)

# 查找protobuf
find_package(Protobuf REQUIRED)
if(Protobuf_FOUND)
    message(STATUS "Found Protobuf ${Protobuf_VERSION}")
    include_directories(${Protobuf_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "Protobuf not found")
endif()

# 指定libuv_cpp库和头文件
find_library(LIBUV_CPP_LIBRARY NAMES libuv_cpp.a PATHS ${CMAKE_SOURCE_DIR}/third_party/libuv_cpp/lib)
include_directories(${CMAKE_SOURCE_DIR}/third_party/libuv_cpp/include)

# 查找libuv库
find_library(UV_LIBRARY NAMES uv libuv REQUIRED)
find_path(UV_INCLUDE_DIR NAMES uv.h PATH_SUFFIXES uv)
include_directories(${UV_INCLUDE_DIR})

# 查找MySQL Connector/C++库（传统API）
find_library(MYSQL_CPPCONN_LIBRARY NAMES mysqlcppconn REQUIRED)

# 查找MySQL X DevAPI库
find_library(MYSQLCPPCONNX_LIBRARY NAMES mysqlcppconnx REQUIRED)
find_path(MYSQLX_INCLUDE_DIR NAMES mysqlx/xdevapi.h 
          PATHS /usr/include /usr/local/include /usr/include/mysql-cppconn
          PATH_SUFFIXES mysql-cppconn-8 mysql-cppconn8 mysqlx
          REQUIRED)
include_directories(${MYSQLX_INCLUDE_DIR})

# 查找 hiredis 和 redis++
find_library(HIREDIS_LIBRARY NAMES hiredis REQUIRED)
find_path(HIREDIS_INCLUDE_DIR NAMES hiredis/hiredis.h REQUIRED)
include_directories(${HIREDIS_INCLUDE_DIR})

find_library(REDIS_PLUS_PLUS_LIBRARY NAMES redis++ redis-plus-plus REQUIRED)
find_path(REDIS_PLUS_PLUS_INCLUDE_DIR NAMES sw/redis++/redis++.h REQUIRED)
include_directories(${REDIS_PLUS_PLUS_INCLUDE_DIR})

# 设置 ppconsul 的 CMake 模块路径
list(APPEND CMAKE_MODULE_PATH "/usr/local/cmake")
find_package(ppconsul REQUIRED)
include_directories("/usr/local/include")  # ppconsul 头文件位置

find_library(ELASTICLIENT_LIBRARY NAMES elasticlient REQUIRED)

# 查找jsoncpp库
find_library(JSONCPP_LIBRARY NAMES jsoncpp REQUIRED)
find_path(JSONCPP_INCLUDE_DIR NAMES json/json.h REQUIRED)
include_directories(${JSONCPP_INCLUDE_DIR})

# 添加源代码子目录
add_subdirectory(src)

# 项目主源文件
add_executable(game_server src/main.cpp)

# 链接库
target_link_libraries(game_server PRIVATE
    core
    games
    mines_pro
    ${LIBUV_CPP_LIBRARY}
    Threads::Threads
    ${UV_LIBRARY}
    ${MYSQL_CPPCONN_LIBRARY}  # 传统API库
    ${MYSQLCPPCONNX_LIBRARY}  # X DevAPI库
    ${HIREDIS_LIBRARY}
    ${REDIS_PLUS_PLUS_LIBRARY}
    ppconsul  # 使用 ppconsul 目标而不是变量
    ${ELASTICLIENT_LIBRARY}
    ${JSONCPP_LIBRARY}
    ${Protobuf_LIBRARIES}
)

# 创建bin目录并拷贝可执行文件和配置文件
set(BIN_DIR ${CMAKE_SOURCE_DIR}/bin)

# 安装目标，确保bin目录存在并复制文件
add_custom_command(TARGET game_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:game_server> ${BIN_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/config/ ${BIN_DIR}/
    COMMENT "Copying executable and config files to bin directory"
)
