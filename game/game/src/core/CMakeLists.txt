# 设置proto文件输出目录
set(PROTO_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/src/core/infrastructure/proto")

# 创建输出目录
file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})

# 查找proto文件
file(GLOB PROTO_FILES "${CMAKE_SOURCE_DIR}/proto/*.proto")

# 生成protobuf源文件和头文件的列表
set(PROTO_SRCS)
set(PROTO_HDRS)

# 对每个proto文件生成对应的.pb.cc和.pb.h
foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    set(PROTO_HDR "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.pb.h")
    set(PROTO_SRC "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.pb.cc")
    
    add_custom_command(
        OUTPUT ${PROTO_HDR} ${PROTO_SRC}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        ARGS --cpp_out=${PROTO_OUTPUT_DIR} --proto_path=${CMAKE_SOURCE_DIR}/proto ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating ${PROTO_NAME}.pb.h and ${PROTO_NAME}.pb.cc"
    )
    
    list(APPEND PROTO_HDRS ${PROTO_HDR})
    list(APPEND PROTO_SRCS ${PROTO_SRC})
endforeach()

file(GLOB_RECURSE CORE_SRC
    "application/*.cpp"
    "domain/*.cpp"
    "infrastructure/*.cpp"
    "interfaces/*.cpp"
)

# 将之前在shared中定义的头文件添加到core，仅保留实际存在的文件
set(CORE_HEADERS
    "domain/models/user.h"
    "domain/models/player_in_game.h"
)

# 使用file(GLOB...)查找所有头文件可能更安全
file(GLOB_RECURSE CORE_HEADERS_AUTO
    "application/*.h"
    "domain/*.h"
    "infrastructure/*.h"
    "interfaces/*.h"
)

# 将protobuf生成的源文件添加到库源文件列表
add_library(core STATIC ${CORE_SRC} ${PROTO_SRCS} ${CORE_HEADERS} ${CORE_HEADERS_AUTO})

target_include_directories(core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${UV_INCLUDE_DIR}
    ${Protobuf_INCLUDE_DIRS}  # 添加protobuf包含目录
)

target_link_libraries(core PUBLIC
    ${LIBUV_CPP_LIBRARY}
    Threads::Threads
    ${UV_LIBRARY}
    ${MYSQL_CPPCONN_LIBRARY}
    # 条件链接MySQL X DevAPI库
    $<$<BOOL:${MySQLCppConn8_FOUND}>:${MySQLCppConn8_LIBRARIES}>
    ${HIREDIS_LIBRARY}
    ${REDIS_PLUS_PLUS_LIBRARY}
    ${ppconsul_LIBRARIES}
    ${ELASTICLIENT_LIBRARY}
    ${JSONCPP_LIBRARY}
    ${Protobuf_LIBRARIES}  # 链接protobuf库
) 