cmake_minimum_required(VERSION 3.10)
project(nlohmann_json VERSION 3.11.2 LANGUAGES CXX)

add_library(nlohmann_json INTERFACE)

target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
) 