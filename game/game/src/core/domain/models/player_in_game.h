#ifndef PLAYER_IN_GAME_H
#define PLAYER_IN_GAME_H

#include <string>
#include <memory>
#include <chrono>
#include <list>
#include <vector>
#include <mutex>
#include <unordered_map>
// bet_record.h 不再需要 - 下注记录存储在游戏排行榜中

// 前向声明，避免循环引用
class PlayerSession;
class User;

class PlayerInGame
{
public:
    // 构造函数声明 - 现在主要基于 User 对象构造，PlayerSession 用 weak_ptr
    PlayerInGame(std::shared_ptr<User> user, std::weak_ptr<PlayerSession> session = std::weak_ptr<PlayerSession>());
    
    // 获取玩家登录名
    std::string getLoginname() const { return loginname_; }
    
    // 设置玩家登录名
    void setLoginname(const std::string &name) { loginname_ = name; }
    
    // 获取玩家会话 - 用于通信
    std::weak_ptr<PlayerSession> getSession() const { return playerSession_; }
    
    // 设置玩家会话 - 用于通信
    void setSession(std::shared_ptr<PlayerSession> session);
    
    // 获取用户对象 - 游戏状态的核心
    std::shared_ptr<User> getUser() const { return user_; }
    
    // 设置用户对象
    void setUser(std::shared_ptr<User> user);
    
    // 检查玩家是否活跃
    bool isActive() const { return active_; }
    
    // 设置玩家活跃状态
    void setActive(bool isActive) { active_ = isActive; }
    
    // 获取玩家加入游戏的时间
    std::chrono::system_clock::time_point getJoinTime() const { return joinTime_; }
    
    // 获取玩家余额 - 从游戏状态中的 User 对象获取
    double getBalance() const;
    
    // 设置玩家余额 - 更新游戏状态中的 User 对象
    void setBalance(double newBalance);
    
    // 检查玩家是否已下注
    bool hasBet() const { return isBet_; }
    
    // 设置玩家下注状态
    void setBetStatus(bool hasBet) { isBet_ = hasBet; }
    
    // 重置下注状态（每局开始时调用）
    void resetBetStatus() { isBet_ = false; }
    
    // 下注记录相关方法已移除 - 实际下注记录存储在游戏排行榜中
    
    // 设置自动兑现配置
    void setAutoCashConfig(int32_t playType, bool enable, int32_t targetGrid);
    
    // 获取自动兑现配置
    bool getAutoCashEnabled(int32_t playType) const;
    int32_t getAutoCashTargetGrid(int32_t playType) const;
    
    // 检查是否已经兑现过（防止重复兑现）
    bool hasCashedOut(int32_t playType) const;
    void setCashedOut(int32_t playType, bool cashedOut);
    
    // 重置兑现状态（新游戏开始时调用）
    void resetCashOutStatus();
    
private:
    mutable std::mutex playerMutex_;                 // 只保护玩家内部状态，不保护User对象
    std::string loginname_;                          // 玩家登录名
    std::shared_ptr<User> user_;                     // 游戏中的用户对象（核心状态）- 由游戏锁保护
    std::weak_ptr<PlayerSession> playerSession_;     // 指向玩家会话的指针（用于通信，离线后自动失效）
    std::chrono::system_clock::time_point joinTime_; // 游戏时间
    bool active_;                                    // 玩家是否活跃
    bool isBet_;                                     // 玩家是否已下注
    // betRecords_ 已移除 - 实际下注记录存储在游戏排行榜中
    
    // 自动兑现相关字段
    struct AutoCashConfig {
        bool enabled = false;                        // 是否启用自动兑现
        int32_t targetGrid = 21;                     // 目标格子（默认21）
        bool hasCashedOut = false;                   // 是否已经兑现过
    };
    std::unordered_map<int32_t, AutoCashConfig> autoCashConfigs_;  // playType -> 配置
};

#endif // PLAYER_IN_GAME_H
