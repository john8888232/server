#!/bin/bash

# 运行调试版本的脚本
# 设置所有必要的调试环境变量
# 在 game/bin 目录中运行

set -e

echo "=== Running DEBUG version with memory debugging ==="

# 检查调试版本是否存在
BIN_DIR="game/bin"
if [ ! -f "$BIN_DIR/game_server_debug" ]; then
    echo "Debug version not found at $BIN_DIR/game_server_debug"
    echo "Please run build_debug.sh first."
    exit 1
fi

# 设置核心转储
echo "Setting up core dumps..."
#ulimit -c unlimited
# 设置核心转储路径到bin目录
CORE_PATTERN="$PWD/$BIN_DIR/core.%e.%p.%t"
echo "$CORE_PATTERN" | sudo tee /proc/sys/kernel/core_pattern > /dev/null || {
    echo "Warning: Could not set core dump pattern (need sudo). Using default."
    CORE_PATTERN="/tmp/core.%e.%p.%t"
}

# 检查是否有sanitizer支持
if ldd "$BIN_DIR/game_server_debug" 2>/dev/null | grep -q "libasan\|libubsan"; then
    echo "Sanitizers detected in executable"
    # 设置AddressSanitizer选项
    export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:log_path=./asan_log:symbolize=1"
    # 设置UndefinedBehaviorSanitizer选项
    export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1:log_path=./ubsan_log"
    SANITIZER_ENABLED=1
else
    echo "No sanitizers detected, running without sanitizer options"
    SANITIZER_ENABLED=0
fi

# 设置内存监控选项
export MEMORY_MONITOR_ENABLED=1
export DEBUG_LOGGING=1

# 创建日志目录
mkdir -p "$BIN_DIR/logs"

echo "Debug environment configured:"
echo "  Working directory: $BIN_DIR"
echo "  Core dumps: enabled (pattern: $CORE_PATTERN)"
if [ "$SANITIZER_ENABLED" = "1" ]; then
    echo "  ASAN_OPTIONS: $ASAN_OPTIONS"
    echo "  UBSAN_OPTIONS: $UBSAN_OPTIONS"
fi
echo "  Memory Monitor: enabled"
echo "  Debug Logging: enabled"
echo ""

# 进入bin目录（配置文件所在目录）
cd "$BIN_DIR"

# 显示内存信息
echo "Current memory usage:"
free -h
echo ""

echo "Starting game server with debugging..."
echo "Working directory: $(pwd)"

# 检查是否要后台运行
if [ "$1" = "--background" ] || [ "$1" = "-b" ]; then
    echo "Starting in background mode..."
    echo "Log files will be in logs/ directory (app_*.log)"
    echo "==========================================="
    
    # 后台运行并记录PID
    nohup ./game_server_debug > /dev/null 2>&1 &
    PID=$!
    echo $PID > logs/game_server_debug.pid
    
    echo "Game server started in background"
    echo "PID: $PID"
    echo "Log files: logs/app_*.log"
    echo ""
    echo "To monitor logs: tail -f logs/app_*.log"
    echo "To stop server: kill $PID"
    echo "Or use: ./stop_debug.sh"
else
    echo "Press Ctrl+C to stop"
    echo "Log files will be in logs/ directory (app_*.log)"
    echo "==========================================="
    
    # 前台运行
    ./game_server_debug
fi