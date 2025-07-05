#ifndef USER_H
#define USER_H

#include <string>
#include <cstdint>
#include <memory>
#include <decimal/decimal>

// 用户领域模型 - 表示系统中的玩家用户
class User {
public:
    // 用户状态枚举
    enum class Status {
        Normal = 0,
        Disabled = 1
    };
    
    // 构造函数
    User(int64_t playerId, 
         int avatarId, 
         const std::string& loginName,
         const std::string& nickName,
         double amount,
         int vipLevel,
         const std::string& currency_,
         Status playerStatus);
    
    // 获取用户信息
    int64_t getPlayerId() const { return playerId_; }
    int getAvatarId() const { return avatarId_; }
    const std::string& getLoginName() const { return loginName_; }
    const std::string& getNickName() const { return nickName_; }
    double getBalance() const { return amount_; }
    int getVipLevel() const { return vipLevel_; }
    const std::string& getCurrency() const { return currency_; }
    Status getStatus() const { return playerStatus_; }
    std::string getAvatar() const { return std::to_string(avatarId_); }
    bool isActive() const { return playerStatus_ == Status::Normal; }
    
    // 余额更新方法 - 支持游戏中的余额变化
    void setBalance(double newBalance) { amount_ = newBalance; }
    void addBalance(double amount) { amount_ += amount; }
    void subtractBalance(double amount) { amount_ -= amount; }
    
private:
    int64_t playerId_;
    int avatarId_;
    std::string loginName_;
    std::string nickName_;
    double amount_;
    int vipLevel_;
    Status playerStatus_;
    std::string currency_;
};

#endif // USER_H
