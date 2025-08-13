#include "mines_cash_handler.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "core/infrastructure/protogen/game.pb.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/dependency_container.h"
#include "games/game_registry.h"
#include "core/infrastructure/common/error_code.h"
#include "games/game_factory.h"
#include "core/infrastructure/network/protocol.h"

extern DependencyContainer& getDependencyContainer();

MinesCashHandler::MinesCashHandler(ResponseCallback responseCallback)
    : responseCallback_(std::move(responseCallback)) {
    initialize();
}

bool MinesCashHandler::initialize() {
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
        
        minesGameRepository_ = std::make_shared<MinesGameRepositoryImpl>();
        if (!minesGameRepository_) {
            LOG_ERROR("Failed to create MinesGameRepositoryImpl");
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
    LOG_INFO("Handling cash request from session %s", sessionId.c_str());
    
    try {
        proto::MinesCashReq request;
        if (!request.ParseFromString(data)) {
            LOG_ERROR("Failed to parse MinesCashReq from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST_FORMAT, "", 0.0);
            return;
        }
        
        if (request.loginname().empty()) {
            LOG_ERROR("Missing loginname in cash request from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST, "", 0.0);
            return;
        }
        
        std::shared_ptr<MinesGame> currentGame = gameService_->getCurrentGame();
        if (!currentGame) {
            LOG_ERROR("No active game found for player %s (session %s)", 
                     request.loginname().c_str(), sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::NO_ACTIVE_GAME, "", 0.0);
            return;
        }
        
        // 使用统一的兑现执行器（IMMEDIATE_DB模式）
        auto cashExecutor = currentGame->createCashOutExecutor(true);
        auto result = cashExecutor->execute(currentGame, 
                                           request.roundid(), request.loginname(), 
                                           request.playtype(), CashOutMode::IMMEDIATE_DB);
        
        if (result.success) {
            // 设置响应
            auto response = result.response;
            if (result.reckonRecord) {
                auto* protoReckon = response.mutable_reckon();
                protoReckon->CopyFrom(*result.reckonRecord);
            }
            
            std::string responseData;
            response.SerializeToString(&responseData);
            responseCallback_(sessionId, Protocol::SC_MINES_CASH_RES, responseData);
            
            LOG_INFO("Cash out successful for player %s (session %s): playType=%d, payout=%.2f, balance=%.2f", 
                     request.loginname().c_str(), sessionId.c_str(), 
                     request.playtype(), result.payoutAmount, response.balance());
        } else {
            // 发送错误响应
            std::string responseData;
            result.response.SerializeToString(&responseData);
            responseCallback_(sessionId, Protocol::SC_MINES_CASH_RES, responseData);
            
            LOG_WARN("Cash out failed for player %s (session %s): %s", 
                     request.loginname().c_str(), sessionId.c_str(), result.response.message().c_str());
        }
        
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
    response.set_code(errorCode);
    response.set_message(ErrorCode::getErrorMessage(errorCode));
    response.set_roundid(roundId);
    response.set_balance(balance);

    std::string responseData;
    response.SerializeToString(&responseData);
    responseCallback_(sessionId, Protocol::SC_MINES_CASH_RES, responseData);
} 