#include "disconnect_handler.h"
#include "core/infrastructure/network/protocol.h"
#include "core/infrastructure/common/app_context.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>

DisconnectHandler::DisconnectHandler(ResponseCallback responseCallback, std::shared_ptr<TcpServer> tcpServer)
    : responseCallback_(std::move(responseCallback)), tcpServer_(tcpServer) {
}

DisconnectHandler::~DisconnectHandler() {
}

void DisconnectHandler::handleMessage(const std::string& sessionId, const std::string& data) {
    try {
        LOG_INFO("Processing client disconnect notification for session: %s", sessionId.c_str());
        
        // 立即清理玩家会话映射关系并将游戏中的玩家设为非活跃状态
        immediateCleanupPlayerSession(sessionId);
        
        LOG_INFO("Successfully processed disconnect notification for session: %s", sessionId.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in disconnect handler for session %s: %s", sessionId.c_str(), e.what());
    }
}

uint32_t DisconnectHandler::getMsgId() const {
    return Protocol::GW_CONN_CLOSE;
}

void DisconnectHandler::immediateCleanupPlayerSession(const std::string& sessionId) {
    if (!tcpServer_) {
        LOG_ERROR("TcpServer not available for disconnect handling");
        return;
    }
    
    // 获取玩家会话信息（在清理之前）
    auto playerSession = tcpServer_->getConnectionManager()->getPlayerSession(sessionId);
    if (!playerSession) {
        LOG_WARN("Player session %s not found for immediate cleanup", sessionId.c_str());
        return;
    }
    
    std::string loginname = playerSession->getLoginname();
    LOG_INFO("Immediately cleaning up player session due to disconnect: %s (session: %s)", 
             loginname.c_str(), sessionId.c_str());
    
    // 1. 在游戏中将该玩家标记为非活跃状态
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    if (gameManager && !loginname.empty()) {
        auto currentGame = gameManager->getPlayerGame(loginname);
        if (currentGame) {
            // 断线时只标记为非活跃状态，不移除玩家数据
            currentGame->setPlayerInactive(loginname);
            LOG_INFO("Set player %s as inactive in game %s due to disconnect", 
                     loginname.c_str(), currentGame->roundID().c_str());
        } else {
            LOG_DEBUG("Player %s not found in any active game", loginname.c_str());
        }
    }
    
    // 2. 立即清理playerSession的所有映射关系
    bool cleaned = tcpServer_->getConnectionManager()->unregisterPlayerSession(sessionId);
    if (cleaned) {
        LOG_INFO("Successfully cleaned up player session mappings for %s (session: %s)", 
                 loginname.c_str(), sessionId.c_str());
    } else {
        LOG_WARN("Failed to clean up player session mappings for session: %s", sessionId.c_str());
    }
    
    LOG_INFO("Immediate cleanup completed for player %s (session: %s). " 
             "Note: 180s timeout cleanup mechanism remains active for other scenarios.", 
             loginname.c_str(), sessionId.c_str());
}
