#!/bin/bash

# 构建压测机器人
echo "构建压测机器人..."

cd /home/ec2-user/server/gate/cmd/stress_test

# 创建bin目录
mkdir -p bin

# 构建二进制文件
go build -o bin/stress_test main.go

if [ $? -eq 0 ]; then
    echo "构建成功！二进制文件位于: bin/stress_test"
    echo ""
    echo "使用方法:"
    echo "./bin/stress_test -help                    # 查看帮助"
    echo "./bin/stress_test -bots 100               # 启动100个机器人"
    echo "./bin/stress_test -bots 50 -cancel-ratio 0.3  # 50个机器人，30%取消下注"
    echo ""
    echo "参数说明:"
    echo "-bots: 机器人数量 (1-5000, 默认10)"
    echo "-server: 服务器地址 (默认ws://127.0.0.1:5600/ws)"
    echo "-bet-min: 最小下注金额 (默认10.0)"
    echo "-bet-max: 最大下注金额 (默认100.0)"
    echo "-cancel-ratio: 取消下注比例 (0.0-1.0, 默认0.5)"
else
    echo "构建失败！"
    exit 1
fi