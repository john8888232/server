#include "mines_cancel_bet_handler.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <google/protobuf/util/json_util.h>
#include "core/infrastructure/proto/game.pb.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/dependency_container.h"
#include "games/game_registry.h"
#include "core/infrastructure/common/error_code.h"
#include "games/game_factory.h"
#include "core/infrastructure/network/protocol.h"

extern DependencyContainer& getDependencyContainer();

MinesCancelBetHandler::MinesCancelBetHandler(ResponseCallback responseCallback)
    : responseCallback_(std::move(responseCallback)) {
    initialize();
}

bool MinesCancelBetHandler::initialize() {
    try {
        // 从依赖容器获取AppContext
        auto& container = getDependencyContainer();
        auto appContext = container.resolve<AppContext>();
        if (!appContext) {
            LOG_ERROR("AppContext not available in dependency container");
            return false;
        }
        
        auto gameManager = appContext->getGameManager();
        if (!gameManager) {
            LOG_ERROR("GameManager is null");
            return false;
        }
        
        auto gameRegistry = gameManager->getGameRegistry();
        if (!gameRegistry) {
            LOG_ERROR("Game registry not available");
            return false;
        }
        
        auto factory = gameRegistry->getFactory("mines_pro");
        if (!factory) {
            LOG_ERROR("Mines factory not found");
            return false;
        }
        
        auto baseGameService = factory->getGameService();
        gameService_ = std::dynamic_pointer_cast<MinesGameService>(baseGameService);
        if (!gameService_) {
            LOG_ERROR("Failed to cast to MinesGameService");
            return false;
        }
        
        LOG_DEBUG("MinesCancelBetHandler initialized");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during MinesCancelBetHandler initialization: %s", e.what());
        return false;
    }
}

void MinesCancelBetHandler::handleMessage(const std::string& sessionId, const std::string& data) {
    LOG_INFO("Handling cancel bet request from session %s", sessionId.c_str());
    
    try {
        proto::MinesCancelBetReq request;
        if (!request.ParseFromString(data)) {
            LOG_ERROR("Failed to parse MinesCancelBetReq from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST_FORMAT, "", 0.0, 0.0);
            return;
        }
        
        if (request.loginname().empty()) {
            LOG_ERROR("Missing loginname in cancel bet request from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST, "", 0.0, 0.0);
            return;
        }
        
        auto currentGame = gameService_->getCurrentGame();
        if (!currentGame) {
            LOG_ERROR("No active game found for player %s (session %s)", 
                     request.loginname().c_str(), sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::NO_ACTIVE_GAME, "", 0.0, 0.0);
            return;
        }
        
        proto::MinesCancelBetRes response;
        
        bool success = currentGame->processCancelBet(request.loginname(), request.roundid(), 
                                                   request.playtype(), response);
        
        if (success) {
            LOG_INFO("Bet cancelled successfully for player %s (session %s): playType=%d, balance=%.2f", 
                     request.loginname().c_str(), sessionId.c_str(), 
                     request.playtype(), response.balance());
        } else {
            LOG_WARN("Failed to cancel bet for player %s (session %s): %s", 
                     request.loginname().c_str(), sessionId.c_str(), response.message().c_str());
        }
        
        std::string responseData;
        response.SerializeToString(&responseData);
        responseCallback_(sessionId, Protocol::SC_MINES_CANCEL_BET_RES, responseData);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in MinesCancelBetHandler: %s", e.what());
        sendErrorResponse(sessionId, ErrorCode::GAME_INTERNAL_ERROR, "", 0.0, 0.0);
    }
}

uint32_t MinesCancelBetHandler::getMsgId() const {
    return Protocol::CS_MINES_CANCEL_BET_REQ;
}

void MinesCancelBetHandler::sendErrorResponse(const std::string& sessionId, int errorCode, const std::string& roundId, double balance, double refund) {
    proto::MinesCancelBetRes response;
    response.set_code(errorCode);
    response.set_message(ErrorCode::getErrorMessage(errorCode));
    response.set_roundid(roundId);
    response.set_balance(balance);
    // 注意：如果proto中没有refund字段，则不设置
    
    std::string responseData;
    response.SerializeToString(&responseData);
    responseCallback_(sessionId, Protocol::SC_MINES_CANCEL_BET_RES, responseData);
} 