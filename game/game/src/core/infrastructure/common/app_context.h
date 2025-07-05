#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <memory>
#include "third_party/libuv_cpp/include/EventLoop.hpp"
#include "core/infrastructure/service_discovery/consul_client.h"
#include "core/infrastructure/network/tcp_server.h"
#include "core/infrastructure/network/http_server.h"
#include "core/interfaces/message_router.h"
#include "core/interfaces/handlers/handler_registry.h"
#include "core/domain/models/game_manager.h"
#include "config_manager.h"

// 前向声明
class GameRegistry;

// 应用上下文 - 管理所有全局资源的单例类
class AppContext {
public:
    // 获取单例实例
    static AppContext& getInstance() {
        static AppContext instance;
        return instance;
    }

    // 禁止拷贝和移动
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;

    // 初始化上下文
    bool initialize(std::shared_ptr<ConfigManager> configManager);
    
    // 清理资源
    void cleanup();
    
    // 注册所有消息处理器
    bool registerAllHandlers();
    
    // 启动所有游戏服务
    bool startAllGames();
    
    // 重新加载指定游戏类型的配置 - 供HTTP接口调用
    bool reloadGameConfig(const std::string& gameType);
    
    // 资源访问器
    uv::EventLoop* getEventLoop() const { return eventLoop_; }
    std::shared_ptr<ConsulClient> getConsulClient() const { return consulClient_; }
    std::shared_ptr<TcpServer> getTcpServer() const { return tcpServer_; }
    std::shared_ptr<HttpServer> getHttpServer() const { return httpServer_; }
    std::shared_ptr<MessageRouter> getMessageRouter() const { return messageRouter_; }
    std::shared_ptr<HandlerRegistry> getHandlerRegistry() const { return handlerRegistry_; }
    std::shared_ptr<ConfigManager> getConfigManager() const { return configManager_; }
    std::shared_ptr<GameManager> getGameManager() const { return gameManager_; }

private:
    // 私有构造函数
    AppContext() : eventLoop_(nullptr) {}
    
    // 建立配置更新链路
    void setupConfigUpdateChain(std::shared_ptr<GameRegistry> gameRegistry);
    
    // 处理游戏配置更新
    // 配置更新相关方法已精简，现在使用ConfigManager::reloadGameConfigFromRedis()直接处理
    
    // 各种资源 - AppContext拥有所有这些资源
    uv::EventLoop* eventLoop_;  // 不拥有此指针，由libuv管理
    
    // 使用shared_ptr表示共享访问的组件
    std::shared_ptr<ConsulClient> consulClient_;
    std::shared_ptr<TcpServer> tcpServer_;
    std::shared_ptr<HttpServer> httpServer_;
    std::shared_ptr<MessageRouter> messageRouter_;
    std::shared_ptr<HandlerRegistry> handlerRegistry_;
    
    // 使用shared_ptr管理生命周期
    std::shared_ptr<ConfigManager> configManager_;
    std::shared_ptr<GameManager> gameManager_;
};

#endif // APP_CONTEXT_H 