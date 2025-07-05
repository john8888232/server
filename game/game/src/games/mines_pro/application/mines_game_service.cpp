#include "mines_game_service.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "games/mines_pro/domain/models/mines_game.h"
#include "core/infrastructure/common/app_context.h"
#include "core/domain/models/game_manager.h"
#include "games/game_def.h"
#include "games/game_registry.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "games/game_factory.h"

MinesGameService::MinesGameService(GameFactory* factory) : factory_(factory) {
    LOG_DEBUG("MinesGameService initialized");
}

MinesGameService::~MinesGameService() {
    LOG_INFO("MinesGameService destroyed");
}

std::shared_ptr<IGame> MinesGameService::createGame() {
    if (!factory_) {
        LOG_ERROR("Factory is null");
        return nullptr;
    }
    
    auto game = std::make_shared<MinesGame>();
    const auto& config = factory_->getConfig();
    LOG_INFO("Creating MinesGame with config: %s", config.dump().c_str());
    game->initializeWithConfig(config);
    
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    if (!gameManager->addGame(game)) {
        LOG_ERROR("Failed to add game to manager");
        return nullptr;
    }
    
    if (!game->start()) {
        LOG_ERROR("Failed to start game instance");
        gameManager->removeGame(game);
        return nullptr;
    }
    
    LOG_INFO("Created and started new MinesGame with roundId: %s", game->roundID().c_str());
    return game;
}

bool MinesGameService::destroyGame(std::shared_ptr<IGame> game) {
    if (!game) {
        LOG_ERROR("Invalid game pointer in destroyGame");
        return false;
    }
    
    LOG_INFO("Destroying game: %s", game->roundID().c_str());
    
    game->stop();
    
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    if (!gameManager->removeGame(game)) {
        LOG_ERROR("Failed to remove game from manager");
        return false;
    }
    
    LOG_INFO("Game destroyed and removed from manager");
    return true;
}

void MinesGameService::stopAllGames() {
    if (!factory_) {
        LOG_ERROR("Factory is null in stopAllGames");
        return;
    }
    
    std::string gameType = factory_->getGameType();
    LOG_INFO("Stopping all games of type: %s", gameType.c_str());
    
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    
    if (!gameManager) {
        LOG_WARN("GameManager not available during stopAllGames");
        return;
    }
    
    auto games = gameManager->getGamesByType(gameType);
    
    if (games.empty()) {
        LOG_INFO("No games of type %s found to stop", gameType.c_str());
        return;
    }
    
    LOG_INFO("Found %d games of type %s to stop", games.size(), gameType.c_str());
    
    for (auto& game : games) {
        if (game) {
            LOG_INFO("Stopping game instance: %s", game->roundID().c_str());
            
            if (destroyGame(game)) {
                LOG_INFO("Successfully stopped and removed game instance: %s", game->roundID().c_str());
            } else {
                LOG_ERROR("Failed to stop game instance: %s", game->roundID().c_str());
            }
        }
    }
    
    LOG_INFO("Completed stopping all games of type: %s", gameType.c_str());
}

std::shared_ptr<MinesGame> MinesGameService::getGameByRoundId(const std::string& roundId) {
    if (!factory_) {
        LOG_ERROR("Factory is null in getGameByRoundId");
        return nullptr;
    }
    
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    if (!gameManager) {
        LOG_ERROR("GameManager not available");
        return nullptr;
    }
    
    auto games = gameManager->getGamesByType(factory_->getGameType());
    for (auto& game : games) {
        if (game && game->roundID() == roundId) {
            return std::dynamic_pointer_cast<MinesGame>(game);
        }
    }
    
    LOG_WARN("Game with roundId %s not found", roundId.c_str());
    return nullptr;
}

std::shared_ptr<MinesGame> MinesGameService::getCurrentGame() {
    if (!factory_) {
        LOG_ERROR("Factory is null in getCurrentGame");
        return nullptr;
    }
    
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    if (!gameManager) {
        LOG_ERROR("GameManager not available");
        return nullptr;
    }
    
    auto games = gameManager->getGamesByType(factory_->getGameType());
    if (!games.empty()) {
        // 返回第一个游戏实例（全局共享模式）
        return std::dynamic_pointer_cast<MinesGame>(games[0]);
    }
    
    LOG_WARN("No active game found for type %s", factory_->getGameType().c_str());
    return nullptr;
}
