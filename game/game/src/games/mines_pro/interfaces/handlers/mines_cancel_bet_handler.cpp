#include "mines_cancel_bet_handler.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "core/infrastructure/protogen/game.pb.h"
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
        
        // 获取MinesGameRepository
        minesGameRepository_ = std::make_shared<MinesGameRepositoryImpl>();
        if (!minesGameRepository_) {
            LOG_ERROR("Failed to create MinesGameRepositoryImpl");
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
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST_FORMAT, "", 0.0, 0.0, 0);
            return;
        }
        
        if (request.loginname().empty()) {
            LOG_ERROR("Missing loginname in cancel bet request from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST, "", 0.0, 0.0, 0);
            return;
        }
        
        auto currentGame = gameService_->getCurrentGame();
        if (!currentGame) {
            LOG_ERROR("No active game found for player %s (session %s)", 
                     request.loginname().c_str(), sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::NO_ACTIVE_GAME, "", 0.0, 0.0, 0);
            return;
        }
        
        proto::MinesCancelBetRes response;
        double refundAmount = 0.0;
        
        // 1. 先获取 PlayerInGame 对象
        std::shared_ptr<PlayerInGame> playerInGame = currentGame->getPlayer(request.loginname());
        if (!playerInGame) {
            LOG_ERROR("Player not found in game: %s (session %s)", request.loginname().c_str(), sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::PLAYER_NOT_FOUND, request.roundid(), 0.0, 0.0, request.playtype());
            return;
        }
        // 2. 验证取消下注请求
        bool validationSuccess = currentGame->validateCancelBet(playerInGame, request.roundid(), 
                                                              request.playtype(), response, refundAmount);
        if (!validationSuccess) {
            std::string responseData;
            response.SerializeToString(&responseData);
            responseCallback_(sessionId, Protocol::SC_MINES_CANCEL_BET_RES, responseData);
            return;
        }
        
        // 2. 执行数据库退款
        bool refundSuccess = minesGameRepository_->updatePlayerCancel(
            playerInGame->getPlayerId(),            // playerId
            refundAmount,                      // change_amount (正数表示退款)
            request.roundid(),                       // roundID
            request.playtype(),                     // playtype
            playerInGame->getClientIp(), // playerIP
            playerInGame                                      // playerInGame
        );
        
        if (!refundSuccess) {
            LOG_ERROR("Database refund failed for player %s (session %s)", request.loginname().c_str(), sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::DATABASE_ERROR, request.roundid(), response.balance(), 0.0, request.playtype());
            return;
        }
        
        LOG_INFO("Database refund successful for player %s: amount=%.2f, new_balance=%.2f", 
                 request.loginname().c_str(), refundAmount, playerInGame->getBalance());
        
        // 3. 同步取消下注信息到游戏状态
        currentGame->syncPlayerCancelBet(playerInGame, request.playtype(), refundAmount);
        sendErrorResponse(sessionId, ErrorCode::SUCCESS, request.roundid(), playerInGame->getBalance(), refundAmount, request.playtype());
        
        LOG_INFO("Cancel bet successful for player %s (session %s): playType=%d, refund=%.2f, balance=%.2f", 
                 request.loginname().c_str(), sessionId.c_str(), 
                 request.playtype(), refundAmount, response.balance());
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in MinesCancelBetHandler: %s", e.what());
        sendErrorResponse(sessionId, ErrorCode::GAME_INTERNAL_ERROR, "", 0.0, 0.0, 0);
    }
}

uint32_t MinesCancelBetHandler::getMsgId() const {
    return Protocol::CS_MINES_CANCEL_BET_REQ;
}

void MinesCancelBetHandler::sendErrorResponse(const std::string& sessionId, int errorCode, const std::string& roundId, double balance, double refund, int playType) {
    proto::MinesCancelBetRes response;
    response.set_code(errorCode);
    response.set_message(ErrorCode::getErrorMessage(errorCode));
    response.set_roundid(roundId);
    response.set_balance(balance);
    response.set_refundamount(refund);
    response.set_playtype(playType);
    
    std::string responseData;
    response.SerializeToString(&responseData);
    responseCallback_(sessionId, Protocol::SC_MINES_CANCEL_BET_RES, responseData);
} 