#include "mines_factory.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <third_party/nlohmann/json.hpp>
#include "games/mines_pro/application/mines_game_service.h"
#include "games/mines_pro/interfaces/handlers/mines_place_bet_handler.h"
#include "games/mines_pro/interfaces/handlers/mines_auto_cash_handler.h"
#include "games/mines_pro/interfaces/handlers/mines_cash_handler.h"
#include "games/mines_pro/interfaces/handlers/mines_cancel_bet_handler.h"
#include "core/infrastructure/network/protocol.h"
#include "core/infrastructure/common/app_context.h"

using json = nlohmann::json;


MinesFactory::MinesFactory() {
    gameService_ = std::make_shared<MinesGameService>(this);
}

MinesFactory::~MinesFactory() {
}

std::shared_ptr<IGameService> MinesFactory::getGameService() {
    return gameService_;
}

bool MinesFactory::registerMessageHandlers(MessageRouter* router) {
    if (!router) {
        LOG_ERROR("Message router is null");
        return false;
    }
    
    try {
        // 获取响应回调函数
        auto& appContext = AppContext::getInstance();
        auto tcpServer = appContext.getTcpServer();
        if (!tcpServer) {
            LOG_ERROR("TcpServer not available for mines handlers");
            return false;
        }
        
        auto responseCallback = [tcpServer](const std::string& sessionId, uint32_t protocolId, const std::string& response) {
            tcpServer->sendToPlayer(sessionId, protocolId, response);
        };
        
        // 注册下注请求处理器
        auto placeBetHandler = std::make_shared<MinesPlaceBetHandler>(responseCallback);
        router->registerHandlerObject(placeBetHandler);
        LOG_DEBUG("Registered MinesPlaceBetHandler for protocol ID 0x%x", Protocol::CS_MINES_PLACE_BET_REQ);
        
        // 注册自动兑现请求处理器
        auto autoCashHandler = std::make_shared<MinesAutoCashHandler>(responseCallback);
        router->registerHandlerObject(autoCashHandler);
        LOG_DEBUG("Registered MinesAutoCashHandler for protocol ID 0x%x", Protocol::CS_MINES_AUTO_CASH_REQ);
        
        // 注册手动兑现请求处理器
        auto cashHandler = std::make_shared<MinesCashHandler>(responseCallback);
        router->registerHandlerObject(cashHandler);
        LOG_DEBUG("Registered MinesCashHandler for protocol ID 0x%x", Protocol::CS_MINES_CASH_REQ);
        
        // 注册取消下注请求处理器
        auto cancelBetHandler = std::make_shared<MinesCancelBetHandler>(responseCallback);
        router->registerHandlerObject(cancelBetHandler);
        LOG_DEBUG("Registered MinesCancelBetHandler for protocol ID 0x%x", Protocol::CS_MINES_CANCEL_BET_REQ);
        
        LOG_DEBUG("All message handlers registered successfully for mines_pro game");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while registering mines message handlers: %s", e.what());
        return false;
    }
}

std::string MinesFactory::getGameType() const {
    return "mines_pro";
}

bool MinesFactory::start() {
    LOG_INFO("Starting %s game service", getGameType().c_str());
    
    if (!gameService_) {
        LOG_ERROR("Game service not initialized");
        return false;
    }
    
    LOG_INFO("Creating global game instance for %s (GLOBAL_SHARED mode)", getGameType().c_str());
    
    auto globalGame = gameService_->createGame();
    if (!globalGame) {
        LOG_ERROR("Failed to create global %s game instance", getGameType().c_str());
        return false;
    }
    
    LOG_INFO("Created and started global %s game instance with roundId: %s", 
             getGameType().c_str(), globalGame->roundID().c_str());
    
    LOG_INFO("%s game service started successfully", getGameType().c_str());
    return true;
}

void MinesFactory::stop() {
    LOG_INFO("Stopping %s game service", getGameType().c_str());
    
    if (!gameService_) {
        LOG_WARN("Game service already cleaned up");
        return;
    }
    
    auto minesGameService = std::dynamic_pointer_cast<MinesGameService>(gameService_);
    if (minesGameService) {
        minesGameService->stopAllGames();
    } else {
        LOG_ERROR("Failed to cast gameService_ to MinesGameService");
    }
    
    LOG_INFO("Cleaning up %s game service", getGameType().c_str());
    gameService_.reset();
    
    LOG_INFO("%s game service stopped", getGameType().c_str());
}

GameFactory::GameCreationMode MinesFactory::getGameCreationMode() const {
    return GameCreationMode::GLOBAL_SHARED;
}

void MinesFactory::onConfigUpdated(const json& oldConfig, const json& newConfig) {
    LOG_INFO("MinesFactory received config update");
    
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    
    if (!gameManager) {
        LOG_ERROR("GameManager not available for config update");
        return;
    }
    
    auto games = gameManager->getGamesByType("mines_pro");
    
    if (games.empty()) {
        LOG_INFO("No existing mines_pro game instances found. Config will be applied to new instances when created.");
        return;
    }
    
    LOG_INFO("Updating config for %d mines_pro game instances", games.size());
    
    for (auto& game : games) {
        if (game) {
            game->onConfigUpdated(newConfig);
        }
    }
    
    LOG_INFO("MinesFactory config update completed");
}