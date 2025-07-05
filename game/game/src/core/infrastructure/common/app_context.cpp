#include "app_context.h"
#include "config_manager.h"
#include "Logger_helper.h"
#include "core/infrastructure/persistence/database_factory.h"
#include "core/domain/models/game_manager.h"
#include "games/game_registry.h"

bool AppContext::initialize(std::shared_ptr<ConfigManager> configManager) {
    try {
        if (!configManager) {
            LOG_ERROR("ConfigManager is null");
            return false;
        }
        configManager_ = configManager;
        LOG_INFO("ConfigManager transferred to AppContext");
        
        eventLoop_ = uv::EventLoop::DefaultLoop();
        LOG_DEBUG("Event loop initialized");
        
        // 创建消息路由器
        messageRouter_ = std::make_shared<MessageRouter>();
        LOG_DEBUG("Message router created");
        
        // 创建并初始化游戏管理器
        gameManager_ = std::make_shared<GameManager>();
        const auto& serverConfig = configManager_->getServerConfig();
        std::string serverId = "default-server";
        if (serverConfig.contains("consul") && serverConfig["consul"].contains("service_id")) {
            serverId = serverConfig["consul"]["service_id"].get<std::string>();
        }
        
        if (!gameManager_->initialize(serverId, messageRouter_)) {
            LOG_ERROR("Failed to initialize game manager");
            return false;
        }
        LOG_DEBUG("Game manager initialized");
        
        // 获取游戏注册表
        auto gameRegistry = gameManager_->getGameRegistry();
        if (!gameRegistry) {
            LOG_ERROR("Failed to get game registry from game manager");
            return false;
        }
        
        // 建立ConfigManager和GameRegistry之间的配置更新链路
        setupConfigUpdateChain(gameRegistry);
        
        // 创建并初始化Consul客户端
        consulClient_ = std::make_shared<ConsulClient>(eventLoop_);
        if (!consulClient_->initialize(*configManager_)) {
            LOG_ERROR("Failed to initialize Consul client");
            return false;
        }
        LOG_DEBUG("Consul client initialized");
        
        // 创建TCP服务器
        tcpServer_ = std::make_shared<TcpServer>(eventLoop_);
        
        // 初始化服务器配置
        if (!tcpServer_->initialize(*configManager_)) {
            LOG_ERROR("Failed to initialize TCP server");
            return false;
        }
        
        // 设置消息路由器
        tcpServer_->setMessageRouter(messageRouter_);
        
        // 创建HTTP服务器，使用同一个事件循环
        httpServer_ = std::make_shared<HttpServer>(eventLoop_);
        
        // 初始化HTTP服务器配置
        if (!httpServer_->initialize(*configManager_)) {
            LOG_ERROR("Failed to initialize HTTP server");
            return false;
        }
        
        // 创建消息处理器注册器 
        auto responseCallback = [this](const std::string& sessionId, uint32_t protocolId, const std::string& response) {
            if (tcpServer_) {
                tcpServer_->sendToPlayer(sessionId, protocolId, response);
            }
        };
        handlerRegistry_ = std::make_shared<HandlerRegistry>(messageRouter_, responseCallback);
        
        // 注册所有消息处理器
        if (!registerAllHandlers()) {
            LOG_ERROR("Failed to register message handlers");
            return false;
        }
        
        LOG_DEBUG("AppContext initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during AppContext initialization: %s", e.what());
        return false;
    }
}

void AppContext::setupConfigUpdateChain(std::shared_ptr<GameRegistry> gameRegistry) {
    // 配置更新链已精简，现在配置重载由ConfigManager::reloadGameConfigFromRedis()直接处理
    LOG_INFO("Config update chain setup - using simplified direct reload mechanism");
}



