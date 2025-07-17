project(heartbeat_example CXX)

# Require C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Heartbeat client
add_executable(heartbeat_client heartbeat_client.cpp)
target_link_libraries(heartbeat_client uvcpp)

# Heartbeat server
add_executable(heartbeat_server heartbeat_server.cpp)
target_link_libraries(heartbeat_server uvcpp)

# Copy executables to bin directory
set_target_properties(
    heartbeat_client
    heartbeat_server
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
) 