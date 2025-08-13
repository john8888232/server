#ifndef I_BET_RECORD_H
#define I_BET_RECORD_H

#include <string>
#include <vector>
#include <chrono>

struct RecordDetail{
    int32_t playType_;         // 下注类型 
    double before_amount_;     // 前金额
    double amount_;            // 金额
    double after_amount_;      // 后金额
    std::chrono::system_clock::time_point time_; // 时间
};

struct BetRecord {
    int32_t playType_;                  // 下注类型 
    double amount_;                     // 下注总金额
    std::chrono::system_clock::time_point bet_time_; // 下注时间
    std::vector<RecordDetail> details_; // 下注详情
};
  
struct ReckonRecord {
    int32_t playType_;        // 类型 
    double before_amount;     // 派奖前金额
    double amount_;           // 派奖金额
    double after_amount;      // 派奖后金额
    double multi_;            // 倍数
    std::chrono::system_clock::time_point  reckon_time_;     // 派奖时间
};
  
struct PlayerBriefInfo {
    std::string nickname_;       // 玩家昵称
    std::string avatar_;         // 玩家头像URL
    std::string loginname_;      // 登录名
    uint64_t player_id_;         // ID
    double original_balance_;    // 原始余额
    std::string client_ip_;      // 客户端IP
    int merchant_id_;            // 商户ID
};

struct PlayerRoundInfo {
    PlayerBriefInfo info_;                   // 玩家基本信息
    std::vector<BetRecord> bets_;            // 玩家下注信息
    std::vector<ReckonRecord> reckons_;      // 玩家派奖信息
};

class IBetRecord {
public:
    IBetRecord() = default;
    virtual ~IBetRecord() = default;
public:
    void reset(){
        roundID_.clear();
        gameType_.clear();
        players_.clear();
    }
public:
    std::string roundID_;
    std::string gameType_;
    std::vector<PlayerRoundInfo> players_;
};

#endif // I_BET_RECORD_H 