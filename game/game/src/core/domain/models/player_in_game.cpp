#include "player_in_game.h"
#include "player_session.h"
#include "user.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <algorithm>

PlayerInGame::PlayerInGame(std::shared_ptr<User> user, std::weak_ptr<PlayerSession> session)
    : loginname_(""),
      user_(user),
      playerSession_(session),
      joinTime_(std::chrono::system_clock::now()),
      active_(true),
      isBet_(false) {
    if (user) {
        loginname_ = user->getLoginName();
    } else {
        auto sessionPtr = session.lock();
        if (sessionPtr) {
            loginname_ = sessionPtr->getLoginname();
        }
    }
}

void PlayerInGame::setSession(std::shared_ptr<PlayerSession> session) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    playerSession_ = session; 
    if (session) {
        active_ = true;
        if (loginname_.empty()) {
            loginname_ = session->getLoginname();
        }
    }
}

void PlayerInGame::setUser(std::shared_ptr<User> user) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    user_ = user;
    if (user) {
        // 确保 loginname 一致
        if (loginname_.empty()) {
            loginname_ = user->getLoginName();
        }
    }
}

double PlayerInGame::getBalance() const {
    // 不再加锁，由调用方保证线程安全（在gameMutex_保护下调用）
    if (user_) {
        return user_->getBalance();
    }
    return 0.0;
}

void PlayerInGame::setBalance(double newBalance) {
    // 不再加锁，由调用方保证线程安全（在gameMutex_保护下调用）
    if (user_) {
        user_->setBalance(newBalance);
        LOG_DEBUG("Updated balance for player %s to %.2f", loginname_.c_str(), newBalance);
    }
}

// betRecords_ 相关方法已移除 - 实际下注记录存储在游戏排行榜中

// 自动兑现相关方法实现
void PlayerInGame::setAutoCashConfig(int32_t playType, bool enable, int32_t targetGrid) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    
    if (enable) {
        // 启用自动兑现
        autoCashConfigs_[playType] = {true, targetGrid, false};
        LOG_INFO("Enabled auto cash for player %s, playType=%d, targetGrid=%d", 
                 loginname_.c_str(), playType, targetGrid);
    } else {
        // 取消自动兑现：删除配置
        auto it = autoCashConfigs_.find(playType);
        if (it != autoCashConfigs_.end()) {
            autoCashConfigs_.erase(it);
            LOG_INFO("Disabled auto cash for player %s, playType=%d", 
                     loginname_.c_str(), playType);
        }
    }
}

bool PlayerInGame::getAutoCashEnabled(int32_t playType) const {
    std::lock_guard<std::mutex> lock(playerMutex_);
    auto it = autoCashConfigs_.find(playType);
    return (it != autoCashConfigs_.end()) ? it->second.enabled : false;
}

int32_t PlayerInGame::getAutoCashTargetGrid(int32_t playType) const {
    std::lock_guard<std::mutex> lock(playerMutex_);
    auto it = autoCashConfigs_.find(playType);
    return (it != autoCashConfigs_.end()) ? it->second.targetGrid : 21;  // 默认21
}

bool PlayerInGame::hasCashedOut(int32_t playType) const {
    std::lock_guard<std::mutex> lock(playerMutex_);
    auto it = autoCashConfigs_.find(playType);
    return (it != autoCashConfigs_.end()) ? it->second.hasCashedOut : false;
}

void PlayerInGame::setCashedOut(int32_t playType, bool cashedOut) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    auto it = autoCashConfigs_.find(playType);
    if (it != autoCashConfigs_.end()) {
        it->second.hasCashedOut = cashedOut;
        LOG_INFO("Set cash out status for player %s, playType=%d, cashedOut=%s", 
                 loginname_.c_str(), playType, cashedOut ? "true" : "false");
    }
}

void PlayerInGame::resetCashOutStatus() {
    std::lock_guard<std::mutex> lock(playerMutex_);
    for (auto& config : autoCashConfigs_) {
        config.second.hasCashedOut = false;
    }
    LOG_INFO("Reset cash out status for player %s", loginname_.c_str());
} 