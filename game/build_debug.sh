#!/bin/bash

set -eo pipefail

# ---------------------------
# 配置（根据需求修改）
# ---------------------------
BUILD_DIR="build_debug"         # 构建目录
BIN_DIR="../game/bin"           # 输出目录
TARGET_EXE="game_server"        # 目标可执行文件名
SOURCE_DIR="../game"            # 源代码目录
MAX_JOBS=$(($(nproc)))          # 并行编译线程数

# ---------------------------
# 颜色定义
# ---------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# ---------------------------
# 安装ASan运行时库
# ---------------------------
install_asan_runtime() {
    echo -e "${YELLOW}>>> 正在安装ASan运行时库...${NC}"
    
    if command -v dnf &> /dev/null; then
        sudo dnf install -y libasan libasan-static || return 1
    elif command -v apt-get &> /dev/null; then
        sudo apt-get install -y libasan6 libasan6-static || return 1
    else
        echo -e "${RED}✖ 不支持的包管理器${NC}"
        return 1
    fi
    
    echo -e "${GREEN}✔ ASan运行时库安装成功${NC}"
    return 0
}

# ---------------------------
# 全面检测ASan支持
# ---------------------------
check_asan_support() {
    echo -e "${YELLOW}=== 全面检测ASan支持性 ==="
    echo -e "检测阶段："
    echo -e "1. 编译检查"
    echo -e "2. 动态链接检查"
    echo -e "3. 静态链接检查${NC}"
    
    local test_src=$(mktemp)
    local test_exe=$(mktemp)
    local support=0
    
    # 测试代码（包含真实内存操作）
    cat <<EOF > "$test_src"
#include <stdlib.h>
int main() {
    int *p = (int*)malloc(sizeof(int));
    *p = 42;  // 真实内存访问
    free(p);
    return p == NULL;
}
EOF

    # 阶段1：编译检测
    if ! g++ -fsanitize=address -x c++ "$test_src" -o "$test_exe" &>/dev/null; then
        echo -e "${RED}✖ 编译测试失败（不支持-fsanitize=address）${NC}"
        rm -f "$test_src" "$test_exe"
        return 1
    fi
    echo -e "${GREEN}✔ 编译测试通过${NC}"

    # 阶段2：动态链接检测
    if ldd "$test_exe" 2>/dev/null | grep -q libasan; then
        echo -e "${GREEN}✔ 动态链接验证通过 (找到libasan.so)${NC}"
        support=0  # bash中0表示成功
    else
        echo -e "${YELLOW}⚠ 未找到动态库，尝试静态链接...${NC}"
        
        # 阶段3：静态链接检测
        if g++ -fsanitize=address -static-libasan -x c++ "$test_src" -o "$test_exe" &>/dev/null; then
            echo -e "${GREEN}✔ 静态链接验证通过${NC}"
            support=0  # bash中0表示成功
        else
            echo -e "${RED}✖ 静态链接测试失败${NC}"
            support=1  # bash中1表示失败
        fi
    fi

    rm -f "$test_src" "$test_exe"
    return $support
}

