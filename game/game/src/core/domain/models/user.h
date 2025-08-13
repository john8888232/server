#ifndef USER_H
#define USER_H

#include <string>
#include <cstdint>

// 用户领域模型 - 表示系统中的玩家用户
class User {
public:
    // 用户状态枚举
    enum class Status {
        Normal = 0,
        Disabled = 1
    };
    
    User(uint64_t playerId, 
         const std::string& avatarUrl, 
         const std::string& loginName,
         const std::string& username,
         const std::string& nickName,
         double amount,
         const std::string& currency,
         Status playerStatus,
         int32_t merchantId);
    
    // 获取用户信息
    uint64_t getPlayerId() const { return playerId_; }
    const std::string& getAvatarUrl() const { return avatarUrl_; }
    const std::string& getLoginName() const { return loginName_; }
    const std::string& getUserName() const { return username_; }
    const std::string& getNickName() const { return nickName_; }
    double getBalance() const { return amount_; }
    const std::string& getCurrency() const { return currency_; }
    Status getStatus() const { return playerStatus_; }
    bool isActive() const { return playerStatus_ == Status::Normal; }
    const std::string& getClientIp() const { return clientIp_; }
    void setClientIp(const std::string& clientIp) { clientIp_ = clientIp; }
    void setBalance(double newBalance) { amount_ = newBalance; }
    void addBalance(double amount) { amount_ += amount; }
    void subtractBalance(double amount) { amount_ -= amount; }
    int32_t getMerchantId() const { return merchantId_; }
private:
    uint64_t playerId_;
    std::string avatarUrl_;
    std::string loginName_;
    std::string username_;
    std::string nickName_;
    double amount_;
    Status playerStatus_;
    std::string currency_;
    std::string clientIp_;
    int32_t merchantId_;
};

#endif // USER_H
