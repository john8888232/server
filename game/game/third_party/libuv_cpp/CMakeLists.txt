cmake_minimum_required(VERSION 3.10)
project(libuv_cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找系统uv库
find_path(UV_INCLUDE_DIR NAMES uv.h PATH_SUFFIXES uv)
find_library(UV_LIBRARY NAMES uv libuv)

if(NOT UV_LIBRARY)
    message(FATAL_ERROR "libuv library not found")
endif()

if(NOT UV_INCLUDE_DIR)
    message(FATAL_ERROR "libuv headers not found")
endif()

# 这是一个纯头文件库，创建一个STATIC库以包含LogWriter的实现
add_library(uv_cpp_impl STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LogWriter.cpp
)

target_include_directories(uv_cpp_impl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${UV_INCLUDE_DIR}
)

target_link_libraries(uv_cpp_impl PUBLIC ${UV_LIBRARY})

# 主接口库
add_library(uv_cpp INTERFACE)

target_include_directories(uv_cpp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${UV_INCLUDE_DIR}
)

target_link_libraries(uv_cpp INTERFACE 
    ${UV_LIBRARY}
    uv_cpp_impl
) 