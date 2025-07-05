#include "handler_registry.h"
#include "login_handler.h"
#include "logout_handler.h"
#include "heartbeat_handler.h"
#include "disconnect_handler.h"
#include "generic_handler.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "core/infrastructure/common/app_context.h"
#include "games/game_registry.h"

HandlerRegistry::HandlerRegistry(std::shared_ptr<MessageRouter> messageRouter, ResponseCallback responseCallback)
    : messageRouter_(std::move(messageRouter)), responseCallback_(std::move(responseCallback)) {
}

bool HandlerRegistry::registerSystemHandlers() {
    LOG_INFO("Registering system message handlers...");
    
    try {
        // 获取TcpServer实例（用于断线处理器）
        auto tcpServer = AppContext::getInstance().getTcpServer();
        if (!tcpServer) {
            LOG_ERROR("TcpServer instance not available for disconnect handler");
            return false;
        }
        
        // 注册客户端断线通知处理器（系统消息）
        auto disconnectHandler = std::make_shared<DisconnectHandler>(responseCallback_, tcpServer);
        messageRouter_->registerHandlerObject(disconnectHandler);
        LOG_DEBUG("Registered client disconnect handler for protocol ID 0x%x", Protocol::GW_CONN_CLOSE);
        
        // 系统中的其他基础消息处理器...
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to register system message handlers: %s", e.what());
        return false;
    }
}

bool HandlerRegistry::registerUserHandlers() {
    LOG_INFO("Registering user message handlers...");
    
    try {
        // 获取TcpServer实例
        auto tcpServer = AppContext::getInstance().getTcpServer();
        if (!tcpServer) {
            LOG_ERROR("TcpServer instance not available");
            return false;
        }
        
        auto loginHandler = std::make_shared<LoginHandler>(responseCallback_, tcpServer);
        messageRouter_->registerHandlerObject(loginHandler);
        LOG_DEBUG("Registered login handler for protocol ID 0x%x", Protocol::CS_LOGIN_REQ);
        
        auto logoutHandler = std::make_shared<LogoutHandler>(responseCallback_, tcpServer);
        messageRouter_->registerHandlerObject(logoutHandler);
        LOG_DEBUG("Registered logout handler for protocol ID 0x%x", Protocol::CS_LOGOUT_REQ);
        
        auto heartbeatHandler = std::make_shared<HeartbeatHandler>(responseCallback_);
        messageRouter_->registerHandlerObject(heartbeatHandler);
        LOG_DEBUG("Registered heartbeat handler for protocol ID 0x%x", Protocol::HEARTBEAT_PROTOCOL_ID);
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to register user message handlers: %s", e.what());
        return false;
    }
}

bool HandlerRegistry::registerGameBaseHandlers() {
    LOG_INFO("Registering base game handlers...");
    
    try {
        // 这里注册通用游戏消息处理器，不涉及具体游戏逻辑
        // 例如: 游戏列表查询、游戏房间管理、游戏历史记录等
        
        // 示例：注册游戏列表查询处理器
        // auto gameListHandler = std::make_shared<GameListHandler>(responseCallback_);
        // messageRouter_->registerHandlerObject(gameListHandler);
        
        LOG_INFO("Base game handlers registered successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to register base game handlers: %s", e.what());
        return false;
    }
}

bool HandlerRegistry::registerGameSpecificHandlers() {
    LOG_INFO("Registering game-specific message handlers...");
    
    try {
        // 获取GameManager和GameRegistry
        auto& appContext = AppContext::getInstance();
        auto gameManager = appContext.getGameManager();
        if (!gameManager) {
            LOG_ERROR("GameManager not available");
            return false;
        }
        
        auto gameRegistry = gameManager->getGameRegistry();
        if (!gameRegistry) {
            LOG_ERROR("GameRegistry not available");
            return false;
        }
        
        // 通过GameRegistry注册所有游戏的消息处理器
        if (!gameRegistry->registerMessageHandlers(messageRouter_)) {
            LOG_ERROR("Failed to register game message handlers");
            return false;
        }
        
        LOG_INFO("Game-specific message handlers registered successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to register game-specific message handlers: %s", e.what());
        return false;
    }
}

template<typename HandlerType>
bool HandlerRegistry::registerHandler(uint32_t protocolId) {
    try {
        auto handler = std::make_shared<HandlerType>(responseCallback_);
        messageRouter_->registerHandlerObject(handler);
        LOG_DEBUG("Registered handler for protocol ID 0x%x", protocolId);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to register handler for protocol ID 0x%x: %s", protocolId, e.what());
        return false;
    }
} 