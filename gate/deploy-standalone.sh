#!/bin/bash
# 独立部署脚本 - 仅部署Gateway，连接外部Consul和Redis

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 获取主机IP地址
get_host_ip() {
  # 尝试获取主要的非回环IPv4地址
  local ip=$(hostname -I | awk '{print $1}')
  if [[ -z "$ip" ]]; then
    # 备用方法
    ip=$(ip -4 addr show | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | grep -v "127.0.0.1" | head -n 1)
  fi
  echo "$ip"
}

# 生成配置文件
generate_config() {
  echo -e "${BLUE}正在生成配置文件...${NC}"
  
  # 确保环境变量已加载
  if [[ -f .env ]]; then
    export $(grep -v '^#' .env | xargs)
  fi
  
  # 使用envsubst替换配置模板中的环境变量
  if [[ -f configs/config.template.yaml ]]; then
    envsubst < configs/config.template.yaml > configs/config.yaml
    echo -e "${GREEN}配置文件已生成: configs/config.yaml${NC}"
  else
    echo -e "${RED}错误: 配置模板文件不存在: configs/config.template.yaml${NC}"
    exit 1
  fi
}

# 检查并更新环境配置
check_env_config() {
  local need_update=false
  local host_ip=$(get_host_ip)
  
  # 检查环境变量模板是否存在
  if [[ ! -f env.standalone.template ]]; then
    echo -e "${RED}错误: 环境变量模板文件不存在: env.standalone.template${NC}"
    exit 1
  fi
  
  # 检查.env文件是否存在
  if [[ ! -f .env ]]; then
    need_update=true
  else
    # 如果.env文件比模板旧，则更新
    if [[ env.standalone.template -nt .env ]]; then
      echo -e "${YELLOW}环境变量模板已更新，需要更新.env文件${NC}"
      need_update=true
    fi
  fi
  
  # 如果需要更新，从模板复制并替换占位符
  if [[ "$need_update" = true ]]; then
    echo -e "${BLUE}正在更新环境变量配置...${NC}"
    cp env.standalone.template .env
    
    # 替换IP地址占位符
    sed -i "s/your-[0-9.]\+/$host_ip/g" .env
    echo -e "${GREEN}环境变量已更新: .env${NC}"
  fi
  
  # 加载环境变量
  export $(grep -v '^#' .env | xargs)
  
  # 验证关键环境变量
  if [[ -z "$CONSUL_ADDR" ]]; then
    echo -e "${RED}错误: CONSUL_ADDR 未设置${NC}"
    exit 1
  fi
  
  if [[ -z "$REDIS_ADDR" ]]; then
    echo -e "${RED}错误: REDIS_ADDR 未设置${NC}"
    exit 1
  fi
  
  echo -e "${GREEN}环境配置检查通过${NC}"
  echo -e "${BLUE}当前配置:${NC}"
  echo -e "  CONSUL_ADDR=${CONSUL_ADDR}"
  echo -e "  REDIS_ADDR=${REDIS_ADDR}"
  echo -e "  LOG_LEVEL=${LOG_LEVEL:-info}"
}

# 强制更新环境变量
force_update_env() {
  local host_ip=$(get_host_ip)
  
  echo -e "${BLUE}强制更新环境变量配置...${NC}"
  
  # 检查环境变量模板是否存在
  if [[ ! -f env.standalone.template ]]; then
    echo -e "${RED}错误: 环境变量模板文件不存在: env.standalone.template${NC}"
    exit 1
  fi
  
  # 从模板复制并替换占位符
  cp env.standalone.template .env
  
  # 替换IP地址占位符
  sed -i "s/your-[0-9.]\+/$host_ip/g" .env
  
  # 加载环境变量
  export $(grep -v '^#' .env | xargs)
  
  echo -e "${GREEN}环境变量已更新: .env${NC}"
  echo -e "${BLUE}当前配置:${NC}"
  echo -e "  CONSUL_ADDR=${CONSUL_ADDR}"
  echo -e "  REDIS_ADDR=${REDIS_ADDR}"
  echo -e "  LOG_LEVEL=${LOG_LEVEL:-info}"
  
  # 验证关键环境变量
  if [[ -z "$CONSUL_ADDR" ]]; then
    echo -e "${RED}错误: CONSUL_ADDR 未设置${NC}"
    exit 1
  fi
  
  if [[ -z "$REDIS_ADDR" ]]; then
    echo -e "${RED}错误: REDIS_ADDR 未设置${NC}"
    exit 1
  fi
}

# 测试连接
test_connection() {
  echo -e "${BLUE}测试Consul连接...${NC}"
  if curl -s -m 5 "http://${CONSUL_ADDR}/v1/status/leader" > /dev/null; then
    echo -e "${GREEN}Consul连接成功: ${CONSUL_ADDR}${NC}"
  else
    echo -e "${RED}Consul连接失败: ${CONSUL_ADDR}${NC}"
  fi
  
  echo -e "${BLUE}测试Redis连接...${NC}"
  # 这里只是简单检查端口是否开放，不验证密码
  REDIS_HOST=$(echo $REDIS_ADDR | cut -d':' -f1)
  REDIS_PORT=$(echo $REDIS_ADDR | cut -d':' -f2)
  
  if nc -z -w5 $REDIS_HOST $REDIS_PORT; then
    echo -e "${GREEN}Redis端口开放: ${REDIS_ADDR}${NC}"
  else
    echo -e "${RED}Redis端口无法连接: ${REDIS_ADDR}${NC}"
  fi
}

# 主函数
main() {
  case "$1" in
    build)
      echo -e "${BLUE}构建Docker镜像...${NC}"
      docker compose -f docker-compose.standalone.yml build
      echo -e "${GREEN}构建完成${NC}"
      ;;
    
    start)
      check_env_config
      generate_config
      echo -e "${BLUE}启动服务...${NC}"
      docker compose -f docker-compose.standalone.yml up -d
      echo -e "${GREEN}服务已启动${NC}"
      ;;
    
    stop)
      echo -e "${BLUE}停止服务...${NC}"
      docker compose -f docker-compose.standalone.yml down
      echo -e "${GREEN}服务已停止${NC}"
      ;;
    
    restart)
      check_env_config
      generate_config
      echo -e "${BLUE}重启服务...${NC}"
      docker compose -f docker-compose.standalone.yml down
      docker compose -f docker-compose.standalone.yml up -d
      echo -e "${GREEN}服务已重启${NC}"
      ;;
    
    logs)
      echo -e "${BLUE}查看日志...${NC}"
      docker compose -f docker-compose.standalone.yml logs -f
      ;;
    
    status)
      check_env_config
      echo -e "${BLUE}服务状态:${NC}"
      docker compose -f docker-compose.standalone.yml ps
      ;;
    
    update-env)
      force_update_env
      generate_config
      echo -e "${GREEN}环境变量和配置文件已更新${NC}"
      ;;
    
    test-conn)
      check_env_config
      test_connection
      ;;
    
    *)
      echo -e "${BLUE}Gateway独立部署脚本${NC}"
      echo -e "用法: $0 {build|start|stop|restart|logs|status|update-env|test-conn}"
      echo -e "  build      - 构建Docker镜像"
      echo -e "  start      - 启动服务"
      echo -e "  stop       - 停止服务"
      echo -e "  restart    - 重启服务"
      echo -e "  logs       - 查看日志"
      echo -e "  status     - 查看服务状态"
      echo -e "  update-env - 强制更新环境变量和配置文件"
      echo -e "  test-conn  - 测试外部服务连接"
      exit 1
      ;;
  esac
}

main "$@"