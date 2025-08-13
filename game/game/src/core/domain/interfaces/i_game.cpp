#include "i_game.h"
#include "core/domain/models/player_session.h"
#include <iostream>
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/dependency_container.h"
#include "games/game_def.h"

extern DependencyContainer& getDependencyContainer();

// 游戏状态相关方法 - 使用gameStateMutex_
std::string IGame::roundID() const {
    std::shared_lock<std::shared_mutex> lock(gameStateMutex_);
    return roundID_;
}

std::string IGame::gameType() const {
    std::shared_lock<std::shared_mutex> lock(gameStateMutex_);
    return gameType_;
}

std::chrono::system_clock::time_point IGame::startTime() const {
    std::shared_lock<std::shared_mutex> lock(gameStateMutex_);
    return startTime_;
}

void IGame::setRoundID(const std::string& roundID) {
    std::unique_lock<std::shared_mutex> lock(gameStateMutex_);
    roundID_ = roundID;
}

void IGame::setGameType(const std::string& gameType) {
    std::unique_lock<std::shared_mutex> lock(gameStateMutex_);
    gameType_ = gameType;
}

void IGame::setStartTime(const std::chrono::system_clock::time_point& startTime) {
    std::unique_lock<std::shared_mutex> lock(gameStateMutex_);
    startTime_ = startTime;
}

// 玩家管理相关方法 - 使用playersMutex_
std::unordered_map<std::string, std::shared_ptr<PlayerInGame>> IGame::getPlayers() const {
    std::shared_lock<std::shared_mutex> lock(playersMutex_); 
    return players_;
}

void IGame::addPlayer(std::shared_ptr<PlayerSession> session) {
    if (!session) {
        LOG_ERROR("Cannot add player to game: no session");
        return;
    }
    
    std::string loginname = session->getLoginname();
    std::shared_ptr<PlayerInGame> playerInGame;
    
    {
        std::unique_lock<std::shared_mutex> lock(playersMutex_);
        auto existingPlayerIt = players_.find(loginname);
    
        if (existingPlayerIt != players_.end()) {
            // 获取PlayerInGame对象，但不调用其方法
            playerInGame = existingPlayerIt->second;
        } else {
            // 新玩家加入游戏
            auto user = session->getPlayer();
            if (user) {
                std::weak_ptr<PlayerSession> weakSession = session;
                    playerInGame = std::make_shared<PlayerInGame>(user, weakSession);
                players_[loginname] = playerInGame;
                LOG_INFO("Player %s joined game with User object", loginname.c_str());
            } else {
                LOG_ERROR("Cannot add player %s to game: no User object in session", loginname.c_str());
                return;
            }
        }
    }  // 释放playersMutex_锁
    
    // 在锁外调用PlayerInGame方法
    if (playerInGame) {
        playerInGame->setActive(true);
        playerInGame->setSession(session);
        LOG_INFO("Player %s connected to game", loginname.c_str());
    }
}

void IGame::removePlayer(const std::string& loginname) {
    std::unique_lock<std::shared_mutex> lock(playersMutex_);  // 写锁
    auto it = players_.find(loginname);
    if (it != players_.end()) {
        LOG_INFO("Removing player %s from game", loginname.c_str());
        players_.erase(it);
    } else {
        LOG_WARN("Cannot remove player %s: not found in game", loginname.c_str());
    }
}

void IGame::setPlayerInactive(const std::string& loginname) {
    std::shared_ptr<PlayerInGame> playerInGame;
    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);  // 读锁
        auto it = players_.find(loginname);
        if (it != players_.end()) {
            playerInGame = it->second;
        }
    }
    
    if (playerInGame) {
        playerInGame->setActive(false);
        LOG_INFO("Set player %s as inactive in game", loginname.c_str());
    } else {
        LOG_WARN("Cannot set player %s as inactive: not found in game", loginname.c_str());
    }
}

void IGame::handlePlayerDisconnect(const std::string& loginname) {
    // 默认实现：仅设置玩家为非活跃状态
    setPlayerInactive(loginname);
}

void IGame::setStatus(GameStatus status) {
    GameStatus oldStatus = status_.exchange(status, std::memory_order_acq_rel);
    LOG_DEBUG("Game status changed from %d to %d", (int)oldStatus, (int)status);
}

bool IGame::compareAndSwapStatus(GameStatus expected, GameStatus desired) {
    bool success = status_.compare_exchange_strong(expected, desired, std::memory_order_acq_rel);
    if (success) {
        LOG_DEBUG("Game status successfully changed from %d to %d", (int)expected, (int)desired);
    } else {
        LOG_DEBUG("Game status compare_exchange failed: expected %d, actual %d, desired %d", 
                  (int)expected, (int)status_.load(), (int)desired);
    }
    return success;
}

void IGame::clearPlayers() {
    std::unique_lock<std::shared_mutex> lock(playersMutex_);  // 写锁
    players_.clear();
    LOG_INFO("All players cleared from game");
}

size_t IGame::getPlayerCount() const {
    std::shared_lock<std::shared_mutex> lock(playersMutex_);  // 读锁
    return players_.size();
}

bool IGame::hasPlayer(const std::string& loginname) const {
    std::shared_lock<std::shared_mutex> lock(playersMutex_);  // 读锁
    return players_.find(loginname) != players_.end();
}

std::shared_ptr<PlayerInGame> IGame::getPlayer(const std::string& loginname) const {
    std::shared_lock<std::shared_mutex> lock(playersMutex_);  // 读锁
    auto it = players_.find(loginname);
    return (it != players_.end()) ? it->second : nullptr;
}


std::string IGame::generateRoundId() {
    //01 05 1234567890123456 --游戏类型ID 机器ID 简化雪花算法生成的ID
    static std::atomic<uint64_t> sequence{0};
    static const uint64_t EPOCH = 1640995200000; // 2022-01-01 00:00:00 UTC
    int gameTypeId = getGameTypeId(gameType_);
    uint64_t machineId = 1; // 默认值
    auto& container = getDependencyContainer();
    auto appContext = container.resolve<AppContext>();
    if (appContext) {
        auto tcpServer = appContext->getTcpServer();
        if (tcpServer) {
            machineId = tcpServer->getServerNumericId();
            machineId %= 100;
        }
    }
    auto now = std::chrono::system_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    uint64_t seq = sequence.fetch_add(1) & 0xFFF; // 12位序列号
    uint64_t snowflakeId = ((timestamp - EPOCH) << 12) | seq; 
    
    // 使用游戏类型ID和机器ID作为前缀
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << gameTypeId 
       << std::setfill('0') << std::setw(2) << machineId 
       << snowflakeId;
    
    std::string roundId = ss.str();
    LOG_INFO("Generated roundId: %s for game type: %s (gameTypeId: %02d, machineId: %02d)", 
             roundId.c_str(), gameType_.c_str(), gameTypeId, static_cast<int>(machineId));
    return roundId;
}
