#include "mines_place_bet_handler.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "core/infrastructure/protogen/game.pb.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/dependency_container.h"
#include "core/infrastructure/common/error_code.h"
#include "core/infrastructure/network/protocol.h"
#include "core/infrastructure/repositories/user_balance_repository_impl.h"
#include "games/game_registry.h"
#include "games/game_factory.h"

extern DependencyContainer& getDependencyContainer();

MinesPlaceBetHandler::MinesPlaceBetHandler(ResponseCallback responseCallback)
    : responseCallback_(std::move(responseCallback)) {
    initialize();
}

bool MinesPlaceBetHandler::initialize() {
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
        
        // 初始化用户余额仓库
        userBalanceRepository_ = std::make_shared<UserBalanceRepositoryImpl>();
        if (!userBalanceRepository_) {
            LOG_ERROR("Failed to create UserBalanceRepository");
            return false;
        }
        
        // 初始化Mines游戏仓库
        minesGameRepository_ = std::make_shared<MinesGameRepositoryImpl>();
        if (!minesGameRepository_) {
            LOG_ERROR("Failed to create MinesGameRepository");
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
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST_FORMAT, "", 0.0, 0);
            return;
        }
        if (request.loginname().empty()) {
            LOG_ERROR("Missing loginname in place bet request from session %s", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::INVALID_REQUEST, "", 0.0, 0);
            return;
        }
        auto game = gameService_->getCurrentGame();
        if (!game) {
            LOG_ERROR("No current game available (session %s)", sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::SYSTEM_ERROR, request.roundid(), 0.0, request.playtype());
            return;
        }
        proto::MinesPlaceBetRes response;
        
        std::shared_ptr<PlayerInGame> playerInGame = game->getPlayer(request.loginname());
        if (!playerInGame) {
            LOG_ERROR("Player not found in game: %s (session %s)", request.loginname().c_str(), sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::PLAYER_NOT_FOUND, request.roundid(), 0.0, request.playtype());
            return;
        }

        // 2. 验证下注请求
        bool validationSuccess = game->validateBet(playerInGame, request.roundid(), 
                                                  request.playtype(), request.amount(), response);
        if (!validationSuccess) {
            std::string responseData;
            response.SerializeToString(&responseData);
            responseCallback_(sessionId, Protocol::SC_MINES_PLACE_BET_RES, responseData);
            return;
        }
        // 2. 执行数据库扣款
        bool deductionSuccess = minesGameRepository_->updatePlayerBet(
            playerInGame->getPlayerId(),            // playerId
            -request.amount(),                 // change_amount (负数表示扣款)
            request.roundid(),                       // roundID
            request.playtype(),                     // playtype
            playerInGame->getClientIp(),            // playerIP
            playerInGame                                     
        );
        
        if (!deductionSuccess) {
            LOG_ERROR("Database deduction failed for player %s (session %s)", request.loginname().c_str(), sessionId.c_str());
            sendErrorResponse(sessionId, ErrorCode::DEDUCTION_FAILED, request.roundid(), response.balance(), request.playtype());
            return;
        }
        
        LOG_INFO("Database deduction successful for player %s: amount=%.2f, new_balance=%.2f", 
                 request.loginname().c_str(), request.amount(), playerInGame->getBalance());
        
        // 3. 同步下注信息到游戏状态
        game->syncPlayerBet(playerInGame, request.playtype(), request.amount());
        sendErrorResponse(sessionId, ErrorCode::SUCCESS, request.roundid(), playerInGame->getBalance(), request.playtype());
    
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in MinesPlaceBetHandler: %s", e.what());
        sendErrorResponse(sessionId, ErrorCode::GAME_INTERNAL_ERROR, "", 0.0, 0);
    }
}

uint32_t MinesPlaceBetHandler::getMsgId() const {
    return Protocol::CS_MINES_PLACE_BET_REQ;
}

void MinesPlaceBetHandler::sendErrorResponse(const std::string& sessionId, int errorCode, const std::string& roundId, double balance, int playType) {
    proto::MinesPlaceBetRes response;
    response.set_code(errorCode);
    response.set_message(ErrorCode::getErrorMessage(errorCode));
    response.set_roundid(roundId);
    response.set_balance(balance);
    response.set_playtype(playType);
    
    std::string responseData;
    response.SerializeToString(&responseData);
    responseCallback_(sessionId, Protocol::SC_MINES_PLACE_BET_RES, responseData);
} 