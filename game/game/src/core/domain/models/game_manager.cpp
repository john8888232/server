#include "game_manager.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "src/games/game_factory.h"
#include "src/games/game_registry.h"
#include "core/interfaces/message_router.h"
#include <algorithm>

GameManager::GameManager() {
    gameRegistry_ = std::make_shared<GameRegistry>();
}

GameManager::~GameManager() {
    // 清理所有游戏
    std::lock_guard<std::mutex> lock(mutex_);
    games_.clear();
    playerToGame_.clear();
}

bool GameManager::initialize(const std::string& serverId, std::shared_ptr<MessageRouter> messageRouter) {
    serverId_ = serverId;
    
    // 初始化游戏注册表
    if (!gameRegistry_->initialize()) {
        LOG_ERROR("Failed to initialize game registry");
        return false;
    }
    
    LOG_DEBUG("Game manager initialized with server ID: %s", serverId_.c_str());
    return true;
}

bool GameManager::addGame(std::shared_ptr<IGame> game) {
    if (!game) {
        LOG_ERROR("Cannot add null game to manager");
        return false;
    }
    std::string roundId = game->roundID();
    std::lock_guard<std::mutex> lock(mutex_);
    games_.push_back(game);
    LOG_INFO("Added game %s (type: %s) to manager", roundId.c_str(), game->gameType().c_str());
    return true;
}

std::shared_ptr<IGame> GameManager::findGameByRoundId(const std::string& roundId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(games_.begin(), games_.end(), 
                          [&roundId](const std::shared_ptr<IGame>& game) {
                              return game->roundID() == roundId;
                          });
    
    if (it != games_.end()) {
        return *it;
    }
    
    return nullptr;
}

bool GameManager::removeGame(std::shared_ptr<IGame> game) {
    if (!game) {
        LOG_ERROR("Cannot remove null game");
        return false;
    }
    
    std::string roundId = game->roundID();
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(games_.begin(), games_.end(),
                          [&game](const std::shared_ptr<IGame>& g) {
                              return g == game;
                          });
    
    if (it == games_.end()) {
        LOG_WARN("Cannot remove game %s: not found", roundId.c_str());
        return false;
    }
    
    // 移除所有玩家到该游戏的映射
    for (auto playerIt = playerToGame_.begin(); playerIt != playerToGame_.end();) {
        if (playerIt->second == game) {
            playerIt = playerToGame_.erase(playerIt);
        } else {
            ++playerIt;
        }
    }
    
    games_.erase(it);
    LOG_INFO("Removed game %s from manager", roundId.c_str());
    return true;
}

bool GameManager::addPlayerToGame(std::shared_ptr<PlayerSession> player, std::shared_ptr<IGame> game) {
    if (!player || !game) {
        LOG_ERROR("Cannot add player to game: player or game is null");
        return false;
    }
    
    std::string loginname = player->getLoginname();
    std::string roundId = game->roundID();
    
    std::shared_ptr<IGame> oldGame;
    
    {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查玩家是否已在其他游戏中
    auto playerGameIt = playerToGame_.find(loginname);
    if (playerGameIt != playerToGame_.end()) {
            oldGame = playerGameIt->second;  // 只保存引用，不调用方法
        }
        
        // 更新映射
        playerToGame_[loginname] = game;
    }  // 释放GameManager锁
    
    // 在锁外调用游戏方法
        if (oldGame) {
        oldGame->removePlayer(loginname);  // 安全调用
            LOG_INFO("Player %s removed from previous game %s", loginname.c_str(), oldGame->roundID().c_str());
        }
    
    game->addPlayer(player);  // 安全调用
    LOG_INFO("Player %s added to game %s", loginname.c_str(), roundId.c_str());
    
    return true;
}

bool GameManager::removePlayerFromGame(const std::string& loginname) {
    std::shared_ptr<IGame> game;
    
    {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto playerGameIt = playerToGame_.find(loginname);
    if (playerGameIt == playerToGame_.end()) {
        LOG_WARN("Cannot remove player %s from game: player not in any game", loginname.c_str());
        return false;
    }
    
        game = playerGameIt->second;  // 只保存引用，不调用方法
        playerToGame_.erase(playerGameIt);
    }  // 释放GameManager锁
    
    // 在锁外调用游戏方法
    if (game) {
        game->removePlayer(loginname);  // 安全调用
        LOG_INFO("Player %s removed from game %s", loginname.c_str(), game->roundID().c_str());
    }
    
    return true;
}

std::shared_ptr<IGame> GameManager::getPlayerGame(const std::string& loginname) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerToGame_.find(loginname);
    if (it == playerToGame_.end()) {
        return nullptr;
    }
    
    return it->second;
}

std::vector<std::shared_ptr<IGame>> GameManager::getAllGames() {
    std::lock_guard<std::mutex> lock(mutex_);
    return games_;
}

std::vector<std::shared_ptr<IGame>> GameManager::getGamesByType(const std::string& gameType) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::shared_ptr<IGame>> result;
    
    for (const auto& game : games_) {
        if (game->gameType() == gameType) {
            result.push_back(game);
        }
    }
    
    return result;
}

void GameManager::cleanupFinishedGames() {
    std::vector<std::shared_ptr<IGame>> gamesToCheck;
    
    {
    std::lock_guard<std::mutex> lock(mutex_);
    
        // 收集所有游戏引用，不调用方法
        gamesToCheck = games_;
    }  // 释放GameManager锁
    
    // 在锁外检查游戏状态
    std::vector<std::shared_ptr<IGame>> gamesToRemove;
    for (const auto& game : gamesToCheck) {
        if (game && !game->inProgress() && game->getPlayers().empty()) {
            gamesToRemove.push_back(game);
        }
    }
    
    // 重新获取锁进行移除
    if (!gamesToRemove.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& gameToRemove : gamesToRemove) {
            auto it = std::find(games_.begin(), games_.end(), gameToRemove);
            if (it != games_.end()) {
            LOG_INFO("Cleaned up finished game %s", (*it)->roundID().c_str());
                games_.erase(it);
            }
        }
        
        LOG_INFO("Cleaned up %zu finished games", gamesToRemove.size());
    }
}

void GameManager::registerGameStatusCallback(GameStatusCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    statusCallbacks_.push_back(std::move(callback));
}

void GameManager::onGameStatusChanged(std::shared_ptr<IGame> game, GameStatus newStatus) {
    if (!game) {
        LOG_ERROR("Cannot trigger status change for null game");
        return;
    }
    
    LOG_INFO("Game %s status changed to %d", game->roundID().c_str(), static_cast<int>(newStatus));
    
    std::vector<GameStatusCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callbacks = statusCallbacks_;
    }
    
    for (const auto& callback : callbacks) {
        try {
            callback(game, newStatus);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in game status callback: %s", e.what());
        }
    }
} 