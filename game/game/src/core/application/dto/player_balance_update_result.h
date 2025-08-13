#ifndef PLAYER_BALANCE_UPDATE_RESULT_H
#define PLAYER_BALANCE_UPDATE_RESULT_H

#include <string>
#include <cstdint>

// 玩家余额更新结果结构 - 用于层间数据传输
struct PlayerBalanceUpdateResult {
    std::string loginName;      // 玩家登录名
    uint64_t playerId;           // 玩家ID
    double originalBalance;     // 原始余额
    double newBalance;          // 期望的新余额
    double actualBalance;       // 实际数据库中的余额
    bool success;               // 是否更新成功
    std::string errorMessage;   // 错误信息
};

#endif // PLAYER_BALANCE_UPDATE_RESULT_H 

