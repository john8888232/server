#ifndef I_USER_BALANCE_REPOSITORY_H
#define I_USER_BALANCE_REPOSITORY_H

#include <cstdint>
#include <sys/types.h>
#include "core/application/dto/i_bet_record.h"

// 用户余额仓库接口 - 负责处理与用户余额相关的持久化操作
class IUserBalanceRepository {
public:
    virtual ~IUserBalanceRepository() = default;

    // 获取玩家当前余额
    virtual double getPlayerBalance(const uint64_t& playerId) = 0;

    // 更新单个玩家余额
    virtual bool updatePlayerBalance(const PlayerRoundInfo& info, uint32_t reason) = 0;
};

#endif // I_USER_BALANCE_REPOSITORY_H