#include "mines_cash_handler.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <google/protobuf/util/json_util.h>
#include "core/infrastructure/proto/game.pb.h"
#include "core/infrastructure/common/app_context.h"
#include "games/game_registry.h"
#include "games/game_def.h"
#include "core/infrastructure/common/error_code.h"

MinesCashHandler::MinesCashHandler(ResponseCallback responseCallback)
    : responseCallback_(std::move(responseCallback)) {
    initialize();
}

bool MinesCashHandler::initialize() {
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
        
        LOG_DEBUG("MinesCashHandler initialized");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during MinesCashHandler initialization: %s", e.what());
        return false;
    }
}

void MinesCashHandler::handleMessage(const std::string& sessionId, const std::string& data) {
    LOG_INFO("Handling cash out request from session %s", sessionId.c_str());
    
    try {
        proto::MinesCashReq request;
        if (!request.ParseFromString(data)) {
            LOG_ERROR("Failed to parse MinesCashReq from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST_FORMAT, "", 0.0);
            return;
        }
        
        // 验证loginname字段
        if (request.loginname().empty()) {
            LOG_ERROR("Missing loginname in cash out request from session %s", sessionId.c_str());
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
        
        proto::MinesCashRes response;
        
        // 使用loginname而不是sessionId进行业务逻辑处理
        bool success = currentGame->processCashOut(request.loginname(), request.roundid(), 
                                                  request.playtype(), response);
        
        if (success) {
            LOG_INFO("Cash out successful for player %s (session %s): playType=%d, payout=%.2f, newBalance=%.2f", 
                     request.loginname().c_str(), sessionId.c_str(), request.playtype(),
                     response.has_reckon() ? response.reckon().amount() : 0.0,
                     response.balance());
        } else {
            LOG_WARN("Cash out failed for player %s (session %s): %s", 
                     request.loginname().c_str(), sessionId.c_str(), response.message().c_str());
        }
        
        std::string responseData;
        response.SerializeToString(&responseData);
        responseCallback_(sessionId, Protocol::SC_MINES_CASH_RES, responseData);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in MinesCashHandler: %s", e.what());
        sendErrorResponse(sessionId, ErrorCode::GAME_INTERNAL_ERROR, "", 0.0);
    }
}

uint32_t MinesCashHandler::getMsgId() const {
    return Protocol::CS_MINES_CASH_REQ;
}

void MinesCashHandler::sendErrorResponse(const std::string& sessionId, int errorCode, const std::string& roundId, double balance) {
    proto::MinesCashRes response;
    response.set_roundid(roundId);
    response.set_code(errorCode);
    response.set_message(ErrorCode::getErrorMessage(errorCode));
    response.set_balance(balance);
    response.set_playtype(0);  // 错误时设为0
    
    std::string responseData;
    response.SerializeToString(&responseData);
    responseCallback_(sessionId, Protocol::SC_MINES_CASH_RES, responseData);
} 