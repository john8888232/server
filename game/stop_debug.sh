#!/bin/bash

# 停止调试版本服务器的脚本

set -e

echo "=== Stopping DEBUG game server ==="

BIN_DIR="game/bin"
PID_FILE="$BIN_DIR/logs/game_server_debug.pid"

# 检查PID文件是否存在
if [ ! -f "$PID_FILE" ]; then
    echo "PID file not found: $PID_FILE"
    echo "Trying to find running debug server processes..."
    
    # 查找正在运行的调试服务器进程
    PIDS=$(pgrep -f "game_server_debug" || true)
    if [ -z "$PIDS" ]; then
        echo "No running debug server processes found"
        exit 0
    else
        echo "Found debug server processes: $PIDS"
        for pid in $PIDS; do
            echo "Killing process $pid..."
            kill -TERM "$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
        done
        echo "All debug server processes stopped"
        exit 0
    fi
fi

# 读取PID
PID=$(cat "$PID_FILE")
echo "Found PID file with PID: $PID"

# 检查进程是否还在运行
if ! kill -0 "$PID" 2>/dev/null; then
    echo "Process $PID is not running"
    rm -f "$PID_FILE"
    exit 0
fi

echo "Stopping debug server (PID: $PID)..."

# 尝试优雅停止
kill -TERM "$PID" 2>/dev/null || true

# 等待进程停止
for i in {1..10}; do
    if ! kill -0 "$PID" 2>/dev/null; then
        echo "Debug server stopped gracefully"
        rm -f "$PID_FILE"
        exit 0
    fi
    echo "Waiting for process to stop... ($i/10)"
    sleep 1
done

# 如果还没停止，强制杀死
echo "Process didn't stop gracefully, forcing termination..."
kill -KILL "$PID" 2>/dev/null || true

# 再次检查
if ! kill -0 "$PID" 2>/dev/null; then
    echo "Debug server stopped (forced)"
    rm -f "$PID_FILE"
else
    echo "Failed to stop debug server"
    exit 1
fi
