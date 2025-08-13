#ifndef USER_BALANCE_REPOSITORY_IMPL_H
#define USER_BALANCE_REPOSITORY_IMPL_H

#include <memory>
#include <atomic>
#include "core/domain/repositories/i_user_balance_repository.h"
#include "core/infrastructure/persistence/database_factory.h"

// 用户余额仓库实现
class UserBalanceRepositoryImpl : public IUserBalanceRepository {
public:
    UserBalanceRepositoryImpl();
    ~UserBalanceRepositoryImpl() override;

    // 获取玩家当前余额
    double getPlayerBalance(const uint64_t& playerId) override;

     // 更新单个玩家余额
    bool updatePlayerBalance(const PlayerRoundInfo& info, uint32_t reason) override;

private:
    // 生成订单ID
    std::string generateOrderId();
    
    // 生成交易ID
    std::string generateTransId();
        
    // 数据库工厂
    std::shared_ptr<DatabaseFactory> dbFactory_;
    
    // 序列号生成器
    static std::atomic<uint64_t> orderSequence_;
    static std::atomic<uint64_t> transSequence_;
};

#endif // USER_BALANCE_REPOSITORY_IMPL_H 