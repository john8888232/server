#include "mines_place_bet_handler.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <google/protobuf/util/json_util.h>
#include "core/infrastructure/proto/game.pb.h"
#include "core/infrastructure/common/app_context.h"
#include "games/game_registry.h"
#include "games/game_def.h"
#include "core/infrastructure/common/error_code.h"

MinesPlaceBetHandler::MinesPlaceBetHandler(ResponseCallback responseCallback)
    : responseCallback_(std::move(responseCallback)) {
    initialize();
}

bool MinesPlaceBetHandler::initialize() {
    try {
        // 从AppContext获取GameRegistry，然后获取MinesFactory的GameService
        auto& appContext = AppContext::getInstance();
        auto gameManager = appContext.getGameManager();
        auto gameRegistry = gameManager->getGameRegistry();
        
        if (!gameRegistry) {
            LOG_ERROR("Game registry not available");
            return false;
        }
        
        auto factory = gameRegistry->getGameFactory("mines_pro");
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
        
        LOG_DEBUG("MinesPlaceBetHandler initialized");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during MinesPlaceBetHandler initialization: %s", e.what());
        return false;
    }
}

void MinesPlaceBetHandler::handleMessage(const std::string& sessionId, const std::string& data) {
    LOG_INFO("Handling place bet request from session %s", sessionId.c_str());
    
    try {
        proto::MinesPlaceBetReq request;
        if (!request.ParseFromString(data)) {
            LOG_ERROR("Failed to parse MinesPlaceBetReq from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST_FORMAT, "", 0.0);
            return;
        }
        
        if (request.loginname().empty()) {
            LOG_ERROR("Missing loginname in place bet request from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST, "", 0.0);
            return;
        }
        
        auto currentGame = gameService_->getCurrentGame();
        if (!currentGame) {
            LOG_ERROR("No active game found for player %s (session %s)", 
                     request.loginname().c_str(), sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::NO_ACTIVE_GAME, "", 0.0);
            return;
        }
        
        proto::MinesPlaceBetRes response;
        
        bool success = currentGame->processPlaceBet(request.loginname(), request.roundid(), 
                                                   request.playtype(), request.amount(), response);
        
        if (success) {
            LOG_INFO("Bet placed successfully for player %s (session %s): playType=%d, amount=%.2f, newBalance=%.2f", 
                     request.loginname().c_str(), sessionId.c_str(), request.playtype(), request.amount(), response.balance());
        } else {
            LOG_WARN("Failed to place bet for player %s (session %s): %s", 
                     request.loginname().c_str(), sessionId.c_str(), response.message().c_str());
        }
        
        std::string responseData;
        response.SerializeToString(&responseData);
        responseCallback_(sessionId, Protocol::SC_MINES_PLACE_BET_RES, responseData);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in MinesPlaceBetHandler: %s", e.what());
        sendErrorResponse(sessionId, ErrorCode::GAME_INTERNAL_ERROR, "", 0.0);
    }
}

uint32_t MinesPlaceBetHandler::getMsgId() const {
    return Protocol::CS_MINES_PLACE_BET_REQ;
}

void MinesPlaceBetHandler::sendErrorResponse(const std::string& sessionId, int errorCode, const std::string& roundId, double balance) {
    proto::MinesPlaceBetRes response;
    response.set_code(errorCode);
    response.set_message(ErrorCode::getErrorMessage(errorCode));
    response.set_roundid(roundId);
    response.set_balance(balance);
    
    std::string responseData;
    response.SerializeToString(&responseData);
    responseCallback_(sessionId, Protocol::SC_MINES_PLACE_BET_RES, responseData);
} 