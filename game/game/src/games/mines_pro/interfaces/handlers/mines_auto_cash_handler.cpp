#include "mines_auto_cash_handler.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <google/protobuf/util/json_util.h>
#include "core/infrastructure/proto/game.pb.h"
#include "core/infrastructure/common/app_context.h"
#include "games/game_registry.h"
#include "games/game_def.h"
#include "core/infrastructure/common/error_code.h"
#include "games/mines_pro/domain/models/mines_game.h"

MinesAutoCashHandler::MinesAutoCashHandler(ResponseCallback responseCallback)
    : responseCallback_(std::move(responseCallback)) {
    initialize();
}

bool MinesAutoCashHandler::initialize() {
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
        
        // 直接用 gameService_ 获取当前游戏
        auto minesGame = gameService_->getCurrentGame();
        if (!minesGame) {
            LOG_ERROR("No active mines_pro game found");
            sendErrorResponse(sessionId, ErrorCode::GAME_NOT_FOUND, request.roundid());
            return;
        }

        proto::MinesAutoCashRes response;
        bool success = minesGame->processAutoCash(
            request.loginname(), request.roundid(),
            request.playtype(), request.enable() == 1, request.grid(), response);

        std::string responseData;
        response.SerializeToString(&responseData);
        responseCallback_(sessionId, Protocol::SC_MINES_AUTO_CASH_RES, responseData);
        
        if (success) {
            LOG_INFO("Auto cash request processed successfully for player %s", request.loginname().c_str());
        } else {
            LOG_WARN("Auto cash request failed for player %s: %s",
                     request.loginname().c_str(), response.message().c_str());
        }
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
    response.set_roundid(roundId);
    response.set_code(errorCode);
    response.set_message(ErrorCode::getErrorMessage(errorCode));
    response.set_enable(0);
    response.set_grid(21);  // 默认目标格子为21
    
    std::string responseData;
    response.SerializeToString(&responseData);
    responseCallback_(sessionId, Protocol::SC_MINES_AUTO_CASH_RES, responseData);
} 