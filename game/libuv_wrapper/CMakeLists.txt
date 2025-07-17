CMAKE_MINIMUM_REQUIRED(VERSION 3.12)  # 推荐升级至 CMake 3.12+
PROJECT(uv_cpp)

# 设置库输出目录
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)

# 包含路径设置
INCLUDE_DIRECTORIES(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/libuv1.51.0/include
)

# 收集源文件（需根据实际目录调整）
FILE(GLOB_RECURSE SOURCES 
    "${PROJECT_SOURCE_DIR}/uv/*.cpp"
    "${PROJECT_SOURCE_DIR}/uv/http/*.cpp"
)

# 编译选项
SET(CMAKE_C_STANDARD 11)
SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

# 构建静态库 uv_cpp
ADD_LIBRARY(${PROJECT_NAME} STATIC ${SOURCES})

# 合并 libuv.a 到 uv_cpp.a
ADD_CUSTOM_COMMAND(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory tmp_merge  # 创建临时目录
    COMMAND cd tmp_merge && ar x ${PROJECT_SOURCE_DIR}/libuv1.51.0/lib/gcc11.5.0/libuv.a  # 解压 libuv.a
    COMMAND cd tmp_merge && ar x $<TARGET_FILE:${PROJECT_NAME}>  # 解压当前生成的 uv_cpp.a
    COMMAND ar crs $<TARGET_FILE:${PROJECT_NAME}> tmp_merge/*.o  # 合并所有 .o 文件
    COMMAND ${CMAKE_COMMAND} -E remove_directory tmp_merge  # 清理临时目录
    COMMENT "Merging libuv.a into ${PROJECT_NAME}.a"
)

TARGET_LINK_LIBRARIES(${PROJECT_NAME} PRIVATE pthread)

# Find the include paths of libuv1.51.0
include_directories(${CMAKE_SOURCE_DIR}/libuv1.51.0/include)
link_directories(${CMAKE_SOURCE_DIR}/libuv1.51.0/lib)

# Change version number to match the new libuv version
set(UV_CPP_VERSION "1.5.7")
