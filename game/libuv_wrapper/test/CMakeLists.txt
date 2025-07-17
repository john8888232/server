cmake_minimum_required(VERSION 3.12)
project(uv_cpp_examples)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Include main project directory
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../libuv1.51.0/include
)

# Find threads
find_package(Threads REQUIRED)

# Basic TCP server example
add_executable(basic_server
    basic_server/server.cpp
)
target_link_libraries(basic_server
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libuv_cpp.a
    Threads::Threads
)

# Basic TCP client example
add_executable(basic_client
    basic_client/client.cpp
)
target_link_libraries(basic_client
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libuv_cpp.a
    Threads::Threads
)

# HTTP server example
add_executable(http_server
    http_server/http_server.cpp
)
target_link_libraries(http_server
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libuv_cpp.a
    Threads::Threads
)

# HTTP client example
add_executable(http_client
    http_client/http_client.cpp
)
target_link_libraries(http_client
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libuv_cpp.a
    Threads::Threads
)

# UDP example
add_executable(udp_example
    udp_example/udp_example.cpp
)
target_link_libraries(udp_example
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libuv_cpp.a
    Threads::Threads
)

# DNS example
add_executable(dns_example
    dns_example/dns_example.cpp
)
target_link_libraries(dns_example
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libuv_cpp.a
    Threads::Threads
)

# Heartbeat example
add_executable(heartbeat_client
    heartbeat_example/heartbeat_client.cpp
)
target_link_libraries(heartbeat_client
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libuv_cpp.a
    Threads::Threads
)

add_executable(heartbeat_server
    heartbeat_example/heartbeat_server.cpp
)
target_link_libraries(heartbeat_server
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libuv_cpp.a
    Threads::Threads
) 