# ---------------------------
# 安装系统依赖
# ---------------------------
install_dependencies() {
    echo -e "${YELLOW}=== 检查系统依赖 ==="
    
    local missing=()
    
    # 检查基础工具链
    for cmd in g++ cmake make; do
        if ! command -v $cmd &> /dev/null; then
            missing+=("$cmd")
        fi
    done
    
    # 安装缺失工具
    if [ ${#missing[@]} -gt 0 ]; then
        echo -e "${YELLOW}缺少必要工具: ${missing[*]}${NC}"
        if command -v dnf &> /dev/null; then
            sudo dnf install -y "${missing[@]}" || return 1
        elif command -v apt-get &> /dev/null; then
            sudo apt-get install -y "${missing[@]}" || return 1
        else
            echo -e "${RED}不支持的包管理器${NC}"
            return 1
        fi
    fi
    
    # 检查ASan运行时
    if ! ls /usr/lib*/libasan.so* 2>/dev/null && \
       ! g++ -print-file-name=libasan.a | grep -q libasan.a; then
        install_asan_runtime || {
            echo -e "${YELLOW}⚠ 继续尝试构建（可能使用静态库）${NC}"
        }
    fi
    
    echo -e "${GREEN}所有依赖就绪${NC}"
    return 0
}

# ---------------------------
# 主构建流程
# ---------------------------
main() {
    # 初始化检查
    install_dependencies || {
        echo -e "${RED}依赖安装失败${NC}"
        exit 1
    }

    # ASan支持检测
    local SANITIZER_FLAGS=""
    local SANITIZER_LDFLAGS=""
    local ASAN_ENABLED=0
    
    if check_asan_support; then
        echo -e "${GREEN}=== ASan支持已确认 ==="
        SANITIZER_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
        SANITIZER_LDFLAGS="-fsanitize=address"
        ASAN_ENABLED=1
        
        # 检查是否需要静态链接（跳过，使用动态链接）
        echo -e "${GREEN}使用动态链接ASan${NC}"
        
        # 设置ASan环境变量
        export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1"
        export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
        echo -e "${GREEN}ASan环境变量已设置${NC}"
    else
        echo -e "${YELLOW}=== 构建无ASan的调试版本 ==="
        ASAN_ENABLED=0
    fi

    # 构建配置
    export CXXFLAGS="-g3 -O0 -DDEBUG $SANITIZER_FLAGS"
    export CFLAGS="-g3 -O0 -DDEBUG $SANITIZER_FLAGS"
    export LDFLAGS="$SANITIZER_LDFLAGS"

    # 准备构建目录
    echo -e "${YELLOW}=== 准备构建环境 ==="
    [ -d "$BUILD_DIR" ] && rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # CMake配置
    echo -e "${YELLOW}=== 配置CMake ==="
    echo -e "CXXFLAGS: $CXXFLAGS"
    echo -e "LDFLAGS: $LDFLAGS"
    
    cmake "$SOURCE_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
        -DCMAKE_C_FLAGS="$CFLAGS" \
        -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS" \
        -DENABLE_DEBUG_LOGGING=ON

    # 编译
    echo -e "${YELLOW}=== 开始编译（使用 $MAX_JOBS 线程）==="
    make -j"$MAX_JOBS"

    # 部署可执行文件
    echo -e "${YELLOW}=== 部署可执行文件 ==="
    mkdir -p "$BIN_DIR"
    if [ -f "$TARGET_EXE" ]; then
        cp "$TARGET_EXE" "$BIN_DIR/${TARGET_EXE}_debug"
        echo -e "${GREEN}调试版本已部署: ${BIN_DIR}/${TARGET_EXE}_debug${NC}"
        
        # 生成启动脚本
        cat <<EOF > "$BIN_DIR/run_debug.sh"
#!/bin/bash
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
mkdir -p logs
exec ./${TARGET_EXE}_debug "\$@"
EOF
        chmod +x "$BIN_DIR/run_debug.sh"
        echo -e "${GREEN}启动脚本已生成: ${BIN_DIR}/run_debug.sh${NC}"
    else
        echo -e "${RED}错误: 未找到目标可执行文件${NC}"
        ls -la
        exit 1
    fi

    echo -e "\n${GREEN}=== 构建成功完成 ==="
    echo -e "▸ 调试版本: ${BIN_DIR}/${TARGET_EXE}_debug"
    echo -e "▸ 启动命令: cd ${BIN_DIR} && ./run_debug.sh"
    echo -e "▸ ASan状态: $([ "$ASAN_ENABLED" -eq 1 ] && echo "已启用" || echo "未启用")"
    
    # 验证ASan是否真正链接
    if [ "$ASAN_ENABLED" -eq 1 ]; then
        if ldd "$BIN_DIR/${TARGET_EXE}_debug" 2>/dev/null | grep -q libasan; then
            echo -e "▸ ASan验证: ${GREEN}✔ 已正确链接${NC}"
        else
            echo -e "▸ ASan验证: ${YELLOW}⚠ 可能使用静态链接${NC}"
        fi
    fi
    echo -e "${NC}"
}

# 执行主函数
main "$@"