bool AppContext::registerAllHandlers() {
    LOG_INFO("Registering all message handlers...");
    
    if (!handlerRegistry_) {
        LOG_ERROR("Handler registry not initialized");
        return false;
    }
    
    if (!gameManager_) {
        LOG_ERROR("Game manager not initialized");
        return false;
    }
    
    // 分别注册各类处理器
    bool success = true;
    
    // 系统级处理器
    if (!handlerRegistry_->registerSystemHandlers()) {
        LOG_ERROR("Failed to register system handlers");
        success = false;
    }
    
    // 用户相关处理器
    if (!handlerRegistry_->registerUserHandlers()) {
        LOG_ERROR("Failed to register user handlers");
        success = false;
    }
    
    // 游戏基础处理器（通用游戏消息）
    if (!handlerRegistry_->registerGameBaseHandlers()) {
        LOG_ERROR("Failed to register game base handlers");
        success = false;
    }
    
    // 游戏特定处理器
    if (!handlerRegistry_->registerGameSpecificHandlers()) {
        LOG_ERROR("Failed to register game specific handlers");
        success = false;
    }
    
    if (success) {
        LOG_INFO("All message handlers registered successfully");
    } else {
        LOG_WARN("Some message handlers failed to register");
    }
    
    return success;
}

bool AppContext::startAllGames() {
    LOG_INFO("Starting all game services...");
    
    if (!gameManager_) {
        LOG_ERROR("Game manager not initialized");
        return false;
    }
    
    auto gameRegistry = gameManager_->getGameRegistry();
    if (!gameRegistry) {
        LOG_ERROR("Game registry not available");
        return false;
    }
    
    if (!gameRegistry->startAllGames()) {
        LOG_ERROR("Failed to start all games");
        return false;
    }
    
    LOG_INFO("All game services started successfully");
    return true;
}

bool AppContext::reloadGameConfig(const std::string& gameType) {
    LOG_INFO("Reloading game config for type: %s", gameType.c_str());
    
    if (!configManager_) {
        LOG_ERROR("ConfigManager not initialized");
        return false;
    }
    return configManager_->reloadGameConfigFromRedis(gameType);
}

void AppContext::cleanup() {
    LOG_INFO("Starting AppContext cleanup...");
    
    // 清理处理器注册器（停止消息处理）
    if (handlerRegistry_) {
        LOG_INFO("Destroying handler registry");
        handlerRegistry_.reset();
    }
    
    // 停止TCP服务器
    if (tcpServer_) {
        LOG_INFO("Stopping TCP server");
        tcpServer_->stop();
        tcpServer_.reset();
    }
    
    // 停止HTTP服务器
    if (httpServer_) {
        LOG_INFO("Stopping HTTP server");
        httpServer_->stop();
        httpServer_.reset();
    }
    
    // 从Consul注销服务
    if (consulClient_) {
        LOG_INFO("Deregistering service from Consul");
        try {
            consulClient_->deregisterService();
        } catch (const std::exception& e) {
            LOG_WARN("Exception during Consul deregistration: %s", e.what());
        }
        consulClient_.reset();
    }
    
    // 停止所有游戏服务并清理游戏管理器
    if (gameManager_) {
        LOG_INFO("Stopping all game services");
        auto gameRegistry = gameManager_->getGameRegistry();
        if (gameRegistry) {
            gameRegistry->stopAllGames();
        }
        
        LOG_INFO("Destroying game manager");
        gameManager_.reset();
    }
    
    // 清理消息路由器
    if (messageRouter_) {
        LOG_INFO("Destroying message router");
        messageRouter_.reset();
    }
    
    // 清理数据库连接
    LOG_INFO("Cleaning up database connections");
    try {
        DatabaseFactory::getInstance().cleanup();
    } catch (const std::exception& e) {
        LOG_WARN("Exception during database cleanup: %s", e.what());
    }
    
    // 停止事件循环
    if (eventLoop_) {
        LOG_INFO("Stopping event loop");
        try {
            eventLoop_->stop();
        } catch (const std::exception& e) {
            LOG_WARN("Exception while stopping event loop: %s", e.what());
        }
        eventLoop_ = nullptr;
    }
    
    // 清理配置管理器
    if (configManager_) {
        LOG_INFO("Destroying config manager");
        configManager_.reset();
    }
    
    LOG_INFO("AppContext cleanup completed");
} 