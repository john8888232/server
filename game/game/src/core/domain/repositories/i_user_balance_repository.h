#ifndef I_USER_BALANCE_REPOSITORY_H
#define I_USER_BALANCE_REPOSITORY_H

#include <string>
#include <vector>

// 玩家余额更新请求结构
struct PlayerBalanceUpdate {
    std::string loginName;      // 玩家登录名
    double originalBalance;     // 原始余额
    double newBalance;          // 新余额
    std::string reason;         // 更新原因
};

// 玩家余额更新结果结构
struct PlayerBalanceUpdateResult {
    std::string loginName;      // 玩家登录名
    double originalBalance;     // 原始余额
    double newBalance;          // 期望的新余额
    double actualBalance;       // 实际数据库中的余额
    bool success;               // 是否更新成功
    std::string errorMessage;   // 错误信息
};

// 用户余额仓库接口 - 负责处理与用户余额相关的持久化操作
class IUserBalanceRepository {
public:
    virtual ~IUserBalanceRepository() = default;
    
    // 批量更新玩家余额，使用独立的事务处理每个玩家
    virtual bool updatePlayerBalancesBatch(
        const std::vector<PlayerBalanceUpdate>& updates, 
        std::vector<PlayerBalanceUpdateResult>& results) = 0;
    
    // 更新单个玩家的余额
    virtual PlayerBalanceUpdateResult updatePlayerBalance(
        const PlayerBalanceUpdate& update) = 0;
};

#endif // I_USER_BALANCE_REPOSITORY_H 