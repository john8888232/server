#include "player_in_game.h"
#include "player_session.h"
#include "user.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"

PlayerInGame::PlayerInGame(std::shared_ptr<User> user, std::weak_ptr<PlayerSession> session)
    : loginname_(""),
      user_(user),
      playerSession_(session),
      joinTime_(std::chrono::system_clock::now()),
      active_(true) {
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

bool PlayerInGame::isActive() const {
    std::lock_guard<std::mutex> lock(playerMutex_);
    return active_;
}

void PlayerInGame::setActive(bool isActive) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    active_ = isActive;
}

double PlayerInGame::getBalance() const {
    std::lock_guard<std::mutex> lock(playerMutex_);
    if (user_) {
        return user_->getBalance();
    }
    return 0.0;
}

int64_t PlayerInGame::getPlayerId() const {
    std::lock_guard<std::mutex> lock(playerMutex_);
    if (user_) {
        return user_->getPlayerId();
    }
    return 0;
}

std::string PlayerInGame::getClientIp() const {
    std::lock_guard<std::mutex> lock(playerMutex_);
    if (user_) {
        return user_->getClientIp();
    }
    return "";
}

void PlayerInGame::setBalance(double newBalance) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    if (user_) {
        user_->setBalance(newBalance);
        LOG_DEBUG("Updated balance for player %s to %.2f", loginname_.c_str(), newBalance);
    }
}

bool PlayerInGame::hasBet() const {
    std::lock_guard<std::mutex> lock(playerMutex_);
    return !betPlayTypes_.empty();
}

void PlayerInGame::setBetPlayType(int32_t playType) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    betPlayTypes_.insert(playType); 
}

void PlayerInGame::cancelBetPlayType(int32_t playType) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    betPlayTypes_.erase(playType);
}

void PlayerInGame::resetBetPlayType() {
    std::lock_guard<std::mutex> lock(playerMutex_);
    betPlayTypes_.clear();
}

// 自动兑现相关方法实现
void PlayerInGame::setAutoCashConfig(int32_t playType, bool enable, int32_t targetGrid) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    
    if (enable) {
        // 启用自动兑现
        autoCashConfigs_[playType] = {true, targetGrid};
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
    return cashedOutPlayTypes_.count(playType) > 0;
}

void PlayerInGame::setCashedOut(int32_t playType) {
    std::lock_guard<std::mutex> lock(playerMutex_);
    cashedOutPlayTypes_.insert(playType);
}

void PlayerInGame::resetCashOutPlayType() {
    std::lock_guard<std::mutex> lock(playerMutex_);
    cashedOutPlayTypes_.clear();
} 