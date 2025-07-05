#include "logout_handler.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "core/infrastructure/proto/game.pb.h"
#include "core/infrastructure/common/error_code.h"
#include "core/infrastructure/network/protocol.h"
#include "core/infrastructure/common/app_context.h"
#include "core/domain/models/game_manager.h"
#include "core/application/services/user_service.h"

LogoutHandler::LogoutHandler(ResponseCallback responseCallback, std::shared_ptr<TcpServer> tcpServer)
    : responseCallback_(std::move(responseCallback)), tcpServer_(std::move(tcpServer)) {
}

void LogoutHandler::handleMessage(const std::string& sessionId, const std::string& data) {
    try {
        proto::LogoutReq request;
        if (!request.ParseFromString(data)) {
            LOG_ERROR("Failed to parse LogoutReq from session %s", sessionId.c_str());
            return;
        }

        LOG_INFO("Processing logout request for %s (session: %s)", 
                 request.loginname().c_str(), sessionId.c_str());

        proto::LogoutRes response;
        response.set_loginname(request.loginname());

        auto playerSession = tcpServer_->getConnectionManager()->getPlayerSession(sessionId);
        if (!playerSession) {
            LOG_WARN("Player session %s not found for logout", sessionId.c_str());
            response.set_code(ErrorCode::NOT_FOUND);
            response.set_msg("player not exist");
        } else {
            std::string loginname = playerSession->getLoginname();
            
            if (loginname != request.loginname()) {
                LOG_WARN("Loginname mismatch in logout request: session=%s, request=%s", 
                         loginname.c_str(), request.loginname().c_str());
                response.set_code(ErrorCode::NOT_FOUND);
                response.set_msg("loginname error");
            } else {
                if (!canPlayerLogout(loginname)) {
                    LOG_INFO("Player %s cannot logout: has active bets in game", loginname.c_str());
                    response.set_code(ErrorCode::IN_GAME); 
                    response.set_msg("in game and bet, cannot logout");
                } else {
                    response.set_code(ErrorCode::SUCCESS); 
                    response.set_msg("logout success");
                    LOG_INFO("Player %s logout successful", loginname.c_str());
                }
            }
        }

        // 先发送响应，再清理映射关系
        std::string responseData;
        if (response.SerializeToString(&responseData)) {
            responseCallback_(sessionId, Protocol::CS_LOGOUT_RES, responseData);
            LOG_INFO("Sent logout response to session %s, code: %d", 
                     sessionId.c_str(), response.code());
        } else {
            LOG_ERROR("Failed to serialize logout response for session %s", sessionId.c_str());
        }

        // 只有成功退出时才清理映射关系
        if (response.code() == ErrorCode::SUCCESS && playerSession) {
            std::string loginname = playerSession->getLoginname();
            cleanupPlayerMappings(sessionId, loginname);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in logout handler for session %s: %s", sessionId.c_str(), e.what());
        
        proto::LogoutRes response;
        response.set_code(ErrorCode::SYSTEM_ERROR);
        response.set_msg("system error");
        
        std::string responseData;
        if (response.SerializeToString(&responseData)) {
            responseCallback_(sessionId, Protocol::CS_LOGOUT_RES, responseData);
        }
    }
}

uint32_t LogoutHandler::getMsgId() const {
    return Protocol::CS_LOGOUT_REQ;
}

bool LogoutHandler::canPlayerLogout(const std::string& loginname) {
    try {
        auto& appContext = AppContext::getInstance();
        auto gameManager = appContext.getGameManager();
        if (!gameManager) {
            LOG_DEBUG("GameManager not available, allowing logout");
            return true;
        }

        auto currentGame = gameManager->getPlayerGame(loginname);
        if (!currentGame) {
            LOG_DEBUG("Player %s not in any game, allowing logout", loginname.c_str());
            return true;
        }

        GameStatus gameStatus = currentGame->gameStatus();
        
        if (gameStatus != GameStatus::START_JETTON && gameStatus != GameStatus::STOP_JETTON) {
            LOG_DEBUG("Player %s in game with status %d, allowing logout", 
                     loginname.c_str(), (int)gameStatus);
            return true;
        }

        const auto& players = currentGame->getPlayers();
        auto it = players.find(loginname);
        if (it == players.end()) {
            LOG_DEBUG("Player %s not found in game players, allowing logout", loginname.c_str());
            return true;
        }

        auto playerInGame = it->second;
        if (!playerInGame) {
            LOG_DEBUG("PlayerInGame object is null for %s, allowing logout", loginname.c_str());
            return true;
        }

        auto betRecords = playerInGame->getBetRecords();
        if (betRecords.empty()) {
            LOG_DEBUG("Player %s has no bets, allowing logout", loginname.c_str());
            return true;
        }

        // 有下注且游戏在进行中，不允许退出
        LOG_INFO("Player %s has %d bet(s) in active game (status: %d), denying logout", 
                 loginname.c_str(), (int)betRecords.size(), (int)gameStatus);
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception checking logout permission for %s: %s", loginname.c_str(), e.what());
        return true;
    }
}

void LogoutHandler::cleanupPlayerMappings(const std::string& sessionId, const std::string& loginname) {
    try {
        LOG_INFO("Cleaning up mappings for player %s (session: %s)", loginname.c_str(), sessionId.c_str());

        // 清理Redis中的用户在线信息
        UserService userService;
        bool redisCleared = userService.clearUserRedis(loginname);
        if (redisCleared) {
            LOG_INFO("Successfully cleared Redis online info for player: %s", loginname.c_str());
        } else {
            LOG_WARN("Failed to clear Redis online info for player: %s", loginname.c_str());
        }

        auto& appContext = AppContext::getInstance();
        auto gameManager = appContext.getGameManager();
        if (gameManager) {
            auto currentGame = gameManager->getPlayerGame(loginname);
            if (currentGame) {
                currentGame->removePlayer(loginname);
                LOG_INFO("Removed player %s from game %s", loginname.c_str(), currentGame->roundID().c_str());
            }
            
            gameManager->removePlayerFromGame(loginname);
        }

        if (tcpServer_) {
            bool cleaned = tcpServer_->getConnectionManager()->unregisterPlayerSession(sessionId);
            if (cleaned) {
                LOG_INFO("Successfully cleaned up player session mappings for %s", loginname.c_str());
            } else {
                LOG_WARN("Failed to clean up player session mappings for session: %s", sessionId.c_str());
            }
        }

        LOG_INFO("Cleanup completed for player %s", loginname.c_str());

    } catch (const std::exception& e) {
        LOG_ERROR("Exception during cleanup for player %s: %s", loginname.c_str(), e.what());
    }
} 