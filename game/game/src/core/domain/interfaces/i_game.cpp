#include "i_game.h"
#include "core/domain/models/player_session.h"
#include <iostream>
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>
#include "core/infrastructure/common/app_context.h"
#include "games/game_def.h"

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
        LOG_INFO("Player %s reconnected to game", loginname.c_str());
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

std::string IGame::generateRoundId(const std::string& gameType) {
    // 生成格式: 4位游戏类型ID + 4位服务器ID + 12位时间戳(年月日时分秒) + 5位随机数
    
    // 获取游戏类型对应的数字ID (4位，不足补0)
    int gameTypeId = getGameTypeId(gameType);
    std::stringstream gameTypeStream;
    gameTypeStream << std::setfill('0') << std::setw(4) << gameTypeId;
    std::string gameTypePart = gameTypeStream.str();
    
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    std::string serverId = gameManager->getServerId();
    
    std::string serverIdNum = "0";
    size_t underscorePos = serverId.find_last_of('_');
    if (underscorePos != std::string::npos && underscorePos + 1 < serverId.length()) {
        serverIdNum = serverId.substr(underscorePos + 1);
    }
    
    std::stringstream serverIdStream;
    try {
        int serverIdInt = std::stoi(serverIdNum);
        serverIdStream << std::setfill('0') << std::setw(4) << serverIdInt;
    } catch (const std::exception& e) {
        serverIdStream << "0001";
    }
    std::string serverIdPart = serverIdStream.str();
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::stringstream timeStream;
    timeStream << std::setfill('0') 
               << std::setw(2) << (tm.tm_year % 100)  // 年份取后2位
               << std::setw(2) << (tm.tm_mon + 1)      
               << std::setw(2) << tm.tm_mday 
               << std::setw(2) << tm.tm_hour
               << std::setw(2) << tm.tm_min 
               << std::setw(2) << tm.tm_sec; 
    std::string timePart = timeStream.str();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000, 99999);
    int randomNum = dis(gen);
    
    std::stringstream ss;
    ss << gameTypePart << serverIdPart << timePart << randomNum;
    
    std::string roundId = ss.str();
    LOG_INFO("Generated roundId: %s for game type: %s", roundId.c_str(), gameType.c_str());
    return roundId;
}

std::string IGame::generateBetId() {
    // 生成随机下注ID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    std::stringstream ss;
    ss << "bet-";
    for (int i = 0; i < 16; i++) {
        ss << std::setw(2) << std::setfill('0') << std::hex << dis(gen);
    }
    
    return ss.str();
}

int IGame::getGameTypeId(const std::string& gameType) {
    // 根据游戏类型返回对应的数字ID
    if (gameType == "mines_pro") {
        return MINES_PRO_ID;
    }
    // 可以在这里添加其他游戏类型
    // else if (gameType == "slots") {
    //     return SLOTS_ID;
    // }
    
    // 默认返回1000
    return 1000;
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

// ========== 玩家管理方法实现 ==========

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