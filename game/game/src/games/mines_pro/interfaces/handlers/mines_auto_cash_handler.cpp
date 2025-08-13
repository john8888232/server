#include "mines_auto_cash_handler.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "core/infrastructure/protogen/game.pb.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/dependency_container.h"
#include "games/game_registry.h"
#include "core/infrastructure/common/error_code.h"
#include "games/mines_pro/domain/models/mines_game.h"
#include "games/game_factory.h"
#include "core/infrastructure/network/protocol.h"

extern DependencyContainer& getDependencyContainer();

MinesAutoCashHandler::MinesAutoCashHandler(ResponseCallback responseCallback)
    : responseCallback_(std::move(responseCallback)) {
    initialize();
}

bool MinesAutoCashHandler::initialize() {
    try {
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
        
        LOG_DEBUG("MinesAutoCashHandler initialized");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during MinesAutoCashHandler initialization: %s", e.what());
        return false;
    }
}

void MinesAutoCashHandler::handleMessage(const std::string& sessionId, const std::string& data) {
    LOG_INFO("Handling auto cash request from session %s", sessionId.c_str());
    
    try {
        proto::MinesAutoCashReq request;
        if (!request.ParseFromString(data)) {
            LOG_ERROR("Failed to parse MinesAutoCashReq from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST_FORMAT, "");
            return;
        }
        
        if (request.loginname().empty()) {
            LOG_ERROR("Missing loginname in auto cash request from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST, "");
            return;
        }
        
        auto currentGame = gameService_->getCurrentGame();
        if (!currentGame) {
            LOG_ERROR("No active game found for player %s (session %s)", 
                     request.loginname().c_str(), sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::NO_ACTIVE_GAME, "");
            return;
        }
        
        proto::MinesAutoCashRes response;
        
        bool success = currentGame->processAutoCash(request.loginname(), request.roundid(), 
                                                  request.playtype(), request.enable(), 
                                                  request.grid(), response);
        
        if (success) {
            LOG_INFO("Auto cash %s for player %s (session %s): playType=%d, targetGrid=%d", 
                     request.enable() ? "enabled" : "disabled",
                     request.loginname().c_str(), sessionId.c_str(), 
                     request.playtype(), request.grid());
        } else {
            LOG_WARN("Failed to process auto cash for player %s (session %s): %s", 
                     request.loginname().c_str(), sessionId.c_str(), response.message().c_str());
        }
        
        std::string responseData;
        response.SerializeToString(&responseData);
        responseCallback_(sessionId, Protocol::SC_MINES_AUTO_CASH_RES, responseData);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in MinesAutoCashHandler: %s", e.what());
        sendErrorResponse(sessionId, ErrorCode::GAME_INTERNAL_ERROR, "");
    }
}

uint32_t MinesAutoCashHandler::getMsgId() const {
    return Protocol::CS_MINES_AUTO_CASH_REQ;
}

void MinesAutoCashHandler::sendErrorResponse(const std::string& sessionId, int errorCode, const std::string& roundId) {
    proto::MinesAutoCashRes response;
    response.set_code(errorCode);
    response.set_message(ErrorCode::getErrorMessage(errorCode));
    response.set_roundid(roundId);
    
    std::string responseData;
    response.SerializeToString(&responseData);
    responseCallback_(sessionId, Protocol::SC_MINES_AUTO_CASH_RES, responseData);
} 