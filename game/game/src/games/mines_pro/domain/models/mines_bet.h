#ifndef MINES_BET_H
#define MINES_BET_H

#include <string>
#include <chrono>

enum class MinesBetStatus {
    PENDING,        // 待处理
    ACTIVE,         // 活跃中
    CASHED_OUT,     // 已兑现
    LOST,           // 已输掉
    CANCELLED       // 已取消
};

enum class MinesBetType {
    MANUAL,         // 手动投注
    AUTO            // 自动投注
};

class MinesBet {
public:
    MinesBet();
    MinesBet(const std::string& betId, int64_t userId, const std::string& gameId);
    ~MinesBet();

    // 基本属性
    const std::string& getBetId() const { return betId_; }
    int64_t getUserId() const { return userId_; }
    const std::string& getGameId() const { return gameId_; }
    
    // 投注信息
    double getBetAmount() const { return betAmount_; }
    void setBetAmount(double amount) { betAmount_ = amount; }
    
    int getMinesCount() const { return minesCount_; }
    void setMinesCount(int count) { minesCount_ = count; }
    
    MinesBetType getBetType() const { return betType_; }
    void setBetType(MinesBetType type) { betType_ = type; }
    
    // 状态管理
    MinesBetStatus getStatus() const { return status_; }
    void setStatus(MinesBetStatus status) { status_ = status; }
    
    // 兑现信息
    double getCashOutMultiplier() const { return cashOutMultiplier_; }
    void setCashOutMultiplier(double multiplier) { cashOutMultiplier_ = multiplier; }
    
    double getWinAmount() const { return winAmount_; }
    void setWinAmount(double amount) { winAmount_ = amount; }
    
    // 自动兑现设置
    int getAutoCashOutTileCount() const { return autoCashOutTileCount_; }
    void setAutoCashOutTileCount(int count) { autoCashOutTileCount_ = count; }
    
    double getAutoCashOutMultiplier() const { return autoCashOutMultiplier_; }
    void setAutoCashOutMultiplier(double multiplier) { autoCashOutMultiplier_ = multiplier; }
    
    bool isAutoCashOutEnabled() const { return autoCashOutEnabled_; }
    void setAutoCashOutEnabled(bool enabled) { autoCashOutEnabled_ = enabled; }
    
    // 时间戳
    std::chrono::system_clock::time_point getBetTime() const { return betTime_; }
    void setBetTime(const std::chrono::system_clock::time_point& time) { betTime_ = time; }
    
    std::chrono::system_clock::time_point getCashOutTime() const { return cashOutTime_; }
    void setCashOutTime(const std::chrono::system_clock::time_point& time) { cashOutTime_ = time; }
    
    // 业务逻辑
    bool canCashOut() const;
    bool shouldAutoCashOut(int revealedTileCount, double currentMultiplier) const;
    double calculateWinAmount(double multiplier) const;
    
    // 序列化
    std::string toJson() const;
    bool fromJson(const std::string& json);

private:
    std::string betId_;
    int64_t userId_;
    std::string gameId_;
    
    // 投注信息
    double betAmount_;
    int minesCount_;
    MinesBetType betType_;
    MinesBetStatus status_;
    
    // 兑现信息
    double cashOutMultiplier_;
    double winAmount_;
    
    // 自动兑现设置
    bool autoCashOutEnabled_;
    int autoCashOutTileCount_;      // 达到多少个格子时自动兑现
    double autoCashOutMultiplier_;  // 达到多少倍数时自动兑现
    
    // 时间戳
    std::chrono::system_clock::time_point betTime_;
    std::chrono::system_clock::time_point cashOutTime_;
};

#endif // MINES_BET_H





