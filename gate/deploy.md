# Gateway独立部署说明

本文档说明如何使用Docker部署Gateway服务

## 环境要求

- Docker 20.10+
- Docker Compose 2.0+（使用`docker compose`）
- Linux/macOS系统

## 部署步骤

### 1. 准备环境变量

复制环境变量模板并根据实际情况修改：

```bash
cp env.standalone.template .env
```

主要环境变量：
- `CONSUL_ADDR`: Consul服务地址，格式为`host:port`
- `REDIS_ADDR`: Redis服务地址，格式为`host:port`
- `REDIS_PASSWORD`: Redis密码（如果有）
- `LOG_LEVEL`: 日志级别（可选，默认为`info`）

### 2. 构建Docker镜像

```bash
./deploy-standalone.sh build
```

### 3. 启动服务

```bash
./deploy-standalone.sh start
```

启动时，脚本会自动：
1. 检查环境变量配置
2. 生成配置文件（使用环境变量替换模板中的占位符）
3. 启动Docker容器

### 4. 管理服务

- 查看服务状态：`./deploy-standalone.sh status`
- 查看服务日志：`./deploy-standalone.sh logs`
- 停止服务：`./deploy-standalone.sh stop`
- 重启服务：`./deploy-standalone.sh restart`
- 更新环境变量：`./deploy-standalone.sh update-env`
- 测试外部服务连接：`./deploy-standalone.sh test-conn`

## 配置文件说明

Gateway服务使用配置模板方式：

1. **配置模板 + 环境变量替换**：
   - `configs/config.template.yaml`中包含配置模板，使用`${VARIABLE}`格式的占位符
   - 部署时，脚本从`.env`文件读取环境变量，使用`envsubst`工具替换占位符，生成最终的`config.yaml`
   - 运行时，应用程序只读取生成的`config.yaml`文件，不直接访问环境变量

## 日志

- 日志文件存储在Docker卷`gateway_logs`中
- 日志会自动按日期和大小进行轮转
- 每个日志文件最大100MB
- 日志命名格式：`gateway_YYMMDD_HHMMSS.log`

## 故障排除

如果遇到连接问题：

1. 检查环境变量是否正确：`cat .env`
2. 测试外部服务连接：`./deploy-standalone.sh test-conn`
3. 检查生成的配置文件：`cat configs/config.yaml`
4. 查看容器日志：`./deploy-standalone.sh logs`

如果配置有更新，可以强制更新环境变量和配置文件：
```bash
./deploy-standalone.sh update-env
./deploy-standalone.sh restart
```